#include "PCH.h"
#include "ChildHostilityUnblocker.h"

#include "Config.h"
#include "Log.h"
#include "Offsets.h"

#include <MinHook.h>

namespace hag {
namespace {

using IsChildFn = bool (*)(void*);

IsChildFn g_originalIsChild = nullptr;
void* g_hookTarget = nullptr;
bool g_hookInstalled = false;

bool Detour_IsChild(void*) {
    return false;
}

void* ResolveCharacterIsChildTarget() {
    __try {
        auto** vtable = reinterpret_cast<void**>(offsets::FromRVA(offsets::kCharacterVtable));
        return vtable ? vtable[offsets::kVSlotIsChild] : nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool EnsureMinHook() {
    const auto status = MH_Initialize();
    if (status == MH_OK || status == MH_ERROR_ALREADY_INITIALIZED) return true;
    HAG_ERR("ChildHostilityUnblocker: MH_Initialize failed status={}", static_cast<int>(status));
    return false;
}

bool EnableHook() {
    if (g_hookInstalled) return true;
    if (!EnsureMinHook()) return false;

    g_hookTarget = ResolveCharacterIsChildTarget();
    if (!g_hookTarget) {
        HAG_ERR("ChildHostilityUnblocker: could not resolve Character::IsChild target");
        return false;
    }

    const auto createStatus = MH_CreateHook(g_hookTarget,
                                            reinterpret_cast<LPVOID>(&Detour_IsChild),
                                            reinterpret_cast<LPVOID*>(&g_originalIsChild));
    if (createStatus != MH_OK) {
        HAG_ERR("ChildHostilityUnblocker: MH_CreateHook target={} status={}",
                g_hookTarget,
                static_cast<int>(createStatus));
        g_hookTarget = nullptr;
        g_originalIsChild = nullptr;
        return false;
    }

    const auto enableStatus = MH_EnableHook(g_hookTarget);
    if (enableStatus != MH_OK) {
        HAG_ERR("ChildHostilityUnblocker: MH_EnableHook target={} status={}",
                g_hookTarget,
                static_cast<int>(enableStatus));
        MH_RemoveHook(g_hookTarget);
        g_hookTarget = nullptr;
        g_originalIsChild = nullptr;
        return false;
    }

    g_hookInstalled = true;
    HAG_INFO("ChildHostilityUnblocker: enabled Character::IsChild hook target={}", g_hookTarget);
    return true;
}

bool DisableHook() {
    if (!g_hookInstalled) return true;

    bool ok = true;
    const auto disableStatus = MH_DisableHook(g_hookTarget);
    if (disableStatus != MH_OK && disableStatus != MH_ERROR_DISABLED) {
        HAG_ERR("ChildHostilityUnblocker: MH_DisableHook target={} status={}",
                g_hookTarget,
                static_cast<int>(disableStatus));
        ok = false;
    }

    const auto removeStatus = MH_RemoveHook(g_hookTarget);
    if (removeStatus != MH_OK) {
        HAG_ERR("ChildHostilityUnblocker: MH_RemoveHook target={} status={}",
                g_hookTarget,
                static_cast<int>(removeStatus));
        ok = false;
    }

    HAG_INFO("ChildHostilityUnblocker: disabled Character::IsChild hook target={}", g_hookTarget);
    g_hookInstalled = false;
    g_hookTarget = nullptr;
    g_originalIsChild = nullptr;
    return ok;
}

}  // namespace

bool ChildHostilityUnblocker::Apply() {
    return Config::Get().childHostilityUnblocker ? EnableHook() : DisableHook();
}

}  // namespace hag
