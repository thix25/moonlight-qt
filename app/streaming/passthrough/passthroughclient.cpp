#include "passthroughclient.h"
#include "usbipexporter.h"
#include "bthidcapture.h"
#include "usbipdaemon.h"

#include <QRandomGenerator>
#include <QtDebug>

PassthroughClient::PassthroughClient(QObject* parent)
    : QObject(parent)
    , m_ServerPort(MlptProtocol::DEFAULT_PORT)
    , m_Connected(false)
    , m_VhciAvailable(false)
    , m_StatusText(tr("Not connected"))
    , m_Daemon(nullptr)
    , m_ServerBackend(MlptProtocol::VHCI_BACKEND_LEGACY)
    , m_ReconnectAttempts(0)
{
    memset(m_SessionId, 0, sizeof(m_SessionId));

    // Register UsbIpHeader for cross-thread queued signal/slot delivery.
    // Without this, urbCompleted signals emitted from the libusb event
    // thread are silently dropped and no URBs ever complete.
    qRegisterMetaType<MlptProtocol::UsbIpHeader>();

    // Initialize libusb
    UsbIpExporter::initLibusb();

    connect(&m_Socket, &QTcpSocket::connected, this, &PassthroughClient::onSocketConnected);
    connect(&m_Socket, &QTcpSocket::disconnected, this, &PassthroughClient::onSocketDisconnected);
    connect(&m_Socket, &QTcpSocket::readyRead, this, &PassthroughClient::onReadyRead);
    connect(&m_Socket, &QTcpSocket::errorOccurred, this, &PassthroughClient::onSocketError);

    m_KeepaliveTimer.setInterval(10000); // 10 seconds
    connect(&m_KeepaliveTimer, &QTimer::timeout, this, &PassthroughClient::onKeepaliveTimer);

    m_ReconnectTimer.setSingleShot(true);
    connect(&m_ReconnectTimer, &QTimer::timeout, this, &PassthroughClient::onReconnectTimer);

    // Initialize device enumeration
    m_DeviceEnumerator.enumerate();

    // Handle hot-plug device removal: auto-detach forwarded devices that were unplugged
    connect(&m_DeviceEnumerator, &DeviceEnumerator::deviceRemoved, this,
        [this](uint32_t deviceId) {
            if (m_Exporters.contains(deviceId) || m_BtCaptures.contains(deviceId)) {
                qInfo() << "Passthrough: forwarded device" << deviceId << "was unplugged, detaching";
                detachDevice(deviceId);
            }
        });
}

PassthroughClient::~PassthroughClient()
{
    disconnectFromServer();
    cleanupAllExporters();
    cleanupAllBtCaptures();
    if (m_Daemon) {
        m_Daemon->stop();
        delete m_Daemon;
        m_Daemon = nullptr;
    }
}

void PassthroughClient::connectToServer(const QString& address, uint16_t port)
{
    m_ServerAddress = address;
    m_ServerPort = port;
    m_ReconnectAttempts = 0;

    // Generate a random session ID
    QRandomGenerator* rng = QRandomGenerator::global();
    for (int i = 0; i < 16; i++) {
        m_SessionId[i] = static_cast<uint8_t>(rng->generate());
    }

    setStatusText(tr("Connecting to %1:%2...").arg(address).arg(port));
    m_Socket.connectToHost(address, port);
}

void PassthroughClient::disconnectFromServer()
{
    m_KeepaliveTimer.stop();
    m_ReconnectTimer.stop();
    m_DeviceEnumerator.stopHotplugPolling();
    m_ReconnectAttempts = MAX_RECONNECT_ATTEMPTS; // Prevent reconnect

    // Release all forwarded devices back to client
    cleanupAllExporters();
    cleanupAllBtCaptures();

    // Stop USB/IP daemon if running
    if (m_Daemon) {
        m_Daemon->stop();
    }
    m_ServerBackend = MlptProtocol::VHCI_BACKEND_LEGACY;

    if (m_Socket.state() != QAbstractSocket::UnconnectedState) {
        m_Socket.disconnectFromHost();
    }

    setConnected(false);
    setStatusText(tr("Disconnected"));
}

