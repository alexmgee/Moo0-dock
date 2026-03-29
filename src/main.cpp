/*
 * Moo0-dock
 * Docks Moo0 System Monitor to the Windows 11 taskbar.
 *
 * - Finds Moo0's window and locks it to the taskbar region
 * - Slides in/out with taskbar auto-hide
 * - Hides during fullscreen apps
 * - Drag to reposition, persists settings across restarts
 * - System tray icon with right-click menu
 *
 * Build: cmake --build build --config Release
 * No external dependencies beyond the Windows SDK.
 */

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <shlobj.h>
#include <string>
#include <fstream>
#include <sstream>
#include <cmath>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

// ============================================================================
// Constants
// ============================================================================

static const wchar_t* APP_NAME        = L"Moo0-dock";
static const wchar_t* APP_MUTEX       = L"Global\\Moo0DockMutex";
static const wchar_t* MOO0_CLASS      = L"MahobiWindowClass";   // Moo0's window class (verified via EnumWindows)
static const wchar_t* MOO0_TITLE_FRAG = L"System Monitor";      // Fallback title match
static const UINT     WM_TRAYICON     = WM_APP + 1;
static const UINT     WM_APPBAR_CB    = WM_APP + 2;
static const UINT     TIMER_POLL      = 1;
static const UINT     TIMER_ANIM      = 2;
static const int      POLL_IDLE_MS    = 200;
static const int      POLL_ANIM_MS   = 16;   // ~60fps during slide
static const int      TRAY_ICON_ID    = 1;

// Menu IDs
enum MenuID {
    IDM_LOCK_TOGGLE = 1001,
    IDM_MONITOR_PRIMARY,
    IDM_MONITOR_SECONDARY,
    IDM_MONITOR_AUTO,
    IDM_STARTUP_TOGGLE,
    IDM_RESET_POSITION,
    IDM_QUIT,
};

// Visibility states
enum DockState {
    STATE_SEARCHING,    // Moo0 not found yet
    STATE_DOCKED,       // Visible, locked to taskbar
    STATE_SLIDING_OUT,  // Taskbar hiding, Moo0 sliding with it
    STATE_SLIDING_IN,   // Taskbar showing, Moo0 sliding with it
    STATE_HIDDEN,       // Taskbar is auto-hidden, Moo0 is hidden
    STATE_FULLSCREEN,   // A fullscreen app is active
    STATE_UNLOCKED,     // User is repositioning
};

// ============================================================================
// Configuration
// ============================================================================

struct Config {
    int  offsetX       = 0;      // X offset from taskbar left edge
    int  monitorIndex  = -1;     // -1 = auto (use primary), 0 = primary, 1 = secondary
    bool lockStartup   = false;  // Launch at Windows startup
    bool isLocked      = true;   // Position locked

    std::wstring filePath;

    void setDefaults() {
        offsetX = 0;
        monitorIndex = -1;
        isLocked = true;
        lockStartup = false;
    }

    void load() {
        std::wifstream f(filePath);
        if (!f.is_open()) {
            setDefaults();
            return;
        }
        std::wstring line;
        while (std::getline(f, line)) {
            size_t eq = line.find(L'=');
            if (eq == std::wstring::npos) continue;
            std::wstring key = line.substr(0, eq);
            std::wstring val = line.substr(eq + 1);
            if      (key == L"offsetX")      offsetX      = std::stoi(val);
            else if (key == L"monitorIndex")  monitorIndex  = std::stoi(val);
            else if (key == L"lockStartup")  lockStartup  = (val == L"1");
            else if (key == L"isLocked")     isLocked     = (val == L"1");
        }
    }

    void save() const {
        std::wofstream f(filePath);
        if (!f.is_open()) return;
        f << L"offsetX="      << offsetX      << L"\n";
        f << L"monitorIndex="  << monitorIndex  << L"\n";
        f << L"lockStartup="  << (lockStartup ? L"1" : L"0") << L"\n";
        f << L"isLocked="     << (isLocked ? L"1" : L"0")    << L"\n";
    }
};

// ============================================================================
// Application state
// ============================================================================

