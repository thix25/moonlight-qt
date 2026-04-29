#include "deviceenumerator.h"

#include <QtDebug>
#include <QRegularExpression>

#ifdef Q_OS_WIN32
#include <Windows.h>
#include <SetupAPI.h>
#include <devguid.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <usbiodef.h>
#include <BluetoothAPIs.h>
#include <hidsdi.h>
#include <devpkey.h>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "bthprops.lib")
#pragma comment(lib, "hid.lib")
#endif

DeviceEnumerator::DeviceEnumerator(QObject* parent)
    : QAbstractListModel(parent)
    , m_NextDeviceId(1)
{
    connect(&m_HotplugTimer, &QTimer::timeout, this, &DeviceEnumerator::pollHotplug);
}

int DeviceEnumerator::rowCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent);
    return m_Devices.size();
}

QVariant DeviceEnumerator::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_Devices.size())
        return QVariant();

    const auto& dev = m_Devices[index.row()];

    switch (role) {
    case DeviceIdRole:       return dev.deviceId;
    case NameRole:           return dev.name;
    case VendorIdRole:       return dev.vendorId;
    case ProductIdRole:      return dev.productId;
    case TransportRole:      return dev.transport;
    case DeviceClassRole:    return dev.deviceClass;
    case SerialNumberRole:   return dev.serialNumber;
    case IsForwardingRole:   return dev.isForwarding;
    case AutoForwardRole:    return dev.autoForward;
    case BatteryPercentRole: return dev.batteryPercent;
    case RssiRole:           return dev.rssi;
    case BtPairedRole:       return dev.btPaired;
    case BtConnectedRole:    return dev.btConnected;
    case StatusTextRole: {
        if (dev.isForwarding)
            return tr("Forwarding");
        return tr("Available");
    }
    case DeviceClassNameRole: {
        switch (dev.deviceClass) {
        case MlptProtocol::DEVCLASS_HID_KEYBOARD: return tr("Keyboard");
        case MlptProtocol::DEVCLASS_HID_MOUSE:    return tr("Mouse");
        case MlptProtocol::DEVCLASS_HID_GAMEPAD:  return tr("Gamepad");
        case MlptProtocol::DEVCLASS_HID_OTHER:    return tr("HID Device");
        case MlptProtocol::DEVCLASS_STORAGE:       return tr("Storage");
        case MlptProtocol::DEVCLASS_AUDIO:         return tr("Audio");
        case MlptProtocol::DEVCLASS_VIDEO:         return tr("Webcam");
        case MlptProtocol::DEVCLASS_BT_ADAPTER:    return tr("Bluetooth Adapter");
        default:                                    return tr("USB Device");
        }
    }
    case VidPidTextRole:
        return QString("%1:%2")
            .arg(dev.vendorId, 4, 16, QLatin1Char('0'))
            .arg(dev.productId, 4, 16, QLatin1Char('0'));
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> DeviceEnumerator::roleNames() const
{
    return {
        { DeviceIdRole,       "deviceId" },
        { NameRole,           "deviceName" },
        { VendorIdRole,       "vendorId" },
        { ProductIdRole,      "productId" },
        { TransportRole,      "transport" },
        { DeviceClassRole,    "deviceClass" },
        { SerialNumberRole,   "serialNumber" },
        { IsForwardingRole,   "isForwarding" },
        { AutoForwardRole,    "autoForward" },
        { StatusTextRole,     "statusText" },
        { DeviceClassNameRole,"deviceClassName" },
        { VidPidTextRole,     "vidPidText" },
        { BatteryPercentRole, "batteryPercent" },
        { RssiRole,           "rssi" },
        { BtPairedRole,       "btPaired" },
        { BtConnectedRole,    "btConnected" },
    };
}

void DeviceEnumerator::enumerate()
{
    beginResetModel();
    m_Devices.clear();
    m_NextDeviceId = 1;

    enumerateUsb();
    enumerateBluetooth();

    endResetModel();
    emit devicesChanged();

    qInfo() << "Passthrough: enumerated" << m_Devices.size() << "devices"
            << "(USB + Bluetooth)";
}

void DeviceEnumerator::setDeviceForwarding(uint32_t deviceId, bool forwarding)
{
    for (int i = 0; i < m_Devices.size(); i++) {
        if (m_Devices[i].deviceId == deviceId) {
            m_Devices[i].isForwarding = forwarding;
            QModelIndex idx = index(i);
            emit dataChanged(idx, idx, { IsForwardingRole, StatusTextRole });
            return;
        }
    }
}

void DeviceEnumerator::setAutoForward(int row, bool autoFwd)
{
    if (row < 0 || row >= m_Devices.size()) return;

    m_Devices[row].autoForward = autoFwd;
    QModelIndex idx = index(row);
    emit dataChanged(idx, idx, { AutoForwardRole });
}

// ─── Platform-specific enumeration ───

#ifdef Q_OS_WIN32

static uint8_t classifyUsbDevice(const QString& deviceClass, const QString& compatIds)
{
    QString cls = deviceClass.toUpper();

    if (cls == "BLUETOOTHDEVICE" || cls == "BLUETOOTH")
        return MlptProtocol::DEVCLASS_BT_ADAPTER;
    if (cls == "DISKDRIVE" || cls == "CDROM" || cls == "USBSTOR" || cls == "WPD")
        return MlptProtocol::DEVCLASS_STORAGE;
    if (cls == "CAMERA" || cls == "IMAGE")
        return MlptProtocol::DEVCLASS_VIDEO;
    if (cls == "MEDIA" || cls == "AUDIOINPUT" || cls == "AUDIOOUTPUT")
        return MlptProtocol::DEVCLASS_AUDIO;

    // Check HID subtype via compatible IDs
    QString compat = compatIds.toUpper();
    if (compat.contains("HID_DEVICE_SYSTEM_KEYBOARD") || compat.contains("KEYBOARD"))
        return MlptProtocol::DEVCLASS_HID_KEYBOARD;
    if (compat.contains("HID_DEVICE_SYSTEM_MOUSE") || compat.contains("MOUSE"))
        return MlptProtocol::DEVCLASS_HID_MOUSE;
    if (compat.contains("HID_DEVICE_SYSTEM_GAME") || compat.contains("GAMEPAD") || compat.contains("JOYSTICK"))
        return MlptProtocol::DEVCLASS_HID_GAMEPAD;
    if (cls == "HIDCLASS" || cls == "HID")
        return MlptProtocol::DEVCLASS_HID_OTHER;

    return MlptProtocol::DEVCLASS_OTHER;
}

static QString getDeviceRegistryProperty(HDEVINFO devInfo, SP_DEVINFO_DATA* devInfoData, DWORD property)
{
    DWORD bufSize = 0;
    SetupDiGetDeviceRegistryPropertyW(devInfo, devInfoData, property, nullptr, nullptr, 0, &bufSize);
    if (bufSize == 0) return QString();

    QByteArray buf(bufSize, 0);
    if (!SetupDiGetDeviceRegistryPropertyW(devInfo, devInfoData, property, nullptr,
                                           reinterpret_cast<PBYTE>(buf.data()), bufSize, nullptr)) {
        return QString();
    }

    // Handle REG_MULTI_SZ (multiple null-terminated strings)
    return QString::fromWCharArray(reinterpret_cast<const wchar_t*>(buf.constData()));
}

void DeviceEnumerator::enumerateUsb()
{
    HDEVINFO devInfo = SetupDiGetClassDevsW(nullptr, L"USB", nullptr,
                                             DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (devInfo == INVALID_HANDLE_VALUE) {
        qWarning() << "Passthrough: SetupDiGetClassDevs failed:" << GetLastError();
        return;
    }

    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(devInfoData);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(devInfo, i, &devInfoData); i++) {
        // Get instance path
        WCHAR instanceId[MAX_DEVICE_ID_LEN];
        if (CM_Get_Device_IDW(devInfoData.DevInst, instanceId, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS)
            continue;

        QString instancePath = QString::fromWCharArray(instanceId);

        // Skip USB hubs and root hubs
        if (instancePath.toUpper().contains("ROOT_HUB") ||
            instancePath.toUpper().contains("USB\\ROOT"))
            continue;

        // Parse VID/PID from instance path: USB\VID_xxxx&PID_xxxx\...
        uint16_t vid = 0, pid = 0;
        QRegularExpression vidPidRe("VID_([0-9A-Fa-f]{4})&PID_([0-9A-Fa-f]{4})");
        auto match = vidPidRe.match(instancePath);
        if (match.hasMatch()) {
            vid = match.captured(1).toUShort(nullptr, 16);
            pid = match.captured(2).toUShort(nullptr, 16);
        } else {
            continue; // Not a real USB device (hub controller, etc.)
        }

        // Get friendly name
        QString friendlyName = getDeviceRegistryProperty(devInfo, &devInfoData, SPDRP_FRIENDLYNAME);
        if (friendlyName.isEmpty()) {
            friendlyName = getDeviceRegistryProperty(devInfo, &devInfoData, SPDRP_DEVICEDESC);
        }
        if (friendlyName.isEmpty()) {
            friendlyName = QString("USB Device %1:%2")
                .arg(vid, 4, 16, QLatin1Char('0'))
                .arg(pid, 4, 16, QLatin1Char('0'));
        }

        // Get device class and compatible IDs for classification
        QString deviceClass = getDeviceRegistryProperty(devInfo, &devInfoData, SPDRP_CLASS);
        QString compatIds = getDeviceRegistryProperty(devInfo, &devInfoData, SPDRP_COMPATIBLEIDS);

        // Extract serial from instance path (3rd segment after \\)
        QString serial;
        QStringList pathParts = instancePath.split('\\');
        if (pathParts.size() >= 3) {
            serial = pathParts[2];
            // Skip auto-generated serials (contain & which means composite)
            if (serial.contains('&')) serial.clear();
        }

        PassthroughDevice dev;
        dev.deviceId = m_NextDeviceId++;
        dev.vendorId = vid;
        dev.productId = pid;
        dev.name = friendlyName;
        dev.serialNumber = serial;
        dev.instancePath = instancePath;
        dev.transport = MlptProtocol::TRANSPORT_USB;
        dev.deviceClass = classifyUsbDevice(deviceClass, compatIds);
        dev.isForwarding = false;
        dev.autoForward = false;
        dev.batteryPercent = -1;
        dev.rssi = 0;
        dev.btPaired = false;
        dev.btConnected = false;

        m_Devices.append(dev);
    }

    SetupDiDestroyDeviceInfoList(devInfo);
}

// Try to find HID VID/PID for a Bluetooth device by its address
static void findBtHidVidPid(const QString& btAddrClean, uint16_t& outVid, uint16_t& outPid)
{
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);

    HDEVINFO devInfo = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr,
                                             DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE) return;

    SP_DEVICE_INTERFACE_DATA ifData = {};
    ifData.cbSize = sizeof(ifData);

    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devInfo, nullptr, &hidGuid, i, &ifData); i++) {
        DWORD reqSize = 0;
        SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, nullptr, 0, &reqSize, nullptr);

        auto* detail = static_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(malloc(reqSize));
        if (!detail) continue;
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        SP_DEVINFO_DATA devInfoData = {};
        devInfoData.cbSize = sizeof(devInfoData);

        if (SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, detail, reqSize, nullptr, &devInfoData)) {
            // Check instance path or parent for BT address
            WCHAR instanceId[MAX_DEVICE_ID_LEN];
            bool found = false;
            if (CM_Get_Device_IDW(devInfoData.DevInst, instanceId, MAX_DEVICE_ID_LEN, 0) == CR_SUCCESS) {
                if (QString::fromWCharArray(instanceId).toUpper().contains(btAddrClean))
                    found = true;
            }
            if (!found) {
                DEVINST parentInst;
                if (CM_Get_Parent(&parentInst, devInfoData.DevInst, 0) == CR_SUCCESS) {
                    WCHAR parentId[MAX_DEVICE_ID_LEN];
                    if (CM_Get_Device_IDW(parentInst, parentId, MAX_DEVICE_ID_LEN, 0) == CR_SUCCESS) {
                        if (QString::fromWCharArray(parentId).toUpper().contains(btAddrClean))
                            found = true;
                    }
                }
            }

            if (found) {
                // Open HID device to get attributes
                HANDLE hTest = CreateFileW(detail->DevicePath,
                    GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    nullptr, OPEN_EXISTING, 0, nullptr);
                if (hTest != INVALID_HANDLE_VALUE) {
                    HIDD_ATTRIBUTES attrs = {};
                    attrs.Size = sizeof(attrs);
                    if (HidD_GetAttributes(hTest, &attrs)) {
                        outVid = attrs.VendorID;
                        outPid = attrs.ProductID;
                    }
                    CloseHandle(hTest);
                }
                free(detail);
                break;
            }
        }
        free(detail);
    }

    SetupDiDestroyDeviceInfoList(devInfo);
}

