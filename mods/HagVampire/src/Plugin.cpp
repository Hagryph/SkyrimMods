#include "PCH.h"

#include "ConsoleExec.h"
#include "HagUIAPI.h"
#include "Log.h"
#include "SKSE_Min.h"
#include "SkyrimModAPI.h"

namespace hag {

namespace {

bool g_initialized = false;

void RunVampireChange() {
    HAG_INFO("Vampire button pressed: calling PlayerVampireQuestScript.VampireChange(Game.GetPlayer())");

    constexpr const char* kQuestFunctionCommand = "cqf PlayerVampireQuest VampireChange player";
    console::Result result = console::Run(kQuestFunctionCommand);

    if (!result.compiled) {
        HAG_WARN("primary vampire change command did not compile; trying form-id fallback");
        result = console::Run("cqf 000EAFD5 VampireChange player");
    }

    if (result.faulted) {
        HAG_ERR("vampire change command faulted");
        return;
    }
    if (result.noCompiler) {
        HAG_ERR("vampire change command could not run: ScriptCompiler is not available yet");
        return;
    }
    if (!result.compiled) {
        HAG_ERR("vampire change command did not compile; output='{}'", result.output);
        return;
    }

    HAG_INFO("vampire change command compiled ({} bytes). output='{}'", result.compiledSize, result.output);
}

void OnTransformClicked(void*) {
    RunVampireChange();
}

}  // namespace

void Init(HagUI_PageHandle* page) {
    if (g_initialized) return;
    g_initialized = true;

    Log::Init("HagVampire");
    HAG_INFO("HagVampire loading");

    HMODULE h = ::GetModuleHandleW(L"HagUI.dll");
    auto getApi = reinterpret_cast<HagUI_GetAPIFn>(h ? ::GetProcAddress(h, "HagUI_GetAPI") : nullptr);
    const HagUIAPI* api = getApi ? getApi(HAGUI_ABI_VERSION) : nullptr;
    if (!api || !page) {
        HAG_ERR("HagVampire could not resolve HagUI API/page");
        return;
    }

    api->AddButton(page, "transform_vampire", "Become Vampire", &OnTransformClicked, nullptr);
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
