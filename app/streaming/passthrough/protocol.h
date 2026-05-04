// Moonlight Passthrough Protocol - Shared between client and server
// No Qt dependency - pure C++ with standard headers only
#pragma once

#include <cstdint>
#include <cstring>

namespace MlptProtocol {

// Protocol constants
static constexpr uint8_t  MAGIC[4] = { 'M', 'L', 'P', 'T' };
static constexpr uint16_t VERSION   = 0x0001;
static constexpr uint16_t DEFAULT_PORT = 47991;

// Header: [4B magic] [2B version] [2B msg_type] [4B payload_length]
static constexpr size_t HEADER_SIZE = 12;

// Message types
enum MsgType : uint16_t {
    // Handshake
    MSG_HELLO           = 0x0001,
    MSG_HELLO_ACK       = 0x0002,

    // Device management
    MSG_DEVICE_LIST     = 0x0010,
    MSG_DEVICE_ATTACH   = 0x0011,
    MSG_DEVICE_ATTACH_ACK = 0x0012,
    MSG_DEVICE_DETACH   = 0x0013,
    MSG_DEVICE_DETACH_ACK = 0x0014,

    // USB/IP URB traffic
    MSG_USBIP_SUBMIT    = 0x0030,
    MSG_USBIP_RETURN    = 0x0031,
    MSG_USBIP_UNLINK    = 0x0032,

    // Bluetooth metadata
    MSG_BT_DEVICE_INFO  = 0x0040,

