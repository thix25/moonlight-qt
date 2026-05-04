#pragma once

#ifdef Q_OS_WIN32

#include <QString>
#include <QStringList>
#include <cstdint>

// Automatic WinUSB driver installer.
//
// When libusb cannot open a USB device because a kernel driver (e.g. USBSTOR,
// bthusb, usbvideo) holds it exclusively, Moonlight calls installForDevice()
// which replicates what Zadig does: generates a WinUSB .inf and installs it
// via UpdateDriverForPlugAndPlayDevicesW.  If Moonlight is not running as
// administrator the function spawns an elevated subprocess (UAC prompt) that
// performs just the driver-install step, then returns.
class WinUsbInstaller
{
public:
    // Install WinUSB driver for the USB device with the given VID/PID.
    // Blocks until installation completes or fails (can take several seconds,
    // including a UAC prompt if not already elevated).
    // Returns empty string on success, or a user-facing error message on failure.
    static QString installForDevice(uint16_t vid, uint16_t pid,
                                    const QString& instancePath,
                                    const QString& deviceName);

    // Called by main() when --install-winusb args are detected (elevated subprocess).
    // args is the full QCoreApplication::arguments() list.
    // Returns 0 on success, non-zero on failure.
    static int elevatedInstallMain(const QStringList& args);

private:
    static bool isRunningAsAdmin();
    static QString generateInf(uint16_t vid, uint16_t pid, const QString& deviceName);
    static bool doInstall(uint16_t vid, uint16_t pid, const QString& infPath);
};

#endif // Q_OS_WIN32
