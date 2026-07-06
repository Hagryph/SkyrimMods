#include "PCH.h"
#include "CellChangeHook.h"

#include "GameOffsets.h"
#include "Hooking.h"
#include "Log.h"
#include "Offsets.h"
#include "SaveStorage.h"

#include <mutex>
#include <vector>

namespace hag::cell_change {

namespace {

using LoadedCellBatchFn = void (*)(void* cellManager, std::uint32_t flags);
LoadedCellBatchFn g_origLoadedCellBatch = nullptr;

struct CallbackEntry {
    void* module = nullptr;
    HagLoader_CellChangeCb callback = nullptr;
    void* user = nullptr;
};

std::mutex g_callbackMutex;
std::vector<CallbackEntry> g_callbacks;

void InvokeCallbackGuarded(const CallbackEntry& entry) noexcept {
    __try {
        entry.callback(entry.user);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HAG_ERR("CellChangeHook: callback faulted for module={}", entry.module);
    }
}

void NotifyCallbacks() {
    std::vector<CallbackEntry> callbacks;
    {
        std::lock_guard lock(g_callbackMutex);
        callbacks = g_callbacks;
    }

    for (const auto& entry : callbacks) {
        if (!entry.callback) continue;
        InvokeCallbackGuarded(entry);
    }
}

void Detour_LoadedCellBatch(void* cellManager, std::uint32_t flags) {
    if (g_origLoadedCellBatch) {
        g_origLoadedCellBatch(cellManager, flags);
    }

    const std::uint32_t removed =
        save_storage::PruneRuntimeFormIDSets("loaded-cell batch complete");
    if (removed != 0) {
        HAG_INFO("CellChangeHook: pruned {} stale saved form id(s) after loaded-cell batch", removed);
    }
    NotifyCallbacks();
}

}  // namespace

void InstallHooks() {
    const auto target = offsets::FromRVA(game::cell::LoadedCellBatch);
    if (Hooking::Create<LoadedCellBatchFn>(target, &Detour_LoadedCellBatch, g_origLoadedCellBatch)) {
        HAG_INFO("CellChangeHook: hooked loaded-cell batch @{:#x}", target);
    } else {
        HAG_ERR("CellChangeHook: failed to hook loaded-cell batch");
    }
}

bool RegisterCallback(void* module, HagLoader_CellChangeCb callback, void* user) {
    if (!callback) return false;
    std::lock_guard lock(g_callbackMutex);
    for (auto& entry : g_callbacks) {
        if (entry.module == module && entry.callback == callback) {
            entry.user = user;
            return true;
        }
    }

    g_callbacks.push_back({module, callback, user});
    HAG_INFO("CellChangeHook: registered callback for module={}", module);
    return true;
}

}  // namespace hag::cell_change
