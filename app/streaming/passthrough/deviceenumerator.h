#pragma once

#include <QObject>
#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QDateTime>

#include "protocol.h"

struct PassthroughDevice {
    uint32_t deviceId;
    uint16_t vendorId;
    uint16_t productId;
    QString  name;
    QString  manufacturer;    // USB manufacturer string or BT vendor
    QString  serialNumber;
    QString  instancePath;    // Windows device instance path
    QString  locationInfo;    // Physical USB port location (e.g. "Port_#0003.Hub_#0002")
    QString  driver;          // Active driver name (e.g. "usbvideo", "WinUSB")
    uint8_t  transport;       // MlptProtocol::DeviceTransport
    uint8_t  deviceClass;     // MlptProtocol::DeviceClass
    bool     isForwarding;
    bool     autoForward;
    QDateTime addedTime;      // When the device was first seen
    quint64  storageSizeBytes; // Physical disk size (storage class, 0 if unknown)
    QString  lastError;       // Last attach/detach error (empty = no error)

    // Bluetooth-specific
    int8_t   batteryPercent;  // -1 if unknown
    int8_t   rssi;            // 0 if unknown
    bool     btPaired;
    bool     btConnected;
};

class DeviceEnumerator : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(int count READ count NOTIFY devicesChanged)

public:
    enum Roles {
        DeviceIdRole = Qt::UserRole + 1,
        NameRole,
        VendorIdRole,
        ProductIdRole,
        TransportRole,
        DeviceClassRole,
        SerialNumberRole,
        IsForwardingRole,
        AutoForwardRole,
        StatusTextRole,
        DeviceClassNameRole,
        VidPidTextRole,
        BatteryPercentRole,
        RssiRole,
        BtPairedRole,
        BtConnectedRole,
        LocationInfoRole,
        DriverRole,
        ManufacturerRole,
        AddedTimeRole,
        StorageSizeTextRole,
        LastErrorRole,
    };

    explicit DeviceEnumerator(QObject* parent = nullptr);

    // QAbstractListModel interface
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_Devices.size(); }

    const QList<PassthroughDevice>& devices() const { return m_Devices; }

    void enumerate();
    void setDeviceForwarding(uint32_t deviceId, bool forwarding);
    void setDeviceError(uint32_t deviceId, const QString& error);

    // Start/stop periodic hot-plug polling
    void startHotplugPolling(int intervalMs = 5000);
    void stopHotplugPolling();

    Q_INVOKABLE void setAutoForward(int index, bool autoFwd);

    // Persist auto-forward device fingerprints to QSettings
    void saveAutoForwardList() const;
    void loadAutoForwardList();

    // Get list of device IDs that should be auto-forwarded
    QList<uint32_t> getAutoForwardDeviceIds() const;

    // Device fingerprint for matching across sessions
    static QString deviceFingerprint(const PassthroughDevice& dev);

signals:
    void devicesChanged();
    void deviceAdded(uint32_t deviceId);
    void deviceRemoved(uint32_t deviceId);

public slots:
    void pollHotplug();

private:
    void enumerateUsb();
    void enumerateBluetooth();

    uint32_t m_NextDeviceId;
    QList<PassthroughDevice> m_Devices;
    QTimer m_HotplugTimer;
};