void DeviceEnumerator::enumerateBluetooth()
{
    BLUETOOTH_DEVICE_SEARCH_PARAMS searchParams;
    memset(&searchParams, 0, sizeof(searchParams));
    searchParams.dwSize = sizeof(searchParams);
    searchParams.fReturnAuthenticated = TRUE;
    searchParams.fReturnRemembered = TRUE;
    searchParams.fReturnConnected = TRUE;
    searchParams.fReturnUnknown = TRUE;
    searchParams.fIssueInquiry = FALSE;  // Don't do active scan, just list known devices
    searchParams.cTimeoutMultiplier = 0;

    BLUETOOTH_DEVICE_INFO deviceInfo;
    deviceInfo.dwSize = sizeof(deviceInfo);

    HBLUETOOTH_DEVICE_FIND hFind = BluetoothFindFirstDevice(&searchParams, &deviceInfo);
    if (hFind == nullptr) {
        // No BT devices or BT not available - this is normal
        return;
    }

    do {
        PassthroughDevice dev;
        dev.deviceId = m_NextDeviceId++;
        dev.name = QString::fromWCharArray(deviceInfo.szName);
        dev.serialNumber = QString("%1:%2:%3:%4:%5:%6")
            .arg(deviceInfo.Address.rgBytes[5], 2, 16, QLatin1Char('0'))
            .arg(deviceInfo.Address.rgBytes[4], 2, 16, QLatin1Char('0'))
            .arg(deviceInfo.Address.rgBytes[3], 2, 16, QLatin1Char('0'))
            .arg(deviceInfo.Address.rgBytes[2], 2, 16, QLatin1Char('0'))
            .arg(deviceInfo.Address.rgBytes[1], 2, 16, QLatin1Char('0'))
            .arg(deviceInfo.Address.rgBytes[0], 2, 16, QLatin1Char('0'));
        dev.instancePath.clear();
        dev.transport = MlptProtocol::TRANSPORT_BLUETOOTH;
        dev.isForwarding = false;
        dev.autoForward = false;
        dev.btPaired = deviceInfo.fAuthenticated;
        dev.btConnected = deviceInfo.fConnected;
        dev.rssi = 0;

        // Try to get VID/PID from HID device associated with this BT device
        dev.vendorId = 0;
        dev.productId = 0;
        QString btAddrClean = dev.serialNumber.toUpper().remove(':');
        if (deviceInfo.fConnected) {
            findBtHidVidPid(btAddrClean, dev.vendorId, dev.productId);
        }

        // Classify by CoD (Class of Device)
        ULONG cod = deviceInfo.ulClassofDevice;
        uint8_t majorClass = (cod >> 8) & 0x1F;
        uint8_t minorClass = (cod >> 2) & 0x3F;

        switch (majorClass) {
        case 0x05: // Peripheral
            if (minorClass & 0x10) // Keyboard
                dev.deviceClass = MlptProtocol::DEVCLASS_HID_KEYBOARD;
            else if (minorClass & 0x20) // Pointing device (mouse)
                dev.deviceClass = MlptProtocol::DEVCLASS_HID_MOUSE;
            else if (minorClass & 0x08) // Gamepad
                dev.deviceClass = MlptProtocol::DEVCLASS_HID_GAMEPAD;
            else if (minorClass & 0x04) // Joystick
                dev.deviceClass = MlptProtocol::DEVCLASS_HID_GAMEPAD;
            else
                dev.deviceClass = MlptProtocol::DEVCLASS_HID_OTHER;
            break;
        case 0x04: // Audio/Video
            dev.deviceClass = MlptProtocol::DEVCLASS_AUDIO;
            break;
        default:
            dev.deviceClass = MlptProtocol::DEVCLASS_OTHER;
            break;
        }

        // Try to get battery level from the BT device registry
        // Windows stores battery info in the device node properties
        dev.batteryPercent = -1;
        if (deviceInfo.fConnected) {
            // Search for battery info via DEVPKEY_Bluetooth_Battery
            // The HID device node often has a "BatteryLevel" registry value
            GUID hidGuid;
            HidD_GetHidGuid(&hidGuid);

            HDEVINFO hidDevInfo = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr,
                                                        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
            if (hidDevInfo != INVALID_HANDLE_VALUE) {
                SP_DEVICE_INTERFACE_DATA ifData = {};
                ifData.cbSize = sizeof(ifData);

                for (DWORD j = 0; SetupDiEnumDeviceInterfaces(hidDevInfo, nullptr, &hidGuid, j, &ifData); j++) {
                    SP_DEVINFO_DATA hidInfoData = {};
                    hidInfoData.cbSize = sizeof(hidInfoData);
                    DWORD reqSize = 0;
                    SetupDiGetDeviceInterfaceDetailW(hidDevInfo, &ifData, nullptr, 0, &reqSize, nullptr);

                    auto* detail = static_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(malloc(reqSize));
                    if (!detail) continue;
                    detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

                    if (SetupDiGetDeviceInterfaceDetailW(hidDevInfo, &ifData, detail, reqSize, nullptr, &hidInfoData)) {
                        WCHAR instanceId[MAX_DEVICE_ID_LEN];
                        if (CM_Get_Device_IDW(hidInfoData.DevInst, instanceId, MAX_DEVICE_ID_LEN, 0) == CR_SUCCESS) {
                            QString instPath = QString::fromWCharArray(instanceId).toUpper();
                            if (instPath.contains(btAddrClean)) {
                                // Found HID device for this BT device. Check for battery in registry.
                                HKEY devRegKey = SetupDiOpenDevRegKey(hidDevInfo, &hidInfoData,
                                                                      DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
                                if (devRegKey != INVALID_HANDLE_VALUE) {
                                    DWORD batteryLevel = 0;
                                    DWORD size = sizeof(batteryLevel);
                                    DWORD type = 0;
                                    if (RegQueryValueExW(devRegKey, L"BatteryLevel", nullptr, &type,
                                                         reinterpret_cast<LPBYTE>(&batteryLevel), &size) == ERROR_SUCCESS) {
                                        if (type == REG_DWORD && batteryLevel <= 100) {
                                            dev.batteryPercent = static_cast<int8_t>(batteryLevel);
                                        }
                                    }
                                    RegCloseKey(devRegKey);
                                }
                                free(detail);
                                break;
                            }
                        }
                    }
                    free(detail);
                }
                SetupDiDestroyDeviceInfoList(hidDevInfo);
            }
        }

        m_Devices.append(dev);

    } while (BluetoothFindNextDevice(hFind, &deviceInfo));

    BluetoothFindDeviceClose(hFind);
}

#else
// Non-Windows stubs
void DeviceEnumerator::enumerateUsb()
{
    qInfo() << "Passthrough: USB enumeration not implemented on this platform";
}

void DeviceEnumerator::enumerateBluetooth()
{
    qInfo() << "Passthrough: Bluetooth enumeration not implemented on this platform";
}
#endif

// ─── Hot-plug polling ───

void DeviceEnumerator::startHotplugPolling(int intervalMs)
{
    m_HotplugTimer.start(intervalMs);
    qInfo() << "Passthrough: hotplug polling started, interval:" << intervalMs << "ms";
}

void DeviceEnumerator::stopHotplugPolling()
{
    m_HotplugTimer.stop();
}

void DeviceEnumerator::pollHotplug()
{
    // Save current device fingerprints (VID:PID:serial + instancePath)
    QSet<QString> oldFingerprints;
    QHash<QString, uint32_t> oldIdMap;
    for (const auto& dev : m_Devices) {
        QString fp = QString("%1:%2:%3:%4")
            .arg(dev.vendorId).arg(dev.productId)
            .arg(dev.serialNumber).arg(dev.instancePath);
        oldFingerprints.insert(fp);
        oldIdMap.insert(fp, dev.deviceId);
    }

    // Save forwarding state
    QHash<QString, bool> forwardingState;
    QHash<QString, bool> autoForwardState;
    for (const auto& dev : m_Devices) {
        QString fp = QString("%1:%2:%3:%4")
            .arg(dev.vendorId).arg(dev.productId)
            .arg(dev.serialNumber).arg(dev.instancePath);
        forwardingState.insert(fp, dev.isForwarding);
        autoForwardState.insert(fp, dev.autoForward);
    }

    // Re-enumerate
    beginResetModel();
    m_Devices.clear();
    m_NextDeviceId = 1; // Reset to allow stable ID reuse
    enumerateUsb();
    enumerateBluetooth();
    endResetModel();

    // Build new fingerprints and check for changes
    QSet<QString> newFingerprints;
    for (auto& dev : m_Devices) {
        QString fp = QString("%1:%2:%3:%4")
            .arg(dev.vendorId).arg(dev.productId)
            .arg(dev.serialNumber).arg(dev.instancePath);
        newFingerprints.insert(fp);

        // Restore state for devices that persisted
        if (oldFingerprints.contains(fp)) {
            dev.deviceId = oldIdMap[fp]; // Keep stable ID
            dev.isForwarding = forwardingState.value(fp, false);
            dev.autoForward = autoForwardState.value(fp, false);
        }
    }

    // Detect added devices
    QSet<QString> added = newFingerprints - oldFingerprints;
    for (const auto& fp : added) {
        for (const auto& dev : m_Devices) {
            QString devFp = QString("%1:%2:%3:%4")
                .arg(dev.vendorId).arg(dev.productId)
                .arg(dev.serialNumber).arg(dev.instancePath);
            if (devFp == fp) {
                qInfo() << "Passthrough: device added:" << dev.name;
                emit deviceAdded(dev.deviceId);
                break;
            }
        }
    }

    // Detect removed devices
    QSet<QString> removed = oldFingerprints - newFingerprints;
    for (const auto& fp : removed) {
        uint32_t id = oldIdMap.value(fp, 0);
        if (id > 0) {
            qInfo() << "Passthrough: device removed, id:" << id;
            emit deviceRemoved(id);
        }
    }

    if (!added.isEmpty() || !removed.isEmpty()) {
        emit devicesChanged();
    }
}
