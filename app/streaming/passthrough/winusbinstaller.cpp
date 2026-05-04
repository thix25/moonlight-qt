#include "winusbinstaller.h"

#ifdef Q_OS_WIN32

#include <QtDebug>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QCoreApplication>
#include <QThread>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h>   // ShellExecuteExW

// INSTALLFLAG values (from newdev.h — we avoid the WDK dependency)
static const DWORD MLPT_INSTALLFLAG_FORCE           = 0x00000001;
static const DWORD MLPT_INSTALLFLAG_NONINTERACTIVE  = 0x00000004;

// UpdateDriverForPlugAndPlayDevicesW function pointer type
typedef BOOL (WINAPI* PFN_UpdateDriver)(HWND, LPCWSTR, LPCWSTR, DWORD, PBOOL);

// Fixed device-interface GUID embedded in every Moonlight WinUSB INF.
// Applications that want to open the device by interface use this GUID;
// libusb does not, so the value doesn't need to match anything in particular.
static const char k_MoonlightDevIfaceGuid[] = "{6E45736A-2B1B-4078-B772-B3AF2B6FDDE8}";

// ---------------------------------------------------------------------------
// isRunningAsAdmin
// ---------------------------------------------------------------------------
bool WinUsbInstaller::isRunningAsAdmin()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;

    TOKEN_ELEVATION elevation = {};
    DWORD size = 0;
    bool elevated = GetTokenInformation(token, TokenElevation,
                                        &elevation, sizeof(elevation), &size)
                    && (elevation.TokenIsElevated != 0);
    CloseHandle(token);
    return elevated;
}

// ---------------------------------------------------------------------------
// generateInf
// Writes a WinUSB .inf to %TEMP%/Moonlight-WinUSB/VID_xxxx_PID_yyyy.inf
// and returns the absolute path, or an empty string on failure.
// ---------------------------------------------------------------------------
QString WinUsbInstaller::generateInf(uint16_t vid, uint16_t pid, const QString& deviceName)
{
    QString vidStr = QString("%1").arg(vid, 4, 16, QLatin1Char('0')).toUpper();
    QString pidStr = QString("%1").arg(pid, 4, 16, QLatin1Char('0')).toUpper();

    QString infDir = QDir::tempPath() + "/Moonlight-WinUSB";
    QDir().mkpath(infDir);

    QString infPath = QString("%1/VID_%2_PID_%3.inf").arg(infDir, vidStr, pidStr);

    QString hwId = QString("USB\\VID_%1&PID_%2").arg(vidStr, pidStr);

    // Sanitize device name for use inside an INF string value
    QString safeName = deviceName.isEmpty()
        ? QString("USB Device %1:%2").arg(vidStr, pidStr)
        : deviceName;
    safeName.replace('"', '\'');

    QFile f(infPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "WinUsbInstaller: cannot create INF file:" << infPath;
        return {};
    }

    QTextStream s(&f);
    // Windows INF files use CRLF
    s << "[Version]\r\n"
      << "Signature   = \"$Windows NT$\"\r\n"
      << "Class       = \"Universal Serial Bus devices\"\r\n"
      << "ClassGuid   = {88BAE032-5A81-49F0-BC3D-A4FF138216D6}\r\n"
      << "Provider    = %ManufacturerName%\r\n"
      << "DriverVer   = 05/04/2026,1.0.0.0\r\n"
      << "\r\n"
      << "[Manufacturer]\r\n"
      << "%ManufacturerName% = Standard,NTamd64,NTx86,NTarm64\r\n"
      << "\r\n"
      << "[Standard.NTamd64]\r\n"
      << "%DeviceName% = USB_Install," << hwId << "\r\n"
      << "\r\n"
      << "[Standard.NTx86]\r\n"
      << "%DeviceName% = USB_Install," << hwId << "\r\n"
      << "\r\n"
      << "[Standard.NTarm64]\r\n"
      << "%DeviceName% = USB_Install," << hwId << "\r\n"
      << "\r\n"
      << "[USB_Install]\r\n"
      << "Include = winusb.inf\r\n"
      << "Needs   = WINUSB.NT\r\n"
      << "\r\n"
      << "[USB_Install.Services]\r\n"
      << "Include = winusb.inf\r\n"
      << "Needs   = WINUSB.NT.Services\r\n"
      << "\r\n"
      << "[USB_Install.HW]\r\n"
      << "AddReg = Dev_AddReg\r\n"
      << "\r\n"
      << "[Dev_AddReg]\r\n"
      << "HKR,,DeviceInterfaceGUIDs,0x10000,\"" << k_MoonlightDevIfaceGuid << "\"\r\n"
      << "\r\n"
      << "[Strings]\r\n"
      << "ManufacturerName = \"Moonlight\"\r\n"
      << "DeviceName       = \"" << safeName << " (WinUSB)\"\r\n";

    f.close();
    qInfo() << "WinUsbInstaller: generated INF at" << infPath;
    return infPath;
}

