#pragma once
#include "PCH.h"
#include <string>
#include <vector>

// List save games and load one by name, reproducing the console `load` path
// (BGSSaveLoadManager). Reverse-engineered from the disassembly; see project memory.
namespace hag::saveload {

// Save base names (without ".ess") from the game's save folder. Enumerated in-process, so under
// Mod Organizer's VFS this resolves to the active profile's saves. Empty if the folder is missing.
std::vector<std::string> ListSaves();

// Request a load of 'saveName' (base filename, no ".ess"). Returns false if the save-load manager
// isn't available yet or an access violation was caught. Must run on the game main thread when
// in-game; at the main menu the game-update thread is idle so it runs inline.
bool Load(const std::string& saveName);

}  // namespace hag::saveload
