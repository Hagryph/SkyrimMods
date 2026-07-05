#pragma once
#include <cstdint>

// HagLoaderAPI.h - generic services exported by HagLoader.dll for external mods.
//
// HagUIAPI remains the UI/widget API. This table is for loader-level capabilities that many mods
// may need, such as running a console/Papyrus command through Skyrim's own Script CompileAndRun path.

extern "C" {

#define HAGLOADER_ABI_VERSION 1u

typedef struct HagLoader_ConsoleResult {
    bool     faulted;
    bool     noCompiler;
    bool     compiled;
    uint32_t compiledSize;
    const char* output;  // valid until the next RunConsoleCommand call on this thread
} HagLoader_ConsoleResult;

typedef struct HagLoaderAPI {
    uint32_t abiVersion;

    bool (*RunConsoleCommand)(const char* command, HagLoader_ConsoleResult* result);
} HagLoaderAPI;

typedef const HagLoaderAPI* (*HagLoader_GetAPIFn)(uint32_t abiVersion);

}  // extern "C"
