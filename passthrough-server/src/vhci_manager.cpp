#include "vhci_manager.h"

#include <cstdio>
#include <cstring>
#include <algorithm>

#ifdef _WIN32
#include <SetupAPI.h>
#include <initguid.h>
#pragma comment(lib, "setupapi.lib")

// Legacy GUID (old usbip-win by cezanne)
DEFINE_GUID(GUID_DEVINTERFACE_VHCI_USBIP_LEGACY,
    0xD35F7840, 0x6A0C, 0x11D2, 0xB8, 0x41, 0x00, 0xC0, 0x4F, 0xAD, 0x51, 0x71);

// Win2 GUID (usbip-win2 by vadimgrn — standard USB Host Controller interface)
DEFINE_GUID(GUID_DEVINTERFACE_USB_HOST_CONTROLLER_WIN2,
    0xB4030C06, 0xDC5F, 0x4FCC, 0x87, 0xEB, 0xE5, 0x51, 0x5A, 0x09, 0x35, 0xC0);
#endif

// Maximum buffer for ReadFile — header + up to 1MB data
static constexpr size_t VHCI_READ_BUFFER_SIZE = sizeof(NativeUsbIpHeader) + (1024 * 1024);

// ============================================================================
// Constructor / Destructor
// ============================================================================

VhciManager::VhciManager(VhciBackendType forceBackend)
    : m_DriverAvailable(false)
    , m_Backend(forceBackend)
{
    m_DriverAvailable = discoverVhciPath();
    if (m_DriverAvailable) {
        const char* backendName = (m_Backend == VhciBackendType::WIN2) ? "usbip-win2" : "legacy (usbip-win)";
        printf("[VHCI] Driver found (%s) and opened successfully\n", backendName);
    } else {
        printf("[VHCI] Driver not available (no VHCI driver installed)\n");
    }
}

VhciManager::~VhciManager()
{
    // Detach all devices (stops read threads, unplugs, closes handles)
    std::vector<uint32_t> deviceIds;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        for (auto& [id, dev] : m_AttachedDevices) {
            deviceIds.push_back(id);
        }
    }
    for (uint32_t id : deviceIds) {
        detachDevice(id);
    }
}

bool VhciManager::isDriverAvailable() const { return m_DriverAvailable; }

void VhciManager::setUrbCallback(VhciUrbCallback callback)
{
    m_UrbCallback = std::move(callback);
}

// ============================================================================
// VHCI device path discovery (dual-mode)
// ============================================================================

#ifdef _WIN32

bool VhciManager::discoverVhciPath()
{
    // Try the requested backend first
    if (m_Backend == VhciBackendType::WIN2) {
        if (discoverVhciPathWin2()) return true;
        printf("[VHCI] usbip-win2 not found, trying legacy...\n");
        m_Backend = VhciBackendType::LEGACY;
        return discoverVhciPathLegacy();
    } else {
        if (discoverVhciPathLegacy()) return true;
        printf("[VHCI] Legacy driver not found, trying usbip-win2...\n");
        m_Backend = VhciBackendType::WIN2;
        return discoverVhciPathWin2();
    }
}

