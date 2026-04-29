// UsbIpExporter — Captures a USB device via libusb and handles USB/IP URB traffic
#pragma once

#include <QObject>
#include <QThread>
#include <QMutex>
#include <QHash>
#include <QByteArray>
#include <functional>
#include <atomic>

#include "protocol.h"

// Forward-declare libusb types (avoid pulling in libusb.h in header)
struct libusb_context;
struct libusb_device_handle;
struct libusb_transfer;

// LIBUSB_CALL is __stdcall on Windows, default on others
#ifdef _WIN32
#define MLPT_LIBUSB_CALL __stdcall
#else
#define MLPT_LIBUSB_CALL
#endif

class UsbIpExporter : public QObject
{
    Q_OBJECT

public:
    explicit UsbIpExporter(QObject* parent = nullptr);
    ~UsbIpExporter();

    // Initialize libusb context (call once)
    static bool initLibusb();
    static void shutdownLibusb();

    // Open a device by VID/PID and optional serial
    bool openDevice(uint16_t vendorId, uint16_t productId, const QString& serial = QString());

    // Open a device by bus/port (more specific)
    bool openDeviceByPath(uint8_t busNumber, uint8_t portNumber);

    // Close the device and release all interfaces
    void closeDevice();

    bool isOpen() const { return m_DeviceHandle != nullptr; }

    // Get USB descriptors for VHCI plugin on server
    QByteArray deviceDescriptor() const { return m_DeviceDescriptor; }
    QByteArray configDescriptor() const { return m_ConfigDescriptor; }
    uint8_t usbSpeed() const { return m_UsbSpeed; }

    // Handle a URB submit from the server (async — result comes via urbCompleted signal)
    void submitUrb(const MlptProtocol::UsbIpHeader& header, const QByteArray& data);

    // Cancel a pending URB
    void unlinkUrb(uint32_t seqNum);

    // Device ID for this exporter
    void setDeviceId(uint32_t id) { m_DeviceId = id; }
    uint32_t deviceId() const { return m_DeviceId; }

signals:
    // Emitted when a URB completes (submit result to send back to server)
    void urbCompleted(uint32_t deviceId, const MlptProtocol::UsbIpHeader& header, const QByteArray& data);

    // Emitted when the device is disconnected unexpectedly
    void deviceDisconnected(uint32_t deviceId);

private:
    // libusb async event handling thread
    void startEventThread();
    void stopEventThread();

    // Detach kernel drivers and claim all interfaces
    bool claimAllInterfaces();
    void releaseAllInterfaces();

    // Read USB descriptors from the device
    bool readDescriptors();

    // libusb transfer callback (static, delegates to instance)
    static void MLPT_LIBUSB_CALL transferCallback(libusb_transfer* transfer);
    void handleTransferComplete(libusb_transfer* transfer);

    static libusb_context* s_LibusbCtx;

    libusb_device_handle* m_DeviceHandle;
    uint32_t m_DeviceId;

    QByteArray m_DeviceDescriptor;   // 18-byte USB device descriptor
    QByteArray m_ConfigDescriptor;   // Full configuration descriptor
    uint8_t m_UsbSpeed;

    int m_NumInterfaces;
    QList<int> m_ClaimedInterfaces;

    // Pending async transfers: seqNum → libusb_transfer*
    QMutex m_TransfersMutex;
    QHash<uint32_t, libusb_transfer*> m_PendingTransfers;

    // Event handling thread
    QThread* m_EventThread;
    std::atomic<bool> m_EventThreadRunning;
};