struct AppState {
    HINSTANCE   hInstance       = nullptr;
    HWND        hWndHost       = nullptr;  // Our invisible message window
    HWND        hWndMoo0       = nullptr;  // Moo0's window handle
    HWND        hWndTaskbar    = nullptr;  // Shell_TrayWnd
    NOTIFYICONDATAW nid        = {};
    Config      config;
    DockState   state          = STATE_SEARCHING;
    bool        appBarRegistered = false;
    bool        fullscreenActive = false;

    // Taskbar tracking
    RECT        taskbarRect    = {};       // Current taskbar screen position
    RECT        taskbarRectPrev = {};      // Previous frame (for slide detection)
    bool        taskbarVisible = true;
    int         taskbarHeight  = 48;

    // Moo0 window info
    RECT        moo0OrigRect   = {};       // Moo0's original size before docking
    int         moo0Width      = 0;
    int         moo0Height     = 0;

    // Drag state
    bool        isDragging     = false;
    POINT       dragStart      = {};
    int         dragOffsetXStart = 0;

    // Animation
    double      slideProgress  = 1.0;      // 0.0 = fully hidden, 1.0 = fully shown

    // Notification area tracking
    int         notifyAreaLeft = 0;        // Left edge of the system tray (right boundary for Moo0)
};

static AppState g_app;

// ============================================================================
// Utility: find config file path next to exe
// ============================================================================

static std::wstring getConfigPath() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring path(exePath);
    size_t slash = path.find_last_of(L'\\');
    if (slash != std::wstring::npos) {
        path = path.substr(0, slash + 1);
    }
    path += L"moo0-dock.cfg";
    return path;
}

// ============================================================================
// Startup registry management
// ============================================================================

static void setStartupRegistry(bool enable) {
    HKEY hKey;
    const wchar_t* subkey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    if (RegOpenKeyExW(HKEY_CURRENT_USER, subkey, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        if (enable) {
            wchar_t exePath[MAX_PATH];
            GetModuleFileNameW(nullptr, exePath, MAX_PATH);
            RegSetValueExW(hKey, APP_NAME, 0, REG_SZ,
                           (BYTE*)exePath, (DWORD)((wcslen(exePath) + 1) * sizeof(wchar_t)));
        } else {
            RegDeleteValueW(hKey, APP_NAME);
        }
        RegCloseKey(hKey);
    }
}

// ============================================================================
// Tray icon
// ============================================================================

static void createTrayIcon(HWND hWnd) {
    g_app.nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_app.nid.hWnd = hWnd;
    g_app.nid.uID = TRAY_ICON_ID;
    g_app.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_app.nid.uCallbackMessage = WM_TRAYICON;
    g_app.nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(g_app.nid.szTip, APP_NAME);
    Shell_NotifyIconW(NIM_ADD, &g_app.nid);
}

static void removeTrayIcon() {
    Shell_NotifyIconW(NIM_DELETE, &g_app.nid);
}

static void updateTrayTooltip(const wchar_t* status) {
    std::wstring tip = std::wstring(APP_NAME) + L" - " + status;
    wcsncpy_s(g_app.nid.szTip, tip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &g_app.nid);
}

static void showTrayMenu(HWND hWnd) {
    HMENU hMenu = CreatePopupMenu();

    // Lock/unlock
    if (g_app.state != STATE_SEARCHING) {
        AppendMenuW(hMenu, MF_STRING | (g_app.config.isLocked ? MF_CHECKED : 0),
                    IDM_LOCK_TOGGLE, L"Lock position\tCtrl+Shift+M");
    }

    // Monitor selection submenu
    HMENU hMonSub = CreatePopupMenu();
    AppendMenuW(hMonSub, MF_STRING | (g_app.config.monitorIndex == -1 ? MF_CHECKED : 0),
                IDM_MONITOR_AUTO, L"Auto (primary)");
    AppendMenuW(hMonSub, MF_STRING | (g_app.config.monitorIndex == 0 ? MF_CHECKED : 0),
                IDM_MONITOR_PRIMARY, L"Primary monitor");
    AppendMenuW(hMonSub, MF_STRING | (g_app.config.monitorIndex == 1 ? MF_CHECKED : 0),
                IDM_MONITOR_SECONDARY, L"Secondary monitor");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hMonSub, L"Monitor");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // Startup toggle
    AppendMenuW(hMenu, MF_STRING | (g_app.config.lockStartup ? MF_CHECKED : 0),
                IDM_STARTUP_TOGGLE, L"Launch at startup");

    // Reset position
    AppendMenuW(hMenu, MF_STRING, IDM_RESET_POSITION, L"Reset position");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_QUIT, L"Quit");

    // Show menu at cursor
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hWnd);
    TrackPopupMenu(hMenu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN,
                   pt.x, pt.y, 0, hWnd, nullptr);
    PostMessageW(hWnd, WM_NULL, 0, 0);
    DestroyMenu(hMenu);
}

