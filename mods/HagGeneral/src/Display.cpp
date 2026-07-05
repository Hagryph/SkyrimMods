#include "PCH.h"
#include "Display.h"
#include "Config.h"
#include "Log.h"

// No D3D hook, no SetFullscreenState. The initial mode is imposed by the SettingsHook read-redirect
// (the game builds its window/swapchain from our flags at startup — the same approach SSE Display
// Tweaks uses). Fullscreen changes are therefore next-launch. The only thing we do live is the
// windowed borderless<->bordered switch, which is a pure Win32 window-style change (no DXGI mode
// switch => no blackscreen, and it never touches the engine's swapchain, so it can't fight it).
namespace hag {
namespace {

bool g_sessionFullscreen = false;   // the display mode the game actually booted in this session

// The render window's class name AND title are both "Skyrim Special Edition" (RE'd from the
// RegisterClassA/CreateWindowExA in FUN_140e423e0). Find it by class first, then by title.
HWND GameWindow() {
    HWND h = ::FindWindowW(L"Skyrim Special Edition", nullptr);
    if (!h) h = ::FindWindowW(nullptr, L"Skyrim Special Edition");
    return h;
}

void ApplyBorderless(HWND hwnd) {
    LONG_PTR style = ::GetWindowLongPtrW(hwnd, GWL_STYLE);
    style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU | WS_BORDER | WS_DLGFRAME);
    style |= WS_POPUP;
    ::SetWindowLongPtrW(hwnd, GWL_STYLE, style);

    HMONITOR mon = ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{}; mi.cbSize = sizeof(mi);
    if (::GetMonitorInfoW(mon, &mi)) {
        ::SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                       mi.rcMonitor.right - mi.rcMonitor.left,
                       mi.rcMonitor.bottom - mi.rcMonitor.top,
                       SWP_FRAMECHANGED | SWP_NOZORDER | SWP_SHOWWINDOW);   // SWP_FRAMECHANGED is mandatory
    } else {
        ::SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
    HAG_INFO("Display: applied borderless (WS_POPUP, monitor-filling)");
}

void ApplyBordered(HWND hwnd) {
    LONG_PTR style = ::GetWindowLongPtrW(hwnd, GWL_STYLE);
    style &= ~WS_POPUP;
    style |= (WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_BORDER);
    ::SetWindowLongPtrW(hwnd, GWL_STYLE, style);
    ::SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    HAG_INFO("Display: applied bordered window");
}

}  // namespace

void Display::CaptureSessionMode() {
    g_sessionFullscreen = Config::Get().fullscreen;
    HAG_INFO("Display: this session booted {}", g_sessionFullscreen ? "exclusive fullscreen" : "windowed");
}

bool Display::SessionFullscreen() { return g_sessionFullscreen; }

void Display::ApplyBorderlessLive() {
    if (g_sessionFullscreen) return;   // exclusive session: no windowed frame to restyle (next-launch)
    HWND hwnd = GameWindow();
    if (!hwnd) { HAG_WARN("Display: game window not found (FindWindow)"); return; }
    if (Config::Get().borderless) ApplyBorderless(hwnd);
    else                          ApplyBordered(hwnd);
}

}  // namespace hag
