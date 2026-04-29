#include "server.h"

#include <cstdio>
#include <cstring>
#include <algorithm>

PassthroughServer::PassthroughServer()
    : m_ListenSocket(INVALID_SOCKET)
    , m_Running(false)
{
}

PassthroughServer::~PassthroughServer()
{
    stop();
}

bool PassthroughServer::start(const ServerConfig& config)
{
    m_Config = config;
    m_Config.vhciAvailable = m_VhciManager.isDriverAvailable();

    // Set up the VHCI URB callback — called from per-device read threads
    m_VhciManager.setUrbCallback(
        [this](uint32_t deviceId, const uint8_t* data, size_t len) {
            forwardVhciUrbToClient(deviceId, data, len);
        });

    m_ListenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_ListenSocket == INVALID_SOCKET) {
        log("Failed to create socket: " + std::to_string(WSAGetLastError()));
        return false;
    }

    int opt = 1;
    setsockopt(m_ListenSocket, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(config.port);

    if (bind(m_ListenSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        log("Bind failed on port " + std::to_string(config.port) + ": " + std::to_string(WSAGetLastError()));
        closesocket(m_ListenSocket);
        m_ListenSocket = INVALID_SOCKET;
        return false;
    }

    if (listen(m_ListenSocket, 4) == SOCKET_ERROR) {
        log("Listen failed: " + std::to_string(WSAGetLastError()));
        closesocket(m_ListenSocket);
        m_ListenSocket = INVALID_SOCKET;
        return false;
    }

    m_Running = true;
    m_AcceptThread = std::thread(&PassthroughServer::acceptLoop, this);

    log("Server listening on port " + std::to_string(config.port));
    return true;
}

void PassthroughServer::stop()
{
    m_Running = false;

    if (m_ListenSocket != INVALID_SOCKET) {
        closesocket(m_ListenSocket);
        m_ListenSocket = INVALID_SOCKET;
    }

    if (m_AcceptThread.joinable()) {
        m_AcceptThread.join();
    }

    {
        std::lock_guard<std::mutex> lock(m_ClientsMutex);
        for (auto& client : m_Clients) {
            client->running = false;
            if (client->socket != INVALID_SOCKET) {
                closesocket(client->socket);
                client->socket = INVALID_SOCKET;
            }
            if (client->thread.joinable()) {
                client->thread.join();
            }
        }
        m_Clients.clear();
    }

    {
        std::lock_guard<std::mutex> lock(m_DeviceOwnersMutex);
        m_DeviceOwners.clear();
    }

    log("Server stopped");
}

void PassthroughServer::acceptLoop()
{
    while (m_Running) {
        sockaddr_in clientAddr{};
        int addrLen = sizeof(clientAddr);

        SOCKET clientSocket = accept(m_ListenSocket,
                                      reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
        if (clientSocket == INVALID_SOCKET) {
            if (m_Running) {
                log("Accept failed: " + std::to_string(WSAGetLastError()));
            }
            continue;
        }

        int nodelay = 1;
        setsockopt(clientSocket, IPPROTO_TCP, TCP_NODELAY,
                   reinterpret_cast<const char*>(&nodelay), sizeof(nodelay));

        char addrStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, addrStr, sizeof(addrStr));

        auto client = std::make_unique<ClientConnection>();
        client->socket = clientSocket;
        client->address = addrStr;
        client->running = true;

        log("Client connected from " + client->address);

        ClientConnection* clientPtr = client.get();
        client->thread = std::thread(&PassthroughServer::clientLoop, this, clientPtr);

        {
            std::lock_guard<std::mutex> lock(m_ClientsMutex);
            m_Clients.push_back(std::move(client));
        }

        notifyStatusChange();
    }
}

void PassthroughServer::clientLoop(ClientConnection* client)
{
    while (client->running && m_Running) {
        uint8_t headerBuf[MlptProtocol::HEADER_SIZE];
        if (!recvExact(client->socket, headerBuf, MlptProtocol::HEADER_SIZE)) {
            break;
        }

        MlptProtocol::Header header;
        if (!MlptProtocol::validateHeader(headerBuf, header)) {
            log("Invalid magic from " + client->address + ", disconnecting");
            break;
        }

        if (header.payloadLen > 16 * 1024 * 1024) {
            log("Payload too large from " + client->address + ": " + std::to_string(header.payloadLen));
            break;
        }

        std::vector<uint8_t> payload(header.payloadLen);
        if (header.payloadLen > 0) {
            if (!recvExact(client->socket, payload.data(), header.payloadLen)) {
                break;
            }
        }

        processMessage(client, header, payload);
    }

    log("Client disconnected: " + client->address);

    // Detach all devices owned by this client
    {
        std::lock_guard<std::mutex> lock(m_DeviceOwnersMutex);
        std::vector<uint32_t> toDetach;
        for (auto& [devId, owner] : m_DeviceOwners) {
            if (owner == client) {
                toDetach.push_back(devId);
            }
        }
        for (uint32_t devId : toDetach) {
            m_DeviceOwners.erase(devId);
            m_VhciManager.detachDevice(devId);
            log("Auto-detached device " + std::to_string(devId) + " (client disconnected)");
        }
    }

    client->running = false;

    if (client->socket != INVALID_SOCKET) {
        closesocket(client->socket);
        client->socket = INVALID_SOCKET;
    }

    notifyStatusChange();
}

// ─── Message I/O (thread-safe) ───

static bool sendAll(SOCKET sock, const char* buf, int len)
{
    int sent = 0;
    while (sent < len) {
        int n = send(sock, buf + sent, len - sent, 0);
        if (n == SOCKET_ERROR) return false;
        sent += n;
    }
    return true;
}

bool PassthroughServer::sendMessage(ClientConnection* client, MlptProtocol::MsgType type,
                                     const void* payload, uint32_t payloadLen)
{
    std::lock_guard<std::mutex> lock(client->sendMutex);

    uint8_t headerBuf[MlptProtocol::HEADER_SIZE];
    MlptProtocol::writeHeader(headerBuf, type, payloadLen);

    if (!sendAll(client->socket, reinterpret_cast<const char*>(headerBuf),
                 MlptProtocol::HEADER_SIZE))
        return false;

    if (payloadLen > 0 && payload) {
        if (!sendAll(client->socket, reinterpret_cast<const char*>(payload),
                     payloadLen))
            return false;
    }
    return true;
}

bool PassthroughServer::recvExact(SOCKET sock, void* buf, int len)
{
    char* ptr = reinterpret_cast<char*>(buf);
    int remaining = len;

    while (remaining > 0) {
        int received = recv(sock, ptr, remaining, 0);
        if (received <= 0) return false;
        ptr += received;
        remaining -= received;
    }
    return true;
}

// ─── Message processing ───

void PassthroughServer::processMessage(ClientConnection* client,
                                        const MlptProtocol::Header& header,
                                        const std::vector<uint8_t>& payload)
{
    switch (static_cast<MlptProtocol::MsgType>(header.msgType)) {
    case MlptProtocol::MSG_HELLO:
        handleHello(client, payload);
        break;
    case MlptProtocol::MSG_DEVICE_LIST:
        handleDeviceList(client, payload);
        break;
    case MlptProtocol::MSG_DEVICE_ATTACH:
        handleDeviceAttach(client, payload);
        break;
    case MlptProtocol::MSG_DEVICE_DETACH:
        handleDeviceDetach(client, payload);
        break;
    case MlptProtocol::MSG_USBIP_RETURN:
        handleUsbIpReturn(client, payload);
        break;
    case MlptProtocol::MSG_KEEPALIVE:
        sendMessage(client, MlptProtocol::MSG_KEEPALIVE);
        break;
    default:
        log("Unknown message type from " + client->address + ": 0x" +
            std::to_string(header.msgType));
        break;
    }
}

void PassthroughServer::handleHello(ClientConnection* client,
                                     const std::vector<uint8_t>& payload)
{
    if (payload.size() < sizeof(MlptProtocol::HelloPayload)) {
        log("HELLO too short from " + client->address);
        return;
    }

    auto* hello = reinterpret_cast<const MlptProtocol::HelloPayload*>(payload.data());
    memcpy(client->sessionId, hello->sessionId, 16);

    log("HELLO from " + client->address +
        " version=" + std::to_string(hello->clientVersion));

    MlptProtocol::HelloAckPayload ack{};
    ack.serverVersion = MlptProtocol::VERSION;
    ack.vhciAvailable = m_Config.vhciAvailable ? 1 : 0;
    memset(ack.reserved, 0, sizeof(ack.reserved));

    sendMessage(client, MlptProtocol::MSG_HELLO_ACK, &ack, sizeof(ack));
}

void PassthroughServer::handleDeviceList(ClientConnection* client,
                                          const std::vector<uint8_t>& payload)
{
    if (payload.size() < sizeof(MlptProtocol::DeviceListHeader)) {
        return;
    }

    auto* listHeader = reinterpret_cast<const MlptProtocol::DeviceListHeader*>(payload.data());
    log("Device list from " + client->address + ": " +
        std::to_string(listHeader->count) + " devices");

    size_t offset = sizeof(MlptProtocol::DeviceListHeader);
    for (uint16_t i = 0; i < listHeader->count && offset < payload.size(); i++) {
        if (offset + sizeof(MlptProtocol::DeviceDescriptor) > payload.size()) break;

        auto* desc = reinterpret_cast<const MlptProtocol::DeviceDescriptor*>(payload.data() + offset);
        offset += sizeof(MlptProtocol::DeviceDescriptor);

        std::string name;
        if (desc->nameLen > 0 && offset + desc->nameLen <= payload.size()) {
            name.assign(reinterpret_cast<const char*>(payload.data() + offset), desc->nameLen);
            offset += desc->nameLen;
        }

        offset += desc->usbDescrDevLen;
        offset += desc->usbDescrConfLen;

        log("  Device " + std::to_string(desc->deviceId) + ": " + name +
            " [" + (desc->transport == MlptProtocol::TRANSPORT_USB ? "USB" : "BT") + "]" +
            " VID:" + std::to_string(desc->vendorId) +
            " PID:" + std::to_string(desc->productId));
    }
}

void PassthroughServer::handleDeviceAttach(ClientConnection* client,
                                            const std::vector<uint8_t>& payload)
{
    if (payload.size() < sizeof(MlptProtocol::DeviceDescriptor)) {
        log("DEVICE_ATTACH too short from " + client->address);
        return;
    }

    auto* desc = reinterpret_cast<const MlptProtocol::DeviceDescriptor*>(payload.data());
    size_t offset = sizeof(MlptProtocol::DeviceDescriptor);

    std::string name;
    if (desc->nameLen > 0 && offset + desc->nameLen <= payload.size()) {
        name.assign(reinterpret_cast<const char*>(payload.data() + offset), desc->nameLen);
        offset += desc->nameLen;
    }

    const uint8_t* usbDevDescr = nullptr;
    const uint8_t* usbConfDescr = nullptr;
    size_t usbDevDescrLen = desc->usbDescrDevLen;
    size_t usbConfDescrLen = desc->usbDescrConfLen;

    if (usbDevDescrLen > 0 && offset + usbDevDescrLen <= payload.size()) {
        usbDevDescr = payload.data() + offset;
        offset += usbDevDescrLen;
    }
    if (usbConfDescrLen > 0 && offset + usbConfDescrLen <= payload.size()) {
        usbConfDescr = payload.data() + offset;
        offset += usbConfDescrLen;
    }

    log("Device attach request from " + client->address +
        " deviceId=" + std::to_string(desc->deviceId) +
        " name=" + name +
        " VID:" + std::to_string(desc->vendorId) +
        " PID:" + std::to_string(desc->productId) +
        " devDescr:" + std::to_string(usbDevDescrLen) +
        " confDescr:" + std::to_string(usbConfDescrLen));

    MlptProtocol::DeviceAttachAckPayload ack{};
    ack.deviceId = desc->deviceId;

    if (!m_VhciManager.isDriverAvailable()) {
        ack.status = MlptProtocol::ATTACH_ERR_DRIVER;
        ack.vhciPort = 0;
        log("  -> Failed: VHCI driver not available");
    } else if (!usbDevDescr || !usbConfDescr) {
        ack.status = MlptProtocol::ATTACH_ERR_FAILED;
        ack.vhciPort = 0;
        log("  -> Failed: missing USB descriptors");
    } else {
        std::string serial(desc->serialNumber,
            strnlen(desc->serialNumber, sizeof(desc->serialNumber)));

        int port = m_VhciManager.attachDevice(
            desc->deviceId, desc->vendorId, desc->productId,
            usbDevDescr, usbDevDescrLen,
            usbConfDescr, usbConfDescrLen,
            serial);

        if (port >= 0) {
            ack.status = MlptProtocol::ATTACH_OK;
            ack.vhciPort = static_cast<uint8_t>(port);

            // Track ownership
            {
                std::lock_guard<std::mutex> lock(m_DeviceOwnersMutex);
                m_DeviceOwners[desc->deviceId] = client;
            }

            log("  -> Attached on VHCI port " + std::to_string(port));
            notifyStatusChange();
        } else {
            ack.status = MlptProtocol::ATTACH_ERR_FAILED;
            ack.vhciPort = 0;
            log("  -> Failed: VHCI plugin failed");
        }
    }

    sendMessage(client, MlptProtocol::MSG_DEVICE_ATTACH_ACK, &ack, sizeof(ack));
}

void PassthroughServer::handleDeviceDetach(ClientConnection* client,
                                            const std::vector<uint8_t>& payload)
{
    if (payload.size() < sizeof(MlptProtocol::DeviceDetachPayload)) {
        return;
    }

    auto* req = reinterpret_cast<const MlptProtocol::DeviceDetachPayload*>(payload.data());
    log("Device detach request from " + client->address +
        " deviceId=" + std::to_string(req->deviceId));

    // Verify the requesting client owns this device
    {
        std::lock_guard<std::mutex> lock(m_DeviceOwnersMutex);
        auto it = m_DeviceOwners.find(req->deviceId);
        if (it == m_DeviceOwners.end() || it->second != client) {
            log("  -> Rejected: client does not own device " + std::to_string(req->deviceId));
            return;
        }
    }

    m_VhciManager.detachDevice(req->deviceId);

    {
        std::lock_guard<std::mutex> lock(m_DeviceOwnersMutex);
        m_DeviceOwners.erase(req->deviceId);
    }

    MlptProtocol::DeviceDetachPayload ack{};
    ack.deviceId = req->deviceId;
    sendMessage(client, MlptProtocol::MSG_DEVICE_DETACH_ACK, &ack, sizeof(ack));

    log("  -> Detached");
    notifyStatusChange();
}

// ─── USB/IP URB forwarding ───

void PassthroughServer::forwardVhciUrbToClient(uint32_t deviceId,
                                                const uint8_t* nativeData, size_t nativeLen)
{
    // Called from VhciManager read thread when the driver has a URB for us.
    // Convert native usbip_header format to our MlptProtocol::UsbIpHeader and send.

    if (nativeLen < sizeof(NativeUsbIpHeader)) return;

    auto* native = reinterpret_cast<const NativeUsbIpHeader*>(nativeData);
    const uint8_t* trailingData = nativeData + sizeof(NativeUsbIpHeader);
    size_t trailingLen = nativeLen - sizeof(NativeUsbIpHeader);

    // Find the client that owns this device
    ClientConnection* owner = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_DeviceOwnersMutex);
        auto it = m_DeviceOwners.find(deviceId);
        if (it != m_DeviceOwners.end()) {
            owner = it->second;
        }
    }

    if (!owner || !owner->running) {
        return;
    }

    if (native->base.command == USBIP_CMD_SUBMIT) {
        // Convert CMD_SUBMIT to our UsbIpHeader format
        MlptProtocol::UsbIpHeader mlptHdr{};
        mlptHdr.seqNum = native->base.seqnum;
        mlptHdr.deviceId = deviceId;
        mlptHdr.direction = static_cast<uint8_t>(native->base.direction);
        mlptHdr.endpoint = static_cast<uint8_t>(native->base.ep);

        // Determine transfer type from endpoint
        if (native->base.ep == 0) {
            mlptHdr.transferType = MlptProtocol::USB_XFER_CONTROL;
        } else {
            // Look up endpoint type from config descriptor
            uint8_t epAddr = static_cast<uint8_t>(native->base.ep);
            if (native->base.direction == 1) {
                epAddr |= 0x80; // IN direction bit
            }
            int epType = m_VhciManager.getEndpointType(deviceId, epAddr);
            if (epType >= 0) {
                mlptHdr.transferType = static_cast<uint8_t>(epType);
            } else {
                // Fallback heuristic
                if (native->u.cmd_submit.number_of_packets > 0) {
                    mlptHdr.transferType = MlptProtocol::USB_XFER_ISOCHRONOUS;
                } else {
                    mlptHdr.transferType = MlptProtocol::USB_XFER_BULK;
                }
            }
        }

        // Check if setup packet is present (control transfers)
        bool hasSetup = false;
        for (int i = 0; i < 8; i++) {
            if (native->u.cmd_submit.setup[i] != 0) {
                hasSetup = true;
                break;
            }
        }
        mlptHdr.flags = hasSetup ? 1 : 0;

        mlptHdr.dataLen = static_cast<uint32_t>(
            native->u.cmd_submit.transfer_buffer_length > 0 ?
            native->u.cmd_submit.transfer_buffer_length : 0);
        mlptHdr.status = 0;
        mlptHdr.startFrame = static_cast<uint32_t>(native->u.cmd_submit.start_frame);
        mlptHdr.numIsoPackets = static_cast<uint32_t>(native->u.cmd_submit.number_of_packets);
        memcpy(mlptHdr.setupPacket, native->u.cmd_submit.setup, 8);

        // Build payload: UsbIpHeader + trailing data (OUT data / ISO descriptors)
        std::vector<uint8_t> payload(sizeof(mlptHdr) + trailingLen);
        memcpy(payload.data(), &mlptHdr, sizeof(mlptHdr));
        if (trailingLen > 0) {
            memcpy(payload.data() + sizeof(mlptHdr), trailingData, trailingLen);
        }

        sendMessage(owner, MlptProtocol::MSG_USBIP_SUBMIT,
                    payload.data(), static_cast<uint32_t>(payload.size()));

    } else if (native->base.command == USBIP_CMD_UNLINK) {
        // Convert CMD_UNLINK to our UsbIpHeader format
        MlptProtocol::UsbIpHeader mlptHdr{};
        mlptHdr.seqNum = native->base.seqnum;
        mlptHdr.deviceId = deviceId;
        // Store the seqnum to unlink in the dataLen field (repurposed for unlink)
        mlptHdr.dataLen = native->u.cmd_unlink.seqnum;
        mlptHdr.status = 0;

        sendMessage(owner, MlptProtocol::MSG_USBIP_UNLINK,
                    &mlptHdr, sizeof(mlptHdr));
    }
}

