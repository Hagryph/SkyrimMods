#include "PCH.h"
#include "SettingsHook.h"
#include "Config.h"
#include "Offsets.h"
#include "Log.h"

#include <cstdint>

// Repoint the game's five setting reads to flag bytes we own. Each read is a 7-byte instruction with
// a 32-bit RIP-relative displacement; we allocate a page near the module (so a disp32 can reach it)
// and rewrite each displacement to target one of our flag bytes. The game then reads OUR bytes:
//   flag[0] <- 3 bAlwaysActive reads (pause check x2 + SetActive)
//   flag[1] <- bFull Screen read (startup window/swapchain build)
//   flag[2] <- bBorderless read  (startup window/swapchain build)
// No trampoline, no per-frame work; nothing external can change what these reads see.
namespace hag {
namespace {

enum { FLAG_AA = 0, FLAG_FS = 1, FLAG_BORDER = 2, FLAG_COUNT = 3 };
volatile std::uint8_t* g_page = nullptr;   // our flag bytes (near the module), read by the game

// Reserve+commit one RW page within ~2GB of `anchor` so a disp32 from a patched instruction reaches
// it. We start a few MB away from the module and scan outward, to avoid the pages immediately
// adjacent to the image (which the loader/startup may still touch).
void* AllocNear(std::uintptr_t anchor) {
    SYSTEM_INFO si{}; ::GetSystemInfo(&si);
    const std::uintptr_t gran = si.dwAllocationGranularity ? si.dwAllocationGranularity : 0x10000;
    const std::uintptr_t reach = 0x7ff00000ull;         // stay comfortably inside +-2GB
    const std::uintptr_t skip  = 0x400000ull;           // begin ~4MB out from the module
    const std::uintptr_t base  = (anchor - skip) & ~(gran - 1);
    for (std::uintptr_t i = 0; i < 0x8000; ++i) {
        const std::uintptr_t down = base - i * gran;
        if (down > gran && anchor - down < reach)
            if (void* p = ::VirtualAlloc(reinterpret_cast<void*>(down), 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) return p;
        const std::uintptr_t up = (anchor + skip) + i * gran;
        if (up - anchor < reach)
            if (void* p = ::VirtualAlloc(reinterpret_cast<void*>(up), 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) return p;
    }
    return nullptr;
}

// Rewrite the disp32 at `instrRVA + dispOff` so the RIP-relative read targets `flag`.
bool Repoint(std::uintptr_t instrRVA, int dispOff, void* flag) {
    const std::uintptr_t instr  = offsets::FromRVA(instrRVA);
    const std::uintptr_t ripEnd = instr + offsets::kAAReadLen;          // disp is relative to next instr (all reads are 7 bytes)
    const std::intptr_t  rel    = reinterpret_cast<std::uintptr_t>(flag) - ripEnd;
    if (rel > INT32_MAX || rel < INT32_MIN) { HAG_ERR("SettingsHook: disp out of range for @{:#x}", instr); return false; }
    const std::int32_t   disp   = static_cast<std::int32_t>(rel);
    void* patchAt = reinterpret_cast<void*>(instr + dispOff);
    DWORD old = 0;
    if (!::VirtualProtect(patchAt, sizeof disp, PAGE_EXECUTE_READWRITE, &old)) { HAG_ERR("SettingsHook: VirtualProtect failed @{:#x}", instr); return false; }
    std::memcpy(patchAt, &disp, sizeof disp);
    ::VirtualProtect(patchAt, sizeof disp, old, &old);
    ::FlushInstructionCache(::GetCurrentProcess(), reinterpret_cast<void*>(instr), offsets::kAAReadLen);
    return true;
}

void WriteFlags() {
    if (!g_page) return;
    const Config& c = Config::Get();
    g_page[FLAG_AA]     = c.alwaysActive ? 1 : 0;
    g_page[FLAG_FS]     = c.fullscreen   ? 1 : 0;
    g_page[FLAG_BORDER] = c.borderless   ? 1 : 0;
}

}  // namespace

bool SettingsHook::Install() {
    void* page = AllocNear(offsets::FromRVA(offsets::kAARead1));
    if (!page) { HAG_ERR("SettingsHook: could not allocate a flag page near the module"); return false; }
    g_page = reinterpret_cast<volatile std::uint8_t*>(page);
    WriteFlags();

    std::uint8_t* aa = reinterpret_cast<std::uint8_t*>(page) + FLAG_AA;
    std::uint8_t* fs = reinterpret_cast<std::uint8_t*>(page) + FLAG_FS;
    std::uint8_t* bd = reinterpret_cast<std::uint8_t*>(page) + FLAG_BORDER;

    const bool ok =
        Repoint(offsets::kAARead1, offsets::kAARead1Disp, aa) &
        Repoint(offsets::kAARead2, offsets::kAARead2Disp, aa) &
        Repoint(offsets::kAARead3, offsets::kAARead3Disp, aa) &
        Repoint(offsets::kFullScreenRead, offsets::kFullScreenDisp, fs) &
        Repoint(offsets::kBorderlessRead, offsets::kBorderlessDisp, bd);
    if (!ok) { HAG_ERR("SettingsHook: failed to repoint one or more setting reads"); return false; }

    HAG_INFO("SettingsHook: flags @{} (aa={} fs={} border={}); repointed 3 bAlwaysActive + 2 display reads",
             page, static_cast<int>(g_page[FLAG_AA]), static_cast<int>(g_page[FLAG_FS]), static_cast<int>(g_page[FLAG_BORDER]));
    return true;
}

void SettingsHook::Apply() {
    WriteFlags();
    if (g_page)
        HAG_INFO("SettingsHook: flags now aa={} fs={} border={}",
                 static_cast<int>(g_page[FLAG_AA]), static_cast<int>(g_page[FLAG_FS]), static_cast<int>(g_page[FLAG_BORDER]));
}

}  // namespace hag
