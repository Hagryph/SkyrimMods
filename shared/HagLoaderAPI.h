#pragma once
#include <cstdint>

// HagLoaderAPI.h - narrow loader services exported by HagLoader.dll for external mods.
//
// Console access is intentionally queue-only. Mods can request that HagLoader queue a console
// command onto the SKSE main-thread task path; they cannot directly run the console compiler or
// receive synchronous command results through this ABI.

extern "C" {

#define HAGLOADER_ABI_VERSION 10u

#define HAGLOADER_CONFIG_GLOBAL 0
#define HAGLOADER_CONFIG_PERSAVE 1

typedef struct HagLoader_ConsoleResult {
    bool faulted;
    bool noCompiler;
    bool compiled;
    uint32_t compiledSize;
    const char* output;  // valid only for the duration of the callback
} HagLoader_ConsoleResult;

typedef void (*HagLoader_ConsoleResultCb)(void* user, const HagLoader_ConsoleResult* result);

typedef struct HagLoader_PapyrusResult {
    bool faulted;
    bool dispatched;
    const char* message;  // valid only for the duration of the callback
} HagLoader_PapyrusResult;

typedef void (*HagLoader_PapyrusResultCb)(void* user, const HagLoader_PapyrusResult* result);
typedef void (*HagLoader_MainThreadTaskCb)(void* user);
typedef void (*HagLoader_HotkeyCb)(void* user);
typedef void (*HagLoader_CellChangeCb)(void* user);

typedef struct HagLoaderAPI {
    uint32_t abiVersion;
    bool (*QueueConsoleCommand)(const char* command);
    bool (*QueueConsoleCommandWithCallback)(const char* command, HagLoader_ConsoleResultCb callback, void* user);
    bool (*GetConfigBool)(int32_t scope, const char* modName, const char* key, bool defaultValue);
    bool (*SetConfigBool)(int32_t scope, const char* modName, const char* key, bool value);
    bool (*GetConfigBoolForModule)(void* moduleHandle, int32_t scope, const char* configName, const char* key, bool defaultValue);
    bool (*SetConfigBoolForModule)(void* moduleHandle, int32_t scope, const char* configName, const char* key, bool value);
    bool (*QueuePapyrusStaticCall)(const char* scriptName, const char* functionName);
    bool (*QueuePapyrusStaticCallWithCallback)(const char* scriptName, const char* functionName, HagLoader_PapyrusResultCb callback, void* user);
    bool (*QueueMainThreadTask)(HagLoader_MainThreadTaskCb callback, void* user);
    int64_t (*GetConfigInt)(int32_t scope, const char* modName, const char* key, int64_t defaultValue);
    bool (*SetConfigInt)(int32_t scope, const char* modName, const char* key, int64_t value);
    int64_t (*GetConfigIntForModule)(void* moduleHandle, int32_t scope, const char* configName, const char* key, int64_t defaultValue);
    bool (*SetConfigIntForModule)(void* moduleHandle, int32_t scope, const char* configName, const char* key, int64_t value);
    bool (*SaveStorageAvailable)();
    bool (*SaveFormIDSetContainsForModule)(void* moduleHandle, const char* setName, uint32_t formID);
    bool (*SaveFormIDSetAddForModule)(void* moduleHandle, const char* setName, uint32_t formID, uint32_t maxEntries);
    uint32_t (*SaveFormIDSetCountForModule)(void* moduleHandle, const char* setName);
    bool (*RegisterHotkeyForModule)(void* moduleHandle, const char* name, int32_t vkCode, HagLoader_HotkeyCb callback, void* user);
    bool (*SetHotkeyForModule)(void* moduleHandle, const char* name, int32_t vkCode);
    bool (*RegisterCellChangeCallbackForModule)(void* moduleHandle, HagLoader_CellChangeCb callback, void* user);
    bool (*PlayIdleWithTargetAutoStop)(uint32_t actorFormID,
                                       uint32_t idleFormID,
                                       uint32_t targetFormID,
                                       uint32_t stopIdleFormID,
                                       uint32_t stopDelayMs);
    bool (*StopIdle)(uint32_t actorFormID, uint32_t stopIdleFormID);
} HagLoaderAPI;

typedef const HagLoaderAPI* (*HagLoader_GetAPIFn)(uint32_t abiVersion);

}  // extern "C"