void PassthroughClient::attachDevice(uint32_t deviceId)
{
    if (!m_Connected) {
        qWarning() << "Cannot attach device: not connected";
        return;
    }

    // Check if already exporting — this can happen if the model's
    // isForwarding flag got out of sync (e.g. after a failed detach).
    // Re-sync the model flag and return success instead of silently
    // ignoring the request, which would leave the UI switch stuck.
    if (m_Exporters.contains(deviceId) || m_BtCaptures.contains(deviceId)) {
        qWarning() << "Device" << deviceId << "already being exported, syncing model";
        m_DeviceEnumerator.setDeviceForwarding(deviceId, true);
        return;
    }

    // Find device info from enumerator
    const auto& devices = m_DeviceEnumerator.devices();
    const PassthroughDevice* devInfo = nullptr;
    for (const auto& d : devices) {
        if (d.deviceId == deviceId) {
            devInfo = &d;
            break;
        }
    }

    if (!devInfo) {
        qWarning() << "Device" << deviceId << "not found in enumerator";
        emit deviceAttachFailed(deviceId, MlptProtocol::ATTACH_ERR_FAILED);
        return;
    }

    // USB devices: use libusb (UsbIpExporter)
    // Bluetooth HID devices: use Windows HID API (BtHidCapture)
    // Bluetooth non-HID devices: not supported yet
    if (devInfo->transport == MlptProtocol::TRANSPORT_USB) {
        // Open device with libusb
        auto* exporter = new UsbIpExporter(this);
        exporter->setDeviceId(deviceId);

        if (!exporter->openDevice(devInfo->vendorId, devInfo->productId, devInfo->serialNumber)) {
            qWarning() << "Failed to open device" << deviceId
                       << devInfo->name
                       << QString::asprintf("(%04x:%04x)", devInfo->vendorId, devInfo->productId)
                       << "with libusb";
            delete exporter;
            emit deviceAttachFailed(deviceId, MlptProtocol::ATTACH_ERR_FAILED);
            return;
        }

        if (m_ServerBackend == MlptProtocol::VHCI_BACKEND_WIN2 && m_Daemon) {
            // Win2 mode: register device with the USB/IP daemon.
            // The VHCI driver will connect directly to our daemon for URB exchange.
            // No URB routing through MLPT TCP is needed.
            QString busid = UsbIpDaemon::makeBusid(deviceId);
            m_Daemon->exportDevice(busid, deviceId, exporter);
        } else {
            // Legacy mode: URBs flow through MLPT TCP
            // Connect URB completion signal to send results over MLPT
            connect(exporter, &UsbIpExporter::urbCompleted, this,
                [this](uint32_t devId, const MlptProtocol::UsbIpHeader& header, const QByteArray& data) {
                    Q_UNUSED(devId);
                    QByteArray payload(reinterpret_cast<const char*>(&header), sizeof(header));
                    payload.append(data);
                    sendMessage(MlptProtocol::MSG_USBIP_RETURN, payload);
                });
        }

        // Connect device disconnection signal
        connect(exporter, &UsbIpExporter::deviceDisconnected, this,
            [this](uint32_t devId) {
                qInfo() << "Device" << devId << "disconnected unexpectedly";
                cleanupExporter(devId);
                detachDevice(devId);
            });

        m_Exporters.insert(deviceId, exporter);

        // Send DEVICE_ATTACH with USB descriptors
        sendDeviceAttachWithDescriptors(deviceId, exporter);
        startAttachTimeout(deviceId);

        qInfo() << "Requested attach for USB device" << deviceId
                << devInfo->name
                << QString::asprintf("(%04x:%04x)", devInfo->vendorId, devInfo->productId)
                << "(captured with libusb, descriptors sent)"
                << (m_ServerBackend == MlptProtocol::VHCI_BACKEND_WIN2 ? "[win2]" : "[legacy]");

    } else if (devInfo->transport == MlptProtocol::TRANSPORT_BLUETOOTH) {
        // Check if this is a HID-capable BT device
        uint8_t dc = devInfo->deviceClass;
        if (dc != MlptProtocol::DEVCLASS_HID_KEYBOARD &&
            dc != MlptProtocol::DEVCLASS_HID_MOUSE &&
            dc != MlptProtocol::DEVCLASS_HID_GAMEPAD &&
            dc != MlptProtocol::DEVCLASS_HID_OTHER) {
            qWarning() << "Device" << deviceId << "is a non-HID Bluetooth device, cannot forward";
            emit deviceAttachFailed(deviceId, MlptProtocol::ATTACH_ERR_FAILED);
            return;
        }

        auto* capture = new BtHidCapture(this);
        capture->setDeviceId(deviceId);

        // Open using BT address (stored in serialNumber) + VID/PID if available
        if (!capture->openDevice(devInfo->serialNumber, devInfo->vendorId, devInfo->productId)) {
            qWarning() << "Failed to open BT HID device" << deviceId << devInfo->name;
            delete capture;
            emit deviceAttachFailed(deviceId, MlptProtocol::ATTACH_ERR_FAILED);
            return;
        }

        if (m_ServerBackend == MlptProtocol::VHCI_BACKEND_WIN2 && m_Daemon) {
            // Win2 mode: BT HID also goes through daemon.
            // BtHidCapture emits urbCompleted like UsbIpExporter.
            // We need a thin adapter. For now, connect the same way.
            // Note: BtHidCapture is not a UsbIpExporter, so we can't directly
            // export it. In win2 mode, BT HID will still use legacy URB relay.
            connect(capture, &BtHidCapture::urbCompleted, this,
                [this](uint32_t devId, const MlptProtocol::UsbIpHeader& header, const QByteArray& data) {
                    Q_UNUSED(devId);
                    QByteArray payload(reinterpret_cast<const char*>(&header), sizeof(header));
                    payload.append(data);
                    sendMessage(MlptProtocol::MSG_USBIP_RETURN, payload);
                });
        } else {
            // Legacy mode: URBs flow through MLPT TCP
            connect(capture, &BtHidCapture::urbCompleted, this,
                [this](uint32_t devId, const MlptProtocol::UsbIpHeader& header, const QByteArray& data) {
                    Q_UNUSED(devId);
                    QByteArray payload(reinterpret_cast<const char*>(&header), sizeof(header));
                    payload.append(data);
                    sendMessage(MlptProtocol::MSG_USBIP_RETURN, payload);
                });
        }

        // Connect device disconnection signal
        connect(capture, &BtHidCapture::deviceDisconnected, this,
            [this](uint32_t devId) {
                qInfo() << "BT device" << devId << "disconnected unexpectedly";
                cleanupBtCapture(devId);
                detachDevice(devId);
            });

        m_BtCaptures.insert(deviceId, capture);

        // Send DEVICE_ATTACH with synthesized USB descriptors
        sendDeviceAttachWithDescriptors(deviceId, capture);
        startAttachTimeout(deviceId);

        qInfo() << "Requested attach for BT HID device" << deviceId
                << "(captured with HID API, descriptors sent)";

    } else {
        qWarning() << "Device" << deviceId << "has unsupported transport" << devInfo->transport;
        emit deviceAttachFailed(deviceId, MlptProtocol::ATTACH_ERR_FAILED);
        return;
    }
}

