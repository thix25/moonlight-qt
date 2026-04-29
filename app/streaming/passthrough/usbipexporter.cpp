#include "usbipexporter.h"

#include <QCoreApplication>
#include <QtDebug>

#include <libusb.h>

// ─── Static members ───

libusb_context* UsbIpExporter::s_LibusbCtx = nullptr;

bool UsbIpExporter::initLibusb()
{
    if (s_LibusbCtx) return true;

    int rc = libusb_init(&s_LibusbCtx);
    if (rc != LIBUSB_SUCCESS) {
        qCritical() << "Failed to init libusb:" << libusb_strerror(static_cast<libusb_error>(rc));
        return false;
    }

    // Set info-level logging
    libusb_set_option(s_LibusbCtx, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_INFO);

    qInfo() << "libusb initialized, version:" << libusb_get_version()->describe;
    return true;
}

void UsbIpExporter::shutdownLibusb()
{
    if (s_LibusbCtx) {
        libusb_exit(s_LibusbCtx);
        s_LibusbCtx = nullptr;
    }
}

// ─── Constructor / Destructor ───

UsbIpExporter::UsbIpExporter(QObject* parent)
    : QObject(parent)
    , m_DeviceHandle(nullptr)
    , m_DeviceId(0)
    , m_UsbSpeed(0)
    , m_NumInterfaces(0)
    , m_EventThread(nullptr)
    , m_EventThreadRunning(false)
{
}

UsbIpExporter::~UsbIpExporter()
{
    closeDevice();
}

// ─── Device open/close ───

bool UsbIpExporter::openDevice(uint16_t vendorId, uint16_t productId, const QString& serial)
{
    if (!s_LibusbCtx) {
        qCritical() << "UsbIpExporter: libusb not initialized";
        return false;
    }

    if (m_DeviceHandle) {
        qWarning() << "UsbIpExporter: device already open";
        return false;
    }

    if (serial.isEmpty()) {
        // Simple open by VID/PID
        m_DeviceHandle = libusb_open_device_with_vid_pid(s_LibusbCtx, vendorId, productId);
        if (!m_DeviceHandle) {
            qWarning() << "UsbIpExporter: failed to open device"
                       << QString::asprintf("%04x:%04x", vendorId, productId);
            return false;
        }
    } else {
        // Open by VID/PID + serial
        libusb_device** devList;
        ssize_t count = libusb_get_device_list(s_LibusbCtx, &devList);
        if (count < 0) {
            qWarning() << "UsbIpExporter: failed to get device list";
            return false;
        }

        QByteArray serialUtf8 = serial.toUtf8();
        bool found = false;

        for (ssize_t i = 0; i < count; i++) {
            struct libusb_device_descriptor desc;
            if (libusb_get_device_descriptor(devList[i], &desc) != 0) continue;
            if (desc.idVendor != vendorId || desc.idProduct != productId) continue;

            libusb_device_handle* handle;
            if (libusb_open(devList[i], &handle) != 0) continue;

            if (desc.iSerialNumber > 0) {
                unsigned char buf[256];
                int len = libusb_get_string_descriptor_ascii(handle, desc.iSerialNumber, buf, sizeof(buf));
                if (len > 0 && QByteArray(reinterpret_cast<char*>(buf), len) == serialUtf8) {
                    m_DeviceHandle = handle;
                    found = true;
                    break;
                }
            }
            libusb_close(handle);
        }

        libusb_free_device_list(devList, 1);

        if (!found) {
            qWarning() << "UsbIpExporter: device with matching serial not found";
            return false;
        }
    }

    // Get device speed
    libusb_device* dev = libusb_get_device(m_DeviceHandle);
    int speed = libusb_get_device_speed(dev);
    switch (speed) {
    case LIBUSB_SPEED_LOW:    m_UsbSpeed = 1; break;
    case LIBUSB_SPEED_FULL:   m_UsbSpeed = 2; break;
    case LIBUSB_SPEED_HIGH:   m_UsbSpeed = 3; break;
    case LIBUSB_SPEED_SUPER:  m_UsbSpeed = 4; break;
    default:                  m_UsbSpeed = 2; break; // Default to full speed
    }

    if (!readDescriptors()) {
        libusb_close(m_DeviceHandle);
        m_DeviceHandle = nullptr;
        return false;
    }

    if (!claimAllInterfaces()) {
        libusb_close(m_DeviceHandle);
        m_DeviceHandle = nullptr;
        return false;
    }

    startEventThread();

    qInfo() << "UsbIpExporter: opened device"
            << QString::asprintf("%04x:%04x", vendorId, productId)
            << "speed:" << m_UsbSpeed;
    return true;
}

