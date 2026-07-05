#include "PCH.h"
#include "ConfigStore.h"
#include "ConsoleQueue.h"
#include "HagLoaderAPI.h"
#include "Log.h"

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

const HagLoaderAPI g_loaderApi = {
    HAGLOADER_ABI_VERSION,
    &C_QueueConsoleCommand,
    &C_QueueConsoleCommandWithCallback,
    &C_GetConfigBool,
    &C_SetConfigBool,
    &C_GetConfigBoolForModule,
    &C_SetConfigBoolForModule,
};

}  // namespace

extern "C" __declspec(dllexport) const HagLoaderAPI* HagLoader_GetAPI(std::uint32_t abiVersion) {
    return (abiVersion >= 1 && abiVersion <= HAGLOADER_ABI_VERSION) ? &g_loaderApi : nullptr;
}
