#pragma once

// SettingsHook — the "read-time redirect" for all three settings. Instead of writing the game's own
// setting values (which other writers race us on) or no-opping functions, we change WHICH variable
// the game reads: we allocate our own flag bytes near the game module and rewrite the RIP-relative
// displacement of the instructions that read bAlwaysActive / bFull Screen / bBorderless so they read
// OUR bytes. Then only HagGeneral drives them; a console SetINISetting or another mod touching those
// settings is simply not read anymore. The display reads happen once at startup (window/swapchain
// creation); Always Active is read every frame by the pause check. The D3D swapchain hook is kept
// only to capture the swapchain/window for LIVE in-session display switching.
namespace hag {

class SettingsHook {
public:
    static bool Install();   // alloc flag bytes + repoint the 5 reads (call at SKSEPlugin_Load)
    static void Apply();     // write the flag bytes from Config (call after Install + on every toggle)
};

}  // namespace hag