bool UsbIpExporter::openDeviceByPath(uint8_t busNumber, uint8_t portNumber)
{
    if (!s_LibusbCtx) return false;
    if (m_DeviceHandle) return false;

    libusb_device** devList;
    ssize_t count = libusb_get_device_list(s_LibusbCtx, &devList);
    if (count < 0) return false;

    bool found = false;
    for (ssize_t i = 0; i < count; i++) {
        if (libusb_get_bus_number(devList[i]) == busNumber &&
            libusb_get_port_number(devList[i]) == portNumber) {
            if (libusb_open(devList[i], &m_DeviceHandle) == 0) {
                found = true;
            }
            break;
        }
    }

    libusb_free_device_list(devList, 1);

    if (!found) return false;

    libusb_device* dev = libusb_get_device(m_DeviceHandle);
    int speed = libusb_get_device_speed(dev);
    switch (speed) {
    case LIBUSB_SPEED_LOW:    m_UsbSpeed = 1; break;
    case LIBUSB_SPEED_FULL:   m_UsbSpeed = 2; break;
    case LIBUSB_SPEED_HIGH:   m_UsbSpeed = 3; break;
    case LIBUSB_SPEED_SUPER:  m_UsbSpeed = 4; break;
    default:                  m_UsbSpeed = 2; break;
    }

    if (!readDescriptors() || !claimAllInterfaces()) {
        libusb_close(m_DeviceHandle);
        m_DeviceHandle = nullptr;
        return false;
    }

    startEventThread();
    return true;
}

void UsbIpExporter::closeDevice()
{
    stopEventThread();

    // Cancel all pending transfers
    {
        QMutexLocker lock(&m_TransfersMutex);
        for (auto* xfer : m_PendingTransfers) {
            libusb_cancel_transfer(xfer);
        }
    }

    // Process remaining events briefly to let cancellations complete
    if (s_LibusbCtx && m_DeviceHandle) {
        struct timeval tv = { 0, 100000 }; // 100ms
        libusb_handle_events_timeout(s_LibusbCtx, &tv);
    }

    // Free any remaining transfers
    {
        QMutexLocker lock(&m_TransfersMutex);
        for (auto* xfer : m_PendingTransfers) {
            libusb_free_transfer(xfer);
        }
        m_PendingTransfers.clear();
    }

    releaseAllInterfaces();

    if (m_DeviceHandle) {
        libusb_close(m_DeviceHandle);
        m_DeviceHandle = nullptr;
    }

    m_DeviceDescriptor.clear();
    m_ConfigDescriptor.clear();
    m_UsbSpeed = 0;

    qInfo() << "UsbIpExporter: device closed, id:" << m_DeviceId;
}

// ─── USB Descriptors ───

bool UsbIpExporter::readDescriptors()
{
    libusb_device* dev = libusb_get_device(m_DeviceHandle);

    // Read device descriptor (18 bytes)
    struct libusb_device_descriptor devDesc;
    if (libusb_get_device_descriptor(dev, &devDesc) != 0) {
        qWarning() << "UsbIpExporter: failed to read device descriptor";
        return false;
    }
    m_DeviceDescriptor = QByteArray(reinterpret_cast<const char*>(&devDesc), sizeof(devDesc));

    // Read full configuration descriptor
    struct libusb_config_descriptor* confDesc = nullptr;
    if (libusb_get_active_config_descriptor(dev, &confDesc) != 0) {
        // Try config 0 if no active config
        if (libusb_get_config_descriptor(dev, 0, &confDesc) != 0) {
            qWarning() << "UsbIpExporter: failed to read config descriptor";
            return false;
        }
    }

    // The raw descriptor includes the full configuration with all interfaces/endpoints
    m_ConfigDescriptor = QByteArray(
        reinterpret_cast<const char*>(confDesc->extra),
        confDesc->extra_length);

    // Actually, we need the complete raw descriptor. Let's read it manually
    // via control transfer for the full config descriptor
    uint16_t totalLen = confDesc->wTotalLength;
    m_NumInterfaces = confDesc->bNumInterfaces;
    libusb_free_config_descriptor(confDesc);

    // Read the raw configuration descriptor (complete, including all interfaces/endpoints)
    QByteArray rawConf(totalLen, 0);
    int rc = libusb_control_transfer(m_DeviceHandle,
        LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_STANDARD | LIBUSB_RECIPIENT_DEVICE,
        LIBUSB_REQUEST_GET_DESCRIPTOR,
        (LIBUSB_DT_CONFIG << 8) | 0,   // Config index 0
        0,                               // Language
        reinterpret_cast<unsigned char*>(rawConf.data()),
        totalLen,
        5000);                           // 5s timeout

    if (rc < 0) {
        qWarning() << "UsbIpExporter: failed to read raw config descriptor:" << libusb_strerror(static_cast<libusb_error>(rc));
        return false;
    }

    m_ConfigDescriptor = rawConf.left(rc);

    qInfo() << "UsbIpExporter: descriptors read — device:" << m_DeviceDescriptor.size()
            << "bytes, config:" << m_ConfigDescriptor.size()
            << "bytes, interfaces:" << m_NumInterfaces;
    return true;
}

