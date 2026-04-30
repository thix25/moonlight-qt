#include "bthidcapture.h"

#include <QtDebug>

#ifdef Q_OS_WIN32
#include <SetupAPI.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <cfgmgr32.h>

#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")
#endif

BtHidCapture::BtHidCapture(QObject* parent)
    : QObject(parent)
    , m_DeviceId(0)
    , m_VendorId(0)
    , m_ProductId(0)
    , m_VersionNumber(0x0100)
#ifdef Q_OS_WIN32
    , m_HidHandle(INVALID_HANDLE_VALUE)
    , m_HidHandleInfo(INVALID_HANDLE_VALUE)
#endif
    , m_InputReportLength(0)
    , m_OutputReportLength(0)
    , m_FeatureReportLength(0)
    , m_UsagePage(0)
    , m_Usage(0)
    , m_NumInputReports(1)
    , m_ReadThread(nullptr)
    , m_ReadRunning(false)
    , m_CurrentConfig(0)
{
}

BtHidCapture::~BtHidCapture()
{
    closeDevice();
}

#ifdef Q_OS_WIN32

bool BtHidCapture::openDevice(const QString& btAddress, uint16_t vendorId, uint16_t productId)
{
    QString hidPath = findHidPathForBtDevice(btAddress);
    if (hidPath.isEmpty()) {
        qWarning() << "BtHidCapture: no HID device found for BT address" << btAddress;

        // Fallback: try to find by VID/PID if provided
        if (vendorId != 0 && productId != 0) {
            qInfo() << "BtHidCapture: trying VID:PID fallback" << vendorId << productId;
            // Enumerate all HID devices and find matching VID/PID
            GUID hidGuid;
            HidD_GetHidGuid(&hidGuid);

            HDEVINFO devInfo = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr,
                                                     DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
            if (devInfo == INVALID_HANDLE_VALUE) return false;

            SP_DEVICE_INTERFACE_DATA ifData = {};
            ifData.cbSize = sizeof(ifData);

            for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devInfo, nullptr, &hidGuid, i, &ifData); i++) {
                DWORD reqSize = 0;
                SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, nullptr, 0, &reqSize, nullptr);

                auto* detail = static_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(malloc(reqSize));
                if (!detail) continue;
                detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

                if (SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, detail, reqSize, nullptr, nullptr)) {
                    HANDLE hTest = CreateFileW(detail->DevicePath,
                        GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        nullptr, OPEN_EXISTING, 0, nullptr);

                    if (hTest != INVALID_HANDLE_VALUE) {
                        HIDD_ATTRIBUTES attrs = {};
                        attrs.Size = sizeof(attrs);
                        if (HidD_GetAttributes(hTest, &attrs)) {
                            if (attrs.VendorID == vendorId && attrs.ProductID == productId) {
                                // Check if this is a Bluetooth device by checking parent device
                                hidPath = QString::fromWCharArray(detail->DevicePath);
                                CloseHandle(hTest);
                                free(detail);
                                break;
                            }
                        }
                        CloseHandle(hTest);
                    }
                }
                free(detail);
            }
            SetupDiDestroyDeviceInfoList(devInfo);
        }

        if (hidPath.isEmpty()) return false;
    }

    return openDevicePath(hidPath);
}

