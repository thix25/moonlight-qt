#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QByteArray>
#include <QList>
#include <QHash>

#include "protocol.h"
#include "deviceenumerator.h"

class UsbIpExporter;
class BtHidCapture;

class PassthroughClient : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)
    Q_PROPERTY(bool vhciAvailable READ isVhciAvailable NOTIFY vhciAvailableChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)

public:
    explicit PassthroughClient(QObject* parent = nullptr);
    ~PassthroughClient();

    void connectToServer(const QString& address, uint16_t port = MlptProtocol::DEFAULT_PORT);
    void disconnectFromServer();

    bool isConnected() const { return m_Connected; }
    bool isVhciAvailable() const { return m_VhciAvailable; }
    QString statusText() const { return m_StatusText; }

    DeviceEnumerator* deviceEnumerator() { return &m_DeviceEnumerator; }

    Q_INVOKABLE void attachDevice(uint32_t deviceId);
    Q_INVOKABLE void detachDevice(uint32_t deviceId);
    Q_INVOKABLE void refreshDevices();

    // Auto-attach all devices marked for auto-forward
    void autoAttachDevices();

signals:
    void connectedChanged();
    void vhciAvailableChanged();
    void statusTextChanged();
    void deviceAttached(uint32_t deviceId, uint8_t vhciPort);
    void deviceAttachFailed(uint32_t deviceId, uint8_t status);
    void deviceDetached(uint32_t deviceId);

private slots:
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketError(QAbstractSocket::SocketError error);
    void onReadyRead();
    void onKeepaliveTimer();
    void onReconnectTimer();

private:
    void sendMessage(MlptProtocol::MsgType type, const QByteArray& payload = QByteArray());
    void sendHello();
    void sendDeviceList();
    void sendDeviceAttachWithDescriptors(uint32_t deviceId, UsbIpExporter* exporter);
    void sendDeviceAttachWithDescriptors(uint32_t deviceId, BtHidCapture* capture);
    void processMessage(const MlptProtocol::Header& header, const QByteArray& payload);
    void processUsbIpSubmit(const QByteArray& payload);
    void processUsbIpUnlink(const QByteArray& payload);

    void setConnected(bool connected);
    void setStatusText(const QString& text);

    void cleanupExporter(uint32_t deviceId);
    void cleanupAllExporters();
    void cleanupBtCapture(uint32_t deviceId);
    void cleanupAllBtCaptures();

    QTcpSocket m_Socket;
    QTimer m_KeepaliveTimer;
    QTimer m_ReconnectTimer;

    QString m_ServerAddress;
    uint16_t m_ServerPort;

    bool m_Connected;
    bool m_VhciAvailable;
    QString m_StatusText;

    QByteArray m_ReceiveBuffer;
    uint8_t m_SessionId[16];

    DeviceEnumerator m_DeviceEnumerator;

    // Active device exporters: deviceId → UsbIpExporter*
    QHash<uint32_t, UsbIpExporter*> m_Exporters;

    // Active BT HID captures: deviceId → BtHidCapture*
    QHash<uint32_t, BtHidCapture*> m_BtCaptures;

    int m_ReconnectAttempts;
    static constexpr int MAX_RECONNECT_ATTEMPTS = 5;
};