// ─── Interface management ───

bool UsbIpExporter::claimAllInterfaces()
{
    m_ClaimedInterfaces.clear();

    for (int i = 0; i < m_NumInterfaces; i++) {
        // Detach any kernel driver first
        if (libusb_kernel_driver_active(m_DeviceHandle, i) == 1) {
            int rc = libusb_detach_kernel_driver(m_DeviceHandle, i);
            if (rc != 0 && rc != LIBUSB_ERROR_NOT_SUPPORTED) {
                qWarning() << "UsbIpExporter: failed to detach kernel driver on interface" << i
                           << ":" << libusb_strerror(static_cast<libusb_error>(rc));
                // Continue anyway — on Windows, LIBUSB_ERROR_NOT_SUPPORTED is normal
            }
        }

        int rc = libusb_claim_interface(m_DeviceHandle, i);
        if (rc != 0) {
            qWarning() << "UsbIpExporter: failed to claim interface" << i
                       << ":" << libusb_strerror(static_cast<libusb_error>(rc));
            // On Windows with WinUSB, we may only be able to claim interface 0
            // Continue and try remaining interfaces
            continue;
        }
        m_ClaimedInterfaces.append(i);
    }

    if (m_ClaimedInterfaces.isEmpty() && m_NumInterfaces > 0) {
        qCritical() << "UsbIpExporter: failed to claim any interfaces";
        return false;
    }

    qInfo() << "UsbIpExporter: claimed" << m_ClaimedInterfaces.size()
            << "of" << m_NumInterfaces << "interfaces";
    return true;
}

void UsbIpExporter::releaseAllInterfaces()
{
    if (!m_DeviceHandle) return;

    for (int i : m_ClaimedInterfaces) {
        libusb_release_interface(m_DeviceHandle, i);
        // Re-attach kernel driver so the device comes back on the client
        libusb_attach_kernel_driver(m_DeviceHandle, i);
    }
    m_ClaimedInterfaces.clear();
}

// ─── URB handling ───

// Context stored as user_data in libusb_transfer
struct UrbContext {
    UsbIpExporter* exporter;
    uint32_t seqNum;
    uint32_t deviceId;
    uint8_t endpoint;
    uint8_t direction;
    uint8_t transferType;
};

