// UsbIpDaemon — Runs a standard USB/IP server on the client side.
// In usbip-win2 mode, the VHCI driver on the server connects directly
// to this daemon to exchange URBs, bypassing the MLPT TCP relay.
#pragma once

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHash>
#include <QMutex>
#include <QByteArray>

#include "protocol.h"

class UsbIpExporter;

// ============================================================================
// USB/IP wire protocol structures (big-endian / network byte order)
// ============================================================================

#pragma pack(push, 1)

// USB/IP operation common header
struct UsbIpOpCommon {
    uint16_t version;   // 0x0111
    uint16_t code;      // OP_REQ_IMPORT, OP_REP_IMPORT, etc.
    uint32_t status;    // 0 = OK
};

// OP_REQ_IMPORT: client (driver) requests to import a device
struct UsbIpOpImportRequest {
    char busid[32];
};

// usbip_usb_device: device info for OP_REP_IMPORT
struct UsbIpUsbDevice {
    char     path[256];
    char     busid[32];
    uint32_t busnum;
    uint32_t devnum;
    uint32_t speed;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bConfigurationValue;
    uint8_t  bNumConfigurations;
    uint8_t  bNumInterfaces;
};

// USB/IP URB header (48 bytes, network byte order on the wire)
struct UsbIpWireHeader {
    uint32_t command;        // 1=CMD_SUBMIT, 2=CMD_UNLINK, 3=RET_SUBMIT, 4=RET_UNLINK
    uint32_t seqnum;
    uint32_t devid;
    uint32_t direction;      // 0=OUT, 1=IN
    uint32_t ep;

    union {
        struct {
            uint32_t transfer_flags;
            int32_t  transfer_buffer_length;
            int32_t  start_frame;
            int32_t  number_of_packets;
            int32_t  interval;
            uint8_t  setup[8];
        } cmd_submit;
        struct {
            int32_t  status;
            int32_t  actual_length;
            int32_t  start_frame;
            int32_t  number_of_packets;
            int32_t  error_count;
        } ret_submit;
        struct {
            uint32_t seqnum;
        } cmd_unlink;
        struct {
            int32_t  status;
        } ret_unlink;
    } u;
};

static_assert(sizeof(UsbIpWireHeader) == 48, "UsbIpWireHeader must be 48 bytes");

// USB/IP ISO packet descriptor (on the wire)
struct UsbIpWireIsoPacket {
    uint32_t offset;
    uint32_t length;
    uint32_t actual_length;
    uint32_t status;
};

#pragma pack(pop)

// USB/IP protocol constants
static constexpr uint16_t USBIP_VERSION       = 0x0111;
static constexpr uint16_t OP_REQ_IMPORT       = 0x8003;
static constexpr uint16_t OP_REP_IMPORT       = 0x0003;
static constexpr uint32_t USBIP_CMD_SUBMIT    = 0x00000001;
static constexpr uint32_t USBIP_CMD_UNLINK    = 0x00000002;
static constexpr uint32_t USBIP_RET_SUBMIT    = 0x00000003;
static constexpr uint32_t USBIP_RET_UNLINK    = 0x00000004;

// ============================================================================
// Per-device connection: one TCP connection from the VHCI driver
// ============================================================================

struct DaemonDeviceSession {
    QTcpSocket* socket = nullptr;
    UsbIpExporter* exporter = nullptr;
    QString busid;
    uint32_t deviceId = 0;
    uint32_t devid = 0;  // busnum<<16 | devnum, generated from deviceId
    QByteArray recvBuffer;
    bool importDone = false;
};

// ============================================================================
// UsbIpDaemon — USB/IP server for client-side device export
// ============================================================================

class UsbIpDaemon : public QObject
{
    Q_OBJECT

public:
    explicit UsbIpDaemon(QObject* parent = nullptr);
    ~UsbIpDaemon();

    // Start listening. port=0 means auto-assign.
    bool start(uint16_t port = 0);
    void stop();

    uint16_t port() const;
    bool isRunning() const { return m_Server.isListening(); }

    // Register a device for export with a given busid.
    // When the VHCI driver connects and sends OP_REQ_IMPORT with this busid,
    // the daemon will use the given UsbIpExporter to handle URBs.
    void exportDevice(const QString& busid, uint32_t deviceId, UsbIpExporter* exporter);

    // Remove a device from export
    void unexportDevice(const QString& busid);

    // Generate a busid string from a deviceId
    static QString makeBusid(uint32_t deviceId);

signals:
    void deviceImported(const QString& busid, uint32_t deviceId);
    void deviceDisconnected(const QString& busid, uint32_t deviceId);

private slots:
    void onNewConnection();
    void onSessionReadyRead();
    void onSessionDisconnected();
    void onUrbCompleted(uint32_t deviceId, const MlptProtocol::UsbIpHeader& header, const QByteArray& data);

private:
    bool handleImportRequest(DaemonDeviceSession* session);
    void handleUrbData(DaemonDeviceSession* session);
    void processCmdSubmit(DaemonDeviceSession* session, const UsbIpWireHeader& hdr, const QByteArray& data);
    void processCmdUnlink(DaemonDeviceSession* session, const UsbIpWireHeader& hdr);
    void sendRetSubmit(DaemonDeviceSession* session, const MlptProtocol::UsbIpHeader& mlptHdr, const QByteArray& data);
    void sendRetUnlink(DaemonDeviceSession* session, uint32_t seqnum, int32_t status);

    // Byte-swap helpers for USB/IP wire format (network byte order)
    static void byteswapHeader(UsbIpWireHeader& hdr);

    QTcpServer m_Server;

    // Exported devices: busid -> exporter (ready for import)
    QMutex m_ExportMutex;
    struct ExportEntry {
        UsbIpExporter* exporter;
        uint32_t deviceId;
    };
    QHash<QString, ExportEntry> m_Exports;

    // Active sessions: socket -> session
    QHash<QTcpSocket*, DaemonDeviceSession*> m_Sessions;

    // Map deviceId -> session for URB completion routing
    QHash<uint32_t, DaemonDeviceSession*> m_DeviceSessions;
};
