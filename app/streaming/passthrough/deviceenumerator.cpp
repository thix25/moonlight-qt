#include "deviceenumerator.h"

#include <QtDebug>
#include <QRegularExpression>
#include <QSettings>
#include <QDateTime>

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
#include <winioctl.h>

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
    case LocationInfoRole:
        return dev.locationInfo;
    case DriverRole:
        return dev.driver;
    case ManufacturerRole:
        return dev.manufacturer;
    case AddedTimeRole:
        return dev.addedTime;
    case StorageSizeTextRole: {
        if (dev.storageSizeBytes == 0) return QString();
        const double GB = 1000000000.0;
        const double TB = 1000000000000.0;
        const double MB = 1000000.0;
        if (dev.storageSizeBytes >= TB)
            return QString("%1 TB").arg(dev.storageSizeBytes / TB, 0, 'f', 1);
        if (dev.storageSizeBytes >= GB)
            return QString("%1 GB").arg(dev.storageSizeBytes / GB, 0, 'f', 1);
        if (dev.storageSizeBytes >= MB)
            return QString("%1 MB").arg(static_cast<int>(dev.storageSizeBytes / MB));
        return QString("%1 KB").arg(static_cast<int>(dev.storageSizeBytes / 1000.0));
    }
    case LastErrorRole:
        return dev.lastError;
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
        { LocationInfoRole,   "locationInfo" },
        { DriverRole,         "driver" },
        { ManufacturerRole,   "manufacturer" },
        { AddedTimeRole,      "addedTime" },
        { StorageSizeTextRole,"storageSizeText" },
        { LastErrorRole,      "lastError" },
    };
}