void UsbIpExporter::submitUrb(const MlptProtocol::UsbIpHeader& header, const QByteArray& data)
{
    if (!m_DeviceHandle) {
        // Send error response
        MlptProtocol::UsbIpHeader resp = header;
        resp.status = -1; // ENODEV
        resp.dataLen = 0;
        emit urbCompleted(m_DeviceId, resp, QByteArray());
        return;
    }

    libusb_transfer* xfer = libusb_alloc_transfer(
        header.transferType == MlptProtocol::USB_XFER_ISOCHRONOUS ? header.numIsoPackets : 0);
    if (!xfer) {
        MlptProtocol::UsbIpHeader resp = header;
        resp.status = -12; // ENOMEM
        resp.dataLen = 0;
        emit urbCompleted(m_DeviceId, resp, QByteArray());
        return;
    }

    auto* ctx = new UrbContext();
    ctx->exporter = this;
    ctx->seqNum = header.seqNum;
    ctx->deviceId = header.deviceId;
    ctx->endpoint = header.endpoint;
    ctx->direction = header.direction;
    ctx->transferType = header.transferType;

    uint8_t ep = header.endpoint;
    if (header.direction == MlptProtocol::USB_DIR_IN) {
        ep |= LIBUSB_ENDPOINT_IN;
    }

    switch (header.transferType) {
    case MlptProtocol::USB_XFER_CONTROL: {
        // Control transfer: setup packet is in header.setupPacket
        // Data follows for OUT, buffer allocated for IN
        int totalLen = LIBUSB_CONTROL_SETUP_SIZE + header.dataLen;
        unsigned char* buf = static_cast<unsigned char*>(malloc(totalLen));

        // Copy setup packet
        memcpy(buf, header.setupPacket, 8);
        // For OUT transfers, copy data after setup packet
        if (header.direction == MlptProtocol::USB_DIR_OUT && !data.isEmpty()) {
            memcpy(buf + LIBUSB_CONTROL_SETUP_SIZE, data.constData(), data.size());
        }

        libusb_fill_control_transfer(xfer, m_DeviceHandle, buf,
            transferCallback, ctx, 5000);
        break;
    }

    case MlptProtocol::USB_XFER_BULK: {
        int bufLen = header.dataLen;
        unsigned char* buf = static_cast<unsigned char*>(malloc(bufLen > 0 ? bufLen : 1));
        if (header.direction == MlptProtocol::USB_DIR_OUT && !data.isEmpty()) {
            memcpy(buf, data.constData(), qMin(static_cast<uint32_t>(data.size()), header.dataLen));
        }

        libusb_fill_bulk_transfer(xfer, m_DeviceHandle, ep, buf, bufLen,
            transferCallback, ctx, 30000);
        break;
    }

    case MlptProtocol::USB_XFER_INTERRUPT: {
        int bufLen = header.dataLen;
        unsigned char* buf = static_cast<unsigned char*>(malloc(bufLen > 0 ? bufLen : 1));
        if (header.direction == MlptProtocol::USB_DIR_OUT && !data.isEmpty()) {
            memcpy(buf, data.constData(), qMin(static_cast<uint32_t>(data.size()), header.dataLen));
        }

        libusb_fill_interrupt_transfer(xfer, m_DeviceHandle, ep, buf, bufLen,
            transferCallback, ctx, 0);
        break;
    }

    case MlptProtocol::USB_XFER_ISOCHRONOUS: {
        int bufLen = header.dataLen;
        unsigned char* buf = static_cast<unsigned char*>(malloc(bufLen > 0 ? bufLen : 1));
        if (header.direction == MlptProtocol::USB_DIR_OUT && !data.isEmpty()) {
            memcpy(buf, data.constData(), qMin(static_cast<uint32_t>(data.size()), header.dataLen));
        }

        libusb_fill_iso_transfer(xfer, m_DeviceHandle, ep, buf, bufLen,
            header.numIsoPackets, transferCallback, ctx, 5000);

        // Set ISO packet lengths
        for (uint32_t i = 0; i < header.numIsoPackets && i < static_cast<uint32_t>(xfer->num_iso_packets); i++) {
            xfer->iso_packet_desc[i].length = bufLen / header.numIsoPackets;
        }
        break;
    }

    default:
        delete ctx;
        libusb_free_transfer(xfer);
        MlptProtocol::UsbIpHeader resp = header;
        resp.status = -22; // EINVAL
        resp.dataLen = 0;
        emit urbCompleted(m_DeviceId, resp, QByteArray());
        return;
    }

    // Track the transfer
    {
        QMutexLocker lock(&m_TransfersMutex);
        m_PendingTransfers.insert(header.seqNum, xfer);
    }

    int rc = libusb_submit_transfer(xfer);
    if (rc != 0) {
        {
            QMutexLocker lock(&m_TransfersMutex);
            m_PendingTransfers.remove(header.seqNum);
        }

        qWarning() << "UsbIpExporter: submit failed:" << libusb_strerror(static_cast<libusb_error>(rc));

        if (xfer->buffer) free(xfer->buffer);
        delete ctx;
        libusb_free_transfer(xfer);

        MlptProtocol::UsbIpHeader resp = header;
        resp.status = rc;
        resp.dataLen = 0;
        emit urbCompleted(m_DeviceId, resp, QByteArray());
    }
}

void UsbIpExporter::unlinkUrb(uint32_t seqNum)
{
    QMutexLocker lock(&m_TransfersMutex);
    auto it = m_PendingTransfers.find(seqNum);
    if (it != m_PendingTransfers.end()) {
        libusb_cancel_transfer(*it);
    }
}

// ─── libusb async callback ───

void MLPT_LIBUSB_CALL UsbIpExporter::transferCallback(libusb_transfer* transfer)
{
    auto* ctx = static_cast<UrbContext*>(transfer->user_data);
    if (ctx && ctx->exporter) {
        ctx->exporter->handleTransferComplete(transfer);
    }
}

