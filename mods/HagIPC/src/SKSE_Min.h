#pragma once
#include <cstdint>

// Minimal, hand-written SKSE plugin ABI for Skyrim SE 1.6.x (AE / post-629).
// No CommonLibSSE, no Address Library — SKSE is used purely as a DLL loader + a
// handful of interfaces (messaging, task marshaling, co-save serialization).
namespace skse {

constexpr std::uint32_t MakeVersion(std::uint32_t major, std::uint32_t minor,
                                    std::uint32_t build, std::uint32_t sub = 0) {
    return (major << 24) | (minor << 16) | (build << 4) | sub;
}
constexpr std::uint32_t kRuntime_1_6_1170 = MakeVersion(1, 6, 1170);  // 0x01064920

struct PluginVersionData {
    enum { kVersion = 1 };
    enum {
        kIndependent_AddressLibraryPostAE = 1 << 0,
        kIndependent_Signatures           = 1 << 1,
        kIndependent_StructsPost629       = 1 << 2,
    };
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

// QueryInterface(id) ids (SKSE64 ordering — confirmed: Messaging == 5 in HagUI)
constexpr std::uint32_t kInterface_Serialization = 3;
constexpr std::uint32_t kInterface_Task          = 4;
constexpr std::uint32_t kInterface_Messaging     = 5;

// ---- Messaging ----
enum : std::uint32_t {
    kMessage_PostLoad = 0,
    kMessage_PostPostLoad,
    kMessage_PreLoadGame,
    kMessage_PostLoadGame,
    kMessage_SaveGame,
    kMessage_DeleteGame,
    kMessage_InputLoaded,
    kMessage_NewGame,
    kMessage_DataLoaded,   // 8 — game data fully loaded
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

// ---- Task (main-thread marshaling) ----
// SKSE runs the task's vtable[0]=Run on the game main thread, then vtable[1]=Dispose.
// CRITICAL ABI: the original is a pure-virtual interface with NO destructor, so slot 0 is
// Run and slot 1 is Dispose. A C++ class with a virtual dtor would insert a deleting-dtor at
// slot 0 and shift Run/Dispose to 1/2 -> SKSE calls the wrong slot -> crash. So use a plain
// struct with a MANUAL vtable, and never delete it ourselves (SKSE calls Dispose).
struct TaskDelegate {
    struct VTbl {
        void (*Run)(TaskDelegate* self);
        void (*Dispose)(TaskDelegate* self);
    };
    const VTbl* vtbl;
};
struct TaskInterface {
    std::uint32_t interfaceVersion;          // == 2
    void (*AddTask)(TaskDelegate* task);     // +0x08  game main-thread tasklet pass
    void (*AddUITask)(void* task);           // +0x10  UI pass (unused)
};

}  // namespace skse
