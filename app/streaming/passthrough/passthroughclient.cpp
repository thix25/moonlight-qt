#include "passthroughclient.h"

#include <QRandomGenerator>
#include <QtDebug>

PassthroughClient::PassthroughClient(QObject* parent)
    : QObject(parent)
    , m_ServerPort(MlptProtocol::DEFAULT_PORT)
    , m_Connected(false)
    , m_VhciAvailable(false)
    , m_StatusText(tr("Not connected"))
    , m_ReconnectAttempts(0)
{
    memset(m_SessionId, 0, sizeof(m_SessionId));

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
}

PassthroughClient::~PassthroughClient()
{
    disconnectFromServer();
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
    m_ReconnectAttempts = MAX_RECONNECT_ATTEMPTS; // Prevent reconnect

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

    MlptProtocol::DeviceDetachPayload payload; // Same struct: just a deviceId
    payload.deviceId = deviceId;

    QByteArray data(reinterpret_cast<const char*>(&payload), sizeof(payload));
    sendMessage(MlptProtocol::MSG_DEVICE_ATTACH, data);

    qInfo() << "Requested attach for device" << deviceId;
}

void PassthroughClient::detachDevice(uint32_t deviceId)
{
    if (!m_Connected) {
        return;
    }

    MlptProtocol::DeviceDetachPayload payload;
    payload.deviceId = deviceId;

    QByteArray data(reinterpret_cast<const char*>(&payload), sizeof(payload));
    sendMessage(MlptProtocol::MSG_DEVICE_DETACH, data);

    qInfo() << "Requested detach for device" << deviceId;
}

void PassthroughClient::refreshDevices()
{
    m_DeviceEnumerator.enumerate();
    if (m_Connected) {
        sendDeviceList();
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
    setConnected(false);

    if (m_ReconnectAttempts < MAX_RECONNECT_ATTEMPTS) {
        int delay = qMin(1000 * (1 << m_ReconnectAttempts), 16000);
        m_ReconnectAttempts++;
        setStatusText(tr("Reconnecting in %1s...").arg(delay / 1000));
        m_ReconnectTimer.start(delay);
    } else {
        setStatusText(tr("Disconnected"));
    }
}

void PassthroughClient::onSocketError(QAbstractSocket::SocketError error)
{
    qWarning() << "Passthrough socket error:" << error << m_Socket.errorString();

    if (!m_Connected && m_ReconnectAttempts < MAX_RECONNECT_ATTEMPTS) {
        int delay = qMin(1000 * (1 << m_ReconnectAttempts), 16000);
        m_ReconnectAttempts++;
        setStatusText(tr("Connection failed, retry in %1s...").arg(delay / 1000));
        m_ReconnectTimer.start(delay);
    } else if (!m_Connected) {
        setStatusText(tr("Connection failed: %1").arg(m_Socket.errorString()));
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

    setStatusText(tr("Reconnecting..."));
    m_Socket.connectToHost(m_ServerAddress, m_ServerPort);
}

// ─── Message sending ───

void PassthroughClient::sendMessage(MlptProtocol::MsgType type, const QByteArray& payload)
{
    uint8_t headerBuf[MlptProtocol::HEADER_SIZE];
    MlptProtocol::writeHeader(headerBuf, type, payload.size());

    m_Socket.write(reinterpret_cast<const char*>(headerBuf), MlptProtocol::HEADER_SIZE);
    if (!payload.isEmpty()) {
        m_Socket.write(payload);
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

    // Build payload: [DeviceListHeader] [DeviceDescriptor + name] * N
    QByteArray payload;

    MlptProtocol::DeviceListHeader listHeader;
    listHeader.count = static_cast<uint16_t>(devices.size());
    payload.append(reinterpret_cast<const char*>(&listHeader), sizeof(listHeader));

    for (const auto& dev : devices) {
        MlptProtocol::DeviceDescriptor desc;
        desc.deviceId = dev.deviceId;
        desc.vendorId = dev.vendorId;
        desc.productId = dev.productId;
        desc.transport = dev.transport;
        desc.deviceClass = dev.deviceClass;

        QByteArray nameUtf8 = dev.name.toUtf8();
        desc.nameLen = static_cast<uint8_t>(qMin(nameUtf8.size(), 255));
        desc.reserved = 0;

        memset(desc.serialNumber, 0, sizeof(desc.serialNumber));
        QByteArray serialUtf8 = dev.serialNumber.toUtf8();
        strncpy(desc.serialNumber, serialUtf8.constData(),
                qMin(static_cast<size_t>(serialUtf8.size()), sizeof(desc.serialNumber) - 1));

        payload.append(reinterpret_cast<const char*>(&desc), sizeof(desc));
        payload.append(nameUtf8.constData(), desc.nameLen);
    }

    sendMessage(MlptProtocol::MSG_DEVICE_LIST, payload);
    qInfo() << "Passthrough: sent device list with" << devices.size() << "devices";
}

// ─── Message processing ───

void PassthroughClient::processMessage(const MlptProtocol::Header& header, const QByteArray& payload)
{
    switch (static_cast<MlptProtocol::MsgType>(header.msgType)) {
    case MlptProtocol::MSG_HELLO_ACK: {
        if (payload.size() < static_cast<int>(sizeof(MlptProtocol::HelloAckPayload))) {
            qWarning() << "Passthrough: HELLO_ACK too short";
            break;
        }
        auto* ack = reinterpret_cast<const MlptProtocol::HelloAckPayload*>(payload.constData());
        m_VhciAvailable = ack->vhciAvailable != 0;
        emit vhciAvailableChanged();

        setConnected(true);
        setStatusText(tr("Connected%1").arg(m_VhciAvailable ? "" : tr(" (VHCI driver not available)")));

        m_KeepaliveTimer.start();
        sendDeviceList();
        qInfo() << "Passthrough: handshake complete, VHCI available:" << m_VhciAvailable;
        break;
    }

    case MlptProtocol::MSG_DEVICE_ATTACH_ACK: {
        if (payload.size() < static_cast<int>(sizeof(MlptProtocol::DeviceAttachAckPayload))) break;
        auto* ack = reinterpret_cast<const MlptProtocol::DeviceAttachAckPayload*>(payload.constData());

        if (ack->status == MlptProtocol::ATTACH_OK) {
            qInfo() << "Passthrough: device" << ack->deviceId << "attached on VHCI port" << ack->vhciPort;
            m_DeviceEnumerator.setDeviceForwarding(ack->deviceId, true);
            emit deviceAttached(ack->deviceId, ack->vhciPort);
        } else {
            qWarning() << "Passthrough: device" << ack->deviceId << "attach failed, status:" << ack->status;
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

    case MlptProtocol::MSG_USBIP_RETURN: {
        // TODO Phase 2: Handle USB/IP URB returns
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
