#pragma once

#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <thread>

#ifdef _WIN32
#include <Windows.h>
#include <shellapi.h>
#endif

class SystemTray {
public:
    SystemTray();
    ~SystemTray();

    // Initialize the tray icon (call from main thread)
    bool init(const std::string& tooltip);

    // Run the message pump (blocks — call from dedicated thread or main thread)
    void run();

    // Stop the message pump (must be called from the window-owning thread)
    void stop();

    // Thread-safe stop request (posts WM_CLOSE to the window)
    void requestStop();

    // Update the tooltip text
    void setTooltip(const std::string& text);

    // Show a balloon notification
    void showBalloon(const std::string& title, const std::string& message);

    // Update status info for menu/tooltip
    void setStatus(const std::string& status);
    void setClientCount(int count);
    void setDeviceCount(int count);

    using ExitCallback = std::function<void()>;
    void setExitCallback(ExitCallback cb) { m_ExitCallback = cb; }

private:
#ifdef _WIN32
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void showContextMenu();
    void updateTooltip();
    void toggleAutoStart();
    bool isAutoStartEnabled() const;

    HWND m_Hwnd;
    NOTIFYICONDATAW m_Nid;
    HMENU m_Menu;
#endif

    std::atomic<bool> m_Running;
    std::string m_Status;
    int m_ClientCount;
    int m_DeviceCount;
    ExitCallback m_ExitCallback;
};
