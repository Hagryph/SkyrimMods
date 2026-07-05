#include "PCH.h"
#include "CellChangeHook.h"

#include "GameOffsets.h"
#include "Hooking.h"
#include "Log.h"
#include "Offsets.h"
#include "SaveStorage.h"

namespace hag::cell_change {

namespace {

using LoadedCellBatchFn = void (*)(void* cellManager, std::uint32_t flags);
LoadedCellBatchFn g_origLoadedCellBatch = nullptr;

void Detour_LoadedCellBatch(void* cellManager, std::uint32_t flags) {
    if (g_origLoadedCellBatch) {
        g_origLoadedCellBatch(cellManager, flags);
    }

    const std::uint32_t removed =
        save_storage::PruneRuntimeFormIDSets("loaded-cell batch complete");
    if (removed != 0) {
        HAG_INFO("CellChangeHook: pruned {} stale saved form id(s) after loaded-cell batch", removed);
    }
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

}  // namespace hag::cell_change
