#pragma once
#include "PCH.h"
#include "GameOffsets.h"

namespace hag::offsets {

inline constexpr std::uintptr_t kImageBase = game::kImageBase;

inline std::uintptr_t Base() { return reinterpret_cast<std::uintptr_t>(::GetModuleHandleW(nullptr)); }
inline std::uintptr_t FromRVA(std::uintptr_t rva) { return Base() + rva; }

inline constexpr auto kAlloc       = game::Alloc;
inline constexpr auto kHeap        = game::Heap;
inline constexpr auto kCtor1       = game::ScriptCtor1;
inline constexpr auto kCtor2       = game::ScriptCtor2;
inline constexpr auto kScriptVtbl  = game::ScriptVtbl;
inline constexpr auto kCompileRun  = game::ScriptCompileRun;
inline constexpr auto kDtor        = game::ScriptDtor;
inline constexpr auto kCompilerPtr = game::ScriptCompilerPtr;

inline constexpr auto kOffFormType = game::Script_FormType;
inline constexpr auto kOffCompiled = game::Script_Compiled;
inline constexpr auto kOffText     = game::Script_Text;

inline constexpr auto kConsolePtr  = game::ConsolePtr;
inline constexpr auto kConsData    = game::Console_Text;
inline constexpr auto kConsLen     = game::Console_Len;

}  // namespace hag::offsets
