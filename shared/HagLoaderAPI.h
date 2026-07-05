#pragma once
#include <cstdint>

// HagLoaderAPI.h - narrow loader services exported by HagLoader.dll for external mods.
//
// Console access is intentionally queue-only. Mods can request that HagLoader queue a console
// command onto the SKSE main-thread task path; they cannot directly run the console compiler or
// receive synchronous command results through this ABI.

extern "C" {

#define HAGLOADER_ABI_VERSION 4u

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

typedef struct HagLoaderAPI {
    uint32_t abiVersion;
    bool (*QueueConsoleCommand)(const char* command);
    bool (*QueueConsoleCommandWithCallback)(const char* command, HagLoader_ConsoleResultCb callback, void* user);
    bool (*GetConfigBool)(int32_t scope, const char* modName, const char* key, bool defaultValue);
    bool (*SetConfigBool)(int32_t scope, const char* modName, const char* key, bool value);
    bool (*GetConfigBoolForModule)(void* moduleHandle, int32_t scope, const char* configName, const char* key, bool defaultValue);
    bool (*SetConfigBoolForModule)(void* moduleHandle, int32_t scope, const char* configName, const char* key, bool value);
} HagLoaderAPI;

typedef const HagLoaderAPI* (*HagLoader_GetAPIFn)(uint32_t abiVersion);

}  // extern "C"