bool VhciManager::discoverVhciPathLegacy()
{
    HDEVINFO devInfo = SetupDiGetClassDevsW(
        &GUID_DEVINTERFACE_VHCI_USBIP_LEGACY, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

    if (devInfo == INVALID_HANDLE_VALUE) {
        return false;
    }

    SP_DEVICE_INTERFACE_DATA ifData = {};
    ifData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    if (!SetupDiEnumDeviceInterfaces(devInfo, nullptr,
            &GUID_DEVINTERFACE_VHCI_USBIP_LEGACY, 0, &ifData)) {
        SetupDiDestroyDeviceInfoList(devInfo);
        return false;
    }

    DWORD requiredSize = 0;
    SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, nullptr, 0, &requiredSize, nullptr);

    auto* detailData = static_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(malloc(requiredSize));
    if (!detailData) {
        SetupDiDestroyDeviceInfoList(devInfo);
        return false;
    }
    detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

    if (!SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, detailData, requiredSize, nullptr, nullptr)) {
        free(detailData);
        SetupDiDestroyDeviceInfoList(devInfo);
        return false;
    }

    m_VhciDevicePath = detailData->DevicePath;
    printf("[VHCI] Legacy device path: %ls\n", m_VhciDevicePath.c_str());

    HANDLE testHandle = CreateFileW(m_VhciDevicePath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, 0, nullptr);

    if (testHandle == INVALID_HANDLE_VALUE) {
        free(detailData);
        SetupDiDestroyDeviceInfoList(devInfo);
        return false;
    }

    CloseHandle(testHandle);
    free(detailData);
    SetupDiDestroyDeviceInfoList(devInfo);
    return true;
}

bool VhciManager::discoverVhciPathWin2()
{
    // Enumerate all USB Host Controller interfaces and find the usbip-win2 one
    HDEVINFO devInfo = SetupDiGetClassDevsW(
        &GUID_DEVINTERFACE_USB_HOST_CONTROLLER_WIN2, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

    if (devInfo == INVALID_HANDLE_VALUE) {
        return false;
    }

    SP_DEVICE_INTERFACE_DATA ifData = {};
    ifData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    // Iterate all USB host controller interfaces to find the usbip-win2 one
    for (DWORD idx = 0;
         SetupDiEnumDeviceInterfaces(devInfo, nullptr,
             &GUID_DEVINTERFACE_USB_HOST_CONTROLLER_WIN2, idx, &ifData);
         idx++)
    {
        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, nullptr, 0, &requiredSize, nullptr);

        auto* detailData = static_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(malloc(requiredSize));
        if (!detailData) continue;
        detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        SP_DEVINFO_DATA devInfoData = {};
        devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

        if (!SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, detailData,
                requiredSize, nullptr, &devInfoData)) {
            free(detailData);
            continue;
        }

        // Check if this is the usbip-win2 device by checking the hardware ID
        wchar_t hwId[256] = {};
        if (SetupDiGetDeviceRegistryPropertyW(devInfo, &devInfoData,
                SPDRP_HARDWAREID, nullptr, reinterpret_cast<BYTE*>(hwId),
                sizeof(hwId), nullptr)) {
            // usbip-win2 hardware ID: ROOT\USBIP_WIN2\UDE
            if (wcsstr(hwId, L"USBIP_WIN2") != nullptr) {
                m_VhciDevicePath = detailData->DevicePath;
                printf("[VHCI] Win2 device path: %ls\n", m_VhciDevicePath.c_str());

                HANDLE testHandle = CreateFileW(m_VhciDevicePath.c_str(),
                    GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    nullptr, OPEN_EXISTING, 0, nullptr);

                if (testHandle != INVALID_HANDLE_VALUE) {
                    CloseHandle(testHandle);
                    free(detailData);
                    SetupDiDestroyDeviceInfoList(devInfo);
                    return true;
                }
            }
        }

        free(detailData);
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    return false;
}

HANDLE VhciManager::openNewVhciHandle()
{
    if (m_VhciDevicePath.empty()) return INVALID_HANDLE_VALUE;

    HANDLE handle = CreateFileW(m_VhciDevicePath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, 0, nullptr);

    if (handle == INVALID_HANDLE_VALUE) {
        printf("[VHCI] Failed to open new VHCI handle: %lu\n", GetLastError());
    }
    return handle;
}

#else
// Non-Windows stubs
bool VhciManager::discoverVhciPath() { return false; }
#endif

// ============================================================================
// Device attach/detach
// ============================================================================

