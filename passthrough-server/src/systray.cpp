#include "systray.h"

#include <cstdio>
#include <cstring>

#ifdef _WIN32

#define WM_TRAYICON (WM_USER + 1)
#define IDM_STATUS      1001
#define IDM_AUTOSTART   1002
#define IDM_EXIT        1003

static const wchar_t* WINDOW_CLASS = L"MoonlightPassthroughTray";
static const wchar_t* AUTOSTART_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* AUTOSTART_VALUE = L"MoonlightPassthrough";

// Store instance pointer for WndProc
static SystemTray* g_TrayInstance = nullptr;

SystemTray::SystemTray()
    : m_Hwnd(nullptr)
    , m_Menu(nullptr)
    , m_Running(false)
    , m_ClientCount(0)
    , m_DeviceCount(0)
{
    memset(&m_Nid, 0, sizeof(m_Nid));
}

SystemTray::~SystemTray()
{
    stop();
}

bool SystemTray::init(const std::string& tooltip)
{
    g_TrayInstance = this;

    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = WINDOW_CLASS;

    if (!RegisterClassExW(&wc)) {
        DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            printf("[Tray] RegisterClassEx failed: %lu\n", err);
            return false;
        }
    }

    // Create hidden message window
    m_Hwnd = CreateWindowExW(0, WINDOW_CLASS, L"Moonlight Passthrough",
                              0, 0, 0, 0, 0, HWND_MESSAGE,
                              nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!m_Hwnd) {
        printf("[Tray] CreateWindow failed: %lu\n", GetLastError());
        return false;
    }

    // Create tray icon
    m_Nid.cbSize = sizeof(m_Nid);
    m_Nid.hWnd = m_Hwnd;
    m_Nid.uID = 1;
    m_Nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    m_Nid.uCallbackMessage = WM_TRAYICON;
    m_Nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);

    // Set tooltip
    std::wstring wtooltip(tooltip.begin(), tooltip.end());
    wcsncpy_s(m_Nid.szTip, wtooltip.c_str(), _TRUNCATE);

    if (!Shell_NotifyIconW(NIM_ADD, &m_Nid)) {
        printf("[Tray] Shell_NotifyIcon failed: %lu\n", GetLastError());
        return false;
    }

    // Create context menu
    m_Menu = CreatePopupMenu();
    AppendMenuW(m_Menu, MF_STRING | MF_GRAYED, IDM_STATUS, L"Moonlight Passthrough Server");
    AppendMenuW(m_Menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m_Menu, MF_STRING | (isAutoStartEnabled() ? MF_CHECKED : 0),
                IDM_AUTOSTART, L"Start with Windows");
    AppendMenuW(m_Menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m_Menu, MF_STRING, IDM_EXIT, L"Exit");

    m_Status = "Ready";
    printf("[Tray] System tray icon created\n");
    return true;
}