bool BtHidCapture::openDevicePath(const QString& hidDevicePath)
{
    if (isOpen()) closeDevice();

    // Open for reading input reports (overlapped for async reads)
    m_HidHandle = CreateFileW(
        reinterpret_cast<LPCWSTR>(hidDevicePath.utf16()),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        nullptr);

    if (m_HidHandle == INVALID_HANDLE_VALUE) {
        // Try read-only (some devices don't allow write)
        m_HidHandle = CreateFileW(
            reinterpret_cast<LPCWSTR>(hidDevicePath.utf16()),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED,
            nullptr);

        if (m_HidHandle == INVALID_HANDLE_VALUE) {
            qWarning() << "BtHidCapture: failed to open" << hidDevicePath
                        << "error:" << GetLastError();
            return false;
        }
    }

    // Open a second handle for HidD info calls (non-overlapped)
    m_HidHandleInfo = CreateFileW(
        reinterpret_cast<LPCWSTR>(hidDevicePath.utf16()),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, 0, nullptr);

    if (m_HidHandleInfo == INVALID_HANDLE_VALUE) {
        qWarning() << "BtHidCapture: failed to open info handle for" << hidDevicePath
                    << "- continuing with overlapped handle only";
    }

    // Get attributes (VID/PID/version)
    HIDD_ATTRIBUTES attrs = {};
    attrs.Size = sizeof(attrs);
    if (HidD_GetAttributes(m_HidHandleInfo != INVALID_HANDLE_VALUE ? m_HidHandleInfo : m_HidHandle, &attrs)) {
        m_VendorId = attrs.VendorID;
        m_ProductId = attrs.ProductID;
        m_VersionNumber = attrs.VersionNumber;
    }

    // Get product name
    WCHAR productName[256] = {};
    if (HidD_GetProductString(m_HidHandleInfo != INVALID_HANDLE_VALUE ? m_HidHandleInfo : m_HidHandle,
                               productName, sizeof(productName))) {
        m_ProductName = QString::fromWCharArray(productName);
    }

    // Get preparsed data for capabilities
    PHIDP_PREPARSED_DATA preparsedData = nullptr;
    if (HidD_GetPreparsedData(m_HidHandleInfo != INVALID_HANDLE_VALUE ? m_HidHandleInfo : m_HidHandle,
                               &preparsedData)) {
        HIDP_CAPS caps = {};
        if (HidP_GetCaps(preparsedData, &caps) == HIDP_STATUS_SUCCESS) {
            m_UsagePage = caps.UsagePage;
            m_Usage = caps.Usage;
            m_InputReportLength = caps.InputReportByteLength;
            m_OutputReportLength = caps.OutputReportByteLength;
            m_FeatureReportLength = caps.FeatureReportByteLength;
            m_NumInputReports = static_cast<uint8_t>(
                caps.NumberInputButtonCaps + caps.NumberInputValueCaps > 0 ? 1 : 0);
        }
        HidD_FreePreparsedData(preparsedData);
    }

    // Get the HID report descriptor
    // Windows doesn't expose the raw HID report descriptor directly.
    // We'll reconstruct a minimal one from the capabilities.
    // For a real implementation, we'd use IOCTL_HID_GET_REPORT_DESCRIPTOR (Win10 1903+)
    // For now, try the IOCTL first, fall back to a generic descriptor.
    {
        // Try IOCTL_HID_GET_REPORT_DESCRIPTOR (available on Win10 1903+)
        // IOCTL code: 0xB0191
        static constexpr DWORD IOCTL_HID_GET_REPORT_DESCRIPTOR = 0x000B0191;

        QByteArray descBuf(4096, 0);
        DWORD bytesReturned = 0;
        HANDLE hForIoctl = (m_HidHandleInfo != INVALID_HANDLE_VALUE) ? m_HidHandleInfo : m_HidHandle;

        if (DeviceIoControl(hForIoctl, IOCTL_HID_GET_REPORT_DESCRIPTOR,
                            nullptr, 0,
                            descBuf.data(), descBuf.size(),
                            &bytesReturned, nullptr) && bytesReturned > 0) {
            m_HidReportDescriptor = descBuf.left(bytesReturned);
            qInfo() << "BtHidCapture: got real HID report descriptor," << bytesReturned << "bytes";
        } else {
            // Fallback: build a minimal generic report descriptor
            // This won't be perfect for all devices but works for basic mice/keyboards
            QByteArray desc;
            desc.append(static_cast<char>(0x05)); desc.append(static_cast<char>(m_UsagePage & 0xFF)); // Usage Page
            if (m_UsagePage > 0xFF) {
                // Extended usage page
                desc.clear();
                desc.append(static_cast<char>(0x06));
                desc.append(static_cast<char>(m_UsagePage & 0xFF));
                desc.append(static_cast<char>((m_UsagePage >> 8) & 0xFF));
            }
            desc.append(static_cast<char>(0x09)); desc.append(static_cast<char>(m_Usage & 0xFF));     // Usage
            desc.append(static_cast<char>(0xA1)); desc.append(static_cast<char>(0x01));                 // Collection (Application)

            if (m_InputReportLength > 1) {
                uint16_t bits = (m_InputReportLength - 1) * 8; // Subtract report ID byte
                desc.append(static_cast<char>(0x19)); desc.append(static_cast<char>(0x00));             // Usage Minimum (0)
                desc.append(static_cast<char>(0x29)); desc.append(static_cast<char>(0xFF));             // Usage Maximum (255)
                desc.append(static_cast<char>(0x15)); desc.append(static_cast<char>(0x00));             // Logical Minimum (0)
                desc.append(static_cast<char>(0x26)); desc.append(static_cast<char>(0xFF)); desc.append(static_cast<char>(0x00)); // Logical Maximum (255)
                desc.append(static_cast<char>(0x75)); desc.append(static_cast<char>(0x08));             // Report Size (8)
                desc.append(static_cast<char>(0x95)); desc.append(static_cast<char>(bits / 8));         // Report Count
                desc.append(static_cast<char>(0x81)); desc.append(static_cast<char>(0x02));             // Input (Data, Variable, Absolute)
            }

            if (m_OutputReportLength > 1) {
                uint16_t bits = (m_OutputReportLength - 1) * 8;
                desc.append(static_cast<char>(0x19)); desc.append(static_cast<char>(0x00));
                desc.append(static_cast<char>(0x29)); desc.append(static_cast<char>(0xFF));
                desc.append(static_cast<char>(0x15)); desc.append(static_cast<char>(0x00));
                desc.append(static_cast<char>(0x26)); desc.append(static_cast<char>(0xFF)); desc.append(static_cast<char>(0x00));
                desc.append(static_cast<char>(0x75)); desc.append(static_cast<char>(0x08));
                desc.append(static_cast<char>(0x95)); desc.append(static_cast<char>(bits / 8));
                desc.append(static_cast<char>(0x91)); desc.append(static_cast<char>(0x02));             // Output (Data, Variable, Absolute)
            }

            desc.append(static_cast<char>(0xC0));                                                       // End Collection

            m_HidReportDescriptor = desc;
            qInfo() << "BtHidCapture: built generic HID report descriptor," << desc.size() << "bytes";
        }
    }

    buildUsbDescriptors();
    startReadThread();

    qInfo() << "BtHidCapture: opened" << m_ProductName
            << QString("(%1:%2)").arg(m_VendorId, 4, 16, QLatin1Char('0')).arg(m_ProductId, 4, 16, QLatin1Char('0'))
            << "input:" << m_InputReportLength << "output:" << m_OutputReportLength
            << "usage:" << QString("0x%1/0x%2").arg(m_UsagePage, 4, 16, QLatin1Char('0')).arg(m_Usage, 4, 16, QLatin1Char('0'));

    return true;
}

