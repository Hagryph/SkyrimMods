#pragma once
#include <cstdint>

// Minimal, hand-written SKSE plugin ABI for Skyrim SE 1.6.x (AE / post-629).
// No CommonLibSSE, no Address Library — SKSE is used purely as a DLL loader.
namespace skse {

// SKSE packs a runtime version as (major<<24)|(minor<<16)|(build<<4)|sub.
constexpr std::uint32_t MakeVersion(std::uint32_t major, std::uint32_t minor,
                                    std::uint32_t build, std::uint32_t sub = 0) {
    return (major << 24) | (minor << 16) | (build << 4) | sub;
}
constexpr std::uint32_t kRuntime_1_6_1170 = MakeVersion(1, 6, 1170);  // 0x01064920

// The data export SKSE reads to decide whether/how to load us.
struct PluginVersionData {
    enum { kVersion = 1 };

    // versionIndependence bits (we set NONE — we pin exact runtimes instead).
    enum {
        kIndependent_AddressLibraryPostAE = 1 << 0,
        kIndependent_Signatures           = 1 << 1,
        kIndependent_StructsPost629       = 1 << 2,
    };

    std::uint32_t dataVersion;            // == kVersion
    std::uint32_t pluginVersion;          // our own version
    char          name[256];
    char          author[256];
    char          supportEmail[252];
    std::uint32_t versionIndependenceEx;
    std::uint32_t versionIndependence;
    std::uint32_t compatibleVersions[16]; // 0-terminated list of supported runtimes
    std::uint32_t seVersionRequired;      // minimum SKSE version, 0 = any
};
static_assert(sizeof(PluginVersionData) == 848);  // 0x350

// Passed to SKSEPlugin_Load.
struct Interface {
    std::uint32_t skseVersion;
    std::uint32_t runtimeVersion;
    std::uint32_t editorVersion;
    std::uint32_t isEditor;
    void*       (*QueryInterface)(std::uint32_t id);
    std::uint32_t (*GetPluginHandle)();
    std::uint32_t (*GetReleaseIndex)();
    const void* (*GetPluginInfo)(const char* name);
};

// QueryInterface(id) ids
constexpr std::uint32_t kInterface_Messaging = 5;

// SKSE -> plugin messages (sender == "SKSE")
enum : std::uint32_t {
    kMessage_PostLoad = 0,
    kMessage_PostPostLoad,
    kMessage_PreLoadGame,
    kMessage_PostLoadGame,
    kMessage_SaveGame,
    kMessage_DeleteGame,
    kMessage_InputLoaded,
    kMessage_NewGame,
    kMessage_DataLoaded,   // 8 — game data fully loaded; UI is up
};

struct Message {
    const char*   sender;
    std::uint32_t type;
    std::uint32_t dataLen;
    void*         data;
};
using EventCallback = void (*)(Message* msg);

struct MessagingInterface {
    std::uint32_t interfaceVersion;
    bool  (*RegisterListener)(std::uint32_t listenerHandle, const char* sender, EventCallback handler);
    bool  (*Dispatch)(std::uint32_t senderHandle, std::uint32_t type, void* data, std::uint32_t dataLen, const char* receiver);
    void* (*GetEventDispatcher)(std::uint32_t dispatcherId);
};

}  // namespace skse