// ---------------------------------------------------------------------------
// doInstall
// Loads newdev.dll at runtime (avoids WDK link dependency) and calls
// UpdateDriverForPlugAndPlayDevicesW to install the WinUSB driver for every
// device matching the given VID/PID hardware ID.  Must be called from an
// elevated process.
// ---------------------------------------------------------------------------
bool WinUsbInstaller::doInstall(uint16_t vid, uint16_t pid, const QString& infPath)
{
    HMODULE hNewDev = LoadLibraryW(L"newdev.dll");
    if (!hNewDev) {
        qWarning() << "WinUsbInstaller: LoadLibrary(newdev.dll) failed:" << GetLastError();
        return false;
    }

    auto fnUpdate = reinterpret_cast<PFN_UpdateDriver>(
        GetProcAddress(hNewDev, "UpdateDriverForPlugAndPlayDevicesW"));
    if (!fnUpdate) {
        qWarning() << "WinUsbInstaller: UpdateDriverForPlugAndPlayDevicesW not found in newdev.dll";
        FreeLibrary(hNewDev);
        return false;
    }

    QString vidStr = QString("%1").arg(vid, 4, 16, QLatin1Char('0')).toUpper();
    QString pidStr = QString("%1").arg(pid, 4, 16, QLatin1Char('0')).toUpper();
    QString hwId   = QString("USB\\VID_%1&PID_%2").arg(vidStr, pidStr);

    std::wstring hwIdW  = hwId.toStdWString();
    std::wstring infW   = infPath.toStdWString();

    BOOL reboot = FALSE;
    BOOL ok = fnUpdate(
        nullptr,
        hwIdW.c_str(),
        infW.c_str(),
        MLPT_INSTALLFLAG_FORCE | MLPT_INSTALLFLAG_NONINTERACTIVE,
        &reboot);

    DWORD err = GetLastError();
    FreeLibrary(hNewDev);

    if (ok) {
        qInfo() << "WinUsbInstaller: WinUSB installed for" << hwId
                << (reboot ? "(reboot required)" : "");
        return true;
    }

    qWarning() << "WinUsbInstaller: UpdateDriverForPlugAndPlayDevicesW failed for"
               << hwId << "error:" << err;
    return false;
}

