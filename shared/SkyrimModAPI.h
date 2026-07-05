#pragma once
#include <cstdint>

// SkyrimModAPI.h - contract between HagLoader (loader + UI host) and external mod DLLs.
//
// HagLoader.dll owns the HagUI panel, enumerates the normal Skyrim SKSE plugin folder
// (Data\SKSE\Plugins, merged by MO2), creates one HagUI page per DLL that exports this contract,
// then calls the mod's optional SkyrimMod_Init export with that page handle. External mods keep
// DllMain trivial and put real startup work in SkyrimMod_Init, which runs after LoadLibrary returns.

extern "C" {

constexpr std::uint32_t SKYRIMMOD_ABI_VERSION = 1;

enum SkyrimModScope {
    SKYRIMMOD_GLOBAL = 0,
    SKYRIMMOD_PERSAVE = 1,
};

typedef const char* (*SkyrimMod_Name_t)();
typedef int (*SkyrimMod_Scope_t)();
typedef void (*SkyrimMod_Init_t)(void* page);
typedef void (*SkyrimMod_OnDataLoaded_t)();

}  // extern "C"