void BtHidCapture::closeDevice()
{
    stopReadThread();

    {
        QMutexLocker lock(&m_PendingMutex);
        // Send error responses for any pending interrupt URBs
        for (auto& pendingHdr : m_PendingInterruptIn) {
            sendUrbResponse(pendingHdr, -19); // ENODEV
        }
        m_PendingInterruptIn.clear();
    }

#ifdef Q_OS_WIN32
    if (m_HidHandleInfo != INVALID_HANDLE_VALUE) {
        CloseHandle(m_HidHandleInfo);
        m_HidHandleInfo = INVALID_HANDLE_VALUE;
    }
    if (m_HidHandle != INVALID_HANDLE_VALUE) {
        CancelIo(m_HidHandle);
        CloseHandle(m_HidHandle);
        m_HidHandle = INVALID_HANDLE_VALUE;
    }
#endif

    m_CurrentConfig = 0;
}

bool BtHidCapture::isOpen() const
{
#ifdef Q_OS_WIN32
    return m_HidHandle != INVALID_HANDLE_VALUE;
#else
    return false;
#endif
}

// ============================================================================
// USB Descriptor synthesis
// ============================================================================

void BtHidCapture::buildUsbDescriptors()
{
    // Build an 18-byte USB Device Descriptor
    m_DeviceDescriptor.resize(18);
    uint8_t* dd = reinterpret_cast<uint8_t*>(m_DeviceDescriptor.data());
    dd[0] = 18;           // bLength
    dd[1] = 0x01;         // bDescriptorType (DEVICE)
    dd[2] = 0x10; dd[3] = 0x01; // bcdUSB = 1.10
    dd[4] = 0x00;         // bDeviceClass (defined at interface level)
    dd[5] = 0x00;         // bDeviceSubClass
    dd[6] = 0x00;         // bDeviceProtocol
    dd[7] = 64;           // bMaxPacketSize0
    dd[8] = m_VendorId & 0xFF; dd[9] = (m_VendorId >> 8) & 0xFF;     // idVendor
    dd[10] = m_ProductId & 0xFF; dd[11] = (m_ProductId >> 8) & 0xFF;  // idProduct
    dd[12] = m_VersionNumber & 0xFF; dd[13] = (m_VersionNumber >> 8) & 0xFF; // bcdDevice
    dd[14] = 0;           // iManufacturer (no string)
    dd[15] = 0;           // iProduct (no string)
    dd[16] = 0;           // iSerialNumber (no string)
    dd[17] = 1;           // bNumConfigurations

    // Build Configuration Descriptor + Interface + HID + Endpoint(s)
    // HID descriptor is 9 bytes
    // Endpoint descriptor is 7 bytes each

    int numEndpoints = 1; // At least interrupt IN
    if (m_OutputReportLength > 0) numEndpoints = 2; // + interrupt OUT

    uint16_t hidDescLen = 9;
    uint16_t totalLen = 9 + 9 + hidDescLen + (7 * numEndpoints); // Config + Interface + HID + Endpoints

    m_ConfigDescriptor.resize(totalLen);
    uint8_t* cd = reinterpret_cast<uint8_t*>(m_ConfigDescriptor.data());
    int offset = 0;

    // Configuration Descriptor (9 bytes)
    cd[offset + 0] = 9;           // bLength
    cd[offset + 1] = 0x02;        // bDescriptorType (CONFIGURATION)
    cd[offset + 2] = totalLen & 0xFF; cd[offset + 3] = (totalLen >> 8) & 0xFF; // wTotalLength
    cd[offset + 4] = 1;           // bNumInterfaces
    cd[offset + 5] = 1;           // bConfigurationValue
    cd[offset + 6] = 0;           // iConfiguration
    cd[offset + 7] = 0x80;        // bmAttributes (bus powered)
    cd[offset + 8] = 50;          // bMaxPower (100mA)
    offset += 9;

    // Interface Descriptor (9 bytes)
    cd[offset + 0] = 9;           // bLength
    cd[offset + 1] = 0x04;        // bDescriptorType (INTERFACE)
    cd[offset + 2] = 0;           // bInterfaceNumber
    cd[offset + 3] = 0;           // bAlternateSetting
    cd[offset + 4] = numEndpoints;// bNumEndpoints
    cd[offset + 5] = 0x03;        // bInterfaceClass (HID)
    // Subclass: 1 = Boot Interface
    cd[offset + 6] = (m_UsagePage == 0x01 && (m_Usage == 0x06 || m_Usage == 0x02)) ? 0x01 : 0x00;
    // Protocol: 1=keyboard, 2=mouse
    if (m_UsagePage == 0x01 && m_Usage == 0x06)
        cd[offset + 7] = 0x01; // Keyboard
    else if (m_UsagePage == 0x01 && m_Usage == 0x02)
        cd[offset + 7] = 0x02; // Mouse
    else
        cd[offset + 7] = 0x00; // None
    cd[offset + 8] = 0;           // iInterface
    offset += 9;

    // HID Descriptor (9 bytes)
    cd[offset + 0] = 9;           // bLength
    cd[offset + 1] = 0x21;        // bDescriptorType (HID)
    cd[offset + 2] = 0x11; cd[offset + 3] = 0x01; // bcdHID = 1.11
    cd[offset + 4] = 0;           // bCountryCode
    cd[offset + 5] = 1;           // bNumDescriptors
    cd[offset + 6] = 0x22;        // bDescriptorType (Report)
    uint16_t rdLen = static_cast<uint16_t>(m_HidReportDescriptor.size());
    cd[offset + 7] = rdLen & 0xFF; cd[offset + 8] = (rdLen >> 8) & 0xFF; // wDescriptorLength
    offset += 9;

    // Endpoint Descriptor - Interrupt IN (7 bytes)
    cd[offset + 0] = 7;           // bLength
    cd[offset + 1] = 0x05;        // bDescriptorType (ENDPOINT)
    cd[offset + 2] = 0x81;        // bEndpointAddress (EP1 IN)
    cd[offset + 3] = 0x03;        // bmAttributes (Interrupt)
    uint16_t maxPkt = m_InputReportLength > 0 ? m_InputReportLength : 8;
    cd[offset + 4] = maxPkt & 0xFF; cd[offset + 5] = (maxPkt >> 8) & 0xFF; // wMaxPacketSize
    cd[offset + 6] = 10;          // bInterval (10ms)
    offset += 7;

    // Endpoint Descriptor - Interrupt OUT (7 bytes) — optional
    if (m_OutputReportLength > 0) {
        cd[offset + 0] = 7;
        cd[offset + 1] = 0x05;
        cd[offset + 2] = 0x02;    // bEndpointAddress (EP2 OUT)
        cd[offset + 3] = 0x03;    // bmAttributes (Interrupt)
        uint16_t outPkt = m_OutputReportLength;
        cd[offset + 4] = outPkt & 0xFF; cd[offset + 5] = (outPkt >> 8) & 0xFF;
        cd[offset + 6] = 10;
        offset += 7;
    }
}

