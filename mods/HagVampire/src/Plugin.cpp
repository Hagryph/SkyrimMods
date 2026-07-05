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

void RunVampireChange() {
    HAG_INFO("Vampire button pressed: calling PlayerVampireQuestScript.VampireChange(Game.GetPlayer())");

    // This is intentionally the console command path. HagLoader::RunConsoleCommand runs the game's
    // console compiler mode, not the normal Papyrus script compiler mode.
    constexpr const char* kQuestFunctionCommand = "cqf PlayerVampireQuest VampireChange player";
    HagLoader_ConsoleResult result{};
    bool ok = g_loaderApi && g_loaderApi->RunConsoleCommand(kQuestFunctionCommand, &result);

    if (result.faulted) {
        HAG_ERR("vampire change command faulted");
        return;
    }
    if (result.noCompiler) {
        HAG_ERR("vampire change command could not run: ScriptCompiler is not available yet");
        return;
    }
    if (!ok || !result.compiled) {
        HAG_ERR("vampire change command did not compile; output='{}'", result.output ? result.output : "");
        return;
    }

    HAG_INFO("vampire change command compiled ({} bytes). output='{}'",
             result.compiledSize, result.output ? result.output : "");
}

void OnTransformClicked(void*) {
    RunVampireChange();
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
    if (!g_loaderApi || !g_loaderApi->RunConsoleCommand) {
        throw std::runtime_error("HagVampire requires HagLoader console API");
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
