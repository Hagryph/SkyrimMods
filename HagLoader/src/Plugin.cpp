#include "PCH.h"
#include "Plugin.h"
#include "SKSE_Min.h"
#include "Log.h"
#include "Hooking.h"
#include "Offsets.h"
#include "UI/HagMenu.h"
#include "api/HagApi.h"
#include "ModManager.h"
#include "ConsoleQueue.h"
#include "GameState.h"
#include "NativeTaskQueue.h"
#include "PapyrusCall.h"

#include <variant>

namespace hag {

Plugin& Plugin::Get() {
    static Plugin s;
    return s;
}

namespace {
// SKSE -> us. Register our menu once the game's UI/data is up (kDataLoaded);
// the UI registry may not exist yet at plugin-load time.
void OnSKSEMessage(skse::Message* msg) {
    if (!msg) return;
    if (msg->type == skse::kMessage_DataLoaded) {
        HAG_INFO("kDataLoaded -> registering HagUIMenu");
        ui::HagMenu::Register();
        ModManager::Get().OnDataLoaded();
    }
}

// Phase-A self-test page: kept in the source but OFF now that HagGeneral is the real consumer.
// Flip to true if you ever need to exercise the option-page pipeline without a consumer plugin.
constexpr bool kRegisterTestPage = false;
void RegisterTestPage() {
    using namespace hag::api;
    HagUI::Get().RegisterPage("General (test)", Scope::Global)
        .Toggle("test_a", "Test toggle A", false,
                [](const Value& v) { HAG_INFO("test toggle A -> {}", std::get<bool>(v)); })
        .Toggle("test_b", "Test toggle B", true,
                [](const Value& v) { HAG_INFO("test toggle B -> {}", std::get<bool>(v)); });
    HAG_INFO("registered Phase-A test page 'General (test)'");
}
}  // namespace

bool Plugin::OnLoad(const skse::Interface* skse) {
    Log::Init("HagLoader");
    HAG_INFO("HagLoader loading - SKSE {} runtime {:#x} base {:#x}",
             skse ? skse->skseVersion : 0u,
             skse ? skse->runtimeVersion : 0u,
             offsets::Base());

    if (!Hooking::Init()) {
        return false;
    }

    game_state::InstallHooks();
    ui::HagMenu::InstallTrigger();  // (debug) click Credits -> open HagUIMenu

    if (kRegisterTestPage) {
        RegisterTestPage();
    }

    ModManager::Get().LoadAll();

    if (skse) {
        auto* task = reinterpret_cast<skse::TaskInterface*>(
            skse->QueryInterface(skse::kInterface_Task));
        console_queue::SetTaskInterface(task);
        papyrus_call::SetTaskInterface(task);
        native_task_queue::SetTaskInterface(task);

        auto* msg = reinterpret_cast<skse::MessagingInterface*>(
            skse->QueryInterface(skse::kInterface_Messaging));
        if (msg) {
            msg->RegisterListener(skse->GetPluginHandle(), "SKSE", &OnSKSEMessage);
            HAG_INFO("registered SKSE message listener");
        } else {
            HAG_ERR("no SKSE messaging interface");
        }
    }

    if (!Hooking::Commit()) {
        return false;
    }

    HAG_INFO("HagLoader loaded.");
    return true;
}

}  // namespace hag

// ---- SKSE ABI exports (the only free functions; they delegate to Plugin) ----

// MUST be constant-initialized: SKSE reads this struct from the DLL's static data
// WITHOUT running our code (so an incompatible plugin can't crash the game during the
// version check). A runtime initializer (e.g. strcpy_s in a lambda) leaves it zeroed
// at probe time -> SKSE reports "bad version data". So: constinit + literals only.
extern "C" __declspec(dllexport) constinit skse::PluginVersionData SKSEPlugin_Version = {
    skse::PluginVersionData::kVersion,   // dataVersion
    1,                                    // pluginVersion
    "HagLoader",                          // name
    "Hagryph",                            // author
    "",                                   // supportEmail
    0,                                    // versionIndependenceEx
    0,                                    // versionIndependence (0 => pin exact runtime below)
    { skse::kRuntime_1_6_1170 },          // compatibleVersions (rest zero-filled)
    0,                                    // seVersionRequired (0 => any)
};

extern "C" __declspec(dllexport) bool SKSEPlugin_Load(const skse::Interface* a_skse) {
    return hag::Plugin::Get().OnLoad(a_skse);
}