// ============================================================================
// AppBar registration (for fullscreen notifications)
// ============================================================================

static void registerAppBar(HWND hWnd) {
    if (g_app.appBarRegistered) return;
    APPBARDATA abd = {};
    abd.cbSize = sizeof(abd);
    abd.hWnd = hWnd;
    abd.uCallbackMessage = WM_APPBAR_CB;
    if (SHAppBarMessage(ABM_NEW, &abd)) {
        g_app.appBarRegistered = true;
    }
}

static void unregisterAppBar(HWND hWnd) {
    if (!g_app.appBarRegistered) return;
    APPBARDATA abd = {};
    abd.cbSize = sizeof(abd);
    abd.hWnd = hWnd;
    SHAppBarMessage(ABM_REMOVE, &abd);
    g_app.appBarRegistered = false;
}

// ============================================================================
// Window finding
// ============================================================================

struct FindMoo0Data {
    HWND result;
};

static BOOL CALLBACK enumWindowsProc(HWND hWnd, LPARAM lParam) {
    auto* data = reinterpret_cast<FindMoo0Data*>(lParam);

    if (!IsWindowVisible(hWnd)) return TRUE;

    wchar_t className[256];
    GetClassNameW(hWnd, className, 256);

    wchar_t title[256];
    GetWindowTextW(hWnd, title, 256);

    // Try matching by window class + title (Moo0 has multiple MahobiWindowClass windows)
    if (wcsstr(className, MOO0_CLASS) != nullptr &&
        wcsstr(title, MOO0_TITLE_FRAG) != nullptr) {
        data->result = hWnd;
        return FALSE;
    }

    // Fallback: match by title fragment alone
    if (wcsstr(title, MOO0_TITLE_FRAG) != nullptr &&
        wcsstr(title, L"Moo0") != nullptr) {
        data->result = hWnd;
        return FALSE;
    }

    return TRUE;
}

static HWND findMoo0Window() {
    FindMoo0Data data = { nullptr };
    EnumWindows(enumWindowsProc, reinterpret_cast<LPARAM>(&data));
    return data.result;
}

static HWND findTaskbar(int monitorIndex) {
    HWND primary = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (monitorIndex <= 0) return primary;

    // For secondary monitor, find Shell_SecondaryTrayWnd
    HWND secondary = FindWindowW(L"Shell_SecondaryTrayWnd", nullptr);
    if (secondary) return secondary;

    // Fallback to primary if secondary not found
    return primary;
}

// ============================================================================
// Taskbar state detection
// ============================================================================

static bool isTaskbarAutoHideEnabled() {
    APPBARDATA abd = {};
    abd.cbSize = sizeof(abd);
    UINT state = (UINT)SHAppBarMessage(ABM_GETSTATE, &abd);
    return (state & ABS_AUTOHIDE) != 0;
}

static bool isTaskbarVisible(const RECT& taskbarRect, int screenHeight) {
    // The taskbar is "visible" if its top edge is above the screen bottom
    // When auto-hidden, the taskbar slides to where only 2px remain visible
    return (taskbarRect.top < screenHeight - 4);
}

