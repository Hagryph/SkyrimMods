#pragma once
#include <cstdint>

// HagLoaderAPI.h - narrow loader services exported by HagLoader.dll for external mods.
//
// Console access is intentionally queue-only. Mods can request that HagLoader queue a console
// command onto the SKSE main-thread task path; they cannot directly run the console compiler or
// receive synchronous command results through this ABI.

extern "C" {

#define HAGLOADER_ABI_VERSION 2u

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
} HagLoaderAPI;

typedef const HagLoaderAPI* (*HagLoader_GetAPIFn)(uint32_t abiVersion);

}  // extern "C"
