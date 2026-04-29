#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <functional>
#include <thread>
#include <atomic>

#ifdef _WIN32
#include <Windows.h>
#endif

// VHCI IOCTL definitions (from usbip-win usbip_vhci_api.h)
#define USBIP_VHCI_IOCTL(idx) \
    CTL_CODE(FILE_DEVICE_BUS_EXTENDER, idx, METHOD_BUFFERED, FILE_READ_DATA)

#define IOCTL_USBIP_VHCI_PLUGIN_HARDWARE    USBIP_VHCI_IOCTL(0x0)
#define IOCTL_USBIP_VHCI_UNPLUG_HARDWARE    USBIP_VHCI_IOCTL(0x1)
#define IOCTL_USBIP_VHCI_GET_PORTS_STATUS   USBIP_VHCI_IOCTL(0x3)

#define MAX_VHCI_SERIAL_ID 127
#define MAX_VHCI_PORTS     127

// VHCI plugin info structure (must match driver)
#pragma pack(push, 1)
struct VhciPlugInfo {
    unsigned long size;
    unsigned int  devid;
    signed char   port;        // -1 for auto-assign
    wchar_t       wserial[MAX_VHCI_SERIAL_ID + 1];
    unsigned char dscr_dev[18];  // USB device descriptor
    unsigned char dscr_conf[9]; // Start of config descriptor (variable length follows)
};

struct VhciUnplugInfo {
    signed char addr;
    char unused[3];
};

struct VhciPortsStatus {
    unsigned char n_max_ports;
    unsigned char port_status[MAX_VHCI_PORTS];
};
#pragma pack(pop)

// Information about an attached device
struct AttachedDevice {
    uint32_t deviceId;
    uint16_t vendorId;
    uint16_t productId;
    int      vhciPort;
    std::string serial;
};

class VhciManager {
public:
    VhciManager();
    ~VhciManager();

    // Check if the usbip-win VHCI driver is loaded and accessible
    bool isDriverAvailable() const;

    // Create a virtual USB port for a device
    // deviceDescriptor: 18-byte USB device descriptor
    // configDescriptor: full USB configuration descriptor
    // serial: serial number string (can be empty)
    // Returns the assigned port number, or -1 on failure
    int attachDevice(uint32_t deviceId, uint16_t vendorId, uint16_t productId,
                     const uint8_t* deviceDescriptor, size_t deviceDescrLen,
                     const uint8_t* configDescriptor, size_t configDescrLen,
                     const std::string& serial = "");

    // Remove a virtual USB port
    bool detachDevice(uint32_t deviceId);

    // Get info about an attached device
    const AttachedDevice* getAttachedDevice(uint32_t deviceId) const;

    // Get the number of currently attached devices
    int getAttachedCount() const;

    // Get port status (true = in use)
    std::vector<bool> getPortStatus() const;

private:
    bool openVhciHandle();
    void closeVhciHandle();
    int findFreePort() const;

#ifdef _WIN32
    HANDLE m_VhciHandle;
#endif
    bool m_DriverAvailable;

    mutable std::mutex m_Mutex;
    std::unordered_map<uint32_t, AttachedDevice> m_AttachedDevices;
};
