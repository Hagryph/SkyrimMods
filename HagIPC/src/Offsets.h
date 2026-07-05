#pragma once
#include "PCH.h"
#include "GameOffsets.h"  // shared raw offset table. THIS is the ONLY file that may include it.

// Per-module base + RVA helpers, and every offset name HagIPC uses, re-exported from the shared
// single source of truth (shared/GameOffsets.h :: namespace game). All other HagIPC files include
// THIS header and use offsets:: names — none of them include GameOffsets.h directly.
namespace hag::offsets {

inline constexpr std::uintptr_t kImageBase = game::kImageBase;

inline std::uintptr_t Base() { return reinterpret_cast<std::uintptr_t>(::GetModuleHandleW(nullptr)); }
inline std::uintptr_t FromRVA(std::uintptr_t rva)      { return Base() + rva; }
inline std::uintptr_t FromImageAddr(std::uintptr_t fa) { return Base() + (fa - kImageBase); }

// ---- game allocator ----
inline constexpr auto kAlloc       = game::Alloc;
inline constexpr auto kHeap        = game::Heap;

// ---- console command execution (Script build) ----
inline constexpr auto kCtor1       = game::ScriptCtor1;
inline constexpr auto kCtor2       = game::ScriptCtor2;
inline constexpr auto kCompileRun  = game::ScriptCompileRun;
inline constexpr auto kDtor        = game::ScriptDtor;
inline constexpr auto kScriptVtbl  = game::ScriptVtbl;
inline constexpr auto kCompilerPtr = game::ScriptCompilerPtr;
inline constexpr auto kOffFormType = game::Script_FormType;
inline constexpr auto kOffCompiled = game::Script_Compiled;
inline constexpr auto kOffText     = game::Script_Text;

// ---- console output buffer ----
inline constexpr auto kConsolePtr  = game::ConsolePtr;
inline constexpr auto kConsData    = game::Console_Text;
inline constexpr auto kConsLen     = game::Console_Len;

// ---- save / load ----
inline constexpr auto kManagerPtr  = game::SaveLoadManagerPtr;
inline constexpr auto kBeginLoad   = game::SaveLoad_BeginLoad;
inline constexpr auto kLoad        = game::SaveLoad_Load;
inline constexpr auto kLoadFlag    = game::SaveLoad_LoadFlag;
inline constexpr auto kSaveDirSng  = game::SaveDirSingleton;
inline constexpr auto kSaveDirSlot = game::SaveDir_VtblSlot;

}  // namespace hag::offsets
