// UsbIpDaemon — USB/IP server implementation for client-side device export
// Handles connections from usbip-win2 VHCI driver and forwards URBs via libusb

#include "usbipdaemon.h"
#include "usbipexporter.h"

#include <QHostAddress>
#include <QtEndian>

// ============================================================================
// Byte-swap helpers (USB/IP wire = big-endian / network byte order)
// ============================================================================

static inline uint16_t bswap16(uint16_t v) { return qToBigEndian(v); }
static inline uint32_t bswap32(uint32_t v) { return qToBigEndian(v); }
static inline int32_t  bswap32s(int32_t v) { return static_cast<int32_t>(qToBigEndian(static_cast<uint32_t>(v))); }

static inline uint16_t fromBE16(uint16_t v) { return qFromBigEndian(v); }
static inline uint32_t fromBE32(uint32_t v) { return qFromBigEndian(v); }
static inline int32_t  fromBE32s(int32_t v) { return static_cast<int32_t>(qFromBigEndian(static_cast<uint32_t>(v))); }

void UsbIpDaemon::byteswapHeader(UsbIpWireHeader& hdr)
{
    hdr.command   = fromBE32(hdr.command);
    hdr.seqnum    = fromBE32(hdr.seqnum);
    hdr.devid     = fromBE32(hdr.devid);
    hdr.direction = fromBE32(hdr.direction);
    hdr.ep        = fromBE32(hdr.ep);

    // Swap the union based on command
    switch (hdr.command) {
    case USBIP_CMD_SUBMIT:
        hdr.u.cmd_submit.transfer_flags = fromBE32(hdr.u.cmd_submit.transfer_flags);
        hdr.u.cmd_submit.transfer_buffer_length = fromBE32s(hdr.u.cmd_submit.transfer_buffer_length);
        hdr.u.cmd_submit.start_frame = fromBE32s(hdr.u.cmd_submit.start_frame);
        hdr.u.cmd_submit.number_of_packets = fromBE32s(hdr.u.cmd_submit.number_of_packets);
        hdr.u.cmd_submit.interval = fromBE32s(hdr.u.cmd_submit.interval);
        // setup[8] is byte array, no swap needed
        break;
    case USBIP_CMD_UNLINK:
        hdr.u.cmd_unlink.seqnum = fromBE32(hdr.u.cmd_unlink.seqnum);
        break;
    case USBIP_RET_SUBMIT:
        hdr.u.ret_submit.status = fromBE32s(hdr.u.ret_submit.status);
        hdr.u.ret_submit.actual_length = fromBE32s(hdr.u.ret_submit.actual_length);
        hdr.u.ret_submit.start_frame = fromBE32s(hdr.u.ret_submit.start_frame);
        hdr.u.ret_submit.number_of_packets = fromBE32s(hdr.u.ret_submit.number_of_packets);
        hdr.u.ret_submit.error_count = fromBE32s(hdr.u.ret_submit.error_count);
        break;
    case USBIP_RET_UNLINK:
        hdr.u.ret_unlink.status = fromBE32s(hdr.u.ret_unlink.status);
        break;
    }
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

UsbIpDaemon::UsbIpDaemon(QObject* parent)
    : QObject(parent)
{
    connect(&m_Server, &QTcpServer::newConnection, this, &UsbIpDaemon::onNewConnection);
}

UsbIpDaemon::~UsbIpDaemon()
{
    stop();
}

// ============================================================================
// Start / Stop
// ============================================================================

bool UsbIpDaemon::start(uint16_t port)
{
    if (m_Server.isListening()) {
        return true;
    }

    // Listen on all interfaces so the server's VHCI driver can connect
    if (!m_Server.listen(QHostAddress::Any, port)) {
        qWarning("[UsbIpDaemon] Failed to listen on port %u: %s",
                 port, qPrintable(m_Server.errorString()));
        return false;
    }

    qInfo("[UsbIpDaemon] Listening on port %u", m_Server.serverPort());
    return true;
}

void UsbIpDaemon::stop()
{
    m_Server.close();

    // Close all active sessions
    for (auto it = m_Sessions.begin(); it != m_Sessions.end(); ++it) {
        it.key()->close();
        delete it.value();
    }
    m_Sessions.clear();
    m_DeviceSessions.clear();
}

uint16_t UsbIpDaemon::port() const
{
    return m_Server.serverPort();
}

// ============================================================================
// Device export management
// ============================================================================

QString UsbIpDaemon::makeBusid(uint32_t deviceId)
{
    // Create a synthetic busid like "0-<deviceId>"
    // Must be < 32 chars and unique per device
    return QString("0-%1").arg(deviceId);
}

void UsbIpDaemon::exportDevice(const QString& busid, uint32_t deviceId, UsbIpExporter* exporter)
{
    QMutexLocker lock(&m_ExportMutex);
    m_Exports[busid] = { exporter, deviceId };
    qInfo("[UsbIpDaemon] Exported device %u as busid '%s'",
          deviceId, qPrintable(busid));
}

void UsbIpDaemon::unexportDevice(const QString& busid)
{
    QMutexLocker lock(&m_ExportMutex);

    auto it = m_Exports.find(busid);
    if (it == m_Exports.end()) return;

    uint32_t deviceId = it->deviceId;
    m_Exports.erase(it);

    // Close any active session for this device
    auto sessionIt = m_DeviceSessions.find(deviceId);
    if (sessionIt != m_DeviceSessions.end()) {
        auto* session = sessionIt.value();
        session->socket->close();
    }

    qInfo("[UsbIpDaemon] Unexported device %u (busid '%s')", deviceId, qPrintable(busid));
}

// ============================================================================
// Connection handling
// ============================================================================

void UsbIpDaemon::onNewConnection()
{
    while (QTcpSocket* socket = m_Server.nextPendingConnection()) {
        qInfo("[UsbIpDaemon] New connection from %s:%u",
              qPrintable(socket->peerAddress().toString()), socket->peerPort());

        auto* session = new DaemonDeviceSession;
        session->socket = socket;

        m_Sessions[socket] = session;

        connect(socket, &QTcpSocket::readyRead, this, &UsbIpDaemon::onSessionReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &UsbIpDaemon::onSessionDisconnected);
    }
}

void UsbIpDaemon::onSessionReadyRead()
{
    auto* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    auto sessionIt = m_Sessions.find(socket);
    if (sessionIt == m_Sessions.end()) return;

    auto* session = sessionIt.value();
    session->recvBuffer.append(socket->readAll());

    if (!session->importDone) {
        // Waiting for OP_REQ_IMPORT
        handleImportRequest(session);
    } else {
        // URB exchange phase
        handleUrbData(session);
    }
}

void UsbIpDaemon::onSessionDisconnected()
{
    auto* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    auto sessionIt = m_Sessions.find(socket);
    if (sessionIt == m_Sessions.end()) return;

    auto* session = sessionIt.value();
    qInfo("[UsbIpDaemon] Session disconnected for busid '%s' device %u",
          qPrintable(session->busid), session->deviceId);

    if (session->exporter) {
        disconnect(session->exporter, nullptr, this, nullptr);
    }

    m_DeviceSessions.remove(session->deviceId);
    m_Sessions.erase(sessionIt);

    if (session->deviceId != 0) {
        emit deviceDisconnected(session->busid, session->deviceId);
    }

    delete session;
    socket->deleteLater();
}

// ============================================================================
// OP_REQ_IMPORT handling
// ============================================================================

bool UsbIpDaemon::handleImportRequest(DaemonDeviceSession* session)
{
    constexpr int importReqSize = sizeof(UsbIpOpCommon) + sizeof(UsbIpOpImportRequest);

    if (session->recvBuffer.size() < importReqSize) {
        return false;  // Not enough data yet
    }

    // Parse the import request
    UsbIpOpCommon opCommon;
    memcpy(&opCommon, session->recvBuffer.constData(), sizeof(opCommon));
    opCommon.version = fromBE16(opCommon.version);
    opCommon.code = fromBE16(opCommon.code);
    opCommon.status = fromBE32(opCommon.status);

    if (opCommon.code != OP_REQ_IMPORT) {
        qWarning("[UsbIpDaemon] Expected OP_REQ_IMPORT (0x%04X), got 0x%04X",
                 OP_REQ_IMPORT, opCommon.code);
        session->socket->close();
        return false;
    }

    UsbIpOpImportRequest importReq;
    memcpy(&importReq, session->recvBuffer.constData() + sizeof(UsbIpOpCommon), sizeof(importReq));
    session->recvBuffer.remove(0, importReqSize);

    QString busid = QString::fromUtf8(importReq.busid,
        static_cast<int>(strnlen(importReq.busid, sizeof(importReq.busid))));

    qInfo("[UsbIpDaemon] OP_REQ_IMPORT for busid '%s'", qPrintable(busid));

    // Look up the exported device
    UsbIpExporter* exporter = nullptr;
    uint32_t deviceId = 0;
    {
        QMutexLocker lock(&m_ExportMutex);
        auto it = m_Exports.find(busid);
        if (it != m_Exports.end()) {
            exporter = it->exporter;
            deviceId = it->deviceId;
        }
    }

    // Build OP_REP_IMPORT reply
    QByteArray reply;
    reply.resize(sizeof(UsbIpOpCommon) + sizeof(UsbIpUsbDevice));
    memset(reply.data(), 0, reply.size());

    auto* repCommon = reinterpret_cast<UsbIpOpCommon*>(reply.data());
    repCommon->version = bswap16(USBIP_VERSION);
    repCommon->code = bswap16(OP_REP_IMPORT);

    if (!exporter || !exporter->isOpen()) {
        // Device not found or not open
        repCommon->status = bswap32(1);  // ST_NA
        session->socket->write(reply.constData(), sizeof(UsbIpOpCommon));
        qWarning("[UsbIpDaemon] Device not found for busid '%s'", qPrintable(busid));
        session->socket->close();
        return false;
    }

    // Fill device info from the exporter's USB descriptors
    repCommon->status = bswap32(0);  // ST_OK

    auto* udev = reinterpret_cast<UsbIpUsbDevice*>(reply.data() + sizeof(UsbIpOpCommon));
    memset(udev, 0, sizeof(UsbIpUsbDevice));

    // path and busid
    QByteArray busidUtf8 = busid.toUtf8();
    strncpy(udev->busid, busidUtf8.constData(), sizeof(udev->busid) - 1);
    strncpy(udev->path, "/virtual/0", sizeof(udev->path) - 1);

    // Generate devid from deviceId: busnum=0, devnum=deviceId
    uint32_t busnum = 0;
    uint32_t devnum = deviceId & 0xFFFF;
    udev->busnum = bswap32(busnum);
    udev->devnum = bswap32(devnum);

    // Extract info from the device descriptor
    QByteArray devDesc = exporter->deviceDescriptor();
    if (devDesc.size() >= 18) {
        auto* d = reinterpret_cast<const uint8_t*>(devDesc.constData());
        udev->idVendor  = bswap16(static_cast<uint16_t>(d[8] | (d[9] << 8)));
        udev->idProduct = bswap16(static_cast<uint16_t>(d[10] | (d[11] << 8)));
        udev->bcdDevice = bswap16(static_cast<uint16_t>(d[12] | (d[13] << 8)));
        udev->bDeviceClass    = d[4];
        udev->bDeviceSubClass = d[5];
        udev->bDeviceProtocol = d[6];
        udev->bNumConfigurations = d[17];
    }

    // Speed
    udev->speed = bswap32(static_cast<uint32_t>(exporter->usbSpeed()));

    // Configuration value and interface count from config descriptor
    QByteArray confDesc = exporter->configDescriptor();
    if (confDesc.size() >= 9) {
        auto* c = reinterpret_cast<const uint8_t*>(confDesc.constData());
        udev->bConfigurationValue = c[5];
        udev->bNumInterfaces = c[4];
    }

    session->socket->write(reply);

    // Import successful — set up the session for URB exchange
    session->busid = busid;
    session->deviceId = deviceId;
    session->exporter = exporter;
    session->devid = (busnum << 16) | devnum;
    session->importDone = true;

    m_DeviceSessions[deviceId] = session;

    // Connect URB completion signal
    connect(exporter, &UsbIpExporter::urbCompleted,
            this, &UsbIpDaemon::onUrbCompleted);

    qInfo("[UsbIpDaemon] Device %u (busid '%s') imported successfully",
          deviceId, qPrintable(busid));
    emit deviceImported(busid, deviceId);

    // Process any remaining data in the buffer (could be URBs already)
    if (!session->recvBuffer.isEmpty()) {
        handleUrbData(session);
    }

    return true;
}

// ============================================================================
// URB exchange
// ============================================================================

void UsbIpDaemon::handleUrbData(DaemonDeviceSession* session)
{
    while (session->recvBuffer.size() >= static_cast<int>(sizeof(UsbIpWireHeader))) {
        // Peek at the header to determine how much data we need
        UsbIpWireHeader hdr;
        memcpy(&hdr, session->recvBuffer.constData(), sizeof(hdr));
        byteswapHeader(hdr);

        int totalSize = sizeof(UsbIpWireHeader);

        if (hdr.command == USBIP_CMD_SUBMIT) {
            // For OUT transfers, data follows the header
            bool isOut = (hdr.direction == 0);
            int32_t bufLen = hdr.u.cmd_submit.transfer_buffer_length;

            if (isOut && bufLen > 0) {
                totalSize += bufLen;
            }

            // ISO packet descriptors follow data for isochronous transfers
            if (hdr.u.cmd_submit.number_of_packets > 0) {
                totalSize += hdr.u.cmd_submit.number_of_packets *
                             static_cast<int>(sizeof(UsbIpWireIsoPacket));
            }
        }
        // CMD_UNLINK has no additional data

        if (session->recvBuffer.size() < totalSize) {
            break;  // Wait for more data
        }

        // Extract the complete message
        QByteArray trailingData;
        if (totalSize > static_cast<int>(sizeof(UsbIpWireHeader))) {
            trailingData = session->recvBuffer.mid(sizeof(UsbIpWireHeader),
                                                    totalSize - sizeof(UsbIpWireHeader));
        }
        session->recvBuffer.remove(0, totalSize);

        // Process
        if (hdr.command == USBIP_CMD_SUBMIT) {
            processCmdSubmit(session, hdr, trailingData);
        } else if (hdr.command == USBIP_CMD_UNLINK) {
            processCmdUnlink(session, hdr);
        } else {
            qWarning("[UsbIpDaemon] Unknown command 0x%08X from driver", hdr.command);
        }
    }
}

void UsbIpDaemon::processCmdSubmit(DaemonDeviceSession* session,
                                    const UsbIpWireHeader& hdr,
                                    const QByteArray& data)
{
    if (!session->exporter) return;

    // Convert USB/IP wire header to MlptProtocol::UsbIpHeader for UsbIpExporter
    MlptProtocol::UsbIpHeader mlptHdr{};
    mlptHdr.seqNum = hdr.seqnum;
    mlptHdr.deviceId = session->deviceId;
    mlptHdr.direction = static_cast<uint8_t>(hdr.direction);
    mlptHdr.endpoint = static_cast<uint8_t>(hdr.ep);

    // Determine transfer type from endpoint
    if (hdr.ep == 0) {
        mlptHdr.transferType = MlptProtocol::USB_XFER_CONTROL;
    } else {
        // Use the exporter's endpoint type knowledge
        // For now, use heuristic based on the submit fields
        if (hdr.u.cmd_submit.number_of_packets > 0) {
            mlptHdr.transferType = MlptProtocol::USB_XFER_ISOCHRONOUS;
        } else if (hdr.u.cmd_submit.interval > 0) {
            mlptHdr.transferType = MlptProtocol::USB_XFER_INTERRUPT;
        } else {
            mlptHdr.transferType = MlptProtocol::USB_XFER_BULK;
        }
    }

    // Check if setup packet is present
    bool hasSetup = false;
    for (int i = 0; i < 8; i++) {
        if (hdr.u.cmd_submit.setup[i] != 0) {
            hasSetup = true;
            break;
        }
    }
    mlptHdr.flags = hasSetup ? 1 : 0;

    mlptHdr.dataLen = static_cast<uint32_t>(
        hdr.u.cmd_submit.transfer_buffer_length > 0 ?
        hdr.u.cmd_submit.transfer_buffer_length : 0);
    mlptHdr.status = 0;
    mlptHdr.startFrame = static_cast<uint32_t>(hdr.u.cmd_submit.start_frame);
    mlptHdr.numIsoPackets = static_cast<uint32_t>(
        hdr.u.cmd_submit.number_of_packets > 0 ?
        hdr.u.cmd_submit.number_of_packets : 0);
    memcpy(mlptHdr.setupPacket, hdr.u.cmd_submit.setup, 8);

    // Submit the URB via the exporter (libusb)
    session->exporter->submitUrb(mlptHdr, data);
}

void UsbIpDaemon::processCmdUnlink(DaemonDeviceSession* session,
                                    const UsbIpWireHeader& hdr)
{
    if (!session->exporter) return;

    uint32_t unlinkSeqnum = hdr.u.cmd_unlink.seqnum;
    session->exporter->unlinkUrb(unlinkSeqnum);

    // Send RET_UNLINK immediately
    sendRetUnlink(session, hdr.seqnum, 0);
}

// ============================================================================
// URB completion callback (from UsbIpExporter via libusb)
// ============================================================================

void UsbIpDaemon::onUrbCompleted(uint32_t deviceId,
                                  const MlptProtocol::UsbIpHeader& header,
                                  const QByteArray& data)
{
    auto sessionIt = m_DeviceSessions.find(deviceId);
    if (sessionIt == m_DeviceSessions.end()) return;

    auto* session = sessionIt.value();
    sendRetSubmit(session, header, data);
}

// ============================================================================
// Send responses back to the VHCI driver
// ============================================================================

void UsbIpDaemon::sendRetSubmit(DaemonDeviceSession* session,
                                 const MlptProtocol::UsbIpHeader& mlptHdr,
                                 const QByteArray& data)
{
    if (!session->socket || session->socket->state() != QAbstractSocket::ConnectedState) return;

    // Build the wire header (big-endian)
    UsbIpWireHeader wireHdr{};
    memset(&wireHdr, 0, sizeof(wireHdr));

    wireHdr.command   = bswap32(USBIP_RET_SUBMIT);
    wireHdr.seqnum    = bswap32(mlptHdr.seqNum);
    wireHdr.devid     = bswap32(session->devid);
    wireHdr.direction = bswap32(static_cast<uint32_t>(mlptHdr.direction));
    wireHdr.ep        = bswap32(static_cast<uint32_t>(mlptHdr.endpoint));

    wireHdr.u.ret_submit.status           = bswap32s(mlptHdr.status);
    wireHdr.u.ret_submit.actual_length    = bswap32s(static_cast<int32_t>(mlptHdr.dataLen));
    wireHdr.u.ret_submit.start_frame      = bswap32s(static_cast<int32_t>(mlptHdr.startFrame));
    wireHdr.u.ret_submit.number_of_packets = bswap32s(static_cast<int32_t>(mlptHdr.numIsoPackets));
    wireHdr.u.ret_submit.error_count      = 0;

    session->socket->write(reinterpret_cast<const char*>(&wireHdr), sizeof(wireHdr));

    // Write transfer data (for IN transfers)
    if (!data.isEmpty()) {
        session->socket->write(data);
    }
}

void UsbIpDaemon::sendRetUnlink(DaemonDeviceSession* session,
                                 uint32_t seqnum, int32_t status)
{
    if (!session->socket || session->socket->state() != QAbstractSocket::ConnectedState) return;

    UsbIpWireHeader wireHdr{};
    memset(&wireHdr, 0, sizeof(wireHdr));

    wireHdr.command   = bswap32(USBIP_RET_UNLINK);
    wireHdr.seqnum    = bswap32(seqnum);
    wireHdr.devid     = bswap32(session->devid);
    wireHdr.u.ret_unlink.status = bswap32s(status);

    session->socket->write(reinterpret_cast<const char*>(&wireHdr), sizeof(wireHdr));
}
