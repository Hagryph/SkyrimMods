#include "PCH.h"

#include "HagLoaderAPI.h"
#include "HagUIAPI.h"
#include "GameOffsets.h"
#include "Log.h"
#include "SKSE_Min.h"
#include "SkyrimModAPI.h"

#include <algorithm>
#include <atomic>
#include <stdexcept>

namespace hag {

namespace {

bool g_initialized = false;
const HagLoaderAPI* g_loaderApi = nullptr;
const HagUIAPI* g_uiApi = nullptr;
HagUI_PageHandle* g_page = nullptr;
HMODULE g_selfModule = nullptr;

enum class VampireAction {
    Transform,
    Cure,
};

std::atomic_bool g_pageActionConsumed{false};
VampireAction g_actionAtPageBuild = VampireAction::Transform;
std::atomic_bool g_corpseFeedingEnabled{true};
std::atomic_bool g_nativeFeedDebug{true};
std::atomic_int g_animationMode{0};

constexpr const char* kConfigName = "HagVampire";
constexpr std::int32_t kConfigScope = HAGLOADER_CONFIG_PERSAVE;
constexpr std::uint32_t kFormFlagDeleted = 1u << 5;
constexpr std::uint32_t kFormFlagDisabled = 1u << 11;
constexpr std::uint8_t kFormTypeActorCharacter = 0x3E;
constexpr std::uint32_t kPlayerRefID = 0x14;

struct TargetInfo {
    void* actor = nullptr;
    std::uint32_t handle = 0;
    std::uint32_t formID = 0;
    std::uint8_t formType = 0;
};

std::uintptr_t SkyrimBase() {
    return reinterpret_cast<std::uintptr_t>(::GetModuleHandleW(nullptr));
}

bool IsPlayerVampire() {
    __try {
        using LookupByIdFn = void* (*)(std::uint32_t);
        auto lookup = reinterpret_cast<LookupByIdFn>(SkyrimBase() + game::form::LookupByID);
        void* form = lookup ? lookup(0x000ED06D) : nullptr;
        if (!form) return false;
        auto* bytes = static_cast<std::uint8_t*>(form);
        if (bytes[game::form::FormType] != 0x09) return false;
        const float value = *reinterpret_cast<float*>(bytes + 0x34);
        return value >= 0.5f;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ConfigGetBool(const char* key, bool defaultValue) {
    if (!g_loaderApi || !g_loaderApi->GetConfigBoolForModule) return defaultValue;
    return g_loaderApi->GetConfigBoolForModule(g_selfModule, kConfigScope, kConfigName, key, defaultValue);
}

void ConfigSetBool(const char* key, bool value) {
    if (!g_loaderApi || !g_loaderApi->SetConfigBoolForModule) return;
    if (!g_loaderApi->SetConfigBoolForModule(g_selfModule, kConfigScope, kConfigName, key, value)) {
        HAG_ERR("failed to save config {}={}", key ? key : "", value);
    }
}

int ConfigGetInt(const char* key, int defaultValue) {
    if (!g_loaderApi || !g_loaderApi->GetConfigIntForModule) return defaultValue;
    const auto raw = g_loaderApi->GetConfigIntForModule(g_selfModule, kConfigScope, kConfigName, key, defaultValue);
    return static_cast<int>(std::clamp<std::int64_t>(raw, 0, 2));
}

void ConfigSetInt(const char* key, int value) {
    if (!g_loaderApi || !g_loaderApi->SetConfigIntForModule) return;
    if (!g_loaderApi->SetConfigIntForModule(g_selfModule, kConfigScope, kConfigName, key, value)) {
        HAG_ERR("failed to save config {}={}", key ? key : "", value);
    }
}

void LoadConfig() {
    g_corpseFeedingEnabled.store(ConfigGetBool("enable_corpse_feeding", true));
    g_nativeFeedDebug.store(ConfigGetBool("native_feed_debug", true));
    g_animationMode.store(ConfigGetInt("animation_mode", 0));
    HAG_INFO("config loaded: enable_corpse_feeding={} native_feed_debug={} animation_mode={}",
             g_corpseFeedingEnabled.load(), g_nativeFeedDebug.load(), g_animationMode.load());
}

void* GetPlayerActorGuarded() noexcept {
    __try {
        return *reinterpret_cast<void**>(SkyrimBase() + game::actor::PlayerSingletonPtr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

std::uint32_t ReadU32Guarded(void* ptr, std::size_t offset, std::uint32_t fallback = 0) noexcept {
    __try {
        return *reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(ptr) + offset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return fallback;
    }
}

std::uint8_t ReadU8Guarded(void* ptr, std::size_t offset, std::uint8_t fallback = 0) noexcept {
    __try {
        return *reinterpret_cast<std::uint8_t*>(static_cast<std::uint8_t*>(ptr) + offset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return fallback;
    }
}

void* ResolveRefHandleRawGuarded(std::uint32_t handle) noexcept {
    if (handle == 0) return nullptr;
    __try {
        using ResolveFn = void (*)(std::uint32_t*, void**);
        auto resolve = reinterpret_cast<ResolveFn>(SkyrimBase() + game::ResolveRefHandle);
        void* ref = nullptr;
        std::uint32_t localHandle = handle;
        resolve(&localHandle, &ref);
        return ref;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool GetCrosshairTargetActorGuarded(TargetInfo* out) noexcept {
    if (!out) return false;
    __try {
        auto** singleton = reinterpret_cast<std::uint8_t**>(
            SkyrimBase() + game::crosshair::CrosshairPickDataSingletonPtr);
        auto* pickData = singleton ? *singleton : nullptr;
        if (!pickData) return false;

        const auto actorHandle = *reinterpret_cast<std::uint32_t*>(
            pickData + game::crosshair::CrosshairPickData_TargetActor);
        const auto targetHandle = *reinterpret_cast<std::uint32_t*>(
            pickData + game::crosshair::CrosshairPickData_Target);
        out->handle = actorHandle ? actorHandle : targetHandle;
        out->actor = ResolveRefHandleRawGuarded(out->handle);
        if (!out->actor) return false;
        out->formID = ReadU32Guarded(out->actor, 0x14);
        out->formType = ReadU8Guarded(out->actor, game::form::FormType);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool HasCurrent3DGuarded(void* ref, bool* has3D) noexcept {
    if (has3D) *has3D = false;
    if (!ref) return false;
    __try {
        using Get3DFn = void* (*)(void*);
        auto** vtbl = *reinterpret_cast<void***>(ref);
        auto get3D = reinterpret_cast<Get3DFn>(vtbl[game::refr::VSlot_GetCurrent3D]);
        if (has3D) *has3D = get3D(ref) != nullptr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool IsDeadGuarded(void* actor, bool* dead) noexcept {
    if (dead) *dead = false;
    if (!actor) return false;
    __try {
        using IsDeadFn = bool (*)(void*, bool);
        auto** vtbl = *reinterpret_cast<void***>(actor);
        auto isDead = reinterpret_cast<IsDeadFn>(vtbl[game::actor::VSlot_IsDead]);
        if (dead) *dead = isDead(actor, true);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool CallNativeVampireFeedGuarded(void* player, void* target) noexcept {
    if (!player || !target) return false;
    __try {
        auto** vtbl = *reinterpret_cast<void***>(player);
        using SetVampireFeedFn = void (*)(void*, bool);
        using InitiateFeedFn = void (*)(void*, void*, void*);
        auto setVampireFeed = reinterpret_cast<SetVampireFeedFn>(
            vtbl[game::actor::VSlot_SetVampireFeed]);
        auto initiateFeed = reinterpret_cast<InitiateFeedFn>(
            vtbl[game::actor::VSlot_InitiateVampireFeedPackage]);
        setVampireFeed(player, true);
        initiateFeed(player, target, nullptr);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

const char* ValidateFeedTarget(const TargetInfo& target) {
    if (!target.actor) return "no crosshair actor target";
    if (target.formType != kFormTypeActorCharacter) return "crosshair target is not an actor";
    if (target.formID == kPlayerRefID) return "crosshair target is the player";

    const std::uint32_t flags = ReadU32Guarded(target.actor, 0x10);
    if ((flags & kFormFlagDeleted) != 0) return "crosshair actor is deleted";
    if ((flags & kFormFlagDisabled) != 0) return "crosshair actor is disabled";

    bool has3D = false;
    if (!HasCurrent3DGuarded(target.actor, &has3D)) return "crosshair actor 3D check faulted";
    if (!has3D) return "crosshair actor has no loaded 3D";

    bool dead = false;
    if (!IsDeadGuarded(target.actor, &dead)) return "crosshair actor death check faulted";
    if (!dead) return "crosshair actor is not dead";

    return nullptr;
}

void RunNativeFeedTask(void*) {
    HAG_INFO("native corpse-feed debug task started");
    if (!g_corpseFeedingEnabled.load()) {
        HAG_INFO("native corpse-feed debug aborted: corpse feeding disabled");
        return;
    }
    if (!g_nativeFeedDebug.load()) {
        HAG_INFO("native corpse-feed debug aborted: debug disabled");
        return;
    }

    const int mode = g_animationMode.load();
    if (mode != 0) {
        HAG_INFO("native corpse-feed debug: animation_mode={} is reserved in v1; using native package", mode);
    }

    void* player = GetPlayerActorGuarded();
    if (!player) {
        HAG_ERR("native corpse-feed debug aborted: player actor unavailable");
        return;
    }

    TargetInfo target{};
    if (!GetCrosshairTargetActorGuarded(&target)) {
        HAG_INFO("native corpse-feed debug aborted: no resolvable crosshair target");
        return;
    }

    const char* rejection = ValidateFeedTarget(target);
    if (rejection) {
        HAG_INFO("native corpse-feed debug rejected handle={:#x} form={:#x} type={:#x}: {}",
                 target.handle, target.formID, target.formType, rejection);
        return;
    }

    HAG_INFO("native corpse-feed debug invoking feed: target handle={:#x} form={:#x}",
             target.handle, target.formID);
    if (!CallNativeVampireFeedGuarded(player, target.actor)) {
        HAG_ERR("native corpse-feed debug failed: native feed call faulted");
        return;
    }
    HAG_INFO("native corpse-feed debug native feed call completed");
}

const char* VampireActionLabel(void*) {
    g_actionAtPageBuild = IsPlayerVampire() ? VampireAction::Cure : VampireAction::Transform;
    g_pageActionConsumed.store(false);
    return g_actionAtPageBuild == VampireAction::Cure ? "Cure Vampirism" : "Transform into a Vampire";
}

void OnCorpseFeedingChanged(void*, HagUI_Value value) {
    if (value.type != HAGUI_VT_BOOL) return;
    g_corpseFeedingEnabled.store(value.b);
    ConfigSetBool("enable_corpse_feeding", value.b);
    HAG_INFO("enable_corpse_feeding -> {}", value.b);
}

void OnNativeFeedDebugChanged(void*, HagUI_Value value) {
    if (value.type != HAGUI_VT_BOOL) return;
    g_nativeFeedDebug.store(value.b);
    ConfigSetBool("native_feed_debug", value.b);
    HAG_INFO("native_feed_debug -> {} (debug button visibility updates on next load)", value.b);
}

void OnAnimationModeChanged(void*, HagUI_Value value) {
    int next = 0;
    if (value.type == HAGUI_VT_INT) {
        next = static_cast<int>(value.i);
    } else if (value.type == HAGUI_VT_DOUBLE) {
        next = static_cast<int>(value.d);
    } else {
        return;
    }

    next = std::clamp(next, 0, 2);
    g_animationMode.store(next);
    ConfigSetInt("animation_mode", next);
    HAG_INFO("animation_mode -> {}", next);
}

void OnNativeFeedDebugClicked(void*) {
    if (!g_loaderApi || !g_loaderApi->QueueMainThreadTask) {
        HAG_ERR("native corpse-feed debug failed: HagLoader main-thread task API unavailable");
        return;
    }
    if (!g_loaderApi->QueueMainThreadTask(&RunNativeFeedTask, nullptr)) {
        HAG_ERR("native corpse-feed debug failed: could not queue main-thread task");
        return;
    }
    HAG_INFO("native corpse-feed debug queued");
}

void OnActionResult(void*, const HagLoader_PapyrusResult* result) {
    if (!result) {
        HAG_ERR("vampire action result callback received null result");
        return;
    }
    if (result->faulted) {
        HAG_ERR("vampire action Papyrus call faulted");
        return;
    }
    if (!result->dispatched) {
        HAG_ERR("vampire action Papyrus call was not dispatched: '{}'",
                result->message ? result->message : "");
        return;
    }
    HAG_INFO("vampire action Papyrus call dispatched: '{}'",
             result->message ? result->message : "");
}

void OnVampireActionClicked(void*) {
    if (g_pageActionConsumed.exchange(true)) {
        HAG_INFO("vampire action ignored: page-build action already queued");
        return;
    }

    constexpr const char* kScript = "HagVampireBridge";
    const char* function = g_actionAtPageBuild == VampireAction::Cure ? "CurePlayer" : "TransformPlayer";
    if (!g_loaderApi || !g_loaderApi->QueuePapyrusStaticCallWithCallback) {
        HAG_ERR("vampire action failed: HagLoader Papyrus API unavailable");
        g_pageActionConsumed.store(false);
        return;
    }
    if (!g_loaderApi->QueuePapyrusStaticCallWithCallback(kScript, function, &OnActionResult, nullptr)) {
        HAG_ERR("vampire action failed: could not queue {}.{}()", kScript, function);
        g_pageActionConsumed.store(false);
        return;
    }
    HAG_INFO("vampire action Papyrus bridge queued: {}.{}()", kScript, function);
}

}  // namespace

void SetModule(HMODULE module) {
    g_selfModule = module;
}

void Init(HagUI_PageHandle* page) {
    HMODULE h = ::GetModuleHandleW(L"HagLoader.dll");
    if (!h) { throw std::runtime_error("HagVampire requires HagLoader.dll"); }

    if (g_initialized) return;
    g_initialized = true;

    Log::Init("HagVampire");
    HAG_INFO("HagVampire loading");

    auto getLoaderApi = reinterpret_cast<HagLoader_GetAPIFn>(::GetProcAddress(h, "HagLoader_GetAPI"));
    g_loaderApi = getLoaderApi ? getLoaderApi(HAGLOADER_ABI_VERSION) : nullptr;
    if (!g_loaderApi || !g_loaderApi->QueuePapyrusStaticCallWithCallback || !g_loaderApi->QueueMainThreadTask) {
        throw std::runtime_error("HagVampire requires HagLoader Papyrus API");
    }
    LoadConfig();

    auto getApi = reinterpret_cast<HagUI_GetAPIFn>(h ? ::GetProcAddress(h, "HagUI_GetAPI") : nullptr);
    g_uiApi = getApi ? getApi(HAGUI_ABI_VERSION) : nullptr;
    g_page = page;
    if (!g_uiApi || !page) {
        throw std::runtime_error("HagVampire requires HagLoader HagUI API/page");
    }

    if (!g_uiApi->AddDynamicButton) {
        throw std::runtime_error("HagVampire requires HagUI dynamic button API");
    }

    g_uiApi->AddDynamicButton(page,
                              "vampire_action",
                              "Transform into a Vampire",
                              &VampireActionLabel,
                              &OnVampireActionClicked,
                              nullptr);
    g_uiApi->AddToggle(page,
                       "enable_corpse_feeding",
                       "Enable corpse feeding",
                       g_corpseFeedingEnabled.load(),
                       &OnCorpseFeedingChanged,
                       nullptr);
    g_uiApi->AddToggle(page,
                       "native_feed_debug",
                       "Native feed debug",
                       g_nativeFeedDebug.load(),
                       &OnNativeFeedDebugChanged,
                       nullptr);
    g_uiApi->AddStepper(page,
                        "animation_mode",
                        "Animation mode (0 Native / 1 Idle / 2 Custom)",
                        0.0,
                        2.0,
                        1.0,
                        static_cast<double>(g_animationMode.load()),
                        &OnAnimationModeChanged,
                        nullptr);
    if (g_nativeFeedDebug.load()) {
        g_uiApi->AddButton(page,
                           "debug_feed_crosshair_corpse",
                           "Debug: Feed crosshair corpse",
                           &OnNativeFeedDebugClicked,
                           nullptr);
    }
    g_uiApi->Refresh();

    HAG_INFO("HagVampire page registered");
}

}  // namespace hag

extern "C" __declspec(dllexport) const char* SkyrimMod_Name() {
    return "Vampire";
}

extern "C" __declspec(dllexport) int SkyrimMod_Scope() {
    return SKYRIMMOD_PERSAVE;
}

extern "C" __declspec(dllexport) void SkyrimMod_Init(void* page) {
    hag::Init(reinterpret_cast<HagUI_PageHandle*>(page));
}

extern "C" __declspec(dllexport) constinit skse::PluginVersionData SKSEPlugin_Version = {
    skse::PluginVersionData::kVersion,
    1,
    "HagVampire",
    "Hagryph",
    "",
    0,
    0,
    { skse::kRuntime_1_6_1170 },
    0,
};

extern "C" __declspec(dllexport) bool SKSEPlugin_Load(const skse::Interface*) {
    return true;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        hag::SetModule(module);
        ::DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