static bool isFullscreenAppActive() {
    // Check if the foreground window is a fullscreen app covering our target monitor
    HWND fg = GetForegroundWindow();
    if (!fg || fg == g_app.hWndHost) return false;

    // Get the monitor that our taskbar is on
    HMONITOR hTaskbarMon = MonitorFromWindow(g_app.hWndTaskbar, MONITOR_DEFAULTTONEAREST);
    HMONITOR hFgMon = MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST);
    if (hTaskbarMon != hFgMon) return false;

    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(hTaskbarMon, &mi);

    RECT fgRect;
    GetWindowRect(fg, &fgRect);

    // Fullscreen if the foreground window covers the entire monitor
    return (fgRect.left <= mi.rcMonitor.left &&
            fgRect.top <= mi.rcMonitor.top &&
            fgRect.right >= mi.rcMonitor.right &&
            fgRect.bottom >= mi.rcMonitor.bottom);
}

// ============================================================================
// Position computation
// ============================================================================

// Recursively search for a window class name within a parent's child hierarchy
static HWND findChildDeep(HWND parent, const wchar_t* className) {
    // Check direct children first
    HWND child = FindWindowExW(parent, nullptr, className, nullptr);
    if (child) return child;

    // Recurse into each direct child
    HWND cur = nullptr;
    while ((cur = FindWindowExW(parent, cur, nullptr, nullptr)) != nullptr) {
        HWND found = findChildDeep(cur, className);
        if (found) return found;
    }
    return nullptr;
}

static void updateNotifyAreaLeft() {
    if (!g_app.hWndTaskbar) return;

    // Try to find TrayNotifyWnd anywhere in the taskbar's child hierarchy
    HWND hNotify = findChildDeep(g_app.hWndTaskbar, L"TrayNotifyWnd");
    if (hNotify) {
        RECT r;
        if (GetWindowRect(hNotify, &r) && r.left > g_app.taskbarRect.left) {
            g_app.notifyAreaLeft = r.left;
            return;
        }
    }

    // Fallback: query our own tray icon position via Shell_NotifyIconGetRect
    // Only valid if the icon is actually on this taskbar (check Y bounds)
    NOTIFYICONIDENTIFIER nii = {};
    nii.cbSize = sizeof(nii);
    nii.hWnd = g_app.hWndHost;
    nii.uID = TRAY_ICON_ID;
    RECT iconRect;
    if (SUCCEEDED(Shell_NotifyIconGetRect(&nii, &iconRect)) &&
        iconRect.left > g_app.taskbarRect.left &&
        iconRect.left < g_app.taskbarRect.right &&
        iconRect.top >= g_app.taskbarRect.top &&
        iconRect.bottom <= g_app.taskbarRect.bottom + 10) {
        g_app.notifyAreaLeft = iconRect.left - 8;
        return;
    }

    // Last resort: estimate based on typical Windows 11 notification area widths
    bool isPrimary = (g_app.hWndTaskbar == FindWindowW(L"Shell_TrayWnd", nullptr));
    int trayWidth = isPrimary ? 300 : 150;
    g_app.notifyAreaLeft = g_app.taskbarRect.right - trayWidth;
}

static void updateTaskbarInfo() {
    g_app.taskbarRectPrev = g_app.taskbarRect;

    HWND tb = findTaskbar(g_app.config.monitorIndex);
    if (tb && tb != g_app.hWndTaskbar) {
        g_app.hWndTaskbar = tb;
    }
    if (g_app.hWndTaskbar) {
        GetWindowRect(g_app.hWndTaskbar, &g_app.taskbarRect);
        g_app.taskbarHeight = g_app.taskbarRect.bottom - g_app.taskbarRect.top;
        updateNotifyAreaLeft();
    }
}