void UsbIpExporter::handleTransferComplete(libusb_transfer* transfer)
{
    auto* ctx = static_cast<UrbContext*>(transfer->user_data);

    // Remove from pending list
    {
        QMutexLocker lock(&m_TransfersMutex);
        m_PendingTransfers.remove(ctx->seqNum);
    }

    // Build response header
    MlptProtocol::UsbIpHeader resp;
    memset(&resp, 0, sizeof(resp));
    resp.seqNum = ctx->seqNum;
    resp.deviceId = ctx->deviceId;
    resp.endpoint = ctx->endpoint;
    resp.direction = ctx->direction;
    resp.transferType = ctx->transferType;

    // Map libusb status to USB/IP status
    switch (transfer->status) {
    case LIBUSB_TRANSFER_COMPLETED:
        resp.status = 0;
        break;
    case LIBUSB_TRANSFER_CANCELLED:
        resp.status = -2; // ECONNRESET (UNLINK)
        break;
    case LIBUSB_TRANSFER_STALL:
        resp.status = -32; // EPIPE
        break;
    case LIBUSB_TRANSFER_NO_DEVICE:
        resp.status = -19; // ENODEV
        // Signal device disconnection
        QMetaObject::invokeMethod(this, [this]() {
            emit deviceDisconnected(m_DeviceId);
        }, Qt::QueuedConnection);
        break;
    case LIBUSB_TRANSFER_TIMED_OUT:
        resp.status = -110; // ETIMEDOUT
        break;
    case LIBUSB_TRANSFER_OVERFLOW:
        resp.status = -75; // EOVERFLOW
        break;
    default:
        resp.status = -5; // EIO
        break;
    }

    // Extract response data
    QByteArray responseData;
    if (transfer->status == LIBUSB_TRANSFER_COMPLETED &&
        ctx->direction == MlptProtocol::USB_DIR_IN) {

        if (ctx->transferType == MlptProtocol::USB_XFER_CONTROL) {
            // Control transfer: data starts after setup packet in buffer
            int dataOffset = LIBUSB_CONTROL_SETUP_SIZE;
            int dataLen = transfer->actual_length;
            if (dataLen > 0 && transfer->buffer) {
                responseData = QByteArray(
                    reinterpret_cast<const char*>(transfer->buffer + dataOffset),
                    dataLen);
            }
        } else {
            if (transfer->actual_length > 0 && transfer->buffer) {
                responseData = QByteArray(
                    reinterpret_cast<const char*>(transfer->buffer),
                    transfer->actual_length);
            }
        }
    }

    resp.dataLen = responseData.size();

    emit urbCompleted(m_DeviceId, resp, responseData);

    // Cleanup
    if (transfer->buffer) free(transfer->buffer);
    delete ctx;
    libusb_free_transfer(transfer);
}

// ─── Event handling thread ───

void UsbIpExporter::startEventThread()
{
    if (m_EventThread) return;

    m_EventThreadRunning = true;

    m_EventThread = QThread::create([this]() {
        qInfo() << "UsbIpExporter: event thread started for device" << m_DeviceId;

        while (m_EventThreadRunning.load()) {
            struct timeval tv = { 0, 250000 }; // 250ms poll interval
            int rc = libusb_handle_events_timeout(s_LibusbCtx, &tv);
            if (rc != 0 && rc != LIBUSB_ERROR_TIMEOUT) {
                qWarning() << "UsbIpExporter: event handling error:"
                           << libusb_strerror(static_cast<libusb_error>(rc));
                if (rc == LIBUSB_ERROR_NO_DEVICE) {
                    QMetaObject::invokeMethod(this, [this]() {
                        emit deviceDisconnected(m_DeviceId);
                    }, Qt::QueuedConnection);
                    break;
                }
            }
        }

        qInfo() << "UsbIpExporter: event thread stopped for device" << m_DeviceId;
    });

    m_EventThread->setObjectName(QString("usb-evt-%1").arg(m_DeviceId));
    m_EventThread->start(QThread::HighestPriority);
}

void UsbIpExporter::stopEventThread()
{
    if (!m_EventThread) return;

    m_EventThreadRunning = false;
    m_EventThread->wait(3000);
    if (m_EventThread->isRunning()) {
        m_EventThread->terminate();
        m_EventThread->wait(1000);
    }

    delete m_EventThread;
    m_EventThread = nullptr;
}
