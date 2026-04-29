#include "vhci_manager.h"

#include <cstdio>
#include <cstring>
#include <algorithm>

#ifdef _WIN32
#include <SetupAPI.h>
#include <initguid.h>
#pragma comment(lib, "setupapi.lib")

// GUID_DEVINTERFACE_VHCI_USBIP from usbip-win
// {D35F7840-6A0C-11D2-B841-00C04FAD5171}
DEFINE_GUID(GUID_DEVINTERFACE_VHCI_USBIP,
    0xD35F7840, 0x6A0C, 0x11D2, 0xB8, 0x41, 0x00, 0xC0, 0x4F, 0xAD, 0x51, 0x71);
#endif

VhciManager::VhciManager()
#ifdef _WIN32
    : m_VhciHandle(INVALID_HANDLE_VALUE)
    , m_DriverAvailable(false)
#else
    : m_DriverAvailable(false)
#endif
{
    m_DriverAvailable = openVhciHandle();
    if (m_DriverAvailable) {
        printf("[VHCI] Driver found and opened successfully\n");
    } else {
        printf("[VHCI] Driver not available (usbip-win VHCI not installed?)\n");
    }
}

VhciManager::~VhciManager()
{
    // Detach all devices
    std::lock_guard<std::mutex> lock(m_Mutex);
    for (auto& [id, dev] : m_AttachedDevices) {
        printf("[VHCI] Cleanup: detaching device %u from port %d\n", id, dev.vhciPort);
#ifdef _WIN32
        if (m_VhciHandle != INVALID_HANDLE_VALUE) {
            VhciUnplugInfo unplug = {};
            unplug.addr = static_cast<signed char>(dev.vhciPort);
            DWORD bytesReturned;
            DeviceIoControl(m_VhciHandle, IOCTL_USBIP_VHCI_UNPLUG_HARDWARE,
                &unplug, sizeof(unplug), nullptr, 0, &bytesReturned, nullptr);
        }
#endif
    }
    m_AttachedDevices.clear();
    closeVhciHandle();
}

bool VhciManager::isDriverAvailable() const
{
    return m_DriverAvailable;
}

#ifdef _WIN32

bool VhciManager::openVhciHandle()
{
    HDEVINFO devInfo = SetupDiGetClassDevsW(
        &GUID_DEVINTERFACE_VHCI_USBIP, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

    if (devInfo == INVALID_HANDLE_VALUE) {
        printf("[VHCI] SetupDiGetClassDevs failed: %lu\n", GetLastError());
        return false;
    }

    SP_DEVICE_INTERFACE_DATA ifData = {};
    ifData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    if (!SetupDiEnumDeviceInterfaces(devInfo, nullptr,
            &GUID_DEVINTERFACE_VHCI_USBIP, 0, &ifData)) {
        printf("[VHCI] No VHCI device interface found: %lu\n", GetLastError());
        SetupDiDestroyDeviceInfoList(devInfo);
        return false;
    }

    // Get required buffer size
    DWORD requiredSize = 0;
    SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, nullptr, 0, &requiredSize, nullptr);

    auto* detailData = static_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(malloc(requiredSize));
    if (!detailData) {
        SetupDiDestroyDeviceInfoList(devInfo);
        return false;
    }
    detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

    if (!SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, detailData, requiredSize, nullptr, nullptr)) {
        printf("[VHCI] Failed to get device interface detail: %lu\n", GetLastError());
        free(detailData);
        SetupDiDestroyDeviceInfoList(devInfo);
        return false;
    }

    // Open the VHCI device
    m_VhciHandle = CreateFileW(detailData->DevicePath,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, 0, nullptr);

    if (m_VhciHandle == INVALID_HANDLE_VALUE) {
        printf("[VHCI] Failed to open VHCI device: %lu\n", GetLastError());
        printf("[VHCI] Path: %ls\n", detailData->DevicePath);
        free(detailData);
        SetupDiDestroyDeviceInfoList(devInfo);
        return false;
    }

    printf("[VHCI] Opened device: %ls\n", detailData->DevicePath);
    free(detailData);
    SetupDiDestroyDeviceInfoList(devInfo);
    return true;
}

void VhciManager::closeVhciHandle()
{
    if (m_VhciHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_VhciHandle);
        m_VhciHandle = INVALID_HANDLE_VALUE;
    }
}