static void positionMoo0() {
    if (!g_app.hWndMoo0 || !IsWindow(g_app.hWndMoo0)) return;
    if (g_app.state == STATE_UNLOCKED) return;

    // Right-align: offset from the notification area's left edge (not taskbar edge)
    int rightEdge = g_app.notifyAreaLeft;
    if (rightEdge <= g_app.taskbarRect.left) rightEdge = g_app.taskbarRect.right;
    int x = rightEdge - g_app.moo0Width - g_app.config.offsetX;
    int y = g_app.taskbarRect.top;

    // Clamp X to taskbar bounds
    int maxX = rightEdge - g_app.moo0Width;
    if (x > maxX) x = maxX;
    if (x < g_app.taskbarRect.left) x = g_app.taskbarRect.left;

    // During slide animation, the taskbar Y changes frame-by-frame.
    // We just follow it directly, which gives us pixel-perfect sync.

    SetWindowPos(g_app.hWndMoo0, HWND_TOPMOST,
                 x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
}

static void hideMoo0() {
    if (!g_app.hWndMoo0 || !IsWindow(g_app.hWndMoo0)) return;
    ShowWindow(g_app.hWndMoo0, SW_HIDE);
}

static void showMoo0() {
    if (!g_app.hWndMoo0 || !IsWindow(g_app.hWndMoo0)) return;
    ShowWindow(g_app.hWndMoo0, SW_SHOWNOACTIVATE);
    // Re-apply topmost
    SetWindowPos(g_app.hWndMoo0, HWND_TOPMOST,
                 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

// ============================================================================
// Hotkey for lock/unlock (Ctrl+Shift+M)
// ============================================================================

static const int HOTKEY_LOCK_ID = 1;

static void registerHotkey(HWND hWnd) {
    RegisterHotKey(hWnd, HOTKEY_LOCK_ID, MOD_CONTROL | MOD_SHIFT, 'M');
}

static void unregisterHotkey(HWND hWnd) {
    UnregisterHotKey(hWnd, HOTKEY_LOCK_ID);
}

static void toggleLock() {
    g_app.config.isLocked = !g_app.config.isLocked;

    if (g_app.config.isLocked) {
        // Re-lock: capture current position as offset
        if (g_app.hWndMoo0 && IsWindow(g_app.hWndMoo0)) {
            RECT moo0Rect;
            GetWindowRect(g_app.hWndMoo0, &moo0Rect);
            int rightEdge = g_app.notifyAreaLeft;
            if (rightEdge <= g_app.taskbarRect.left) rightEdge = g_app.taskbarRect.right;
            g_app.config.offsetX = rightEdge - g_app.moo0Width - moo0Rect.left;
        }
        g_app.state = STATE_DOCKED;
        updateTrayTooltip(L"Locked");
    } else {
        g_app.state = STATE_UNLOCKED;
        updateTrayTooltip(L"Unlocked - drag to reposition");
    }

    g_app.config.save();
}

// ============================================================================
// Core polling logic
// ============================================================================

static void pollUpdate() {
    // Step 1: Find Moo0 if we don't have it
    if (!g_app.hWndMoo0 || !IsWindow(g_app.hWndMoo0)) {
        g_app.hWndMoo0 = findMoo0Window();
        if (!g_app.hWndMoo0) {
            if (g_app.state != STATE_SEARCHING) {
                g_app.state = STATE_SEARCHING;
                updateTrayTooltip(L"Waiting for Moo0...");
            }
            return;
        }

        // Newly found - capture its size
        RECT r;
        GetWindowRect(g_app.hWndMoo0, &r);
        g_app.moo0OrigRect = r;
        g_app.moo0Width = r.right - r.left;
        g_app.moo0Height = r.bottom - r.top;

        // Remove WS_CAPTION and WS_THICKFRAME if present (keep it borderless)
        // Actually, Moo0 manages its own window style - don't touch it.
        // Just ensure it's topmost.
        SetWindowPos(g_app.hWndMoo0, HWND_TOPMOST,
                     0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

        g_app.state = STATE_DOCKED;
        updateTrayTooltip(L"Docked");
    }

    // Step 2: Update taskbar position
    updateTaskbarInfo();

    if (!g_app.hWndTaskbar) return;

    // Step 3: Detect fullscreen state changes (polling + AppBar push)
    bool fullscreen = g_app.fullscreenActive || isFullscreenAppActive();
    if (fullscreen) {
        if (g_app.state != STATE_FULLSCREEN) {
            g_app.state = STATE_FULLSCREEN;
            hideMoo0();
            updateTrayTooltip(L"Hidden (fullscreen)");
        }
        return;
    } else if (g_app.state == STATE_FULLSCREEN) {
        // Exiting fullscreen
        g_app.state = STATE_DOCKED;
        showMoo0();
        positionMoo0();
        updateTrayTooltip(L"Docked");
        return;
    }

    // Step 4: Handle auto-hide behavior
    if (isTaskbarAutoHideEnabled()) {
        HMONITOR hMon = MonitorFromWindow(g_app.hWndTaskbar, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = {};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(hMon, &mi);
        int screenBottom = mi.rcMonitor.bottom;

        bool tbVisible = isTaskbarVisible(g_app.taskbarRect, screenBottom);
        bool tbWasVisible = isTaskbarVisible(g_app.taskbarRectPrev, screenBottom);

        if (tbVisible && !tbWasVisible) {
            // Taskbar is appearing - start sliding in
            g_app.state = STATE_SLIDING_IN;
            showMoo0();
            KillTimer(g_app.hWndHost, TIMER_POLL);
            SetTimer(g_app.hWndHost, TIMER_ANIM, POLL_ANIM_MS, nullptr);
        } else if (!tbVisible && tbWasVisible) {
            // Taskbar is disappearing - start sliding out
            g_app.state = STATE_SLIDING_OUT;
            KillTimer(g_app.hWndHost, TIMER_POLL);
            SetTimer(g_app.hWndHost, TIMER_ANIM, POLL_ANIM_MS, nullptr);
        } else if (!tbVisible && !tbWasVisible) {
            // Taskbar is fully hidden
            if (g_app.state != STATE_HIDDEN) {
                g_app.state = STATE_HIDDEN;
                hideMoo0();
                KillTimer(g_app.hWndHost, TIMER_ANIM);
                SetTimer(g_app.hWndHost, TIMER_POLL, POLL_IDLE_MS, nullptr);
            }
        } else if (tbVisible && tbWasVisible) {
            // Check if taskbar is still sliding (Y position changing)
            bool isSliding = (g_app.taskbarRect.top != g_app.taskbarRectPrev.top);
            if (isSliding) {
                // Keep tracking at high frequency
                positionMoo0();
            } else {
                // Taskbar is stationary and visible
                if (g_app.state == STATE_SLIDING_IN || g_app.state == STATE_SLIDING_OUT) {
                    g_app.state = STATE_DOCKED;
                    KillTimer(g_app.hWndHost, TIMER_ANIM);
                    SetTimer(g_app.hWndHost, TIMER_POLL, POLL_IDLE_MS, nullptr);
                    updateTrayTooltip(L"Docked");
                }
                positionMoo0();
            }
        }
    } else {
        // Auto-hide is off - just keep position locked
        if (g_app.state != STATE_UNLOCKED) {
            positionMoo0();
            if (g_app.state != STATE_DOCKED) {
                g_app.state = STATE_DOCKED;
                showMoo0();
                updateTrayTooltip(L"Docked");
            }
        }
    }

    // Step 5: Update Moo0 size tracking (in case user resizes Moo0)
    if (g_app.hWndMoo0 && IsWindow(g_app.hWndMoo0)) {
        RECT r;
        GetWindowRect(g_app.hWndMoo0, &r);
        g_app.moo0Width = r.right - r.left;
        g_app.moo0Height = r.bottom - r.top;
    }
}

// ============================================================================
// Animation timer callback
// ============================================================================

static void animUpdate() {
    updateTaskbarInfo();
    positionMoo0();

    // Check if slide finished
    HMONITOR hMon = MonitorFromWindow(g_app.hWndTaskbar, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(hMon, &mi);
    int screenBottom = mi.rcMonitor.bottom;

    bool tbVisible = isTaskbarVisible(g_app.taskbarRect, screenBottom);
    bool isSliding = (g_app.taskbarRect.top != g_app.taskbarRectPrev.top);

    if (!isSliding) {
        if (tbVisible) {
            g_app.state = STATE_DOCKED;
            updateTrayTooltip(L"Docked");
        } else {
            g_app.state = STATE_HIDDEN;
            hideMoo0();
        }
        KillTimer(g_app.hWndHost, TIMER_ANIM);
        SetTimer(g_app.hWndHost, TIMER_POLL, POLL_IDLE_MS, nullptr);
    }
}

// ============================================================================
// Window procedure
// ============================================================================

static LRESULT CALLBACK wndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    case WM_CREATE:
        createTrayIcon(hWnd);
        registerAppBar(hWnd);
        registerHotkey(hWnd);
        SetTimer(hWnd, TIMER_POLL, POLL_IDLE_MS, nullptr);
        return 0;

    case WM_TIMER:
        if (wParam == TIMER_POLL) {
            pollUpdate();
        } else if (wParam == TIMER_ANIM) {
            animUpdate();
        }
        return 0;

    case WM_HOTKEY:
        if (wParam == HOTKEY_LOCK_ID) {
            toggleLock();
        }
        return 0;

    case WM_APPBAR_CB:
        switch ((UINT)wParam) {
        case ABN_FULLSCREENAPP:
            g_app.fullscreenActive = (BOOL)lParam;
            if (g_app.fullscreenActive) {
                g_app.state = STATE_FULLSCREEN;
                hideMoo0();
                updateTrayTooltip(L"Hidden (fullscreen)");
            } else {
                g_app.state = STATE_DOCKED;
                showMoo0();
                positionMoo0();
                updateTrayTooltip(L"Docked");
            }
            break;
        case ABN_STATECHANGE:
            // Taskbar settings changed, re-poll
            pollUpdate();
            break;
        }
        return 0;

    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
            showTrayMenu(hWnd);
        } else if (lParam == WM_LBUTTONDBLCLK) {
            toggleLock();
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDM_LOCK_TOGGLE:
            toggleLock();
            break;
        case IDM_MONITOR_AUTO:
            g_app.config.monitorIndex = -1;
            g_app.hWndTaskbar = nullptr;
            g_app.config.save();
            pollUpdate();
            break;
        case IDM_MONITOR_PRIMARY:
            g_app.config.monitorIndex = 0;
            g_app.hWndTaskbar = nullptr;
            g_app.config.save();
            pollUpdate();
            break;
        case IDM_MONITOR_SECONDARY:
            g_app.config.monitorIndex = 1;
            g_app.hWndTaskbar = nullptr;
            g_app.config.save();
            pollUpdate();
            break;
        case IDM_STARTUP_TOGGLE:
            g_app.config.lockStartup = !g_app.config.lockStartup;
            setStartupRegistry(g_app.config.lockStartup);
            g_app.config.save();
            break;
        case IDM_RESET_POSITION:
            g_app.config.offsetX = 0;
            g_app.config.save();
            positionMoo0();
            break;
        case IDM_QUIT:
            PostQuitMessage(0);
            break;
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hWnd, TIMER_POLL);
        KillTimer(hWnd, TIMER_ANIM);
        unregisterHotkey(hWnd);
        unregisterAppBar(hWnd);
        removeTrayIcon();
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
}

// ============================================================================
// Entry point
// ============================================================================

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    // Single instance check
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, APP_MUTEX);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"Moo0-dock is already running.\nCheck the system tray.",
                    APP_NAME, MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    g_app.hInstance = hInstance;

    // Load config
    g_app.config.filePath = getConfigPath();
    g_app.config.load();

    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"Moo0DockHost";
    RegisterClassExW(&wc);

    // Create invisible message-only window
    g_app.hWndHost = CreateWindowExW(
        0, L"Moo0DockHost", APP_NAME,
        WS_OVERLAPPEDWINDOW,
        0, 0, 0, 0,
        HWND_MESSAGE, nullptr, hInstance, nullptr);

    if (!g_app.hWndHost) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
        return 1;
    }

    updateTrayTooltip(L"Waiting for Moo0...");

    // Message loop
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    ReleaseMutex(hMutex);
    CloseHandle(hMutex);
    return (int)msg.wParam;
}