    // Control
    MSG_KEEPALIVE       = 0x00FF,
};

// Device transport type
enum DeviceTransport : uint8_t {
    TRANSPORT_USB       = 0x01,
    TRANSPORT_BLUETOOTH = 0x02,
};

// Device class hints (for UI icons and filtering)
enum DeviceClass : uint8_t {
    DEVCLASS_UNKNOWN    = 0x00,
    DEVCLASS_HID_KEYBOARD = 0x01,
    DEVCLASS_HID_MOUSE  = 0x02,
    DEVCLASS_HID_GAMEPAD = 0x03,
    DEVCLASS_HID_OTHER  = 0x04,
    DEVCLASS_STORAGE    = 0x05,
    DEVCLASS_AUDIO      = 0x06,
    DEVCLASS_VIDEO      = 0x07,  // Webcam
    DEVCLASS_BT_ADAPTER = 0x08,  // Bluetooth adapter (for Mode A forwarding)
    DEVCLASS_OTHER      = 0xFF,
};

// Attachment status (server → client)
enum AttachStatus : uint8_t {
    ATTACH_OK           = 0x00,
    ATTACH_ERR_BUSY     = 0x01,  // Device already attached
    ATTACH_ERR_DRIVER   = 0x02,  // VHCI driver not available
    ATTACH_ERR_FAILED   = 0x03,  // Generic failure
};

// VHCI backend type (server tells client which driver is installed)
enum VhciBackend : uint8_t {
    VHCI_BACKEND_LEGACY = 0x00,  // Old usbip-win (cezanne): server relays URBs
    VHCI_BACKEND_WIN2   = 0x01,  // usbip-win2 (vadimgrn): driver connects directly
};

// USB transfer types (matches USB spec)
enum UsbTransferType : uint8_t {
    USB_XFER_CONTROL     = 0,
    USB_XFER_ISOCHRONOUS = 1,
    USB_XFER_BULK        = 2,
    USB_XFER_INTERRUPT   = 3,
};

// USB direction
enum UsbDirection : uint8_t {
    USB_DIR_OUT = 0,  // Host to device
    USB_DIR_IN  = 1,  // Device to host
};

// Maximum USB descriptor sizes
static constexpr size_t USB_DEVICE_DESCRIPTOR_SIZE = 18;
static constexpr size_t USB_MAX_CONFIG_DESCRIPTOR_SIZE = 4096;
static constexpr size_t USB_SETUP_PACKET_SIZE = 8;

// ─── Wire structures (all little-endian) ───

#pragma pack(push, 1)

struct Header {
    uint8_t  magic[4];
    uint16_t version;
    uint16_t msgType;
    uint32_t payloadLen;
};

// MSG_HELLO payload
struct HelloPayload {
    uint16_t clientVersion;
    uint8_t  sessionId[16];  // Random session identifier
};

// MSG_HELLO_ACK payload
struct HelloAckPayload {
    uint16_t serverVersion;
    uint8_t  vhciAvailable;  // 1 if VHCI driver is loaded
    uint8_t  vhciBackend;    // VhciBackend: 0=legacy, 1=win2
    uint8_t  reserved[4];
};

// Device descriptor sent in MSG_DEVICE_LIST and MSG_DEVICE_ATTACH
// Variable-length: followed by nameLen bytes of UTF-8 device name,
// then usbDescrDevLen bytes of USB device descriptor,
// then usbDescrConfLen bytes of USB configuration descriptor.
struct DeviceDescriptor {
    uint32_t deviceId;        // Unique ID for this session
    uint16_t vendorId;
    uint16_t productId;
    uint8_t  transport;       // DeviceTransport
    uint8_t  deviceClass;     // DeviceClass
    uint8_t  nameLen;         // Length of UTF-8 name following this struct
    uint8_t  usbSpeed;       // USB speed (1=low, 2=full, 3=high, 4=super)
    char     serialNumber[32]; // Null-terminated serial (or empty)
    uint16_t usbDescrDevLen;  // Length of USB device descriptor (18 for standard)
    uint16_t usbDescrConfLen; // Length of full USB configuration descriptor
    // Win2 mode fields (ignored by legacy server, set to 0 in legacy mode):
    uint16_t daemonPort;      // TCP port where client's USB/IP daemon listens (0 = legacy mode)
    char     busid[32];       // USB/IP busid for OP_REQ_IMPORT (empty = legacy mode)
    // Followed by: nameLen bytes of name, usbDescrDevLen bytes, usbDescrConfLen bytes
};

// MSG_DEVICE_LIST payload: [uint16_t count] [DeviceDescriptor + name + usb descriptors] * count
struct DeviceListHeader {
    uint16_t count;
};

// MSG_DEVICE_ATTACH payload: DeviceDescriptor followed by variable data
// The attach message includes full USB descriptors for VHCI plugin
// (same layout as DeviceDescriptor + trailing data)

// MSG_DEVICE_ATTACH_ACK payload
struct DeviceAttachAckPayload {
    uint32_t deviceId;
    uint8_t  status;          // AttachStatus
    uint8_t  vhciPort;        // Assigned VHCI port number
    uint8_t  reserved[2];
};

// MSG_DEVICE_DETACH payload
struct DeviceDetachPayload {
    uint32_t deviceId;
};

// MSG_USBIP_SUBMIT / MSG_USBIP_RETURN header
// Followed by transfer data of dataLen bytes
struct UsbIpHeader {
    uint32_t seqNum;
    uint32_t deviceId;
    uint8_t  direction;       // UsbDirection
    uint8_t  endpoint;
    uint8_t  transferType;    // UsbTransferType
    uint8_t  flags;           // Bit 0: setup packet present (for control xfers)
    uint32_t dataLen;
    int32_t  status;          // 0 = success, negative = error (for RETURN)
    uint32_t startFrame;      // For isochronous transfers
    uint32_t numIsoPackets;   // Number of ISO packet descriptors following data
    uint8_t  setupPacket[8];  // USB setup packet (for control transfers)
};

// ISO packet descriptor (follows data in isochronous transfers)
struct UsbIpIsoPacket {
    uint32_t offset;
    uint32_t length;
    uint32_t actualLength;    // Filled in RETURN
    int32_t  status;          // Filled in RETURN
};

// MSG_BT_DEVICE_INFO payload (supplemental metadata)
struct BtDeviceInfoPayload {
    uint32_t deviceId;
    int8_t   batteryPercent;  // -1 if unknown
    int8_t   rssi;            // dBm, 0 if unknown
    uint8_t  isPaired;
    uint8_t  isConnected;
    uint8_t  numServices;     // Number of service UUIDs following
    uint8_t  reserved[3];
    // Followed by numServices * 16 bytes of service UUIDs
};

#pragma pack(pop)

// ─── Serialization helpers ───

inline void writeHeader(uint8_t* buf, MsgType type, uint32_t payloadLen) {
    auto* h = reinterpret_cast<Header*>(buf);
    memcpy(h->magic, MAGIC, 4);
    h->version = VERSION;
    h->msgType = static_cast<uint16_t>(type);
    h->payloadLen = payloadLen;
}

inline bool validateHeader(const uint8_t* buf, Header& out) {
    memcpy(&out, buf, sizeof(Header));
    return memcmp(out.magic, MAGIC, 4) == 0;
}

} // namespace MlptProtocol