void PassthroughClient::detachDevice(uint32_t deviceId)
{
    if (!m_Connected) {
        return;
    }

    cancelAttachTimeout(deviceId);
    cleanupExporter(deviceId);
    cleanupBtCapture(deviceId);

    MlptProtocol::DeviceDetachPayload payload;
    payload.deviceId = deviceId;

    QByteArray data(reinterpret_cast<const char*>(&payload), sizeof(payload));
    sendMessage(MlptProtocol::MSG_DEVICE_DETACH, data);

    qInfo() << "Requested detach for device" << deviceId;
}

void PassthroughClient::refreshDevices()
{
    // Use pollHotplug() instead of enumerate() to preserve forwarding
    // state and device IDs for devices that are currently being exported.
    // enumerate() wipes everything and reassigns IDs from 1, which
    // desyncs the model from the active exporters in m_Exporters.
    m_DeviceEnumerator.pollHotplug();
    if (m_Connected) {
        sendDeviceList();
    }
}

void PassthroughClient::autoAttachDevices()
{
    if (!m_Connected || !m_VhciAvailable) return;

    QList<uint32_t> autoIds = m_DeviceEnumerator.getAutoForwardDeviceIds();
    if (autoIds.isEmpty()) return;

    qInfo() << "Passthrough: auto-attaching" << autoIds.size() << "devices";

    for (uint32_t id : autoIds) {
        // Skip if already forwarding
        if (m_Exporters.contains(id) || m_BtCaptures.contains(id)) continue;

        attachDevice(id);
    }
}

// ─── Socket event handlers ───

void PassthroughClient::onSocketConnected()
{
    qInfo() << "Passthrough: TCP connected to" << m_ServerAddress;

    // Disable Nagle's algorithm for lower latency
    m_Socket.setSocketOption(QAbstractSocket::LowDelayOption, 1);

    m_ReconnectAttempts = 0;
    sendHello();
    setStatusText(tr("Handshaking..."));
}

