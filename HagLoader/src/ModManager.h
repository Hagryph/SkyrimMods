#pragma once
#include "PCH.h"

#include <vector>

namespace hag {

// Loads external mod DLLs from the normal SKSE\Plugins folder and gives each contract mod one
// HagUI page. Non-contract SKSE plugins are ignored.
class ModManager {
public:
    static ModManager& Get();

    void LoadAll();
    void OnDataLoaded();
    void OnSaveLoaded();

    ModManager(const ModManager&) = delete;
    ModManager& operator=(const ModManager&) = delete;

private:
    ModManager() = default;

    struct LoadedMod {
        HMODULE module = nullptr;
        void (*onDataLoaded)() = nullptr;
        void (*onSaveLoaded)() = nullptr;
    };

    bool loaded_ = false;
    std::vector<LoadedMod> mods_;
};

}  // namespace hag