int VhciManager::attachDevice(uint32_t deviceId, uint16_t vendorId, uint16_t productId,
                               const uint8_t* deviceDescriptor, size_t deviceDescrLen,
                               const uint8_t* configDescriptor, size_t configDescrLen,
                               const std::string& serial)
{
#ifdef _WIN32
    if (m_Backend != VhciBackendType::LEGACY) {
        printf("[VHCI] attachDevice (legacy) called but backend is win2\n");
        return -1;
    }

    std::lock_guard<std::mutex> lock(m_Mutex);

    if (!m_DriverAvailable) {
        printf("[VHCI] Cannot attach: driver not available\n");
        return -1;
    }

    if (m_AttachedDevices.count(deviceId)) {
        printf("[VHCI] Device %u already attached on port %d\n",
               deviceId, m_AttachedDevices[deviceId]->vhciPort);
        return m_AttachedDevices[deviceId]->vhciPort;
    }

    if (deviceDescrLen < 18) {
        printf("[VHCI] Device descriptor too short: %zu bytes\n", deviceDescrLen);
        return -1;
    }
    if (configDescrLen < 9) {
        printf("[VHCI] Config descriptor too short: %zu bytes\n", configDescrLen);
        return -1;
    }

    // Open a dedicated VHCI handle for this device
    HANDLE devHandle = openNewVhciHandle();
    if (devHandle == INVALID_HANDLE_VALUE) {
        return -1;
    }

    // Build plugin info
    size_t pluginfoSize = sizeof(VhciPlugInfoLegacy) + configDescrLen - 9;
    auto* pluginfo = static_cast<VhciPlugInfoLegacy*>(calloc(1, pluginfoSize));
    if (!pluginfo) {
        CloseHandle(devHandle);
        return -1;
    }

    pluginfo->size = static_cast<unsigned long>(pluginfoSize);
    pluginfo->devid = deviceId;
    pluginfo->port = -1; // auto-assign

    if (!serial.empty()) {
        size_t converted;
        mbstowcs_s(&converted, pluginfo->wserial, MAX_VHCI_SERIAL_ID, serial.c_str(), _TRUNCATE);
    } else {
        pluginfo->wserial[0] = L'\0';
    }

    memcpy(pluginfo->dscr_dev, deviceDescriptor, 18);
    memcpy(pluginfo->dscr_conf, configDescriptor, configDescrLen);

    // Plugin via IOCTL on the per-device handle
    DWORD bytesReturned;
    BOOL result = DeviceIoControl(devHandle,
        IOCTL_USBIP_VHCI_PLUGIN_HARDWARE_LEGACY,
        pluginfo, static_cast<DWORD>(pluginfoSize),
        pluginfo, static_cast<DWORD>(pluginfoSize),
        &bytesReturned, nullptr);

    if (!result) {
        printf("[VHCI] PLUGIN IOCTL failed: %lu\n", GetLastError());
        free(pluginfo);
        CloseHandle(devHandle);
        return -1;
    }

    int assignedPort = pluginfo->port;
    free(pluginfo);

    if (assignedPort < 0) {
        printf("[VHCI] No free ports available\n");
        CloseHandle(devHandle);
        return -1;
    }

    // Create AttachedDevice
    auto dev = std::make_unique<AttachedDevice>();
    dev->deviceId = deviceId;
    dev->vendorId = vendorId;
    dev->productId = productId;
    dev->vhciPort = assignedPort;
    dev->serial = serial;
    dev->deviceHandle = devHandle;
    dev->endpointTypes = parseEndpointTypes(configDescriptor, configDescrLen);

    // Start URB read thread
    dev->readRunning = true;
    AttachedDevice* devPtr = dev.get();
    dev->readThread = std::thread([this, devPtr]() {
        // Boost thread priority for low-latency URB forwarding
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        readLoop(devPtr);
    });

    m_AttachedDevices[deviceId] = std::move(dev);

    printf("[VHCI] Device %u (%04X:%04X) attached on port %d, %zu endpoints mapped\n",
           deviceId, vendorId, productId, assignedPort, devPtr->endpointTypes.size());
    return assignedPort;

#else
    (void)deviceId; (void)vendorId; (void)productId;
    (void)deviceDescriptor; (void)deviceDescrLen;
    (void)configDescriptor; (void)configDescrLen;
    (void)serial;
    return -1;
#endif
}

