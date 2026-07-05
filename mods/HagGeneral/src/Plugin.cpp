#include "PCH.h"
#include "Plugin.h"

#include "Config.h"
#include "Display.h"
#include "HagUIBridge.h"
#include "Log.h"
#include "Offsets.h"
#include "SKSE_Min.h"
#include "SettingsHook.h"
#include "SkyrimModAPI.h"

namespace hag {

Plugin& Plugin::Get() {
    static Plugin s;
    return s;
}

void Plugin::Init(void* page) {
    if (initialized_) return;
    initialized_ = true;

    Log::Init("HagGeneral");
    HAG_INFO("HagGeneral loading as external HagUI mod - base {:#x}", offsets::Base());

    Config::Get().Load();

    // Record the display mode we booted in (governs whether Borderless can be toggled live).
    Display::CaptureSessionMode();
    // Repoint the game's setting reads (bAlwaysActive + display) to our own flag bytes.
    SettingsHook::Install();

    HagUIBridge::Register(reinterpret_cast<HagUI_PageHandle*>(page));

    HAG_INFO("HagGeneral loaded.");
}

void Plugin::OnDataLoaded() {
    if (!initialized_) return;
    // Re-assert our flag bytes after the game's startup has fully settled.
    SettingsHook::Apply();
}

}  // namespace hag

extern "C" __declspec(dllexport) const char* SkyrimMod_Name() {
    return "General";
}

extern "C" __declspec(dllexport) int SkyrimMod_Scope() {
    return SKYRIMMOD_GLOBAL;
}

extern "C" __declspec(dllexport) void SkyrimMod_Init(void* page) {
    hag::Plugin::Get().Init(page);
}

extern "C" __declspec(dllexport) void SkyrimMod_OnDataLoaded() {
    hag::Plugin::Get().OnDataLoaded();
}

extern "C" __declspec(dllexport) constinit skse::PluginVersionData SKSEPlugin_Version = {
    skse::PluginVersionData::kVersion,
    1,
    "HagGeneral",
    "Hagryph",
    "",
    0,
    0,
    { skse::kRuntime_1_6_1170 },
    0,
};

extern "C" __declspec(dllexport) bool SKSEPlugin_Load(const skse::Interface*) {
    // HagUI owns this mod's actual initialization so it can create exactly one UI page first.
    return true;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        ::DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