// ============================================================================
// URB handling
// ============================================================================

void BtHidCapture::submitUrb(const MlptProtocol::UsbIpHeader& header, const QByteArray& data)
{
    if (!isOpen()) {
        sendUrbResponse(header, -19); // ENODEV
        return;
    }

    switch (header.transferType) {
    case MlptProtocol::USB_XFER_CONTROL:
        handleControlUrb(header, data);
        break;
    case MlptProtocol::USB_XFER_INTERRUPT:
        if (header.direction == MlptProtocol::USB_DIR_IN) {
            handleInterruptInUrb(header);
        } else {
            handleInterruptOutUrb(header, data);
        }
        break;
    default:
        // Unsupported transfer type for HID
        sendUrbResponse(header, -32); // EPIPE
        break;
    }
}

void BtHidCapture::unlinkUrb(uint32_t seqNum)
{
    QMutexLocker lock(&m_PendingMutex);

    for (int i = 0; i < m_PendingInterruptIn.size(); i++) {
        if (m_PendingInterruptIn[i].seqNum == seqNum) {
            MlptProtocol::UsbIpHeader hdr = m_PendingInterruptIn.takeAt(i);
            sendUrbResponse(hdr, -104); // ECONNRESET (unlinked)
            return;
        }
    }
}

void BtHidCapture::handleControlUrb(const MlptProtocol::UsbIpHeader& header,
                                     const QByteArray& data)
{
    // Parse setup packet
    const uint8_t* setup = header.setupPacket;
    uint8_t bmRequestType = setup[0];
    uint8_t bRequest = setup[1];
    uint16_t wValue = setup[2] | (setup[3] << 8);
    uint16_t wIndex = setup[4] | (setup[5] << 8);
    uint16_t wLength = setup[6] | (setup[7] << 8);

    (void)wIndex;

    // GET_DESCRIPTOR
    if (bmRequestType == 0x80 && bRequest == 0x06) {
        uint8_t descType = (wValue >> 8) & 0xFF;
        uint8_t descIdx = wValue & 0xFF;
        (void)descIdx;

        switch (descType) {
        case 0x01: // DEVICE
            sendUrbResponse(header, 0, m_DeviceDescriptor.left(wLength));
            return;
        case 0x02: // CONFIGURATION
            sendUrbResponse(header, 0, m_ConfigDescriptor.left(wLength));
            return;
        case 0x03: // STRING — return empty
            {
                QByteArray strDesc(4, 0);
                strDesc[0] = 4;   // bLength
                strDesc[1] = 0x03; // bDescriptorType
                strDesc[2] = 0x09; strDesc[3] = 0x04; // English (US)
                sendUrbResponse(header, 0, strDesc.left(wLength));
            }
            return;
        }
    }

    // GET_DESCRIPTOR (Interface/HID class)
    if (bmRequestType == 0x81 && bRequest == 0x06) {
        uint8_t descType = (wValue >> 8) & 0xFF;

        switch (descType) {
        case 0x21: // HID descriptor
            {
                // Extract HID descriptor from config descriptor (offset: 9+9 = 18)
                int hidOffset = 9 + 9; // Config + Interface
                if (hidOffset + 9 <= m_ConfigDescriptor.size()) {
                    sendUrbResponse(header, 0, m_ConfigDescriptor.mid(hidOffset, 9).left(wLength));
                } else {
                    sendUrbResponse(header, -32);
                }
            }
            return;
        case 0x22: // Report descriptor
            sendUrbResponse(header, 0, m_HidReportDescriptor.left(wLength));
            return;
        }
    }

    // SET_CONFIGURATION
    if (bmRequestType == 0x00 && bRequest == 0x09) {
        m_CurrentConfig = wValue & 0xFF;
        sendUrbResponse(header, 0);
        return;
    }

    // GET_CONFIGURATION
    if (bmRequestType == 0x80 && bRequest == 0x08) {
        QByteArray cfg(1, static_cast<char>(m_CurrentConfig));
        sendUrbResponse(header, 0, cfg);
        return;
    }

    // SET_INTERFACE
    if (bmRequestType == 0x01 && bRequest == 0x0B) {
        sendUrbResponse(header, 0);
        return;
    }

    // SET_IDLE (HID class request)
    if (bmRequestType == 0x21 && bRequest == 0x0A) {
        sendUrbResponse(header, 0);
        return;
    }

    // SET_PROTOCOL (HID class request)
    if (bmRequestType == 0x21 && bRequest == 0x0B) {
        sendUrbResponse(header, 0);
        return;
    }

    // GET_REPORT (HID class request)
    if (bmRequestType == 0xA1 && bRequest == 0x01) {
#ifdef Q_OS_WIN32
        uint8_t reportType = (wValue >> 8) & 0xFF;
        uint8_t reportId = wValue & 0xFF;

        HANDLE h = (m_HidHandleInfo != INVALID_HANDLE_VALUE) ? m_HidHandleInfo : m_HidHandle;

        if (reportType == 0x03) {
            // Feature report
            QByteArray buf(m_FeatureReportLength, 0);
            buf[0] = static_cast<char>(reportId);
            if (HidD_GetFeature(h, buf.data(), buf.size())) {
                sendUrbResponse(header, 0, buf.left(wLength));
            } else {
                sendUrbResponse(header, -32); // EPIPE
            }
        } else if (reportType == 0x01) {
            // Input report
            QByteArray buf(m_InputReportLength, 0);
            buf[0] = static_cast<char>(reportId);
            if (HidD_GetInputReport(h, buf.data(), buf.size())) {
                sendUrbResponse(header, 0, buf.left(wLength));
            } else {
                sendUrbResponse(header, -32);
            }
        } else {
            sendUrbResponse(header, -32);
        }
#else
        sendUrbResponse(header, -32);
#endif
        return;
    }

    // SET_REPORT (HID class request)
    if (bmRequestType == 0x21 && bRequest == 0x09) {
#ifdef Q_OS_WIN32
        uint8_t reportType = (wValue >> 8) & 0xFF;

        HANDLE h = (m_HidHandleInfo != INVALID_HANDLE_VALUE) ? m_HidHandleInfo : m_HidHandle;

        if (reportType == 0x03 && data.size() > 0) {
            // Feature report
            QByteArray buf = data;
            if (static_cast<int>(m_FeatureReportLength) > buf.size()) {
                buf.resize(m_FeatureReportLength);
            }
            HidD_SetFeature(h, buf.data(), buf.size());
            sendUrbResponse(header, 0);
        } else if (reportType == 0x02 && data.size() > 0) {
            // Output report
            QByteArray buf = data;
            if (static_cast<int>(m_OutputReportLength) > buf.size()) {
                buf.resize(m_OutputReportLength);
            }
            HidD_SetOutputReport(h, buf.data(), buf.size());
            sendUrbResponse(header, 0);
        } else {
            sendUrbResponse(header, 0);
        }
#else
        sendUrbResponse(header, -32);
#endif
        return;
    }

    // CLEAR_FEATURE / SET_FEATURE on endpoint — just ACK
    if ((bmRequestType == 0x02 || bmRequestType == 0x00) &&
        (bRequest == 0x01 || bRequest == 0x03)) {
        sendUrbResponse(header, 0);
        return;
    }

    // GET_STATUS
    if (bmRequestType == 0x80 && bRequest == 0x00) {
        QByteArray status(2, 0);
        sendUrbResponse(header, 0, status);
        return;
    }

    // Unknown control request — STALL
    qDebug() << "BtHidCapture: unsupported control:" << QString("0x%1").arg(bmRequestType, 2, 16, QLatin1Char('0'))
             << QString("0x%1").arg(bRequest, 2, 16, QLatin1Char('0'))
             << "wValue:" << wValue << "wIndex:" << wIndex;
    sendUrbResponse(header, -32); // EPIPE (stall)
}

