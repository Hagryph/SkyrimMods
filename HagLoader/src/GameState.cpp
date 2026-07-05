#include "PCH.h"
#include "GameState.h"
#include "Hooking.h"
#include "Log.h"
#include "Offsets.h"

#include <atomic>
#include <mutex>
#include <vector>

namespace hag::game_state {

namespace {

std::atomic<bool> g_running{false};
std::mutex g_callbacksMutex;

struct CallbackEntry {
    ChangeCallback callback = nullptr;
    void* user = nullptr;
};

std::vector<CallbackEntry> g_callbacks;

using UpdateMenuStateFn = void (*)(void* ui, void* topMenu, void* arg3, void* arg4);
UpdateMenuStateFn g_origUpdateMenuState = nullptr;

void Detour_UpdateMenuState(void* ui, void* topMenu, void* arg3, void* arg4) {
    if (g_origUpdateMenuState) {
        g_origUpdateMenuState(ui, topMenu, arg3, arg4);
    }
    RefreshFromUI(ui, "UI menu-state update");
}

}  // namespace

bool IsGameRunning() {
    return g_running.load(std::memory_order_acquire);
}

void SetGameRunning(bool running, const char* reason) {
    const bool previous = g_running.exchange(running, std::memory_order_acq_rel);
    if (previous == running) return;

    HAG_INFO("game running state -> {} ({})", running, reason ? reason : "unspecified");

    std::vector<CallbackEntry> callbacks;
    {
        std::lock_guard lock(g_callbacksMutex);
        callbacks = g_callbacks;
    }

    for (const auto& entry : callbacks) {
        if (entry.callback) {
            entry.callback(running, entry.user);
        }
    }
}

void RefreshFromUI(void* ui, const char* reason) {
    if (!ui) {
        SetGameRunning(false, reason ? reason : "UI unavailable");
        return;
    }

    const auto pauses = *reinterpret_cast<std::uint32_t*>(
        reinterpret_cast<char*>(ui) + offsets::kUI_NumPausesGame);
    SetGameRunning(pauses == 0, reason);
}

void AddChangeCallback(ChangeCallback callback, void* user) {
    if (!callback) return;
    std::lock_guard lock(g_callbacksMutex);
    g_callbacks.push_back({callback, user});
}

void InstallHooks() {
    const auto target = offsets::FromRVA(offsets::kUI_UpdateMenuState);
    if (Hooking::Create<UpdateMenuStateFn>(target, &Detour_UpdateMenuState, g_origUpdateMenuState)) {
        HAG_INFO("GameState: hooked UI menu-state update @{:#x}", target);
    } else {
        HAG_ERR("GameState: failed to hook UI menu-state update");
    }
}

}  // namespace hag::game_state
