#include "PCH.h"

#include "HagLoaderAPI.h"
#include "HagUIAPI.h"
#include "GameOffsets.h"
#include "Log.h"
#include "SKSE_Min.h"
#include "SkyrimModAPI.h"

#include <atomic>
#include <stdexcept>

namespace hag {

namespace {

bool g_initialized = false;
const HagLoaderAPI* g_loaderApi = nullptr;

enum class VampireAction {
    Transform,
    Cure,
};

std::atomic_bool g_pageActionConsumed{false};
VampireAction g_actionAtPageBuild = VampireAction::Transform;

std::uintptr_t SkyrimBase() {
    return reinterpret_cast<std::uintptr_t>(::GetModuleHandleW(nullptr));
}

bool IsPlayerVampire() {
    __try {
        using LookupByIdFn = void* (*)(std::uint32_t);
        auto lookup = reinterpret_cast<LookupByIdFn>(SkyrimBase() + game::form::LookupByID);
        void* form = lookup ? lookup(0x000ED06D) : nullptr;
        if (!form) return false;
        auto* bytes = static_cast<std::uint8_t*>(form);
        if (bytes[game::form::FormType] != 0x09) return false;
        const float value = *reinterpret_cast<float*>(bytes + 0x34);
        return value >= 0.5f;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

const char* VampireActionLabel(void*) {
    g_actionAtPageBuild = IsPlayerVampire() ? VampireAction::Cure : VampireAction::Transform;
    g_pageActionConsumed.store(false);
    return g_actionAtPageBuild == VampireAction::Cure ? "Cure Vampirism" : "Transform into a Vampire";
}

void OnActionResult(void*, const HagLoader_PapyrusResult* result) {
    if (!result) {
        HAG_ERR("vampire action result callback received null result");
        return;
    }
    if (result->faulted) {
        HAG_ERR("vampire action Papyrus call faulted");
        return;
    }
    if (!result->dispatched) {
        HAG_ERR("vampire action Papyrus call was not dispatched: '{}'",
                result->message ? result->message : "");
        return;
    }
    HAG_INFO("vampire action Papyrus call dispatched: '{}'",
             result->message ? result->message : "");
}

void OnVampireActionClicked(void*) {
    if (g_pageActionConsumed.exchange(true)) {
        HAG_INFO("vampire action ignored: page-build action already queued");
        return;
    }

    constexpr const char* kScript = "HagVampireBridge";
    const char* function = g_actionAtPageBuild == VampireAction::Cure ? "CurePlayer" : "TransformPlayer";
    if (!g_loaderApi || !g_loaderApi->QueuePapyrusStaticCallWithCallback) {
        HAG_ERR("vampire action failed: HagLoader Papyrus API unavailable");
        g_pageActionConsumed.store(false);
        return;
    }
    if (!g_loaderApi->QueuePapyrusStaticCallWithCallback(kScript, function, &OnActionResult, nullptr)) {
        HAG_ERR("vampire action failed: could not queue {}.{}()", kScript, function);
        g_pageActionConsumed.store(false);
        return;
    }
    HAG_INFO("vampire action Papyrus bridge queued: {}.{}()", kScript, function);
}

}  // namespace

void Init(HagUI_PageHandle* page) {
    HMODULE h = ::GetModuleHandleW(L"HagLoader.dll");
    if (!h) { throw std::runtime_error("HagVampire requires HagLoader.dll"); }

    if (g_initialized) return;
    g_initialized = true;

    Log::Init("HagVampire");
    HAG_INFO("HagVampire loading");

    auto getLoaderApi = reinterpret_cast<HagLoader_GetAPIFn>(::GetProcAddress(h, "HagLoader_GetAPI"));
    g_loaderApi = getLoaderApi ? getLoaderApi(HAGLOADER_ABI_VERSION) : nullptr;
    if (!g_loaderApi || !g_loaderApi->QueuePapyrusStaticCallWithCallback) {
        throw std::runtime_error("HagVampire requires HagLoader Papyrus API");
    }

    auto getApi = reinterpret_cast<HagUI_GetAPIFn>(h ? ::GetProcAddress(h, "HagUI_GetAPI") : nullptr);
    const HagUIAPI* api = getApi ? getApi(HAGUI_ABI_VERSION) : nullptr;
    if (!api || !page) {
        throw std::runtime_error("HagVampire requires HagLoader HagUI API/page");
    }

    if (!api->AddDynamicButton) {
        throw std::runtime_error("HagVampire requires HagUI dynamic button API");
    }

    api->AddDynamicButton(page,
                          "vampire_action",
                          "Transform into a Vampire",
                          &VampireActionLabel,
                          &OnVampireActionClicked,
                          nullptr);
    api->Refresh();

    HAG_INFO("HagVampire page registered");
}

}  // namespace hag

extern "C" __declspec(dllexport) const char* SkyrimMod_Name() {
    return "Vampire";
}

extern "C" __declspec(dllexport) int SkyrimMod_Scope() {
    return SKYRIMMOD_PERSAVE;
}

extern "C" __declspec(dllexport) void SkyrimMod_Init(void* page) {
    hag::Init(reinterpret_cast<HagUI_PageHandle*>(page));
}

extern "C" __declspec(dllexport) constinit skse::PluginVersionData SKSEPlugin_Version = {
    skse::PluginVersionData::kVersion,
    1,
    "HagVampire",
    "Hagryph",
    "",
    0,
    0,
    { skse::kRuntime_1_6_1170 },
    0,
};

extern "C" __declspec(dllexport) bool SKSEPlugin_Load(const skse::Interface*) {
    return true;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        ::DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