void PassthroughClient::onSocketDisconnected()
{
    qInfo() << "Passthrough: disconnected from server";

    m_KeepaliveTimer.stop();
    // Stop hotplug polling so it doesn't keep running while disconnected
    // and doesn't cause double-polling after reconnect.
    m_DeviceEnumerator.stopHotplugPolling();
    setConnected(false);

    // Cancel all pending attach timeouts
    for (auto* timer : m_PendingAttachTimers) {
        timer->stop();
        timer->deleteLater();
    }
    m_PendingAttachTimers.clear();

    // Clean up all forwarding on disconnect — return devices to client
    cleanupAllExporters();
    cleanupAllBtCaptures();

    // Reset all forwarding flags in the enumerator
    for (const auto& dev : m_DeviceEnumerator.devices()) {
        if (dev.isForwarding) {
            m_DeviceEnumerator.setDeviceForwarding(dev.deviceId, false);
        }
    }

    scheduleReconnect();
}

void PassthroughClient::onSocketError(QAbstractSocket::SocketError error)
{
    qWarning() << "Passthrough socket error:" << error
               << m_Socket.errorString()
               << "(target:" << m_ServerAddress << ":" << m_ServerPort << ")";

    if (!m_Connected) {
        scheduleReconnect();
    }
}

void PassthroughClient::onReadyRead()
{
    m_ReceiveBuffer.append(m_Socket.readAll());

    while (static_cast<size_t>(m_ReceiveBuffer.size()) >= MlptProtocol::HEADER_SIZE) {
        MlptProtocol::Header header;
        if (!MlptProtocol::validateHeader(
                reinterpret_cast<const uint8_t*>(m_ReceiveBuffer.constData()), header)) {
            qWarning() << "Passthrough: invalid magic, dropping connection";
            m_Socket.disconnectFromHost();
            return;
        }

        // Reject absurdly large payloads (16 MB limit)
        if (header.payloadLen > 16 * 1024 * 1024) {
            qWarning() << "Passthrough: payload too large (" << header.payloadLen << "), dropping connection";
            m_Socket.disconnectFromHost();
            return;
        }

        size_t totalSize = MlptProtocol::HEADER_SIZE + header.payloadLen;
        if (static_cast<size_t>(m_ReceiveBuffer.size()) < totalSize) {
            break; // Wait for more data
        }

        QByteArray payload = m_ReceiveBuffer.mid(MlptProtocol::HEADER_SIZE, header.payloadLen);
        m_ReceiveBuffer.remove(0, static_cast<int>(totalSize));

        processMessage(header, payload);
    }
}

void PassthroughClient::onKeepaliveTimer()
{
    sendMessage(MlptProtocol::MSG_KEEPALIVE);
}

void PassthroughClient::onReconnectTimer()
{
    if (m_ServerAddress.isEmpty()) return;

    setStatusText(tr("Reconnecting to %1:%2...").arg(m_ServerAddress).arg(m_ServerPort));
    m_Socket.connectToHost(m_ServerAddress, m_ServerPort);
}

void PassthroughClient::scheduleReconnect()
{
    // Guard against double-scheduling when both onSocketError and
    // onSocketDisconnected fire for the same connection failure
    if (m_ReconnectTimer.isActive()) return;

    if (m_ReconnectAttempts < MAX_RECONNECT_ATTEMPTS) {
        int delay = qMin(1000 * (1 << m_ReconnectAttempts), 16000);
        m_ReconnectAttempts++;
        setStatusText(tr("Reconnecting in %1s... (%2:%3)")
                      .arg(delay / 1000).arg(m_ServerAddress).arg(m_ServerPort));
        m_ReconnectTimer.start(delay);
    } else {
        setStatusText(tr("Connection failed: %1").arg(m_Socket.errorString()));
    }
}

// ─── Message sending ───

void PassthroughClient::sendMessage(MlptProtocol::MsgType type, const QByteArray& payload)
{
    // Write header + payload in a single write to avoid split packets
    // (TCP_NODELAY is enabled, so separate writes would send separate packets)
    uint8_t headerBuf[MlptProtocol::HEADER_SIZE];
    MlptProtocol::writeHeader(headerBuf, type, payload.size());

    QByteArray frame;
    frame.reserve(MlptProtocol::HEADER_SIZE + payload.size());
    frame.append(reinterpret_cast<const char*>(headerBuf), MlptProtocol::HEADER_SIZE);
    if (!payload.isEmpty()) {
        frame.append(payload);
    }

    qint64 written = m_Socket.write(frame);
    if (written < 0) {
        qWarning() << "Passthrough: sendMessage failed for type" << type
                    << "- socket error:" << m_Socket.errorString()
                    << "state:" << m_Socket.state();
        // The socket error handler will trigger reconnection
    } else if (written < frame.size()) {
        qWarning() << "Passthrough: sendMessage partial write for type" << type
                    << "- wrote" << written << "of" << frame.size() << "bytes";
    }
}

