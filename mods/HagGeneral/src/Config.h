#pragma once
#include "PCH.h"

// HagGeneral global (auto-global, NOT save-bound) settings: a flat INI next to the
// DLL (Data\SKSE\Plugins\HagGeneral.ini under the MO2 VFS). Read on load; auto-saved
// on every change so the checkbox state persists without any manual step.
namespace hag {

class Config {
public:
    bool fullscreen   = false;   // exclusive fullscreen (else windowed)
    bool borderless   = true;    // borderless windowed (only when not fullscreen)
    bool alwaysActive = false;   // keep the game running/unpaused when the window loses focus

    static Config& Get();        // process-wide singleton
    void Load();                 // (re)read the INI; write defaults if the file is missing
    void Save() const;           // write the current values back to the INI

private:
    Config() = default;
};

}  // namespace hag
