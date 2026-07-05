#pragma once
#include <cstdint>

namespace skse {

constexpr std::uint32_t MakeVersion(std::uint32_t major, std::uint32_t minor,
                                    std::uint32_t build, std::uint32_t sub = 0) {
    return (major << 24) | (minor << 16) | (build << 4) | sub;
}
constexpr std::uint32_t kRuntime_1_6_1170 = MakeVersion(1, 6, 1170);

struct PluginVersionData {
    enum { kVersion = 1 };

    std::uint32_t dataVersion;
    std::uint32_t pluginVersion;
    char          name[256];
    char          author[256];
    char          supportEmail[252];
    std::uint32_t versionIndependenceEx;
    std::uint32_t versionIndependence;
    std::uint32_t compatibleVersions[16];
    std::uint32_t seVersionRequired;
};
static_assert(sizeof(PluginVersionData) == 848);

struct Interface;

}  // namespace skse
