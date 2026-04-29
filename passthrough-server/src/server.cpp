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

    // Create listening socket
    m_ListenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_ListenSocket == INVALID_SOCKET) {
        log("Failed to create socket: " + std::to_string(WSAGetLastError()));
        return false;
    }

    // Allow address reuse
    int opt = 1;
    setsockopt(m_ListenSocket, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));

    // Bind
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

    // Close listen socket to unblock accept()
    if (m_ListenSocket != INVALID_SOCKET) {
        closesocket(m_ListenSocket);
        m_ListenSocket = INVALID_SOCKET;
    }

    if (m_AcceptThread.joinable()) {
        m_AcceptThread.join();
    }

    // Close all client connections
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

        // Disable Nagle for low latency
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

        std::lock_guard<std::mutex> lock(m_ClientsMutex);
        m_Clients.push_back(std::move(client));
    }
}

void PassthroughServer::clientLoop(ClientConnection* client)
{
    while (client->running && m_Running) {
        // Read header
        uint8_t headerBuf[MlptProtocol::HEADER_SIZE];
        if (!recvExact(client->socket, headerBuf, MlptProtocol::HEADER_SIZE)) {
            break;
        }

        MlptProtocol::Header header;
        if (!MlptProtocol::validateHeader(headerBuf, header)) {
            log("Invalid magic from " + client->address + ", disconnecting");
            break;
        }

        // Sanity check payload size (max 16 MB)
        if (header.payloadLen > 16 * 1024 * 1024) {
            log("Payload too large from " + client->address + ": " + std::to_string(header.payloadLen));
            break;
        }

        // Read payload
        std::vector<uint8_t> payload(header.payloadLen);
        if (header.payloadLen > 0) {
            if (!recvExact(client->socket, payload.data(), header.payloadLen)) {
                break;
            }
        }

        processMessage(client, header, payload);
    }

    log("Client disconnected: " + client->address);
    client->running = false;

    if (client->socket != INVALID_SOCKET) {
        closesocket(client->socket);
        client->socket = INVALID_SOCKET;
    }
}

// ─── Message I/O ───

bool PassthroughServer::sendMessage(SOCKET sock, MlptProtocol::MsgType type,
                                     const void* payload, uint32_t payloadLen)
{
    uint8_t headerBuf[MlptProtocol::HEADER_SIZE];
    MlptProtocol::writeHeader(headerBuf, type, payloadLen);

    if (send(sock, reinterpret_cast<const char*>(headerBuf), MlptProtocol::HEADER_SIZE, 0) == SOCKET_ERROR)
        return false;

    if (payloadLen > 0 && payload) {
        if (send(sock, reinterpret_cast<const char*>(payload), payloadLen, 0) == SOCKET_ERROR)
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
    case MlptProtocol::MSG_KEEPALIVE:
        sendMessage(client->socket, MlptProtocol::MSG_KEEPALIVE);
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

    // Send HELLO_ACK
    MlptProtocol::HelloAckPayload ack{};
    ack.serverVersion = MlptProtocol::VERSION;
    ack.vhciAvailable = m_Config.vhciAvailable ? 1 : 0;
    memset(ack.reserved, 0, sizeof(ack.reserved));

    sendMessage(client->socket, MlptProtocol::MSG_HELLO_ACK, &ack, sizeof(ack));
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

    // Parse device descriptors
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

        log("  Device " + std::to_string(desc->deviceId) + ": " + name +
            " [" + (desc->transport == MlptProtocol::TRANSPORT_USB ? "USB" : "BT") + "]" +
            " VID:" + std::to_string(desc->vendorId) +
            " PID:" + std::to_string(desc->productId));
    }
}

void PassthroughServer::handleDeviceAttach(ClientConnection* client,
                                            const std::vector<uint8_t>& payload)
{
    if (payload.size() < sizeof(MlptProtocol::DeviceDetachPayload)) {
        return;
    }

    auto* req = reinterpret_cast<const MlptProtocol::DeviceDetachPayload*>(payload.data());
    log("Device attach request from " + client->address +
        " deviceId=" + std::to_string(req->deviceId));

    // TODO Phase 2: Actually create VHCI port and start USB/IP forwarding
    // For now, send an ACK with the appropriate status
    MlptProtocol::DeviceAttachAckPayload ack{};
    ack.deviceId = req->deviceId;

    if (m_Config.vhciAvailable) {
        ack.status = MlptProtocol::ATTACH_OK;
        ack.vhciPort = 1; // Placeholder
        log("  -> Attached (VHCI port " + std::to_string(ack.vhciPort) + ")");
    } else {
        ack.status = MlptProtocol::ATTACH_ERR_DRIVER;
        ack.vhciPort = 0;
        log("  -> Failed: VHCI driver not available");
    }

    sendMessage(client->socket, MlptProtocol::MSG_DEVICE_ATTACH_ACK, &ack, sizeof(ack));
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

    // TODO Phase 2: Remove VHCI port and stop USB/IP forwarding

    // Send ACK
    MlptProtocol::DeviceDetachPayload ack{};
    ack.deviceId = req->deviceId;
    sendMessage(client->socket, MlptProtocol::MSG_DEVICE_DETACH_ACK, &ack, sizeof(ack));

    log("  -> Detached");
}

void PassthroughServer::log(const std::string& msg)
{
    if (m_LogCallback) {
        m_LogCallback(msg);
    } else {
        printf("[MLPT] %s\n", msg.c_str());
    }
}