int VhciManager::attachDeviceWin2(uint32_t deviceId,
                                   const std::string& clientHost,
                                   uint16_t clientDaemonPort,
                                   const std::string& busid)
{
#ifdef _WIN32
    if (m_Backend != VhciBackendType::WIN2) {
        printf("[VHCI] attachDeviceWin2 called but backend is legacy\n");
        return -1;
    }

    std::lock_guard<std::mutex> lock(m_Mutex);

    if (!m_DriverAvailable) {
        printf("[VHCI] Cannot attach: driver not available\n");
        return -1;
    }

    if (m_AttachedDevices.count(deviceId)) {
        printf("[VHCI] Device %u already attached on port %d\n",
               deviceId, m_AttachedDevices[deviceId]->vhciPort);
        return m_AttachedDevices[deviceId]->vhciPort;
    }

    // Open a VHCI handle for this operation
    HANDLE devHandle = openNewVhciHandle();
    if (devHandle == INVALID_HANDLE_VALUE) {
        return -1;
    }

    // Build win2 plugin_hardware struct
    Win2PluginHardware pluginfo = {};
    pluginfo.size = sizeof(pluginfo);
    pluginfo.port = 0; // OUT: will be filled by driver

    strncpy_s(pluginfo.busid, sizeof(pluginfo.busid), busid.c_str(), _TRUNCATE);

    // Convert daemon port to string for "service" field
    char portStr[32];
    snprintf(portStr, sizeof(portStr), "%u", clientDaemonPort);
    strncpy_s(pluginfo.service, sizeof(pluginfo.service), portStr, _TRUNCATE);

    strncpy_s(pluginfo.host, sizeof(pluginfo.host), clientHost.c_str(), _TRUNCATE);

    printf("[VHCI] Win2 plugin: host=%s, service=%s, busid=%s\n",
           pluginfo.host, pluginfo.service, pluginfo.busid);

    // Issue PLUGIN_HARDWARE IOCTL — the driver will connect to the client's
    // USB/IP daemon at host:service and import the device with the given busid
    DWORD bytesReturned;
    // Output only needs the base size + port field
    constexpr DWORD outLen = offsetof(Win2PluginHardware, port) + sizeof(pluginfo.port);

    BOOL result = DeviceIoControl(devHandle,
        IOCTL_USBIP_VHCI_PLUGIN_HARDWARE_WIN2,
        &pluginfo, sizeof(pluginfo),
        &pluginfo, outLen,
        &bytesReturned, nullptr);

    if (!result) {
        printf("[VHCI] Win2 PLUGIN IOCTL failed: %lu\n", GetLastError());
        CloseHandle(devHandle);
        return -1;
    }

    int assignedPort = pluginfo.port;
    if (assignedPort <= 0) {
        printf("[VHCI] Win2: No port assigned (port=%d)\n", assignedPort);
        CloseHandle(devHandle);
        return -1;
    }

    // Create AttachedDevice — in win2 mode, no read thread is needed
    // The driver handles URB transport directly via WSK
    auto dev = std::make_unique<AttachedDevice>();
    dev->deviceId = deviceId;
    dev->vendorId = 0;   // Not known until driver imports
    dev->productId = 0;
    dev->vhciPort = assignedPort;
    dev->deviceHandle = devHandle;
    dev->readRunning = false;  // No read loop in win2 mode

    m_AttachedDevices[deviceId] = std::move(dev);

    printf("[VHCI] Win2: Device %u attached on port %d (driver connects to %s:%u)\n",
           deviceId, assignedPort, clientHost.c_str(), clientDaemonPort);
    return assignedPort;

#else
    (void)deviceId; (void)clientHost; (void)clientDaemonPort; (void)busid;
    return -1;
#endif
}

