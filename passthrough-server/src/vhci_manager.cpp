#include "vhci_manager.h"

#include <cstdio>
#include <cstring>
#include <algorithm>

#ifdef _WIN32
#include <SetupAPI.h>
#include <initguid.h>
#pragma comment(lib, "setupapi.lib")

DEFINE_GUID(GUID_DEVINTERFACE_VHCI_USBIP,
    0xD35F7840, 0x6A0C, 0x11D2, 0xB8, 0x41, 0x00, 0xC0, 0x4F, 0xAD, 0x51, 0x71);
#endif

// Maximum buffer for ReadFile — header + up to 1MB data
static constexpr size_t VHCI_READ_BUFFER_SIZE = sizeof(NativeUsbIpHeader) + (1024 * 1024);

// ============================================================================
// Constructor / Destructor
// ============================================================================

VhciManager::VhciManager()
    : m_DriverAvailable(false)
{
    m_DriverAvailable = discoverVhciPath();
    if (m_DriverAvailable) {
        printf("[VHCI] Driver found and opened successfully\n");
    } else {
        printf("[VHCI] Driver not available (usbip-win VHCI not installed?)\n");
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
// VHCI device path discovery
// ============================================================================

#ifdef _WIN32

bool VhciManager::discoverVhciPath()
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

    m_VhciDevicePath = detailData->DevicePath;
    printf("[VHCI] Device path: %ls\n", m_VhciDevicePath.c_str());

    // Verify we can actually open the device
    HANDLE testHandle = CreateFileW(m_VhciDevicePath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, 0, nullptr);

    if (testHandle == INVALID_HANDLE_VALUE) {
        printf("[VHCI] Failed to open VHCI device: %lu\n", GetLastError());
        free(detailData);
        SetupDiDestroyDeviceInfoList(devInfo);
        return false;
    }

    CloseHandle(testHandle);
    free(detailData);
    SetupDiDestroyDeviceInfoList(devInfo);
    return true;
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
    size_t pluginfoSize = sizeof(VhciPlugInfo) + configDescrLen - 9;
    auto* pluginfo = static_cast<VhciPlugInfo*>(calloc(1, pluginfoSize));
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
        IOCTL_USBIP_VHCI_PLUGIN_HARDWARE,
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
    dev->readThread = std::thread(&VhciManager::readLoop, this, devPtr);

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

    // Stop the read thread
    dev->readRunning = false;
    if (dev->readThread.joinable()) {
        // Cancel any pending synchronous I/O on the read thread
        CancelSynchronousIo(dev->readThread.native_handle());
        dev->readThread.join();
    }

    // Unplug from VHCI
    if (dev->deviceHandle != INVALID_HANDLE_VALUE) {
        VhciUnplugInfo unplug = {};
        unplug.addr = static_cast<signed char>(dev->vhciPort);
        DWORD bytesReturned;
        DeviceIoControl(dev->deviceHandle, IOCTL_USBIP_VHCI_UNPLUG_HARDWARE,
            &unplug, sizeof(unplug), nullptr, 0, &bytesReturned, nullptr);

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