void PassthroughClient::sendHello()
{
    MlptProtocol::HelloPayload hello;
    hello.clientVersion = MlptProtocol::VERSION;
    memcpy(hello.sessionId, m_SessionId, 16);

    QByteArray data(reinterpret_cast<const char*>(&hello), sizeof(hello));
    sendMessage(MlptProtocol::MSG_HELLO, data);
}

void PassthroughClient::sendDeviceList()
{
    const auto& devices = m_DeviceEnumerator.devices();

    // Build payload: [DeviceListHeader] [DeviceDescriptor + name + usb descriptors] * N
    QByteArray payload;

    MlptProtocol::DeviceListHeader listHeader;
    listHeader.count = static_cast<uint16_t>(devices.size());
    payload.append(reinterpret_cast<const char*>(&listHeader), sizeof(listHeader));

    for (const auto& dev : devices) {
        MlptProtocol::DeviceDescriptor desc;
        memset(&desc, 0, sizeof(desc));
        desc.deviceId = dev.deviceId;
        desc.vendorId = dev.vendorId;
        desc.productId = dev.productId;
        desc.transport = dev.transport;
        desc.deviceClass = dev.deviceClass;

        QByteArray nameUtf8 = dev.name.toUtf8();
        desc.nameLen = static_cast<uint8_t>(qMin(nameUtf8.size(), 255));
        desc.usbSpeed = 0;  // Unknown until device is opened with libusb

        memset(desc.serialNumber, 0, sizeof(desc.serialNumber));
        QByteArray serialUtf8 = dev.serialNumber.toUtf8();
        strncpy(desc.serialNumber, serialUtf8.constData(),
                qMin(static_cast<size_t>(serialUtf8.size()), sizeof(desc.serialNumber) - 1));

        // USB descriptors are only sent in DEVICE_ATTACH, not in DEVICE_LIST
        desc.usbDescrDevLen = 0;
        desc.usbDescrConfLen = 0;

        payload.append(reinterpret_cast<const char*>(&desc), sizeof(desc));
        payload.append(nameUtf8.constData(), desc.nameLen);
    }

    sendMessage(MlptProtocol::MSG_DEVICE_LIST, payload);
    qInfo() << "Passthrough: sent device list with" << devices.size() << "devices";
}

// ─── Message processing ───

void PassthroughClient::sendDeviceAttachWithDescriptors(uint32_t deviceId, UsbIpExporter* exporter)
{
    // Build a DeviceDescriptor with USB descriptors appended
    const auto& devices = m_DeviceEnumerator.devices();
    const PassthroughDevice* devInfo = nullptr;
    for (const auto& d : devices) {
        if (d.deviceId == deviceId) {
            devInfo = &d;
            break;
        }
    }
    if (!devInfo) return;

    QByteArray usbDevDescr = exporter->deviceDescriptor();
    QByteArray usbConfDescr = exporter->configDescriptor();

    MlptProtocol::DeviceDescriptor desc;
    memset(&desc, 0, sizeof(desc));
    desc.deviceId = deviceId;
    desc.vendorId = devInfo->vendorId;
    desc.productId = devInfo->productId;
    desc.transport = devInfo->transport;
    desc.deviceClass = devInfo->deviceClass;

    QByteArray nameUtf8 = devInfo->name.toUtf8();
    desc.nameLen = static_cast<uint8_t>(qMin(nameUtf8.size(), 255));
    desc.usbSpeed = exporter->usbSpeed();

    memset(desc.serialNumber, 0, sizeof(desc.serialNumber));
    QByteArray serialUtf8 = devInfo->serialNumber.toUtf8();
    strncpy(desc.serialNumber, serialUtf8.constData(),
            qMin(static_cast<size_t>(serialUtf8.size()), sizeof(desc.serialNumber) - 1));

    desc.usbDescrDevLen = static_cast<uint16_t>(usbDevDescr.size());
    desc.usbDescrConfLen = static_cast<uint16_t>(usbConfDescr.size());

    // Win2 mode: include daemon port and busid so server can tell driver to connect
    if (m_ServerBackend == MlptProtocol::VHCI_BACKEND_WIN2 && m_Daemon && m_Daemon->isRunning()) {
        desc.daemonPort = m_Daemon->port();
        QString busid = UsbIpDaemon::makeBusid(deviceId);
        QByteArray busidUtf8 = busid.toUtf8();
        strncpy(desc.busid, busidUtf8.constData(),
                qMin(static_cast<size_t>(busidUtf8.size()), sizeof(desc.busid) - 1));
    } else {
        desc.daemonPort = 0;
        memset(desc.busid, 0, sizeof(desc.busid));
    }

    QByteArray payload;
    payload.append(reinterpret_cast<const char*>(&desc), sizeof(desc));
    payload.append(nameUtf8.constData(), desc.nameLen);
    payload.append(usbDevDescr);
    payload.append(usbConfDescr);

    sendMessage(MlptProtocol::MSG_DEVICE_ATTACH, payload);
}

