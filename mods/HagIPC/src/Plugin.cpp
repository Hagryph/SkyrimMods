#include "PCH.h"
#include "SKSE_Min.h"
#include "Log.h"
#include "Config.h"
#include "Offsets.h"
#include "ipc/Server.h"
#include "ipc/MainThread.h"

namespace hag {

namespace {
Config g_cfg;

// Start the server once the game is fully up (the main-thread task queue is draining by then,
// so call/exec/write/console can be marshaled).
void OnSKSEMessage(skse::Message* msg) {
    if (msg && msg->type == skse::kMessage_DataLoaded && g_cfg.enabled) {
        HAG_INFO("kDataLoaded -> starting HagIPC on 127.0.0.1:{}", g_cfg.port);
        ipc::Server::Get().Start(g_cfg.port, g_cfg.token);
    }
}
}  // namespace

bool OnLoad(const skse::Interface* skse) {
    Log::Init("HagIPC");
    HAG_INFO("HagIPC loading - SKSE {} runtime {:#x} base {:#x}",
             skse ? skse->skseVersion : 0u,
             skse ? skse->runtimeVersion : 0u,
             offsets::Base());

    g_cfg = Config::Load();
    if (!g_cfg.enabled) {
        HAG_WARN("HagIPC is disabled in config (Enabled=false); nothing started.");
        return true;
    }

    if (skse) {
        // Task interface: marshal call/exec/write/console onto the game main thread.
        auto* task = reinterpret_cast<skse::TaskInterface*>(skse->QueryInterface(skse::kInterface_Task));
        if (task) {
            mt::SetTaskInterface(task);
            HAG_INFO("SKSE Task interface acquired (interfaceVersion={})", task->interfaceVersion);
        } else {
            HAG_ERR("no SKSE Task interface (id 4) - call/exec/write would run inline (unsafe); check SKSE.");
        }
        // Messaging: schedule the server start at kDataLoaded.
        auto* msg = reinterpret_cast<skse::MessagingInterface*>(skse->QueryInterface(skse::kInterface_Messaging));
        if (msg) {
            msg->RegisterListener(skse->GetPluginHandle(), "SKSE", &OnSKSEMessage);
        } else {
            HAG_ERR("no SKSE messaging interface - cannot schedule server start.");
        }
    }

    HAG_INFO("HagIPC loaded.");
    return true;
}

}  // namespace hag

// ---- SKSE ABI exports ----
extern "C" __declspec(dllexport) constinit skse::PluginVersionData SKSEPlugin_Version = {
    skse::PluginVersionData::kVersion,
    1,
    "HagIPC",
    "Hagryph",
    "",
    0,
    0,
    { skse::kRuntime_1_6_1170 },
    0,
};

extern "C" __declspec(dllexport) bool SKSEPlugin_Load(const skse::Interface* a_skse) {
    return hag::OnLoad(a_skse);
}