bool VhciManager::detachDevice(uint32_t deviceId)
{
#ifdef _WIN32
    std::unique_ptr<AttachedDevice> dev;

    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        auto it = m_AttachedDevices.find(deviceId);
        if (it == m_AttachedDevices.end()) {
            printf("[VHCI] Device %u not found for detach\n", deviceId);
            return false;
        }
        dev = std::move(it->second);
        m_AttachedDevices.erase(it);
    }

    // Stop the read thread (legacy mode only)
    if (dev->readRunning) {
        dev->readRunning = false;
        if (dev->readThread.joinable()) {
            CancelSynchronousIo(dev->readThread.native_handle());
            dev->readThread.join();
        }
    }

    // Unplug from VHCI
    if (dev->deviceHandle != INVALID_HANDLE_VALUE) {
        if (m_Backend == VhciBackendType::WIN2) {
            // Win2: use plugout_hardware IOCTL with port number
            Win2PlugoutHardware plugout = {};
            plugout.size = sizeof(plugout);
            plugout.port = dev->vhciPort;
            DWORD bytesReturned;
            DeviceIoControl(dev->deviceHandle, IOCTL_USBIP_VHCI_PLUGOUT_HARDWARE_WIN2,
                &plugout, sizeof(plugout), nullptr, 0, &bytesReturned, nullptr);
        } else {
            // Legacy: use unplug_hardware IOCTL with port address
            VhciUnplugInfoLegacy unplug = {};
            unplug.addr = static_cast<signed char>(dev->vhciPort);
            DWORD bytesReturned;
            DeviceIoControl(dev->deviceHandle, IOCTL_USBIP_VHCI_UNPLUG_HARDWARE_LEGACY,
                &unplug, sizeof(unplug), nullptr, 0, &bytesReturned, nullptr);
        }

        CloseHandle(dev->deviceHandle);
        dev->deviceHandle = INVALID_HANDLE_VALUE;
    }

    printf("[VHCI] Device %u detached from port %d\n", deviceId, dev->vhciPort);
    return true;

#else
    (void)deviceId;
    return false;
#endif
}

// ============================================================================
// URB read loop (runs in per-device thread)
// ============================================================================

void VhciManager::readLoop(AttachedDevice* dev)
{
#ifdef _WIN32
    printf("[VHCI] Read loop started for device %u on port %d\n",
           dev->deviceId, dev->vhciPort);

    auto buffer = std::make_unique<uint8_t[]>(VHCI_READ_BUFFER_SIZE);

    while (dev->readRunning) {
        DWORD bytesRead = 0;
        BOOL ok = ReadFile(dev->deviceHandle, buffer.get(),
                           static_cast<DWORD>(VHCI_READ_BUFFER_SIZE),
                           &bytesRead, nullptr);

        if (!ok || bytesRead < sizeof(NativeUsbIpHeader)) {
            DWORD err = GetLastError();
            if (dev->readRunning && err != ERROR_OPERATION_ABORTED) {
                printf("[VHCI] ReadFile failed for device %u: error %lu, bytes %lu\n",
                       dev->deviceId, err, bytesRead);
            }
            break;
        }

        auto* hdr = reinterpret_cast<NativeUsbIpHeader*>(buffer.get());

        // Check if we got a partial read (header only, data still pending)
        if (hdr->base.command == USBIP_CMD_SUBMIT) {
            int32_t expectedDataLen = hdr->u.cmd_submit.transfer_buffer_length;
            bool isOut = (hdr->base.direction == 0);

            if (isOut && expectedDataLen > 0) {
                size_t totalExpected = sizeof(NativeUsbIpHeader) + expectedDataLen;
                if (hdr->u.cmd_submit.number_of_packets > 0) {
                    totalExpected += hdr->u.cmd_submit.number_of_packets *
                                    sizeof(NativeUsbIpIsoPacketDescriptor);
                }

                // If we only got the header, do a second read for the data
                if (bytesRead == sizeof(NativeUsbIpHeader) && totalExpected > sizeof(NativeUsbIpHeader)) {
                    DWORD dataBytes = 0;
                    size_t remaining = totalExpected - sizeof(NativeUsbIpHeader);
                    if (remaining > VHCI_READ_BUFFER_SIZE - sizeof(NativeUsbIpHeader)) {
                        remaining = VHCI_READ_BUFFER_SIZE - sizeof(NativeUsbIpHeader);
                    }
                    ok = ReadFile(dev->deviceHandle,
                                  buffer.get() + sizeof(NativeUsbIpHeader),
                                  static_cast<DWORD>(remaining),
                                  &dataBytes, nullptr);
                    if (!ok) {
                        if (dev->readRunning) {
                            printf("[VHCI] Partial ReadFile failed for device %u: %lu\n",
                                   dev->deviceId, GetLastError());
                        }
                        break;
                    }
                    bytesRead += dataBytes;
                }
            }
        }

        // Invoke the callback with the raw native data
        if (m_UrbCallback) {
            m_UrbCallback(dev->deviceId, buffer.get(), bytesRead);
        }
    }

    printf("[VHCI] Read loop ended for device %u\n", dev->deviceId);
#else
    (void)dev;
#endif
}