void PassthroughClient::sendDeviceAttachWithDescriptors(uint32_t deviceId, BtHidCapture* capture)
{
    // Build a DeviceDescriptor with synthesized USB descriptors from BtHidCapture
    const auto& devices = m_DeviceEnumerator.devices();
    const PassthroughDevice* devInfo = nullptr;
    for (const auto& d : devices) {
        if (d.deviceId == deviceId) {
            devInfo = &d;
            break;
        }
    }
    if (!devInfo) return;

    QByteArray usbDevDescr = capture->deviceDescriptor();
    QByteArray usbConfDescr = capture->configDescriptor();

    MlptProtocol::DeviceDescriptor desc;
    memset(&desc, 0, sizeof(desc));
    desc.deviceId = deviceId;
    desc.vendorId = capture->vendorId();
    desc.productId = capture->productId();
    desc.transport = MlptProtocol::TRANSPORT_USB; // Present as USB to server
    desc.deviceClass = devInfo->deviceClass;

    QByteArray nameUtf8 = devInfo->name.toUtf8();
    desc.nameLen = static_cast<uint8_t>(qMin(nameUtf8.size(), 255));
    desc.usbSpeed = capture->usbSpeed();

    memset(desc.serialNumber, 0, sizeof(desc.serialNumber));
    QByteArray serialUtf8 = devInfo->serialNumber.toUtf8();
    strncpy(desc.serialNumber, serialUtf8.constData(),
            qMin(static_cast<size_t>(serialUtf8.size()), sizeof(desc.serialNumber) - 1));

    desc.usbDescrDevLen = static_cast<uint16_t>(usbDevDescr.size());
    desc.usbDescrConfLen = static_cast<uint16_t>(usbConfDescr.size());

    // BT HID always uses legacy mode for URB relay (no USB/IP daemon support yet)
    desc.daemonPort = 0;
    memset(desc.busid, 0, sizeof(desc.busid));

    QByteArray payload;
    payload.append(reinterpret_cast<const char*>(&desc), sizeof(desc));
    payload.append(nameUtf8.constData(), desc.nameLen);
    payload.append(usbDevDescr);
    payload.append(usbConfDescr);

    sendMessage(MlptProtocol::MSG_DEVICE_ATTACH, payload);
}

void PassthroughClient::processUsbIpSubmit(const QByteArray& payload)
{
    if (payload.size() < static_cast<int>(sizeof(MlptProtocol::UsbIpHeader))) {
        qWarning() << "Passthrough: USBIP_SUBMIT too short";
        return;
    }

    MlptProtocol::UsbIpHeader header;
    memcpy(&header, payload.constData(), sizeof(header));

    QByteArray data;
    if (header.dataLen > 0 && payload.size() > static_cast<int>(sizeof(header))) {
        int availableLen = payload.size() - static_cast<int>(sizeof(header));
        if (availableLen < static_cast<int>(header.dataLen)) {
            qWarning() << "Passthrough: USBIP_SUBMIT payload truncated:" << availableLen << "< expected" << header.dataLen;
            MlptProtocol::UsbIpHeader resp = header;
            resp.status = -71; // EPROTO
            resp.dataLen = 0;
            QByteArray respPayload(reinterpret_cast<const char*>(&resp), sizeof(resp));
            sendMessage(MlptProtocol::MSG_USBIP_RETURN, respPayload);
            return;
        }
        data = payload.mid(sizeof(header), header.dataLen);
    }

    // Find the exporter for this device (USB or BT)
    auto it = m_Exporters.find(header.deviceId);
    if (it != m_Exporters.end()) {
        (*it)->submitUrb(header, data);
        return;
    }

    auto btIt = m_BtCaptures.find(header.deviceId);
    if (btIt != m_BtCaptures.end()) {
        (*btIt)->submitUrb(header, data);
        return;
    }

    qWarning() << "Passthrough: USBIP_SUBMIT for unknown device" << header.deviceId;
    // Send error return
    MlptProtocol::UsbIpHeader resp = header;
    resp.status = -19; // ENODEV
    resp.dataLen = 0;
    QByteArray respPayload(reinterpret_cast<const char*>(&resp), sizeof(resp));
    sendMessage(MlptProtocol::MSG_USBIP_RETURN, respPayload);
}

