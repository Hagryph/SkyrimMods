#include "PCH.h"
#include "Hooking.h"
#include "Log.h"

namespace hag {

bool Hooking::Init() {
    const auto s = MH_Initialize();
    if (s != MH_OK) {
        HAG_ERR("MH_Initialize failed: {}", static_cast<int>(s));
        return false;
    }
    return true;
}

bool Hooking::Commit() {
    const auto s = MH_EnableHook(MH_ALL_HOOKS);
    if (s != MH_OK) {
        HAG_ERR("MH_EnableHook failed: {}", static_cast<int>(s));
        return false;
    }
    return true;
}

}  // namespace hag
