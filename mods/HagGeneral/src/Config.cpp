#include "PCH.h"
#include "Config.h"
#include "HagLoaderAPI.h"
#include "Log.h"

namespace hag {

namespace {

constexpr const char* kModName = "HagGeneral";

const HagLoaderAPI* LoaderApi() {
    HMODULE h = ::GetModuleHandleW(L"HagLoader.dll");
    if (!h) return nullptr;
    auto getApi = reinterpret_cast<HagLoader_GetAPIFn>(::GetProcAddress(h, "HagLoader_GetAPI"));
    return getApi ? getApi(HAGLOADER_ABI_VERSION) : nullptr;
}

HMODULE OwnModule() {
    HMODULE module = nullptr;
    ::GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&OwnModule), &module);
    return module;
}

}  // namespace

Config& Config::Get() {
    static Config s;
    return s;
}

void Config::Load() {
    const HagLoaderAPI* api = LoaderApi();
    HMODULE owner = OwnModule();
    if (!api || !api->GetConfigBoolForModule || !api->SetConfigBoolForModule || !owner) {
        HAG_ERR("HagLoader config API unavailable; HagGeneral settings will not persist");
        return;
    }

    fullscreen = api->GetConfigBoolForModule(owner, HAGLOADER_CONFIG_GLOBAL, kModName, "Fullscreen", fullscreen);
    borderless = api->GetConfigBoolForModule(owner, HAGLOADER_CONFIG_GLOBAL, kModName, "Borderless", borderless);
    alwaysActive = api->GetConfigBoolForModule(owner, HAGLOADER_CONFIG_GLOBAL, kModName, "AlwaysActive", alwaysActive);
    childHostilityUnblocker = api->GetConfigBoolForModule(owner,
                                                           HAGLOADER_CONFIG_GLOBAL,
                                                           kModName,
                                                           "ChildHostilityUnblocker",
                                                           childHostilityUnblocker);

    HAG_INFO("loader config: Fullscreen={} Borderless={} AlwaysActive={} ChildHostilityUnblocker={}",
             fullscreen, borderless, alwaysActive, childHostilityUnblocker);
}

void Config::Save() const {
    const HagLoaderAPI* api = LoaderApi();
    HMODULE owner = OwnModule();
    if (!api || !api->SetConfigBoolForModule || !owner) {
        HAG_ERR("HagLoader config API unavailable; cannot save HagGeneral settings");
        return;
    }

    api->SetConfigBoolForModule(owner, HAGLOADER_CONFIG_GLOBAL, kModName, "Fullscreen", fullscreen);
    api->SetConfigBoolForModule(owner, HAGLOADER_CONFIG_GLOBAL, kModName, "Borderless", borderless);
    api->SetConfigBoolForModule(owner, HAGLOADER_CONFIG_GLOBAL, kModName, "AlwaysActive", alwaysActive);
    api->SetConfigBoolForModule(owner,
                                HAGLOADER_CONFIG_GLOBAL,
                                kModName,
                                "ChildHostilityUnblocker",
                                childHostilityUnblocker);
    HAG_INFO("loader config saved: Fullscreen={} Borderless={} AlwaysActive={} ChildHostilityUnblocker={}",
             fullscreen, borderless, alwaysActive, childHostilityUnblocker);
}

}  // namespace hag
