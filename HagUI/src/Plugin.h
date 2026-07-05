#pragma once

namespace skse { struct Interface; }

namespace hag {

// Singleton owning plugin startup. SKSE's C exports delegate here.
class Plugin {
public:
    static Plugin& Get();
    bool OnLoad(const skse::Interface* skse);

private:
    Plugin() = default;
};

}  // namespace hag