// ---------------------------------------------------------------------------
// installForDevice  (public entry point)
// ---------------------------------------------------------------------------
QString WinUsbInstaller::installForDevice(uint16_t vid, uint16_t pid,
                                          const QString& instancePath,
                                          const QString& deviceName)
{
    Q_UNUSED(instancePath) // reserved for future reenumeration helpers

    QString infPath = generateInf(vid, pid, deviceName);
    if (infPath.isEmpty())
        return QStringLiteral("Failed to generate WinUSB INF file.");

    bool ok = false;

    if (isRunningAsAdmin()) {
        // Already elevated — install directly.
        ok = doInstall(vid, pid, infPath);
    } else {
        // Not elevated: spawn Moonlight.exe with --install-winusb args under "runas" verb.
        // The UAC prompt will appear; the sub-process does just the driver install and exits.
        QString exePath = QCoreApplication::applicationFilePath();

        QString vidStr = QString("%1").arg(vid, 4, 16, QLatin1Char('0')).toUpper();
        QString pidStr = QString("%1").arg(pid, 4, 16, QLatin1Char('0')).toUpper();
        QString nativeInf = QDir::toNativeSeparators(infPath);

        // Build parameter string: --install-winusb <vid> <pid> "<infPath>"
        // Quotes around infPath handle spaces in %TEMP% path.
        QString params = QString("--install-winusb %1 %2 \"%3\"")
            .arg(vidStr, pidStr, nativeInf);

        SHELLEXECUTEINFOW sei = {};
        sei.cbSize   = sizeof(sei);
        sei.fMask    = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
        sei.lpVerb   = L"runas";
        sei.lpFile   = reinterpret_cast<LPCWSTR>(exePath.utf16());
        sei.lpParameters = reinterpret_cast<LPCWSTR>(params.utf16());
        sei.nShow    = SW_HIDE;

        if (!ShellExecuteExW(&sei) || !sei.hProcess) {
            DWORD err = GetLastError();
            if (err == ERROR_CANCELLED)
                return QStringLiteral("WinUSB installation cancelled (UAC denied).");
            return QString("Failed to launch elevated installer (error %1).").arg(err);
        }

        // Wait up to 60 s for the elevated subprocess to complete
        DWORD waitResult = WaitForSingleObject(sei.hProcess, 60000);
        DWORD exitCode = 1;
        if (waitResult == WAIT_OBJECT_0)
            GetExitCodeProcess(sei.hProcess, &exitCode);
        CloseHandle(sei.hProcess);

        if (waitResult != WAIT_OBJECT_0)
            return QStringLiteral("WinUSB installer timed out.");

        ok = (exitCode == 0);
    }

    if (!ok)
        return QStringLiteral("Failed to install WinUSB driver. "
                              "Try running Moonlight as administrator.");

    // Give Windows time to re-enumerate the device with the new driver
    // before the caller retries libusb_open().
    QThread::sleep(2);

    return {}; // success
}

// ---------------------------------------------------------------------------
// elevatedInstallMain — called from main() in the elevated subprocess
// ---------------------------------------------------------------------------
int WinUsbInstaller::elevatedInstallMain(const QStringList& args)
{
    // Expected: args contains "--install-winusb <vid_hex> <pid_hex> <infPath>"
    int idx = args.indexOf("--install-winusb");
    if (idx < 0 || idx + 3 >= args.size()) {
        qWarning() << "WinUsbInstaller::elevatedInstallMain: bad arguments" << args;
        return 1;
    }

    bool ok1, ok2;
    uint16_t vid = static_cast<uint16_t>(args[idx + 1].toUInt(&ok1, 16));
    uint16_t pid = static_cast<uint16_t>(args[idx + 2].toUInt(&ok2, 16));
    QString infPath = args[idx + 3];

    if (!ok1 || !ok2) {
        qWarning() << "WinUsbInstaller::elevatedInstallMain: invalid VID/PID:"
                   << args[idx + 1] << args[idx + 2];
        return 1;
    }

    qInfo() << "WinUsbInstaller (elevated): installing WinUSB for"
            << QString("VID_%1_PID_%2")
               .arg(vid, 4, 16, QLatin1Char('0')).toUpper()
               .arg(pid, 4, 16, QLatin1Char('0')).toUpper()
            << "from INF" << infPath;

    return doInstall(vid, pid, infPath) ? 0 : 1;
}

#endif // Q_OS_WIN32