void BtHidCapture::handleInterruptInUrb(const MlptProtocol::UsbIpHeader& header)
{
    // Queue the URB — it will be completed when the read thread gets an input report
    QMutexLocker lock(&m_PendingMutex);
    m_PendingInterruptIn.append(header);
}

void BtHidCapture::handleInterruptOutUrb(const MlptProtocol::UsbIpHeader& header,
                                          const QByteArray& data)
{
#ifdef Q_OS_WIN32
    if (data.size() > 0 && m_HidHandle != INVALID_HANDLE_VALUE) {
        // Write output report (e.g., keyboard LEDs)
        QByteArray buf = data;
        if (static_cast<int>(m_OutputReportLength) > buf.size()) {
            buf.resize(m_OutputReportLength);
        }

        OVERLAPPED overlapped = {};
        overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

        DWORD bytesWritten = 0;
        BOOL ok = WriteFile(m_HidHandle, buf.data(), buf.size(), &bytesWritten, &overlapped);

        if (!ok && GetLastError() == ERROR_IO_PENDING) {
            WaitForSingleObject(overlapped.hEvent, 1000);
            GetOverlappedResult(m_HidHandle, &overlapped, &bytesWritten, FALSE);
        }

        CloseHandle(overlapped.hEvent);
    }
#endif

    sendUrbResponse(header, 0);
}