int VhciManager::attachDevice(uint32_t deviceId, uint16_t vendorId, uint16_t productId,
                               const uint8_t* deviceDescriptor, size_t deviceDescrLen,
                               const uint8_t* configDescriptor, size_t configDescrLen,
                               const std::string& serial)
{
    std::lock_guard<std::mutex> lock(m_Mutex);

    if (m_VhciHandle == INVALID_HANDLE_VALUE) {
        printf("[VHCI] Cannot attach: driver not available\n");
        return -1;
    }

    // Check if device is already attached
    if (m_AttachedDevices.count(deviceId)) {
        printf("[VHCI] Device %u already attached on port %d\n",
               deviceId, m_AttachedDevices[deviceId].vhciPort);
        return m_AttachedDevices[deviceId].vhciPort;
    }

    // Validate descriptor sizes
    if (deviceDescrLen < 18) {
        printf("[VHCI] Device descriptor too short: %zu bytes\n", deviceDescrLen);
        return -1;
    }
    if (configDescrLen < 9) {
        printf("[VHCI] Config descriptor too short: %zu bytes\n", configDescrLen);
        return -1;
    }

    // Allocate variable-length pluginfo
    size_t pluginfoSize = sizeof(VhciPlugInfo) + configDescrLen - 9;
    auto* pluginfo = static_cast<VhciPlugInfo*>(calloc(1, pluginfoSize));
    if (!pluginfo) return -1;

    pluginfo->size = static_cast<unsigned long>(pluginfoSize);
    pluginfo->devid = deviceId;
    pluginfo->port = -1; // Auto-assign

    // Set serial
    if (!serial.empty()) {
        size_t converted;
        mbstowcs_s(&converted, pluginfo->wserial, MAX_VHCI_SERIAL_ID, serial.c_str(), _TRUNCATE);
    } else {
        pluginfo->wserial[0] = L'\0';
    }

    // Copy device descriptor (18 bytes)
    memcpy(pluginfo->dscr_dev, deviceDescriptor, 18);

    // Copy config descriptor (full length, variable)
    memcpy(pluginfo->dscr_conf, configDescriptor, configDescrLen);

    // Send plugin IOCTL
    DWORD bytesReturned;
    BOOL result = DeviceIoControl(m_VhciHandle,
        IOCTL_USBIP_VHCI_PLUGIN_HARDWARE,
        pluginfo, static_cast<DWORD>(pluginfoSize),
        pluginfo, static_cast<DWORD>(pluginfoSize),
        &bytesReturned, nullptr);

    if (!result) {
        printf("[VHCI] PLUGIN IOCTL failed: %lu\n", GetLastError());
        free(pluginfo);
        return -1;
    }

    int assignedPort = pluginfo->port;
    free(pluginfo);

    if (assignedPort < 0) {
        printf("[VHCI] No free ports available\n");
        return -1;
    }

    // Record the attachment
    AttachedDevice dev;
    dev.deviceId = deviceId;
    dev.vendorId = vendorId;
    dev.productId = productId;
    dev.vhciPort = assignedPort;
    dev.serial = serial;
    m_AttachedDevices[deviceId] = dev;

    printf("[VHCI] Device %u (%04X:%04X) attached on port %d\n",
           deviceId, vendorId, productId, assignedPort);
    return assignedPort;
}

bool VhciManager::detachDevice(uint32_t deviceId)
{
    std::lock_guard<std::mutex> lock(m_Mutex);

    auto it = m_AttachedDevices.find(deviceId);
    if (it == m_AttachedDevices.end()) {
        printf("[VHCI] Device %u not found for detach\n", deviceId);
        return false;
    }

    if (m_VhciHandle == INVALID_HANDLE_VALUE) {
        m_AttachedDevices.erase(it);
        return false;
    }

    VhciUnplugInfo unplug = {};
    unplug.addr = static_cast<signed char>(it->second.vhciPort);

    DWORD bytesReturned;
    BOOL result = DeviceIoControl(m_VhciHandle,
        IOCTL_USBIP_VHCI_UNPLUG_HARDWARE,
        &unplug, sizeof(unplug), nullptr, 0, &bytesReturned, nullptr);

    if (!result) {
        printf("[VHCI] UNPLUG IOCTL failed for port %d: %lu\n",
               it->second.vhciPort, GetLastError());
    } else {
        printf("[VHCI] Device %u detached from port %d\n",
               deviceId, it->second.vhciPort);
    }

    m_AttachedDevices.erase(it);
    return result != FALSE;
}

const AttachedDevice* VhciManager::getAttachedDevice(uint32_t deviceId) const
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    auto it = m_AttachedDevices.find(deviceId);
    return (it != m_AttachedDevices.end()) ? &it->second : nullptr;
}

int VhciManager::getAttachedCount() const
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    return static_cast<int>(m_AttachedDevices.size());
}

std::vector<bool> VhciManager::getPortStatus() const
{
    std::vector<bool> status(MAX_VHCI_PORTS, false);

    if (m_VhciHandle == INVALID_HANDLE_VALUE) return status;

    VhciPortsStatus portsStatus = {};
    DWORD bytesReturned;
    BOOL result = DeviceIoControl(m_VhciHandle,
        IOCTL_USBIP_VHCI_GET_PORTS_STATUS,
        nullptr, 0,
        &portsStatus, sizeof(portsStatus),
        &bytesReturned, nullptr);

    if (result) {
        for (int i = 0; i < portsStatus.n_max_ports && i < MAX_VHCI_PORTS; i++) {
            status[i] = (portsStatus.port_status[i] != 0);
        }
    }

    return status;
}

int VhciManager::findFreePort() const
{
    auto status = getPortStatus();
    for (int i = 0; i < static_cast<int>(status.size()); i++) {
        if (!status[i]) return i + 1; // Ports are 1-based
    }
    return -1;
}

#else // Non-Windows stub

bool VhciManager::openVhciHandle() { return false; }
void VhciManager::closeVhciHandle() {}

int VhciManager::attachDevice(uint32_t deviceId, uint16_t vendorId, uint16_t productId,
                               const uint8_t*, size_t, const uint8_t*, size_t,
                               const std::string&)
{
    printf("[VHCI] Not supported on this platform\n");
    return -1;
}

bool VhciManager::detachDevice(uint32_t) { return false; }
const AttachedDevice* VhciManager::getAttachedDevice(uint32_t) const { return nullptr; }
int VhciManager::getAttachedCount() const { return 0; }
std::vector<bool> VhciManager::getPortStatus() const { return {}; }
int VhciManager::findFreePort() const { return -1; }

#endif
