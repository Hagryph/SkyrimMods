#pragma once
#include "PCH.h"

// HagGeneral global (not save-bound) settings. HagLoader owns the config framework
// and stores this mod's values in HagGeneral.ini under the HagGeneral MO2 mod folder.
namespace hag {

class Config {
public:
    bool fullscreen   = false;   // exclusive fullscreen (else windowed)
    bool borderless   = true;    // borderless windowed (only when not fullscreen)
    bool alwaysActive = false;   // keep the game running/unpaused when the window loses focus
    bool childHostilityUnblocker = false;  // make engine child checks return false while enabled

    static Config& Get();        // process-wide singleton
    void Load();                 // (re)read the INI; write defaults if the file is missing
    void Save() const;           // write the current values back to the INI

private:
    Config() = default;
};

}  // namespace hag
