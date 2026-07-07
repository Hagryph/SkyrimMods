#include "PCH.h"
#include "Animation.h"
#include "ConfigStore.h"
#include "CellChangeHook.h"
#include "ConsoleQueue.h"
#include "HagLoaderAPI.h"
#include "HotkeyManager.h"
#include "Log.h"
#include "NativeTaskQueue.h"
#include "PapyrusCall.h"
#include "SaveStorage.h"

namespace {

bool C_QueueConsoleCommand(const char* command) {
    if (!command || !*command) return false;
    const bool queued = hag::console_queue::Queue(command);
    HAG_INFO("HagLoader API QueueConsoleCommand('{}') {}", command, queued ? "queued" : "queue failed");
    return queued;
}

bool C_QueueConsoleCommandWithCallback(const char* command, HagLoader_ConsoleResultCb callback, void* user) {
    if (!command || !*command) return false;
    const bool queued = hag::console_queue::Queue(command, callback, user);
    HAG_INFO("HagLoader API QueueConsoleCommandWithCallback('{}') {}", command, queued ? "queued" : "queue failed");
    return queued;
}

hag::config_store::Scope CfgScope(std::int32_t scope) {
    return scope == HAGLOADER_CONFIG_PERSAVE
        ? hag::config_store::Scope::PerSave
        : hag::config_store::Scope::Global;
}

bool C_GetConfigBool(std::int32_t scope, const char* modName, const char* key, bool defaultValue) {
    return hag::config_store::GetBool(CfgScope(scope), modName ? modName : "", key ? key : "", defaultValue);
}

bool C_SetConfigBool(std::int32_t scope, const char* modName, const char* key, bool value) {
    return hag::config_store::SetBool(CfgScope(scope), modName ? modName : "", key ? key : "", value);
}

bool C_GetConfigBoolForModule(void* moduleHandle, std::int32_t scope, const char* configName, const char* key, bool defaultValue) {
    return hag::config_store::GetBoolForModule(
        static_cast<HMODULE>(moduleHandle), CfgScope(scope), configName ? configName : "", key ? key : "", defaultValue);
}

bool C_SetConfigBoolForModule(void* moduleHandle, std::int32_t scope, const char* configName, const char* key, bool value) {
    return hag::config_store::SetBoolForModule(
        static_cast<HMODULE>(moduleHandle), CfgScope(scope), configName ? configName : "", key ? key : "", value);
}

std::int64_t C_GetConfigInt(std::int32_t scope, const char* modName, const char* key, std::int64_t defaultValue) {
    return hag::config_store::GetInt(CfgScope(scope), modName ? modName : "", key ? key : "", defaultValue);
}

bool C_SetConfigInt(std::int32_t scope, const char* modName, const char* key, std::int64_t value) {
    return hag::config_store::SetInt(CfgScope(scope), modName ? modName : "", key ? key : "", value);
}

std::int64_t C_GetConfigIntForModule(void* moduleHandle, std::int32_t scope, const char* configName, const char* key, std::int64_t defaultValue) {
    return hag::config_store::GetIntForModule(
        static_cast<HMODULE>(moduleHandle), CfgScope(scope), configName ? configName : "", key ? key : "", defaultValue);
}

bool C_SetConfigIntForModule(void* moduleHandle, std::int32_t scope, const char* configName, const char* key, std::int64_t value) {
    return hag::config_store::SetIntForModule(
        static_cast<HMODULE>(moduleHandle), CfgScope(scope), configName ? configName : "", key ? key : "", value);
}

bool C_QueuePapyrusStaticCall(const char* scriptName, const char* functionName) {
    if (!scriptName || !*scriptName || !functionName || !*functionName) return false;
    const bool queued = hag::papyrus_call::QueueStaticCall(scriptName, functionName);
    HAG_INFO("HagLoader API QueuePapyrusStaticCall('{}.{}') {}",
             scriptName, functionName, queued ? "queued" : "queue failed");
    return queued;
}

bool C_QueuePapyrusStaticCallWithCallback(const char* scriptName,
                                          const char* functionName,
                                          HagLoader_PapyrusResultCb callback,
                                          void* user) {
    if (!scriptName || !*scriptName || !functionName || !*functionName) return false;
    const bool queued = hag::papyrus_call::QueueStaticCall(scriptName, functionName, callback, user);
    HAG_INFO("HagLoader API QueuePapyrusStaticCallWithCallback('{}.{}') {}",
             scriptName, functionName, queued ? "queued" : "queue failed");
    return queued;
}

bool C_QueueMainThreadTask(HagLoader_MainThreadTaskCb callback, void* user) {
    const bool queued = hag::native_task_queue::Queue(callback, user);
    HAG_INFO("HagLoader API QueueMainThreadTask {}", queued ? "queued" : "queue failed");
    return queued;
}

bool C_SaveStorageAvailable() {
    return hag::save_storage::Available();
}

bool C_SaveFormIDSetContainsForModule(void* moduleHandle, const char* setName, std::uint32_t formID) {
    return hag::save_storage::ContainsFormIDForModule(
        static_cast<HMODULE>(moduleHandle), setName ? setName : "", formID);
}

bool C_SaveFormIDSetAddForModule(void* moduleHandle, const char* setName, std::uint32_t formID, std::uint32_t maxEntries) {
    return hag::save_storage::AddFormIDForModule(
        static_cast<HMODULE>(moduleHandle), setName ? setName : "", formID, maxEntries);
}

std::uint32_t C_SaveFormIDSetCountForModule(void* moduleHandle, const char* setName) {
    return hag::save_storage::CountFormIDsForModule(
        static_cast<HMODULE>(moduleHandle), setName ? setName : "");
}

bool C_RegisterHotkeyForModule(void* moduleHandle,
                               const char* name,
                               std::int32_t vkCode,
                               HagLoader_HotkeyCb callback,
                               void* user) {
    return hag::hotkeys::RegisterForModule(
        static_cast<HMODULE>(moduleHandle), name ? name : "", vkCode, callback, user);
}

bool C_SetHotkeyForModule(void* moduleHandle, const char* name, std::int32_t vkCode) {
    return hag::hotkeys::SetForModule(
        static_cast<HMODULE>(moduleHandle), name ? name : "", vkCode);
}

bool C_RegisterCellChangeCallbackForModule(void* moduleHandle, HagLoader_CellChangeCb callback, void* user) {
    return hag::cell_change::RegisterCallback(moduleHandle, callback, user);
}

bool C_PlayIdleWithTargetAutoStop(std::uint32_t actorFormID,
                                  std::uint32_t idleFormID,
                                  std::uint32_t targetFormID,
                                  std::uint32_t stopIdleFormID,
                                  std::uint32_t stopDelayMs) {
    const bool started = hag::animation::PlayIdleWithTargetAutoStop(
        actorFormID, idleFormID, targetFormID, stopIdleFormID, stopDelayMs);
    HAG_INFO("HagLoader API PlayIdleWithTargetAutoStop actor={:#x} idle={:#x} target={:#x} stopIdle={:#x} delayMs={} {}",
             actorFormID,
             idleFormID,
             targetFormID,
             stopIdleFormID,
             stopDelayMs,
             started ? "started" : "failed");
    return started;
}

bool C_StopIdle(std::uint32_t actorFormID, std::uint32_t stopIdleFormID) {
    const bool stopped = hag::animation::StopIdle(actorFormID, stopIdleFormID);
    HAG_INFO("HagLoader API StopIdle actor={:#x} stopIdle={:#x} {}",
             actorFormID,
             stopIdleFormID,
             stopped ? "stopped" : "failed");
    return stopped;
}

const HagLoaderAPI g_loaderApi = {
    HAGLOADER_ABI_VERSION,
    &C_QueueConsoleCommand,
    &C_QueueConsoleCommandWithCallback,
    &C_GetConfigBool,
    &C_SetConfigBool,
    &C_GetConfigBoolForModule,
    &C_SetConfigBoolForModule,
    &C_QueuePapyrusStaticCall,
    &C_QueuePapyrusStaticCallWithCallback,
    &C_QueueMainThreadTask,
    &C_GetConfigInt,
    &C_SetConfigInt,
    &C_GetConfigIntForModule,
    &C_SetConfigIntForModule,
    &C_SaveStorageAvailable,
    &C_SaveFormIDSetContainsForModule,
    &C_SaveFormIDSetAddForModule,
    &C_SaveFormIDSetCountForModule,
    &C_RegisterHotkeyForModule,
    &C_SetHotkeyForModule,
    &C_RegisterCellChangeCallbackForModule,
    &C_PlayIdleWithTargetAutoStop,
    &C_StopIdle,
};

}  // namespace

extern "C" __declspec(dllexport) const HagLoaderAPI* HagLoader_GetAPI(std::uint32_t abiVersion) {
    return (abiVersion >= 1 && abiVersion <= HAGLOADER_ABI_VERSION) ? &g_loaderApi : nullptr;
}