// ============================================================================
// Feed URB return to VHCI driver
// ============================================================================

bool VhciManager::feedUrbReturn(uint32_t deviceId, const uint8_t* data, size_t len)
{
#ifdef _WIN32
    std::lock_guard<std::mutex> lock(m_Mutex);

    auto it = m_AttachedDevices.find(deviceId);
    if (it == m_AttachedDevices.end()) {
        printf("[VHCI] feedUrbReturn: device %u not found\n", deviceId);
        return false;
    }

    auto& dev = it->second;
    if (dev->deviceHandle == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD bytesWritten;
    BOOL ok = WriteFile(dev->deviceHandle, data, static_cast<DWORD>(len),
                        &bytesWritten, nullptr);

    if (!ok) {
        printf("[VHCI] WriteFile failed for device %u: %lu\n",
               deviceId, GetLastError());
        return false;
    }

    return true;

#else
    (void)deviceId; (void)data; (void)len;
    return false;
#endif
}

// ============================================================================
// Endpoint type map
// ============================================================================

int VhciManager::getEndpointType(uint32_t deviceId, uint8_t endpointAddress) const
{
    std::lock_guard<std::mutex> lock(m_Mutex);

    auto it = m_AttachedDevices.find(deviceId);
    if (it == m_AttachedDevices.end()) return -1;

    auto epIt = it->second->endpointTypes.find(endpointAddress);
    if (epIt == it->second->endpointTypes.end()) return -1;

    return epIt->second;
}

std::unordered_map<uint8_t, uint8_t>
VhciManager::parseEndpointTypes(const uint8_t* configDescriptor, size_t len)
{
    std::unordered_map<uint8_t, uint8_t> result;

    // USB config descriptor format:
    // Each descriptor starts with bLength (1B), bDescriptorType (1B)
    // Endpoint descriptor: bDescriptorType = 0x05, bEndpointAddress at offset 2, bmAttributes at offset 3
    size_t offset = 0;
    while (offset + 2 <= len) {
        uint8_t bLength = configDescriptor[offset];
        uint8_t bDescriptorType = configDescriptor[offset + 1];

        if (bLength < 2 || offset + bLength > len) break;

        if (bDescriptorType == 0x05 && bLength >= 7) {
            // Endpoint descriptor
            uint8_t bEndpointAddress = configDescriptor[offset + 2];
            uint8_t bmAttributes = configDescriptor[offset + 3];
            uint8_t transferType = bmAttributes & 0x03; // bits 1:0

            result[bEndpointAddress] = transferType;
        }

        offset += bLength;
    }

    return result;
}

// ============================================================================
// Misc
// ============================================================================

int VhciManager::getAttachedCount() const
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    return static_cast<int>(m_AttachedDevices.size());
}