void DeviceEnumerator::enumerate()
{
    beginResetModel();
    m_Devices.clear();
    m_NextDeviceId = 1;

    enumerateUsb();
    enumerateBluetooth();

    // Restore auto-forward flags from saved settings
    loadAutoForwardList();

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

void DeviceEnumerator::setDeviceError(uint32_t deviceId, const QString& error)
{
    for (int i = 0; i < m_Devices.size(); i++) {
        if (m_Devices[i].deviceId == deviceId) {
            m_Devices[i].lastError = error;
            QModelIndex idx = index(i);
            emit dataChanged(idx, idx, { LastErrorRole });
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

    saveAutoForwardList();
}

QString DeviceEnumerator::deviceFingerprint(const PassthroughDevice& dev)
{
    return QString("%1:%2:%3:%4")
        .arg(dev.transport)
        .arg(dev.vendorId, 4, 16, QLatin1Char('0'))
        .arg(dev.productId, 4, 16, QLatin1Char('0'))
        .arg(dev.serialNumber);
}

void DeviceEnumerator::saveAutoForwardList() const
{
    QSettings settings;
    settings.beginGroup("Passthrough");

    QStringList autoFwdList;
    for (const auto& dev : m_Devices) {
        if (dev.autoForward) {
            autoFwdList.append(deviceFingerprint(dev));
        }
    }
    settings.setValue("autoForwardDevices", autoFwdList);

    settings.endGroup();
}

void DeviceEnumerator::loadAutoForwardList()
{
    QSettings settings;
    settings.beginGroup("Passthrough");

    QStringList autoFwdList = settings.value("autoForwardDevices").toStringList();
    settings.endGroup();

    if (autoFwdList.isEmpty()) return;

    QSet<QString> fwdSet(autoFwdList.begin(), autoFwdList.end());

    for (int i = 0; i < m_Devices.size(); i++) {
        QString fp = deviceFingerprint(m_Devices[i]);
        if (fwdSet.contains(fp)) {
            m_Devices[i].autoForward = true;
        }
    }
}

QList<uint32_t> DeviceEnumerator::getAutoForwardDeviceIds() const
{
    QList<uint32_t> result;
    for (const auto& dev : m_Devices) {
        if (dev.autoForward) {
            result.append(dev.deviceId);
        }
    }
    return result;
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

// {53F56307-B6BF-11D0-94F2-00A0C91EFB8B}
static const GUID LocalGUID_DEVINTERFACE_DISK =
    { 0x53F56307, 0xB6BF, 0x11D0, { 0x94, 0xF2, 0x00, 0xA0, 0xC9, 0x1E, 0xFB, 0x8B } };

// Walk the USB device tree to find a disk descendant and query its physical size
static quint64 getStorageCapacity(DEVINST usbDevInst)
{
    // Search children and grandchildren for a USBSTOR\DISK device
    auto findDisk = [](DEVINST parent) -> DEVINST {
        DEVINST child;
        if (CM_Get_Child(&child, parent, 0) != CR_SUCCESS) return 0;
        do {
            WCHAR instId[MAX_DEVICE_ID_LEN];
            if (CM_Get_Device_IDW(child, instId, MAX_DEVICE_ID_LEN, 0) == CR_SUCCESS) {
                if (_wcsnicmp(instId, L"USBSTOR\\DISK", 12) == 0)
                    return child;
            }
            // Check grandchildren (for composite USB devices)
            DEVINST grandchild;
            if (CM_Get_Child(&grandchild, child, 0) == CR_SUCCESS) {
                do {
                    if (CM_Get_Device_IDW(grandchild, instId, MAX_DEVICE_ID_LEN, 0) == CR_SUCCESS) {
                        if (_wcsnicmp(instId, L"USBSTOR\\DISK", 12) == 0)
                            return grandchild;
                    }
                } while (CM_Get_Sibling(&grandchild, grandchild, 0) == CR_SUCCESS);
            }
        } while (CM_Get_Sibling(&child, child, 0) == CR_SUCCESS);
        return 0;
    };

    DEVINST diskInst = findDisk(usbDevInst);
    if (diskInst == 0) return 0;

    WCHAR diskInstId[MAX_DEVICE_ID_LEN];
    if (CM_Get_Device_IDW(diskInst, diskInstId, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS)
        return 0;

    // Get the disk interface path via Configuration Manager
    ULONG bufLen = 0;
    if (CM_Get_Device_Interface_List_SizeW(&bufLen,
            const_cast<LPGUID>(&LocalGUID_DEVINTERFACE_DISK),
            diskInstId, CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS || bufLen <= 1)
        return 0;

    QByteArray ifBuf(static_cast<int>(bufLen * sizeof(WCHAR)), 0);
    if (CM_Get_Device_Interface_ListW(
            const_cast<LPGUID>(&LocalGUID_DEVINTERFACE_DISK),
            diskInstId, reinterpret_cast<PWCHAR>(ifBuf.data()), bufLen,
            CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS)
        return 0;

    QString diskPath = QString::fromWCharArray(reinterpret_cast<const WCHAR*>(ifBuf.constData()));
    if (diskPath.isEmpty()) return 0;

    // Open disk device and query total physical size
    HANDLE hDisk = CreateFileW(reinterpret_cast<LPCWSTR>(diskPath.utf16()),
        GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, 0, nullptr);
    if (hDisk == INVALID_HANDLE_VALUE) return 0;

    GET_LENGTH_INFORMATION lengthInfo = {};
    DWORD bytesReturned = 0;
    quint64 size = 0;
    if (DeviceIoControl(hDisk, IOCTL_DISK_GET_LENGTH_INFO,
            nullptr, 0, &lengthInfo, sizeof(lengthInfo), &bytesReturned, nullptr)) {
        size = static_cast<quint64>(lengthInfo.Length.QuadPart);
    }
    CloseHandle(hDisk);

    return size;
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

    // Handle REG_MULTI_SZ (multiple null-terminated strings ending with double-null):
    // concatenate all substrings with a space so callers can do a single contains() check.
    const wchar_t* ptr = reinterpret_cast<const wchar_t*>(buf.constData());
    const wchar_t* end = ptr + bufSize / sizeof(wchar_t);
    QStringList parts;
    while (ptr < end && *ptr != L'\0') {
        QString s = QString::fromWCharArray(ptr);
        if (!s.isEmpty())
            parts.append(s);
        ptr += s.length() + 1; // skip string + its null terminator
    }
    return parts.join(QLatin1Char(' '));
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

        // Get physical USB port location for distinguishing identical devices
        QString locationInfo = getDeviceRegistryProperty(devInfo, &devInfoData, SPDRP_LOCATION_INFORMATION);
        // Clean up common prefixes to keep the label short
        locationInfo.remove(QRegularExpression("^Port_#"));
        locationInfo.replace(QRegularExpression("\\.Hub_#"), " Hub ");

        // Get manufacturer string
        QString manufacturer = getDeviceRegistryProperty(devInfo, &devInfoData, SPDRP_MFG);

        // Get active driver name (e.g. "usbvideo", "WinUSB", "usbstor")
        QString driverName = getDeviceRegistryProperty(devInfo, &devInfoData, SPDRP_SERVICE);

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
        dev.manufacturer = manufacturer;
        dev.serialNumber = serial;
        dev.instancePath = instancePath;
        dev.locationInfo = locationInfo;
        dev.driver = driverName;
        dev.transport = MlptProtocol::TRANSPORT_USB;
        dev.deviceClass = classifyUsbDevice(deviceClass, compatIds);
        dev.isForwarding = false;
        dev.autoForward = false;
        dev.addedTime = QDateTime::currentDateTime();
        dev.batteryPercent = -1;
        dev.rssi = 0;
        dev.btPaired = false;
        dev.btConnected = false;
        dev.storageSizeBytes = 0;
        dev.lastError.clear();

        // Query physical disk size for storage devices
        if (dev.deviceClass == MlptProtocol::DEVCLASS_STORAGE) {
            dev.storageSizeBytes = getStorageCapacity(devInfoData.DevInst);
        }

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
        dev.locationInfo.clear();
        dev.driver.clear();
        dev.manufacturer.clear();
        dev.transport = MlptProtocol::TRANSPORT_BLUETOOTH;
        dev.isForwarding = false;
        dev.autoForward = false;
        dev.addedTime = QDateTime::currentDateTime();
        dev.storageSizeBytes = 0;
        dev.lastError.clear();
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
    auto makeFp = [](const PassthroughDevice& dev) {
        if (dev.transport == MlptProtocol::TRANSPORT_BLUETOOTH) {
            // BT address (serialNumber) is unique and stable. Don't include VID/PID
            // because they change between connected (resolved via HID) and disconnected (0:0).
            return QString("BT:%1").arg(dev.serialNumber);
        }
        return QString("USB:%1:%2:%3:%4")
            .arg(dev.vendorId).arg(dev.productId)
            .arg(dev.serialNumber).arg(dev.instancePath);
    };

    // Build fingerprints of current devices
    QSet<QString> oldFingerprints;
    QHash<QString, int> oldFpToIndex;
    for (int i = 0; i < m_Devices.size(); i++) {
        QString fp = makeFp(m_Devices[i]);
        oldFingerprints.insert(fp);
        oldFpToIndex.insert(fp, i);
    }

    // Enumerate into temporary storage (save/restore m_Devices & m_NextDeviceId)
    QList<PassthroughDevice> oldDevices = m_Devices;
    uint32_t savedNextId = m_NextDeviceId;

    m_Devices.clear();
    m_NextDeviceId = 1;
    enumerateUsb();
    enumerateBluetooth();
    QList<PassthroughDevice> freshDevices = m_Devices;

    // Build fingerprints of freshly-enumerated devices
    QSet<QString> newFingerprints;
    QHash<QString, int> newFpToIndex;
    for (int i = 0; i < freshDevices.size(); i++) {
        QString fp = makeFp(freshDevices[i]);
        newFingerprints.insert(fp);
        newFpToIndex.insert(fp, i);
    }

    QSet<QString> added = newFingerprints - oldFingerprints;
    QSet<QString> removed = oldFingerprints - newFingerprints;

    if (added.isEmpty() && removed.isEmpty()) {
        // ── No structural changes ──
        // Restore original list (preserving IDs, forwarding, autoForward) and
        // just update volatile properties (BT battery, connected, name) via
        // dataChanged() — no model reset, scroll position is preserved.
        m_Devices = oldDevices;
        m_NextDeviceId = savedNextId;

        for (int i = 0; i < m_Devices.size(); i++) {
            QString fp = makeFp(m_Devices[i]);
            auto freshIdx = newFpToIndex.find(fp);
            if (freshIdx == newFpToIndex.end()) continue;

            const auto& fresh = freshDevices[freshIdx.value()];
            bool changed = false;

            if (m_Devices[i].btConnected != fresh.btConnected) {
                m_Devices[i].btConnected = fresh.btConnected;
                changed = true;
            }
            if (m_Devices[i].batteryPercent != fresh.batteryPercent) {
                m_Devices[i].batteryPercent = fresh.batteryPercent;
                changed = true;
            }
            if (m_Devices[i].btPaired != fresh.btPaired) {
                m_Devices[i].btPaired = fresh.btPaired;
                changed = true;
            }
            if (m_Devices[i].name != fresh.name) {
                m_Devices[i].name = fresh.name;
                changed = true;
            }
            if (m_Devices[i].vendorId != fresh.vendorId) {
                m_Devices[i].vendorId = fresh.vendorId;
                changed = true;
            }
            if (m_Devices[i].productId != fresh.productId) {
                m_Devices[i].productId = fresh.productId;
                changed = true;
            }

            if (changed) {
                QModelIndex idx = index(i);
                emit dataChanged(idx, idx);
            }
        }
        return;
    }

    // ── Structural changes (devices added or removed) ──
    // Fall back to full model reset with state restoration.

    // Save old device IDs for removed-device signals
    QHash<QString, uint32_t> oldIdMap;
    for (const auto& dev : oldDevices) {
        oldIdMap.insert(makeFp(dev), dev.deviceId);
    }

    beginResetModel();

    // Build merged list: keep existing devices in original order (so the list
    // stays stable), then append newly-added devices at the end (so the user
    // can easily identify which device was just plugged in).
    QList<PassthroughDevice> mergedDevices;

    // Existing devices that are still present, in their original order
    for (const auto& oldDev : oldDevices) {
        QString fp = makeFp(oldDev);
        if (removed.contains(fp)) continue; // Unplugged — skip

        auto freshIt = newFpToIndex.find(fp);
        if (freshIt != newFpToIndex.end()) {
            // Use fresh enumeration data (updated name, battery, etc.)
            PassthroughDevice dev = freshDevices[freshIt.value()];
            dev.deviceId = oldDev.deviceId;
            dev.isForwarding = oldDev.isForwarding;
            dev.autoForward = oldDev.autoForward;
            dev.addedTime = oldDev.addedTime;
            dev.lastError = oldDev.lastError;
            mergedDevices.append(dev);
        }
    }

    // New devices appended at the end
    for (const auto& fp : added) {
        auto freshIt = newFpToIndex.find(fp);
        if (freshIt != newFpToIndex.end()) {
            PassthroughDevice dev = freshDevices[freshIt.value()];
            dev.deviceId = 0; // Assigned below
            // addedTime is already set to now by enumerateUsb/enumerateBluetooth
            mergedDevices.append(dev);
        }
    }

    m_Devices = mergedDevices;

    // Fix device IDs: existing devices keep their old IDs, new devices (id==0)
    // get fresh sequential IDs that don't collide with existing ones.
    QSet<uint32_t> usedIds;
    m_NextDeviceId = savedNextId;
    for (auto& dev : m_Devices) {
        if (dev.deviceId == 0 || usedIds.contains(dev.deviceId)) {
            dev.deviceId = m_NextDeviceId++;
        }
        usedIds.insert(dev.deviceId);
    }
    for (uint32_t id : usedIds) {
        if (id >= m_NextDeviceId) {
            m_NextDeviceId = id + 1;
        }
    }

    // Restore auto-forward from settings for newly-added devices
    loadAutoForwardList();

    endResetModel();

    // Emit signals for added/removed devices
    for (const auto& fp : added) {
        for (const auto& dev : m_Devices) {
            if (makeFp(dev) == fp) {
                qInfo() << "Passthrough: device added:" << dev.name;
                emit deviceAdded(dev.deviceId);
                break;
            }
        }
    }

    for (const auto& fp : removed) {
        uint32_t id = oldIdMap.value(fp, 0);
        if (id > 0) {
            qInfo() << "Passthrough: device removed, id:" << id;
            emit deviceRemoved(id);
        }
    }

    emit devicesChanged();
}
