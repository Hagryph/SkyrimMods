#pragma once
#include "PCH.h"
#include "GameOffsets.h"  // shared raw offset table. THIS is the ONLY file that may include it.

// Per-module base + RVA helpers and the offset names HagGeneral uses, re-exported from the
// shared single source of truth (shared/GameOffsets.h :: namespace game). All other HagGeneral
// files include THIS header and use offsets:: names — none include GameOffsets.h directly.
namespace hag::offsets {

inline constexpr std::uintptr_t kImageBase = game::kImageBase;

inline std::uintptr_t Base() { return reinterpret_cast<std::uintptr_t>(::GetModuleHandleW(nullptr)); }
inline std::uintptr_t FromRVA(std::uintptr_t rva)      { return Base() + rva; }
inline std::uintptr_t FromImageAddr(std::uintptr_t fa) { return Base() + (fa - kImageBase); }

// ---- Always Active: the RIP-relative bAlwaysActive read instructions we repoint to our own flag ----
inline constexpr auto kAARead1     = game::alwaysactive::Read1;
inline constexpr auto kAARead2     = game::alwaysactive::Read2;
inline constexpr auto kAARead3     = game::alwaysactive::Read3;
inline constexpr auto kAARead1Disp = game::alwaysactive::Read1Disp;
inline constexpr auto kAARead2Disp = game::alwaysactive::Read2Disp;
inline constexpr auto kAARead3Disp = game::alwaysactive::Read3Disp;
inline constexpr auto kAAReadLen   = game::alwaysactive::ReadLen;

// ---- Display: the two startup reads we repoint to our fullscreen/borderless flag bytes ----
inline constexpr auto kFullScreenRead = game::display::FullScreenRead;
inline constexpr auto kBorderlessRead = game::display::BorderlessRead;
inline constexpr auto kFullScreenDisp = game::display::FullScreenDisp;
inline constexpr auto kBorderlessDisp = game::display::BorderlessDisp;

}  // namespace hag::offsets