void PassthroughClient::processUsbIpUnlink(const QByteArray& payload)
{
    if (payload.size() < static_cast<int>(sizeof(MlptProtocol::UsbIpHeader))) return;

    MlptProtocol::UsbIpHeader header;
    memcpy(&header, payload.constData(), sizeof(header));

    // The seqnum of the URB to unlink is stored in header.dataLen
    // (repurposed by the server's forwardVhciUrbToClient for CMD_UNLINK)
    uint32_t seqNumToUnlink = header.dataLen;

    auto it = m_Exporters.find(header.deviceId);
    if (it != m_Exporters.end()) {
        (*it)->unlinkUrb(seqNumToUnlink);
        return;
    }

    auto btIt = m_BtCaptures.find(header.deviceId);
    if (btIt != m_BtCaptures.end()) {
        (*btIt)->unlinkUrb(seqNumToUnlink);
    }
}

void PassthroughClient::cleanupExporter(uint32_t deviceId)
{
    auto it = m_Exporters.find(deviceId);
    if (it != m_Exporters.end()) {
        // Unexport from daemon if in win2 mode
        if (m_Daemon) {
            m_Daemon->unexportDevice(UsbIpDaemon::makeBusid(deviceId));
        }
        (*it)->closeDevice();
        (*it)->deleteLater();
        m_Exporters.erase(it);
    }
}

void PassthroughClient::cleanupAllExporters()
{
    if (m_Daemon) {
        // Unexport all devices from daemon
        for (auto it = m_Exporters.begin(); it != m_Exporters.end(); ++it) {
            m_Daemon->unexportDevice(UsbIpDaemon::makeBusid(it.key()));
        }
    }
    for (auto* exporter : m_Exporters) {
        exporter->closeDevice();
        exporter->deleteLater();
    }
    m_Exporters.clear();
}

void PassthroughClient::cleanupBtCapture(uint32_t deviceId)
{
    auto it = m_BtCaptures.find(deviceId);
    if (it != m_BtCaptures.end()) {
        (*it)->closeDevice();
        (*it)->deleteLater();
        m_BtCaptures.erase(it);
    }
}

void PassthroughClient::cleanupAllBtCaptures()
{
    for (auto* capture : m_BtCaptures) {
        capture->closeDevice();
        capture->deleteLater();
    }
    m_BtCaptures.clear();
}

