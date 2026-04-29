#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <functional>
#include <thread>
#include <atomic>
#include <memory>

#ifdef _WIN32
#include <Windows.h>
#include <winioctl.h>
#endif

// ============================================================================
// VHCI driver IOCTL codes (from usbip-win usbip_vhci_api.h)
// ============================================================================

#define USBIP_VHCI_IOCTL(idx) \
    CTL_CODE(FILE_DEVICE_BUS_EXTENDER, idx, METHOD_BUFFERED, FILE_READ_DATA)

#define IOCTL_USBIP_VHCI_PLUGIN_HARDWARE    USBIP_VHCI_IOCTL(0x0)
#define IOCTL_USBIP_VHCI_UNPLUG_HARDWARE    USBIP_VHCI_IOCTL(0x1)
#define IOCTL_USBIP_VHCI_GET_PORTS_STATUS   USBIP_VHCI_IOCTL(0x3)

#define MAX_VHCI_SERIAL_ID 127
#define MAX_VHCI_PORTS     127

// ============================================================================
// Native USB/IP protocol header (48 bytes, matches usbip-win driver format)
// Used for ReadFile/WriteFile on the VHCI device handle.
// ============================================================================

#pragma pack(push, 1)

struct NativeUsbIpHeaderBasic {
    uint32_t command;       // USBIP_CMD_SUBMIT=1, USBIP_CMD_UNLINK=2,
                            // USBIP_RET_SUBMIT=3, USBIP_RET_UNLINK=4
    uint32_t seqnum;
    uint32_t devid;
    uint32_t direction;     // 0=OUT, 1=IN
    uint32_t ep;            // endpoint number (0-15)
};

struct NativeUsbIpCmdSubmit {
    uint32_t transfer_flags;
    int32_t  transfer_buffer_length;
    int32_t  start_frame;
    int32_t  number_of_packets;
    int32_t  interval;
    uint8_t  setup[8];
};

struct NativeUsbIpRetSubmit {
    int32_t  status;
    int32_t  actual_length;
    int32_t  start_frame;
    int32_t  number_of_packets;
    int32_t  error_count;
};

struct NativeUsbIpCmdUnlink {
    uint32_t seqnum;        // seqnum of URB to unlink
};

struct NativeUsbIpRetUnlink {
    int32_t  status;
};

struct NativeUsbIpHeader {
    NativeUsbIpHeaderBasic base;
    union {
        NativeUsbIpCmdSubmit   cmd_submit;
        NativeUsbIpRetSubmit   ret_submit;
        NativeUsbIpCmdUnlink   cmd_unlink;
        NativeUsbIpRetUnlink   ret_unlink;
    } u;
};

static_assert(sizeof(NativeUsbIpHeaderBasic) == 20, "NativeUsbIpHeaderBasic must be 20 bytes");
static_assert(sizeof(NativeUsbIpHeader) == 48, "NativeUsbIpHeader must be 48 bytes");

#define USBIP_CMD_SUBMIT    0x0001
#define USBIP_CMD_UNLINK    0x0002
#define USBIP_RET_SUBMIT    0x0003
#define USBIP_RET_UNLINK    0x0004

struct NativeUsbIpIsoPacketDescriptor {
    uint32_t offset;
    uint32_t length;
    uint32_t actual_length;
    uint32_t status;
};

// ============================================================================
// VHCI plugin/unplug/status structures
// ============================================================================

struct VhciPlugInfo {
    unsigned long size;
    unsigned int  devid;
    signed char   port;        // -1 for auto-assign
    wchar_t       wserial[MAX_VHCI_SERIAL_ID + 1];
    unsigned char dscr_dev[18];
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

// ============================================================================
// AttachedDevice — per-device state including VHCI handle and I/O thread
// ============================================================================

struct AttachedDevice {
    uint32_t deviceId;
    uint16_t vendorId;
    uint16_t productId;
    int      vhciPort;
    std::string serial;

#ifdef _WIN32
    HANDLE   deviceHandle;   // Per-device VHCI file handle (for ReadFile/WriteFile)
#endif
    std::thread readThread;
    std::atomic<bool> readRunning;

    // Endpoint address -> USB transfer type (0=ctrl, 1=iso, 2=bulk, 3=intr)
    // Key is bEndpointAddress (endpoint number | direction << 7)
    std::unordered_map<uint8_t, uint8_t> endpointTypes;

    AttachedDevice() :
        deviceId(0), vendorId(0), productId(0), vhciPort(-1),
#ifdef _WIN32
        deviceHandle(INVALID_HANDLE_VALUE),
#endif
        readRunning(false) {}

    // Non-copyable (has thread)
    AttachedDevice(const AttachedDevice&) = delete;
    AttachedDevice& operator=(const AttachedDevice&) = delete;
};

// ============================================================================
// VhciManager — manages VHCI driver interaction and URB I/O
// ============================================================================

// Callback when the VHCI driver has a URB for us to forward to the client.
// Parameters: deviceId, pointer to NativeUsbIpHeader + trailing data, total bytes
using VhciUrbCallback = std::function<void(uint32_t deviceId,
                                           const uint8_t* data, size_t len)>;

class VhciManager {
public:
    VhciManager();
    ~VhciManager();

    bool isDriverAvailable() const;

    // Set callback for when the VHCI driver produces URBs (CMD_SUBMIT / CMD_UNLINK)
    void setUrbCallback(VhciUrbCallback callback);

    // Attach a device: opens a per-device handle, plugins to VHCI, starts URB read loop
    int attachDevice(uint32_t deviceId, uint16_t vendorId, uint16_t productId,
                     const uint8_t* deviceDescriptor, size_t deviceDescrLen,
                     const uint8_t* configDescriptor, size_t configDescrLen,
                     const std::string& serial = "");

    // Detach a device: stops URB read loop, unplugs from VHCI, closes handle
    bool detachDevice(uint32_t deviceId);

    // Feed a URB return (RET_SUBMIT) from the client back to the VHCI driver
    bool feedUrbReturn(uint32_t deviceId, const uint8_t* data, size_t len);

    int getAttachedCount() const;

    // Get the USB transfer type for a given endpoint address on a device
    // Returns: 0=control, 1=iso, 2=bulk, 3=interrupt, -1=unknown
    int getEndpointType(uint32_t deviceId, uint8_t endpointAddress) const;

private:
    bool discoverVhciPath();

#ifdef _WIN32
    HANDLE openNewVhciHandle();
#endif

    void readLoop(AttachedDevice* dev);

    // Parse USB config descriptor to build endpoint type map
    static std::unordered_map<uint8_t, uint8_t>
        parseEndpointTypes(const uint8_t* configDescriptor, size_t len);

    std::wstring m_VhciDevicePath;
    bool m_DriverAvailable;

    VhciUrbCallback m_UrbCallback;

    mutable std::mutex m_Mutex;
    std::unordered_map<uint32_t, std::unique_ptr<AttachedDevice>> m_AttachedDevices;
};