void SystemTray::run()
{
    m_Running = true;
    MSG msg;

    while (m_Running && GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void SystemTray::stop()
{
    m_Running = false;

    if (m_Hwnd) {
        Shell_NotifyIconW(NIM_DELETE, &m_Nid);
        DestroyWindow(m_Hwnd);
        m_Hwnd = nullptr;
    }

    if (m_Menu) {
        DestroyMenu(m_Menu);
        m_Menu = nullptr;
    }

    g_TrayInstance = nullptr;
}

void SystemTray::requestStop()
{
    m_Running = false;
    if (m_Hwnd) {
        PostMessageW(m_Hwnd, WM_CLOSE, 0, 0);
    }
}

void SystemTray::setTooltip(const std::string& text)
{
    std::wstring wtext(text.begin(), text.end());
    wcsncpy_s(m_Nid.szTip, wtext.c_str(), _TRUNCATE);
    m_Nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &m_Nid);
}

void SystemTray::showBalloon(const std::string& title, const std::string& message)
{
    m_Nid.uFlags = NIF_INFO;
    m_Nid.dwInfoFlags = NIIF_INFO;

    std::wstring wtitle(title.begin(), title.end());
    std::wstring wmsg(message.begin(), message.end());
    wcsncpy_s(m_Nid.szInfoTitle, wtitle.c_str(), _TRUNCATE);
    wcsncpy_s(m_Nid.szInfo, wmsg.c_str(), _TRUNCATE);

    Shell_NotifyIconW(NIM_MODIFY, &m_Nid);
}

void SystemTray::setStatus(const std::string& status)
{
    m_Status = status;
    updateTooltip();
}

void SystemTray::setClientCount(int count)
{
    m_ClientCount = count;
    updateTooltip();
}

void SystemTray::setDeviceCount(int count)
{
    m_DeviceCount = count;
    updateTooltip();
}

void SystemTray::updateTooltip()
{
    char tip[128];
    snprintf(tip, sizeof(tip), "MLPT Server - %s\nClients: %d | Devices: %d",
             m_Status.c_str(), m_ClientCount, m_DeviceCount);
    setTooltip(tip);

    // Update status menu item
    if (m_Menu) {
        std::string statusStr = "Status: " + m_Status +
            " | Clients: " + std::to_string(m_ClientCount) +
            " | Devices: " + std::to_string(m_DeviceCount);
        std::wstring wstatus(statusStr.begin(), statusStr.end());
        ModifyMenuW(m_Menu, IDM_STATUS, MF_STRING | MF_GRAYED, IDM_STATUS, wstatus.c_str());
    }
}

LRESULT CALLBACK SystemTray::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_CLOSE) {
        // Clean shutdown requested (from requestStop or system)
        if (g_TrayInstance) {
            g_TrayInstance->stop();
        }
        PostQuitMessage(0);
        return 0;
    }

    if (msg == WM_TRAYICON) {
        if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
            if (g_TrayInstance) {
                g_TrayInstance->showContextMenu();
            }
        } else if (lParam == WM_LBUTTONDBLCLK) {
            // Double-click: show balloon with status
            if (g_TrayInstance) {
                g_TrayInstance->showBalloon("Moonlight Passthrough",
                    "Status: " + g_TrayInstance->m_Status +
                    "\nClients: " + std::to_string(g_TrayInstance->m_ClientCount) +
                    "\nDevices: " + std::to_string(g_TrayInstance->m_DeviceCount));
            }
        }
        return 0;
    }

    if (msg == WM_COMMAND) {
        switch (LOWORD(wParam)) {
        case IDM_AUTOSTART:
            if (g_TrayInstance) {
                g_TrayInstance->toggleAutoStart();
            }
            break;
        case IDM_EXIT:
            if (g_TrayInstance) {
                if (g_TrayInstance->m_ExitCallback) {
                    g_TrayInstance->m_ExitCallback();
                }
                // Post WM_CLOSE which will trigger stop() + PostQuitMessage
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
            }
            break;
        }
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void SystemTray::showContextMenu()
{
    POINT pt;
    GetCursorPos(&pt);

    // Update autostart check state
    CheckMenuItem(m_Menu, IDM_AUTOSTART,
                  isAutoStartEnabled() ? MF_CHECKED : MF_UNCHECKED);

    SetForegroundWindow(m_Hwnd);
    TrackPopupMenu(m_Menu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN,
                   pt.x, pt.y, 0, m_Hwnd, nullptr);
    PostMessageW(m_Hwnd, WM_NULL, 0, 0);
}

void SystemTray::toggleAutoStart()
{
    HKEY key;
    if (isAutoStartEnabled()) {
        // Remove auto-start
        if (RegOpenKeyExW(HKEY_CURRENT_USER, AUTOSTART_KEY, 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
            RegDeleteValueW(key, AUTOSTART_VALUE);
            RegCloseKey(key);
        }
        printf("[Tray] Auto-start disabled\n");
    } else {
        // Add auto-start
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);

        if (RegOpenKeyExW(HKEY_CURRENT_USER, AUTOSTART_KEY, 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
            RegSetValueExW(key, AUTOSTART_VALUE, 0, REG_SZ,
                           reinterpret_cast<const BYTE*>(exePath),
                           static_cast<DWORD>((wcslen(exePath) + 1) * sizeof(wchar_t)));
            RegCloseKey(key);
        }
        printf("[Tray] Auto-start enabled: %ls\n", exePath);
    }
}

bool SystemTray::isAutoStartEnabled() const
{
    HKEY key;
    bool exists = false;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, AUTOSTART_KEY, 0, KEY_READ, &key) == ERROR_SUCCESS) {
        exists = RegQueryValueExW(key, AUTOSTART_VALUE, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
        RegCloseKey(key);
    }

    return exists;
}

#else
// Non-Windows stubs
SystemTray::SystemTray() : m_Running(false), m_ClientCount(0), m_DeviceCount(0) {}
SystemTray::~SystemTray() {}
bool SystemTray::init(const std::string&) { return false; }
void SystemTray::run() {}
void SystemTray::stop() {}
void SystemTray::setTooltip(const std::string&) {}
void SystemTray::showBalloon(const std::string&, const std::string&) {}
void SystemTray::setStatus(const std::string&) {}
void SystemTray::setClientCount(int) {}
void SystemTray::setDeviceCount(int) {}
#endif