void PassthroughClient::processMessage(const MlptProtocol::Header& header, const QByteArray& payload)
{
    switch (static_cast<MlptProtocol::MsgType>(header.msgType)) {
    case MlptProtocol::MSG_HELLO_ACK: {
        if (payload.size() < static_cast<int>(sizeof(MlptProtocol::HelloAckPayload))) {
            qWarning() << "Passthrough: HELLO_ACK too short";
            break;
        }
        auto* ack = reinterpret_cast<const MlptProtocol::HelloAckPayload*>(payload.constData());

        // Verify the server speaks the same protocol version
        if (ack->serverVersion != MlptProtocol::VERSION) {
            qWarning() << "Passthrough: server protocol version mismatch:"
                       << "server=" << ack->serverVersion
                       << "client=" << MlptProtocol::VERSION
                       << "– closing connection";
            setStatusText(tr("Protocol version mismatch (server=%1, client=%2)")
                          .arg(ack->serverVersion).arg(MlptProtocol::VERSION));
            m_Socket.disconnectFromHost();
            break;
        }

        m_VhciAvailable = ack->vhciAvailable != 0;
        m_ServerBackend = ack->vhciBackend;
        emit vhciAvailableChanged();

        // In win2 mode, start the USB/IP daemon so the VHCI driver can connect to us
        if (m_ServerBackend == MlptProtocol::VHCI_BACKEND_WIN2) {
            if (!m_Daemon) {
                m_Daemon = new UsbIpDaemon(this);
            }
            if (!m_Daemon->isRunning()) {
                if (!m_Daemon->start(0)) {
                    qWarning() << "Passthrough: failed to start USB/IP daemon";
                } else {
                    qInfo() << "Passthrough: USB/IP daemon started on port" << m_Daemon->port();
                }
            }
        }

        setConnected(true);
        const char* backendStr = (m_ServerBackend == MlptProtocol::VHCI_BACKEND_WIN2)
                                 ? " [win2]" : " [legacy]";
        setStatusText(tr("Connected%1%2").arg(
            m_VhciAvailable ? "" : tr(" (VHCI driver not available)"),
            QString::fromLatin1(backendStr)));

        m_KeepaliveTimer.start();
        sendDeviceList();

        // Start hot-plug polling to detect device changes
        m_DeviceEnumerator.startHotplugPolling(5000);

        // Auto-attach devices marked for auto-forward
        autoAttachDevices();

        qInfo() << "Passthrough: handshake complete, VHCI available:" << m_VhciAvailable
                << "backend:" << (m_ServerBackend == MlptProtocol::VHCI_BACKEND_WIN2 ? "win2" : "legacy");
        break;
    }

    case MlptProtocol::MSG_DEVICE_ATTACH_ACK: {
        if (payload.size() < static_cast<int>(sizeof(MlptProtocol::DeviceAttachAckPayload))) break;
        auto* ack = reinterpret_cast<const MlptProtocol::DeviceAttachAckPayload*>(payload.constData());

        cancelAttachTimeout(ack->deviceId);

        if (ack->status == MlptProtocol::ATTACH_OK) {
            qInfo() << "Passthrough: device" << ack->deviceId << "attached on VHCI port" << ack->vhciPort;
            m_DeviceEnumerator.setDeviceForwarding(ack->deviceId, true);
            emit deviceAttached(ack->deviceId, ack->vhciPort);
        } else {
            qWarning() << "Passthrough: device" << ack->deviceId << "attach failed, status:" << ack->status;
            // Clean up the exporter/capture that was optimistically created in attachDevice()
            // so the device can be retried later instead of staying stuck as "already exported".
            cleanupExporter(ack->deviceId);
            cleanupBtCapture(ack->deviceId);
            emit deviceAttachFailed(ack->deviceId, ack->status);
        }
        break;
    }

    case MlptProtocol::MSG_DEVICE_DETACH_ACK: {
        if (payload.size() < static_cast<int>(sizeof(MlptProtocol::DeviceDetachPayload))) break;
        auto* p = reinterpret_cast<const MlptProtocol::DeviceDetachPayload*>(payload.constData());

        qInfo() << "Passthrough: device" << p->deviceId << "detached";
        m_DeviceEnumerator.setDeviceForwarding(p->deviceId, false);
        emit deviceDetached(p->deviceId);
        break;
    }

    case MlptProtocol::MSG_USBIP_SUBMIT: {
        // Server is sending us URBs to execute on the local device
        processUsbIpSubmit(payload);
        break;
    }

    case MlptProtocol::MSG_USBIP_UNLINK: {
        processUsbIpUnlink(payload);
        break;
    }

    case MlptProtocol::MSG_KEEPALIVE:
        break;

    default:
        qWarning() << "Passthrough: unknown message type:" << header.msgType;
        break;
    }
}

// ─── Internal state ───

void PassthroughClient::setConnected(bool connected)
{
    if (m_Connected != connected) {
        m_Connected = connected;
        emit connectedChanged();
    }
}

void PassthroughClient::setStatusText(const QString& text)
{
    if (m_StatusText != text) {
        m_StatusText = text;
        emit statusTextChanged();
    }
}

void PassthroughClient::startAttachTimeout(uint32_t deviceId)
{
    cancelAttachTimeout(deviceId); // Cancel any existing timer

    auto* timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(ATTACH_TIMEOUT_MS);
    connect(timer, &QTimer::timeout, this, [this, deviceId]() {
        qWarning() << "Passthrough: attach timeout for device" << deviceId
                   << "- no ACK received within" << ATTACH_TIMEOUT_MS << "ms";

        // Clean up the optimistically created exporter/capture
        cleanupExporter(deviceId);
        cleanupBtCapture(deviceId);

        // Remove the timer
        cancelAttachTimeout(deviceId);

        emit deviceAttachFailed(deviceId, MlptProtocol::ATTACH_ERR_FAILED);
    });
    m_PendingAttachTimers.insert(deviceId, timer);
    timer->start();
}

void PassthroughClient::cancelAttachTimeout(uint32_t deviceId)
{
    auto it = m_PendingAttachTimers.find(deviceId);
    if (it != m_PendingAttachTimers.end()) {
        (*it)->stop();
        (*it)->deleteLater();
        m_PendingAttachTimers.erase(it);
    }
}
