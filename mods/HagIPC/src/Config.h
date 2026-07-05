#pragma once
#include "PCH.h"

// HagIPC global (NOT save-bound) config. A tiny flat INI in the SKSE user dir
// (Documents/My Games/Skyrim Special Edition/SKSE/HagIPC.ini). Created with
// documented defaults on first run; the user edits it, we re-read on load.
namespace hag {

struct Config {
    bool          enabled = true;        // dev tool: on by default for the dev's own setup
    std::uint16_t port    = 19000;       // localhost TCP port
    std::string   token;                 // optional shared secret ("" = no auth)

    // Load (creating the file with defaults if missing). Returns the loaded config.
    static Config Load();
};

}  // namespace hag
