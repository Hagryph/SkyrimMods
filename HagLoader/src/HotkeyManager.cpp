#include "PCH.h"
#include "HotkeyManager.h"
#include "GameState.h"
#include "Log.h"
#include "UI/HagMenu.h"

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace hag::hotkeys {
namespace {

struct Entry {
    HMODULE module = nullptr;
    std::string name;
    std::int32_t vkCode = 0;
    Callback callback = nullptr;
    void* user = nullptr;
};

struct DispatchEntry {
    std::string name;
    Callback callback = nullptr;
    void* user = nullptr;
};

std::mutex g_mutex;
std::vector<Entry> g_entries;
std::atomic_bool g_hooked{false};
HWND g_hwnd = nullptr;
WNDPROC g_previousWndProc = nullptr;

bool ValidKeyCode(std::int32_t vkCode) {
    return vkCode >= 0 && vkCode <= 255;
}

BOOL CALLBACK FindWindowForProcess(HWND hwnd, LPARAM param) {
    DWORD pid = 0;
    ::GetWindowThreadProcessId(hwnd, &pid);
    if (pid != ::GetCurrentProcessId()) return TRUE;
    if (!::IsWindowVisible(hwnd)) return TRUE;
    if (::GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;

    *reinterpret_cast<HWND*>(param) = hwnd;
    return FALSE;
}

HWND FindGameWindow() {
    HWND hwnd = nullptr;
    ::EnumWindows(&FindWindowForProcess, reinterpret_cast<LPARAM>(&hwnd));
    return hwnd;
}

void DispatchKey(std::int32_t vkCode) {
    if (vkCode <= 0) return;
    if (!game_state::IsGameRunning()) return;
    if (ui::HagMenu::IsOpen()) return;

    std::vector<DispatchEntry> callbacks;
    {
        std::lock_guard lock(g_mutex);
        for (const Entry& entry : g_entries) {
            if (entry.vkCode != vkCode || !entry.callback) continue;
            callbacks.push_back({entry.name, entry.callback, entry.user});
        }
    }

    if (callbacks.empty()) return;
    HAG_INFO("hotkey VK {:#x} dispatching {} action(s)", vkCode, callbacks.size());
    for (const DispatchEntry& entry : callbacks) {
        HAG_INFO("hotkey '{}' triggered", entry.name);
        entry.callback(entry.user);
    }
}

LRESULT CALLBACK HagWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && ((lParam & (1ll << 30)) == 0)) {
        DispatchKey(static_cast<std::int32_t>(wParam));
    }

    WNDPROC previous = g_previousWndProc;
    if (msg == WM_NCDESTROY && hwnd == g_hwnd && previous) {
        ::SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(previous));
        g_hwnd = nullptr;
        g_previousWndProc = nullptr;
        g_hooked.store(false, std::memory_order_release);
    }

    return previous
        ? ::CallWindowProcW(previous, hwnd, msg, wParam, lParam)
        : ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

bool RegisterForModule(HMODULE module, const char* name, std::int32_t vkCode, Callback callback, void* user) {
    if (!module || !name || !*name || !callback || !ValidKeyCode(vkCode)) return false;

    {
        std::lock_guard lock(g_mutex);
        for (Entry& entry : g_entries) {
            if (entry.module != module || entry.name != name) continue;
            entry.vkCode = vkCode;
            entry.callback = callback;
            entry.user = user;
            HAG_INFO("hotkey '{}' updated to VK {:#x}", name, vkCode);
            return true;
        }

        g_entries.push_back({module, name, vkCode, callback, user});
    }

    HAG_INFO("hotkey '{}' registered at VK {:#x}", name, vkCode);
    InstallWindowHook();
    return true;
}

bool SetForModule(HMODULE module, const char* name, std::int32_t vkCode) {
    if (!module || !name || !*name || !ValidKeyCode(vkCode)) return false;

    std::lock_guard lock(g_mutex);
    for (Entry& entry : g_entries) {
        if (entry.module != module || entry.name != name) continue;
        entry.vkCode = vkCode;
        HAG_INFO("hotkey '{}' rebound to VK {:#x}", name, vkCode);
        return true;
    }
    return false;
}

bool InstallWindowHook() {
    if (g_hooked.load(std::memory_order_acquire) && g_hwnd && ::IsWindow(g_hwnd)) return true;

    HWND hwnd = FindGameWindow();
    if (!hwnd) {
        HAG_WARN("HotkeyManager: game window not found yet");
        return false;
    }

    ::SetLastError(0);
    const LONG_PTR previous = ::SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&HagWindowProc));
    if (previous == 0 && ::GetLastError() != 0) {
        HAG_ERR("HotkeyManager: SetWindowLongPtrW failed ({})", ::GetLastError());
        return false;
    }

    g_hwnd = hwnd;
    g_previousWndProc = reinterpret_cast<WNDPROC>(previous);
    g_hooked.store(true, std::memory_order_release);
    HAG_INFO("HotkeyManager: installed window proc hook on HWND {}", reinterpret_cast<void*>(hwnd));
    return true;
}

}  // namespace hag::hotkeys
