#pragma once

namespace hag {

// Singleton owning external mod startup. SkyrimMod_* exports delegate here.
class Plugin {
public:
    static Plugin& Get();
    void Init(void* page);
    void OnDataLoaded();

private:
    Plugin() = default;
    bool initialized_ = false;
};

}  // namespace hag