void BtHidCapture::sendUrbResponse(const MlptProtocol::UsbIpHeader& request,
                                    int32_t status, const QByteArray& responseData)
{
    MlptProtocol::UsbIpHeader resp = request;
    resp.status = status;
    resp.dataLen = static_cast<uint32_t>(responseData.size());

    emit urbCompleted(m_DeviceId, resp, responseData);
}

// ============================================================================
// Read thread — reads HID input reports and completes pending interrupt URBs
// ============================================================================

void BtHidCapture::startReadThread()
{
    if (m_ReadThread) return;

    m_ReadRunning = true;
    m_ReadThread = QThread::create([this]() { readLoop(); });
    m_ReadThread->setObjectName("BtHidRead");
    m_ReadThread->start(QThread::HighestPriority);
}

void BtHidCapture::stopReadThread()
{
    m_ReadRunning = false;

#ifdef Q_OS_WIN32
    if (m_HidHandle != INVALID_HANDLE_VALUE) {
        CancelIo(m_HidHandle);
    }
#endif

    if (m_ReadThread) {
        m_ReadThread->wait(3000);
        delete m_ReadThread;
        m_ReadThread = nullptr;
    }
}

void BtHidCapture::readLoop()
{
#ifdef Q_OS_WIN32
    qInfo() << "BtHidCapture: read loop started for" << m_ProductName;

    QByteArray readBuf(m_InputReportLength > 0 ? m_InputReportLength : 64, 0);

    OVERLAPPED overlapped = {};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    while (m_ReadRunning && m_HidHandle != INVALID_HANDLE_VALUE) {
        ResetEvent(overlapped.hEvent);

        DWORD bytesRead = 0;
        BOOL ok = ReadFile(m_HidHandle, readBuf.data(), readBuf.size(), &bytesRead, &overlapped);

        if (!ok) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                // Wait for data with timeout so we can check m_ReadRunning
                DWORD waitResult = WaitForSingleObject(overlapped.hEvent, 500);
                if (waitResult == WAIT_OBJECT_0) {
                    if (!GetOverlappedResult(m_HidHandle, &overlapped, &bytesRead, FALSE)) {
                        DWORD resultErr = GetLastError();
                        if (resultErr == ERROR_DEVICE_NOT_CONNECTED || resultErr == ERROR_INVALID_HANDLE) {
                            qInfo() << "BtHidCapture: device disconnected";
                            m_ReadRunning = false;
                            emit deviceDisconnected(m_DeviceId);
                            break;
                        }
                        continue;
                    }
                } else if (waitResult == WAIT_TIMEOUT) {
                    CancelIo(m_HidHandle);
                    continue;
                } else {
                    break;
                }
            } else if (err == ERROR_DEVICE_NOT_CONNECTED || err == ERROR_INVALID_HANDLE) {
                qInfo() << "BtHidCapture: device disconnected";
                m_ReadRunning = false;
                emit deviceDisconnected(m_DeviceId);
                break;
            } else {
                qWarning() << "BtHidCapture: ReadFile error:" << err;
                continue;
            }
        }

        if (bytesRead > 0) {
            QByteArray report = readBuf.left(bytesRead);

            // Complete a pending interrupt IN URB with this report data
            QMutexLocker lock(&m_PendingMutex);
            if (!m_PendingInterruptIn.isEmpty()) {
                MlptProtocol::UsbIpHeader pendingHdr = m_PendingInterruptIn.takeFirst();
                lock.unlock();
                sendUrbResponse(pendingHdr, 0, report);
            }
            // If no pending URB, the report is dropped (OS will re-request)
        }
    }

    CloseHandle(overlapped.hEvent);
    qInfo() << "BtHidCapture: read loop ended for" << m_ProductName;
