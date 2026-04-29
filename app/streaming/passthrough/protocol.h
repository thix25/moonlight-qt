// Moonlight Passthrough Protocol - Shared between client and server
// No Qt dependency - pure C++ with standard headers only
#pragma once

#include <cstdint>
#include <cstring>

namespace MlptProtocol {

// Protocol constants
static constexpr uint8_t  MAGIC[4] = { 'M', 'L', 'P', 'T' };
static constexpr uint16_t VERSION   = 0x0001;
static constexpr uint16_t DEFAULT_PORT = 47990;

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
    uint8_t  reserved[5];
};

// Device descriptor sent in MSG_DEVICE_LIST and MSG_DEVICE_ATTACH
// Variable-length: followed by nameLen bytes of UTF-8 device name
struct DeviceDescriptor {
    uint32_t deviceId;        // Unique ID for this session
    uint16_t vendorId;
    uint16_t productId;
    uint8_t  transport;       // DeviceTransport
    uint8_t  deviceClass;     // DeviceClass
    uint8_t  nameLen;         // Length of UTF-8 name following this struct
    uint8_t  reserved;
    char     serialNumber[32]; // Null-terminated serial (or empty)
};

// MSG_DEVICE_LIST payload: [uint16_t count] [DeviceDescriptor + name] * count
struct DeviceListHeader {
    uint16_t count;
};

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
    uint8_t  direction;       // 0 = out (host→device), 1 = in (device→host)
    uint8_t  endpoint;
    uint8_t  transferType;    // 0=control, 1=isochronous, 2=bulk, 3=interrupt
    uint8_t  reserved;
    uint32_t dataLen;
    int32_t  status;          // 0 = success, negative = error (for RETURN)
    // For control transfers: 8-byte setup packet follows here
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