void PassthroughServer::handleUsbIpReturn(ClientConnection* client,
                                           const std::vector<uint8_t>& payload)
{
    // Client is sending back a URB completion (RET_SUBMIT).
    // Convert from our MlptProtocol::UsbIpHeader format to native usbip_header
    // and feed it to the VHCI driver via WriteFile.

    if (payload.size() < sizeof(MlptProtocol::UsbIpHeader)) {
        return;
    }

    auto* mlptHdr = reinterpret_cast<const MlptProtocol::UsbIpHeader*>(payload.data());
    const uint8_t* responseData = payload.data() + sizeof(MlptProtocol::UsbIpHeader);
    size_t responseDataLen = payload.size() - sizeof(MlptProtocol::UsbIpHeader);

    // Build native RET_SUBMIT header
    NativeUsbIpHeader native{};
    native.base.command = USBIP_RET_SUBMIT;
    native.base.seqnum = mlptHdr->seqNum;
    native.base.devid = mlptHdr->deviceId;
    native.base.direction = mlptHdr->direction;
    native.base.ep = mlptHdr->endpoint;

    native.u.ret_submit.status = mlptHdr->status;
    native.u.ret_submit.actual_length = static_cast<int32_t>(mlptHdr->dataLen);
    native.u.ret_submit.start_frame = static_cast<int32_t>(mlptHdr->startFrame);
    native.u.ret_submit.number_of_packets = static_cast<int32_t>(mlptHdr->numIsoPackets);
    native.u.ret_submit.error_count = 0;

    // Build the WriteFile buffer: native header + response data
    std::vector<uint8_t> writeBuffer(sizeof(native) + responseDataLen);
    memcpy(writeBuffer.data(), &native, sizeof(native));
    if (responseDataLen > 0) {
        memcpy(writeBuffer.data() + sizeof(native), responseData, responseDataLen);
    }

    if (!m_VhciManager.feedUrbReturn(mlptHdr->deviceId,
                                      writeBuffer.data(), writeBuffer.size())) {
        log("Failed to feed URB return for device " + std::to_string(mlptHdr->deviceId) +
            " seq=" + std::to_string(mlptHdr->seqNum));
    }
}

void PassthroughServer::log(const std::string& msg)
{
    if (m_LogCallback) {
        m_LogCallback(msg);
    } else {
        printf("[MLPT] %s\n", msg.c_str());
    }
}

int PassthroughServer::getClientCount() const
{
    std::lock_guard<std::mutex> lock(m_ClientsMutex);
    int count = 0;
    for (const auto& c : m_Clients) {
        if (c->running) count++;
    }
    return count;
}

int PassthroughServer::getDeviceCount() const
{
    std::lock_guard<std::mutex> lock(m_DeviceOwnersMutex);
    return static_cast<int>(m_DeviceOwners.size());
}

void PassthroughServer::notifyStatusChange()
{
    if (m_StatusCallback) {
        m_StatusCallback(getClientCount(), getDeviceCount());
    }
}
