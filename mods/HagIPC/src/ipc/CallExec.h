#pragma once
#include "PCH.h"

// Call a game function or run a raw code blob. MUST be invoked on the game main thread
// (route via hag::mt::Run). Both are SEH-guarded: a fault returns false instead of a guaranteed
// crash (best-effort — a blob that corrupts the stack can still bring the game down).
namespace hag::exec {

// Call the function at runtime address 'addr' with n (<=8) integer/pointer args (Microsoft x64:
// RCX,RDX,R8,R9 then stack). Return value = RAX. Float args/returns are NOT supported.
bool Call(std::uintptr_t addr, const std::uint64_t* args, int n, std::uint64_t& out);

// Run a position-independent machine-code blob (entry at byte 0, ends with `ret`, MS x64 ABI).
// Returns RAX.
bool ExecBlob(const std::uint8_t* code, std::size_t len, std::uint64_t& out);

}  // namespace hag::exec
