#include "PCH.h"

#include "HagLoaderAPI.h"
#include "HagUIAPI.h"
#include "Log.h"
#include "SKSE_Min.h"
#include "SkyrimModAPI.h"

#include <stdexcept>

namespace hag {

namespace {

bool g_initialized = false;
const HagLoaderAPI* g_loaderApi = nullptr;

void OnTransformResult(void*, const HagLoader_PapyrusResult* result) {
    if (!result) {
        HAG_ERR("vampire transform result callback received null result");
        return;
    }
    if (result->faulted) {
        HAG_ERR("vampire transform Papyrus call faulted");
        return;
    }
    if (!result->dispatched) {
        HAG_ERR("vampire transform Papyrus call was not dispatched: '{}'",
                result->message ? result->message : "");
        return;
    }
    HAG_INFO("vampire transform Papyrus call dispatched: '{}'",
             result->message ? result->message : "");
}

void OnTransformClicked(void*) {
    constexpr const char* kScript = "HagVampireBridge";
    constexpr const char* kFunction = "TransformPlayer";
    if (!g_loaderApi || !g_loaderApi->QueuePapyrusStaticCallWithCallback) {
        HAG_ERR("vampire transform failed: HagLoader Papyrus API unavailable");
        return;
    }
    if (!g_loaderApi->QueuePapyrusStaticCallWithCallback(kScript, kFunction, &OnTransformResult, nullptr)) {
        HAG_ERR("vampire transform failed: could not queue {}.{}()", kScript, kFunction);
        return;
    }
    HAG_INFO("vampire transform Papyrus bridge queued: {}.{}()", kScript, kFunction);
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

    api->AddButton(page, "transform_vampire", "Transform into a Vampire", &OnTransformClicked, nullptr);
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
