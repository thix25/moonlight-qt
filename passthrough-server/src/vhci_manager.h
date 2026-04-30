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
// VHCI backend enumeration
// ============================================================================

enum class VhciBackendType {
    LEGACY,  // Old usbip-win (cezanne): ReadFile/WriteFile URB relay
    WIN2,    // usbip-win2 (vadimgrn): driver connects to remote USB/IP daemon
};

// ============================================================================
// Legacy VHCI driver IOCTL codes (from usbip-win usbip_vhci_api.h)
// ============================================================================

#define USBIP_VHCI_IOCTL_LEGACY(idx) \
    CTL_CODE(FILE_DEVICE_BUS_EXTENDER, idx, METHOD_BUFFERED, FILE_READ_DATA)

#define IOCTL_USBIP_VHCI_PLUGIN_HARDWARE_LEGACY    USBIP_VHCI_IOCTL_LEGACY(0x800)
#define IOCTL_USBIP_VHCI_UNPLUG_HARDWARE_LEGACY    USBIP_VHCI_IOCTL_LEGACY(0x801)
#define IOCTL_USBIP_VHCI_GET_PORTS_STATUS_LEGACY   USBIP_VHCI_IOCTL_LEGACY(0x803)

// ============================================================================
// usbip-win2 VHCI IOCTL codes (from usbip-win2 include/usbip/vhci.h)
// ============================================================================

#define USBIP_VHCI_IOCTL_WIN2(idx) \
    CTL_CODE(FILE_DEVICE_UNKNOWN, idx, METHOD_BUFFERED, FILE_READ_DATA | FILE_WRITE_DATA)

#define IOCTL_USBIP_VHCI_PLUGIN_HARDWARE_WIN2      USBIP_VHCI_IOCTL_WIN2(0x800)
#define IOCTL_USBIP_VHCI_PLUGOUT_HARDWARE_WIN2     USBIP_VHCI_IOCTL_WIN2(0x801)
#define IOCTL_USBIP_VHCI_GET_IMPORTED_DEVICES_WIN2 USBIP_VHCI_IOCTL_WIN2(0x802)

// Shared constants
#define MAX_VHCI_SERIAL_ID 127
#define MAX_VHCI_PORTS     127
#define WIN2_BUS_ID_SIZE   32

// ============================================================================
// Native USB/IP protocol header (48 bytes, matches both driver formats)
// Used for ReadFile/WriteFile on the VHCI device handle (legacy mode).
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
// Legacy VHCI plugin/unplug/status structures (old usbip-win)
// ============================================================================

struct VhciPlugInfoLegacy {
    unsigned long size;
    unsigned int  devid;
    signed char   port;        // -1 for auto-assign
    wchar_t       wserial[MAX_VHCI_SERIAL_ID + 1];
    unsigned char dscr_dev[18];
    unsigned char dscr_conf[9]; // Start of config descriptor (variable length follows)
};

struct VhciUnplugInfoLegacy {
    signed char addr;
    char unused[3];
};

struct VhciPortsStatusLegacy {
    unsigned char n_max_ports;
    unsigned char port_status[MAX_VHCI_PORTS];
};

// ============================================================================
// usbip-win2 VHCI plugin/plugout structures (matches include/usbip/vhci.h)
// ============================================================================

struct Win2PluginHardware {
    unsigned long size;          // IN: sizeof(this full struct)
    int           port;          // OUT: assigned port (>= 1) or 0 on error
    char          busid[WIN2_BUS_ID_SIZE];  // IN: device busid on remote server
    char          service[32];   // IN: TCP port number as string (e.g. "3240")
    char          host[1025];    // IN: hostname or IP address
};

struct Win2PlugoutHardware {
    unsigned long size;
    int           port;          // IN: port to unplug (all ports if <= 0)
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
// Supports both legacy (usbip-win) and win2 (usbip-win2) backends.
// ============================================================================

// Callback when the VHCI driver has a URB for us to forward to the client.
// Parameters: deviceId, pointer to NativeUsbIpHeader + trailing data, total bytes
// Only used in legacy mode.
using VhciUrbCallback = std::function<void(uint32_t deviceId,
                                           const uint8_t* data, size_t len)>;

class VhciManager {
public:
    // If forceBackend is specified, only that backend is tried.
    // Otherwise, tries win2 first, falls back to legacy.
    explicit VhciManager(VhciBackendType forceBackend = VhciBackendType::WIN2);
    ~VhciManager();

    bool isDriverAvailable() const;
    VhciBackendType backend() const { return m_Backend; }

    // Set callback for when the VHCI driver produces URBs (CMD_SUBMIT / CMD_UNLINK)
    // Only effective in legacy mode.
    void setUrbCallback(VhciUrbCallback callback);

    // Attach a device — legacy mode (server relays URBs via ReadFile/WriteFile)
    int attachDevice(uint32_t deviceId, uint16_t vendorId, uint16_t productId,
                     const uint8_t* deviceDescriptor, size_t deviceDescrLen,
                     const uint8_t* configDescriptor, size_t configDescrLen,
                     const std::string& serial = "");

    // Attach a device — win2 mode (driver connects to remote USB/IP daemon)
    int attachDeviceWin2(uint32_t deviceId,
                         const std::string& clientHost,
                         uint16_t clientDaemonPort,
                         const std::string& busid);

    // Detach a device (works for both backends)
    bool detachDevice(uint32_t deviceId);

    // Feed a URB return (RET_SUBMIT) from the client back to the VHCI driver
    // Only used in legacy mode.
    bool feedUrbReturn(uint32_t deviceId, const uint8_t* data, size_t len);

    int getAttachedCount() const;

    // Get the USB transfer type for a given endpoint address on a device
    // Returns: 0=control, 1=iso, 2=bulk, 3=interrupt, -1=unknown
    // Only meaningful in legacy mode.
    int getEndpointType(uint32_t deviceId, uint8_t endpointAddress) const;

private:
    bool discoverVhciPath();
    bool discoverVhciPathLegacy();
    bool discoverVhciPathWin2();

#ifdef _WIN32
    HANDLE openNewVhciHandle();
#endif

    void readLoop(AttachedDevice* dev);

    // Parse USB config descriptor to build endpoint type map
    static std::unordered_map<uint8_t, uint8_t>
        parseEndpointTypes(const uint8_t* configDescriptor, size_t len);

    std::wstring m_VhciDevicePath;
    bool m_DriverAvailable;
    VhciBackendType m_Backend;

    VhciUrbCallback m_UrbCallback;

    mutable std::mutex m_Mutex;
    std::unordered_map<uint32_t, std::unique_ptr<AttachedDevice>> m_AttachedDevices;
};
