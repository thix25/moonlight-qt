// BtHidCapture — Captures a Bluetooth HID device via Windows HID API
// and presents it as a USB HID device for forwarding via USB/IP (VHCI).
//
// This is Phase 3 "Mode B": Individual BT device forwarding.
// The device appears as a USB HID device on the server (not Bluetooth).
#pragma once

#include <QObject>
#include <QThread>
#include <QMutex>
#include <QHash>
#include <QByteArray>
#include <atomic>

#include "protocol.h"

#ifdef Q_OS_WIN32
#include <Windows.h>
#endif

class BtHidCapture : public QObject
{
    Q_OBJECT

public:
    explicit BtHidCapture(QObject* parent = nullptr);
    ~BtHidCapture();

    // Open a BT HID device by its Bluetooth address (XX:XX:XX:XX:XX:XX)
    // Finds the corresponding HID device path via SetupAPI
    bool openDevice(const QString& btAddress, uint16_t vendorId = 0, uint16_t productId = 0);

    // Open by direct HID device path (\\?\HID#...)
    bool openDevicePath(const QString& hidDevicePath);

    void closeDevice();
    bool isOpen() const;

    // USB descriptors synthesized from HID device info
    QByteArray deviceDescriptor() const { return m_DeviceDescriptor; }
    QByteArray configDescriptor() const { return m_ConfigDescriptor; }
    QByteArray hidReportDescriptor() const { return m_HidReportDescriptor; }
    uint8_t usbSpeed() const { return 1; } // Full speed for HID

    // Handle URBs from the server (VHCI sends GET_DESCRIPTOR, SET_REPORT, etc.)
    void submitUrb(const MlptProtocol::UsbIpHeader& header, const QByteArray& data);
    void unlinkUrb(uint32_t seqNum);

    void setDeviceId(uint32_t id) { m_DeviceId = id; }
    uint32_t deviceId() const { return m_DeviceId; }

    uint16_t vendorId() const { return m_VendorId; }
    uint16_t productId() const { return m_ProductId; }

signals:
    void urbCompleted(uint32_t deviceId, const MlptProtocol::UsbIpHeader& header, const QByteArray& data);
    void deviceDisconnected(uint32_t deviceId);

private:
    void startReadThread();
    void stopReadThread();
    void readLoop();

    // Build USB descriptors that represent this HID device
    void buildUsbDescriptors();

    // Handle specific URB types
    void handleControlUrb(const MlptProtocol::UsbIpHeader& header, const QByteArray& data);
    void handleInterruptInUrb(const MlptProtocol::UsbIpHeader& header);
    void handleInterruptOutUrb(const MlptProtocol::UsbIpHeader& header, const QByteArray& data);

    // Send a URB completion response
    void sendUrbResponse(const MlptProtocol::UsbIpHeader& request,
                         int32_t status, const QByteArray& responseData = QByteArray());

    // Find HID device path for a Bluetooth device by BT address
    static QString findHidPathForBtDevice(const QString& btAddress);

    uint32_t m_DeviceId;
    uint16_t m_VendorId;
    uint16_t m_ProductId;
    uint16_t m_VersionNumber;
    QString  m_ProductName;

#ifdef Q_OS_WIN32
    HANDLE m_HidHandle;      // For read/write (overlapped)
    HANDLE m_HidHandleInfo;  // For HidD_ info calls (non-overlapped)
#endif

    // HID capabilities
    uint16_t m_InputReportLength;
    uint16_t m_OutputReportLength;
    uint16_t m_FeatureReportLength;
    uint16_t m_UsagePage;
    uint16_t m_Usage;
    uint8_t  m_NumInputReports;

    // Synthesized USB descriptors
    QByteArray m_DeviceDescriptor;
    QByteArray m_ConfigDescriptor;
    QByteArray m_HidReportDescriptor;

    // Read thread for input reports
    QThread* m_ReadThread;
    std::atomic<bool> m_ReadRunning;

    // Pending interrupt IN URBs waiting for input reports
    QMutex m_PendingMutex;
    QList<MlptProtocol::UsbIpHeader> m_PendingInterruptIn;

    // Current configuration state (set by SET_CONFIGURATION)
    uint8_t m_CurrentConfig;
};