#endif
}

// ============================================================================
// Find HID device path for a Bluetooth device
// ============================================================================

QString BtHidCapture::findHidPathForBtDevice(const QString& btAddress)
{
#ifdef Q_OS_WIN32
    // Strategy: enumerate HID devices, check their parent device tree for BT address
    // BT HID devices have instance paths like: HID\{...}&Dev_AABBCCDDEEFF\...
    // where AABBCCDDEEFF is the BT address without colons

    QString btAddrClean = btAddress.toUpper().remove(':');

    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);

    HDEVINFO devInfo = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr,
                                             DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE) return QString();

    SP_DEVICE_INTERFACE_DATA ifData = {};
    ifData.cbSize = sizeof(ifData);

    QString result;

    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devInfo, nullptr, &hidGuid, i, &ifData); i++) {
        DWORD reqSize = 0;
        SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, nullptr, 0, &reqSize, nullptr);

        auto* detail = static_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(malloc(reqSize));
        if (!detail) continue;
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        SP_DEVINFO_DATA devInfoData = {};
        devInfoData.cbSize = sizeof(devInfoData);

        if (SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, detail, reqSize, nullptr, &devInfoData)) {
            // Get instance ID and check for BT address
            WCHAR instanceId[MAX_DEVICE_ID_LEN];
            if (CM_Get_Device_IDW(devInfoData.DevInst, instanceId, MAX_DEVICE_ID_LEN, 0) == CR_SUCCESS) {
                QString instPath = QString::fromWCharArray(instanceId).toUpper();

                if (instPath.contains(btAddrClean)) {
                    result = QString::fromWCharArray(detail->DevicePath);
                    free(detail);
                    break;
                }
            }

            // Also check parent device instance ID
            DEVINST parentInst;
            if (CM_Get_Parent(&parentInst, devInfoData.DevInst, 0) == CR_SUCCESS) {
                WCHAR parentId[MAX_DEVICE_ID_LEN];
                if (CM_Get_Device_IDW(parentInst, parentId, MAX_DEVICE_ID_LEN, 0) == CR_SUCCESS) {
                    QString parentPath = QString::fromWCharArray(parentId).toUpper();
                    if (parentPath.contains(btAddrClean)) {
                        result = QString::fromWCharArray(detail->DevicePath);
                        free(detail);
                        break;
                    }
                }
            }
        }

        free(detail);
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    return result;
#else
    Q_UNUSED(btAddress);
    return QString();
#endif
}

#else
// Non-Windows stubs
bool BtHidCapture::openDevice(const QString&, uint16_t, uint16_t) { return false; }
bool BtHidCapture::openDevicePath(const QString&) { return false; }
void BtHidCapture::closeDevice() {}
bool BtHidCapture::isOpen() const { return false; }
void BtHidCapture::buildUsbDescriptors() {}
void BtHidCapture::submitUrb(const MlptProtocol::UsbIpHeader& h, const QByteArray&) { sendUrbResponse(h, -19); }
void BtHidCapture::unlinkUrb(uint32_t) {}
void BtHidCapture::handleControlUrb(const MlptProtocol::UsbIpHeader& h, const QByteArray&) { sendUrbResponse(h, -19); }
void BtHidCapture::handleInterruptInUrb(const MlptProtocol::UsbIpHeader& h) { sendUrbResponse(h, -19); }
void BtHidCapture::handleInterruptOutUrb(const MlptProtocol::UsbIpHeader& h, const QByteArray&) { sendUrbResponse(h, -19); }
void BtHidCapture::startReadThread() {}
void BtHidCapture::stopReadThread() {}
void BtHidCapture::readLoop() {}
QString BtHidCapture::findHidPathForBtDevice(const QString&) { return QString(); }
#endif
