#include "PCH.h"

#include "HagLoaderAPI.h"
#include "HagUIAPI.h"
#include "GameOffsets.h"
#include "Log.h"
#include "SKSE_Min.h"
#include "SkyrimModAPI.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
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
std::atomic_int g_animationMode{0};
std::atomic_int g_feedHotkey{'V'};
std::atomic<std::uint32_t> g_lastCorpseLevel{0};
std::atomic<std::uint32_t> g_lastCorpseLevelForm{0};

constexpr const char* kConfigName = "HagVampire";
constexpr const char* kFedCorpseSet = "fed_corpses_v2";
constexpr const char* kFeedHotkeyName = "feed_corpse";
constexpr std::int32_t kConfigScope = HAGLOADER_CONFIG_PERSAVE;
constexpr std::uint32_t kMaxFedCorpseEntries = 65536;
constexpr std::uint32_t kFormFlagDeleted = 1u << 5;
constexpr std::uint32_t kFormFlagDisabled = 1u << 11;
constexpr std::uint8_t kFormTypeActorCharacter = 0x3E;
constexpr std::uint32_t kPlayerRefID = 0x14;
constexpr std::uint32_t kIdleCannibalFeedCrouching = 0x000FE09F;
constexpr std::uint32_t kIdleVampireFeedingBedrollLeft = 0x00023622;
constexpr float kCorpseFeedDistance = 40.0f;
constexpr float kPi = 3.14159265358979323846f;

struct NiPoint3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

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

int ConfigGetInt(const char* key, int defaultValue, int minValue, int maxValue) {
    if (!g_loaderApi || !g_loaderApi->GetConfigIntForModule) return defaultValue;
    const auto raw = g_loaderApi->GetConfigIntForModule(g_selfModule, kConfigScope, kConfigName, key, defaultValue);
    return static_cast<int>(std::clamp<std::int64_t>(raw, minValue, maxValue));
}

void ConfigSetInt(const char* key, int value) {
    if (!g_loaderApi || !g_loaderApi->SetConfigIntForModule) return;
    if (!g_loaderApi->SetConfigIntForModule(g_selfModule, kConfigScope, kConfigName, key, value)) {
        HAG_ERR("failed to save config {}={}", key ? key : "", value);
    }
}

void LoadConfig() {
    g_corpseFeedingEnabled.store(ConfigGetBool("enable_corpse_feeding", true));
    g_animationMode.store(ConfigGetInt("animation_mode", 0, 0, 2));
    g_feedHotkey.store(ConfigGetInt("feed_hotkey", 'V', 1, 255));
    HAG_INFO("config loaded: enable_corpse_feeding={} animation_mode={} feed_hotkey={:#x}",
             g_corpseFeedingEnabled.load(), g_animationMode.load(), g_feedHotkey.load());
}

void PushConfigToUI() {
    if (!g_uiApi || !g_page) return;
    if (g_uiApi->SetToggleState) {
        g_uiApi->SetToggleState(g_page,
                                "enable_corpse_feeding",
                                g_corpseFeedingEnabled.load(),
                                true,
                                "");
    }
    if (g_uiApi->SetIntState) {
        g_uiApi->SetIntState(g_page,
                             "animation_mode",
                             g_animationMode.load(),
                             true,
                             "");
        g_uiApi->SetIntState(g_page,
                             "feed_hotkey",
                             g_feedHotkey.load(),
                             true,
                             "");
    }
    if (g_uiApi->Refresh) {
        g_uiApi->Refresh();
    }
}

void ReloadSaveConfig(const char* reason) {
    LoadConfig();
    PushConfigToUI();
    HAG_INFO("save config refreshed ({})", reason ? reason : "unspecified");
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

void* ReadPtrGuarded(void* ptr, std::size_t offset) noexcept {
    __try {
        return *reinterpret_cast<void**>(static_cast<std::uint8_t*>(ptr) + offset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool ReadPoint3Guarded(void* ptr, std::size_t offset, NiPoint3* out) noexcept {
    if (!ptr || !out) return false;
    __try {
        *out = *reinterpret_cast<NiPoint3*>(static_cast<std::uint8_t*>(ptr) + offset);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void* LookupFormByIDGuarded(std::uint32_t formID) noexcept {
    if (formID == 0) return nullptr;
    __try {
        using LookupByIdFn = void* (*)(std::uint32_t);
        auto lookup = reinterpret_cast<LookupByIdFn>(SkyrimBase() + game::form::LookupByID);
        return lookup ? lookup(formID) : nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
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

bool GetReferenceCalcLevelGuarded(void* ref, bool adjustLevel, std::uint16_t* level) noexcept {
    if (level) *level = 0;
    if (!ref || !level) return false;
    __try {
        using GetCalcLevelFn = std::uint16_t (*)(void*, bool);
        auto getCalcLevel = reinterpret_cast<GetCalcLevelFn>(SkyrimBase() + game::refr::GetCalcLevel);
        *level = getCalcLevel(ref, adjustLevel);
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

bool GetActorValueGuarded(void* actor, std::uint32_t actorValue, float* out) noexcept {
    if (out) *out = 0.0f;
    if (!actor || !out) return false;
    __try {
        auto* avOwner = static_cast<std::uint8_t*>(actor) + game::actor::ActorValueOwnerOffset;
        auto** vtbl = *reinterpret_cast<void***>(avOwner);
        using GetActorValueFn = float (*)(void*, std::uint32_t);
        auto getActorValue = reinterpret_cast<GetActorValueFn>(
            vtbl[game::actor::AVOwner_GetActorValue]);
        *out = getActorValue(avOwner, actorValue);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SetActorValueGuarded(void* actor, std::uint32_t actorValue, float value) noexcept {
    if (!actor) return false;
    __try {
        auto* avOwner = static_cast<std::uint8_t*>(actor) + game::actor::ActorValueOwnerOffset;
        auto** vtbl = *reinterpret_cast<void***>(avOwner);
        using SetActorValueFn = void (*)(void*, std::uint32_t, float);
        auto setActorValue = reinterpret_cast<SetActorValueFn>(
            vtbl[game::actor::AVOwner_SetActorValue]);
        setActorValue(avOwner, actorValue, value);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool MovePlayerNearCorpseGuarded(void* player, void* target, NiPoint3* movedTo) noexcept {
    if (!player || !target) return false;

    NiPoint3 targetPosition{};
    NiPoint3 targetAngle{};
    if (!ReadPoint3Guarded(target, game::refr::DataLocation, &targetPosition)) return false;
    if (!ReadPoint3Guarded(target, game::refr::DataAngle, &targetAngle)) return false;

    void* targetCell = ReadPtrGuarded(target, game::refr::ParentCell);
    if (!targetCell) return false;

    const auto cellFlags = ReadU32Guarded(targetCell, game::refr::CellFlags);
    const bool interior = (cellFlags & game::refr::CellFlag_IsInterior) != 0;
    void* worldSpace = interior ? nullptr : ReadPtrGuarded(targetCell, game::refr::CellWorldSpace);
    if (!interior && !worldSpace) return false;

    const float feedAngle = targetAngle.z + kPi;
    NiPoint3 position{
        targetPosition.x + (std::sin(feedAngle) * kCorpseFeedDistance),
        targetPosition.y + (std::cos(feedAngle) * kCorpseFeedDistance),
        targetPosition.z,
    };
    NiPoint3 rotation = targetAngle;
    rotation.z = feedAngle;

    __try {
        using MoveToImplFn = void (*)(void*, const std::uint32_t*, void*, void*, const NiPoint3*, const NiPoint3*);
        auto moveTo = reinterpret_cast<MoveToImplFn>(SkyrimBase() + game::refr::MoveToImpl);
        std::uint32_t noTargetHandle = 0;
        moveTo(player, &noTargetHandle, targetCell, worldSpace, &position, &rotation);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }

    if (movedTo) *movedTo = position;
    return true;
}

bool PlayFeedIdleGuarded(void* player, void* target, std::uint32_t idleFormID) noexcept {
    if (!player || !target) return false;

    void* idle = LookupFormByIDGuarded(idleFormID);
    if (!idle) return false;

    void* process = ReadPtrGuarded(player, game::actor::ActorProcessOffset);
    if (!process) return false;

    __try {
        using SetupSpecialIdleFn = bool (*)(void*, void*, std::uint32_t, void*, bool, bool, void*);
        auto setupSpecialIdle = reinterpret_cast<SetupSpecialIdleFn>(
            SkyrimBase() + game::actor::AIProcess_SetupSpecialIdle);
        return setupSpecialIdle(process,
                                player,
                                game::actor::DefaultObject_ActionIdle,
                                idle,
                                true,
                                false,
                                target);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool RunNativeCorpseFeedGuarded(void* player, void* target, int animationMode, NiPoint3* movedTo) noexcept {
    if (!player || !target) return false;

    std::uint32_t idleFormID = kIdleCannibalFeedCrouching;
    const char* idleName = "IdleCannibalFeedCrouching";
    if (animationMode == 1) {
        idleFormID = kIdleVampireFeedingBedrollLeft;
        idleName = "VampireFeedingBedRollLeft_Loose";
    } else if (animationMode == 2) {
        HAG_INFO("native corpse-feed aborted: custom animation mode is not implemented yet");
        return false;
    }

    if (!MovePlayerNearCorpseGuarded(player, target, movedTo)) {
        HAG_ERR("native corpse-feed failed: could not move player into feed position");
        return false;
    }

    if (!PlayFeedIdleGuarded(player, target, idleFormID)) {
        HAG_ERR("native corpse-feed failed: PlayIdle {} ({:#x}) returned false/faulted",
                idleName, idleFormID);
        return false;
    }

    if (!SetActorValueGuarded(target, game::actor::AV_Variable08, 9.0f)) {
        HAG_ERR("native corpse-feed failed: could not mark target Variable08");
        return false;
    }

    HAG_INFO("native corpse-feed action started: idle={} ({:#x})", idleName, idleFormID);
    return true;
}

void RunNativeFeedTask(void*) {
    HAG_INFO("native corpse-feed task started");
    if (!g_corpseFeedingEnabled.load()) {
        HAG_INFO("native corpse-feed aborted: corpse feeding disabled");
        return;
    }

    const int mode = g_animationMode.load();

    void* player = GetPlayerActorGuarded();
    if (!player) {
        HAG_ERR("native corpse-feed aborted: player actor unavailable");
        return;
    }

    TargetInfo target{};
    if (!GetCrosshairTargetActorGuarded(&target)) {
        HAG_INFO("native corpse-feed aborted: no resolvable crosshair target");
        return;
    }

    const char* rejection = ValidateFeedTarget(target);
    if (rejection) {
        HAG_INFO("native corpse-feed rejected handle={:#x} form={:#x} type={:#x}: {}",
                 target.handle, target.formID, target.formType, rejection);
        return;
    }

    std::uint16_t corpseLevel = 0;
    const bool hasCorpseLevel = GetReferenceCalcLevelGuarded(target.actor, true, &corpseLevel);
    if (hasCorpseLevel) {
        HAG_INFO("native corpse-feed target accepted: handle={:#x} form={:#x} level={}",
                 target.handle, target.formID, corpseLevel);
    } else {
        HAG_WARN("native corpse-feed target accepted but level lookup failed: handle={:#x} form={:#x}",
                 target.handle, target.formID);
    }

    if (!g_loaderApi->SaveStorageAvailable || !g_loaderApi->SaveStorageAvailable()) {
        HAG_ERR("native corpse-feed aborted: HagLoader SKSE save storage unavailable");
        return;
    }

    if (!g_loaderApi->SaveFormIDSetContainsForModule || !g_loaderApi->SaveFormIDSetAddForModule ||
        !g_loaderApi->SaveFormIDSetCountForModule) {
        HAG_ERR("native corpse-feed aborted: HagLoader save form-ID set API unavailable");
        return;
    }

    float consumedMarker = 0.0f;
    if (GetActorValueGuarded(target.actor, game::actor::AV_Variable08, &consumedMarker) &&
        consumedMarker >= 8.5f) {
        HAG_INFO("native corpse-feed rejected form={:#x}: actor Variable08 already marks vampire feeding ({})",
                 target.formID, consumedMarker);
        return;
    }

    if (g_loaderApi->SaveFormIDSetContainsForModule(g_selfModule, kFedCorpseSet, target.formID)) {
        HAG_INFO("native corpse-feed rejected form={:#x}: corpse already fed in this save",
                 target.formID);
        return;
    }

    const std::uint32_t fedCount =
        g_loaderApi->SaveFormIDSetCountForModule(g_selfModule, kFedCorpseSet);
    if (fedCount >= kMaxFedCorpseEntries) {
        HAG_ERR("native corpse-feed aborted: fed corpse ledger full ({} entries)",
                fedCount);
        return;
    }

    HAG_INFO("native corpse-feed invoking corpse action: target handle={:#x} form={:#x} mode={}",
             target.handle, target.formID, mode);
    NiPoint3 movedTo{};
    if (!RunNativeCorpseFeedGuarded(player, target.actor, mode, &movedTo)) {
        HAG_ERR("native corpse-feed failed: native corpse action did not start");
        return;
    }
    if (!g_loaderApi->SaveFormIDSetAddForModule(g_selfModule, kFedCorpseSet, target.formID, kMaxFedCorpseEntries)) {
        HAG_ERR("native corpse-feed action started but failed to record fed corpse form={:#x}; refusing future unsafe repeats depends on save storage",
                target.formID);
        return;
    }
    g_lastCorpseLevelForm.store(target.formID);
    g_lastCorpseLevel.store(hasCorpseLevel ? corpseLevel : 0);
    if (g_uiApi && g_uiApi->Refresh) {
        g_uiApi->Refresh();
    }
    HAG_INFO("native corpse-feed completed: form={:#x} playerMovedTo=({}, {}, {})",
             target.formID, movedTo.x, movedTo.y, movedTo.z);
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

void QueueNativeFeed(const char* reason) {
    if (!g_loaderApi || !g_loaderApi->QueueMainThreadTask) {
        HAG_ERR("native corpse-feed failed: HagLoader main-thread task API unavailable");
        return;
    }
    if (!g_loaderApi->QueueMainThreadTask(&RunNativeFeedTask, nullptr)) {
        HAG_ERR("native corpse-feed failed: could not queue main-thread task");
        return;
    }
    HAG_INFO("native corpse-feed queued ({})", reason ? reason : "unspecified");
}

void OnFeedHotkey(void*) {
    QueueNativeFeed("hotkey");
}

void RegisterFeedHotkey() {
    if (!g_loaderApi || !g_loaderApi->RegisterHotkeyForModule) {
        HAG_ERR("feed hotkey registration failed: HagLoader hotkey API unavailable");
        return;
    }
    const int hotkey = g_feedHotkey.load();
    if (!g_loaderApi->RegisterHotkeyForModule(g_selfModule, kFeedHotkeyName, hotkey, &OnFeedHotkey, nullptr)) {
        HAG_ERR("feed hotkey registration failed for VK {:#x}", hotkey);
        return;
    }
    HAG_INFO("feed hotkey registered at VK {:#x}", hotkey);
}

const char* FeedingCounterText(void*) {
    static thread_local char text[64];
    std::uint32_t count = 0;
    if (g_loaderApi && g_loaderApi->SaveStorageAvailable && g_loaderApi->SaveStorageAvailable() &&
        g_loaderApi->SaveFormIDSetCountForModule) {
        count = g_loaderApi->SaveFormIDSetCountForModule(g_selfModule, kFedCorpseSet);
    }
    const char* noun = (count == 1) ? "corpse" : "corpses";
    std::snprintf(text, sizeof(text), "%u %s fed", count, noun);
    return text;
}

const char* LastCorpseLevelText(void*) {
    static thread_local char text[64];
    const auto formID = g_lastCorpseLevelForm.load();
    const auto level = g_lastCorpseLevel.load();
    if (formID == 0) {
        std::snprintf(text, sizeof(text), "None");
    } else if (level == 0) {
        std::snprintf(text, sizeof(text), "Unknown");
    } else {
        std::snprintf(text, sizeof(text), "Level %u", level);
    }
    return text;
}

void OnFeedHotkeyChanged(void*, HagUI_Value value) {
    int next = 0;
    if (value.type == HAGUI_VT_INT) {
        next = static_cast<int>(value.i);
    } else if (value.type == HAGUI_VT_DOUBLE) {
        next = static_cast<int>(value.d);
    } else {
        return;
    }

    next = std::clamp(next, 1, 255);
    g_feedHotkey.store(next);
    ConfigSetInt("feed_hotkey", next);
    RegisterFeedHotkey();
    HAG_INFO("feed_hotkey -> {:#x}", next);
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
    if (!g_loaderApi || !g_loaderApi->QueuePapyrusStaticCallWithCallback || !g_loaderApi->QueueMainThreadTask ||
        !g_loaderApi->SaveStorageAvailable || !g_loaderApi->SaveFormIDSetContainsForModule ||
        !g_loaderApi->SaveFormIDSetAddForModule || !g_loaderApi->SaveFormIDSetCountForModule ||
        !g_loaderApi->RegisterHotkeyForModule) {
        throw std::runtime_error("HagVampire requires HagLoader Papyrus API");
    }
    LoadConfig();
    RegisterFeedHotkey();

    auto getApi = reinterpret_cast<HagUI_GetAPIFn>(h ? ::GetProcAddress(h, "HagUI_GetAPI") : nullptr);
    g_uiApi = getApi ? getApi(HAGUI_ABI_VERSION) : nullptr;
    g_page = page;
    if (!g_uiApi || !page) {
        throw std::runtime_error("HagVampire requires HagLoader HagUI API/page");
    }

    if (!g_uiApi->AddDynamicButton || !g_uiApi->SetIntState || !g_uiApi->AddHotkey ||
        !g_uiApi->AddCounter || !g_uiApi->SetGridCell) {
        throw std::runtime_error("HagVampire requires HagUI dynamic button/state/hotkey/counter/grid API");
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
    g_uiApi->AddHotkey(page,
                       "feed_hotkey",
                       "Feed corpse hotkey",
                       g_feedHotkey.load(),
                       &OnFeedHotkeyChanged,
                       nullptr);
    g_uiApi->AddCounter(page,
                        "feeding_counter",
                        "Feeding counter",
                        &FeedingCounterText,
                        nullptr);
    g_uiApi->AddCounter(page,
                        "last_corpse_level",
                        "Last corpse level",
                        &LastCorpseLevelText,
                        nullptr);
    g_uiApi->AddStepper(page,
                        "animation_mode",
                        "Animation mode (0 Crouch / 1 Bedroll / 2 Custom)",
                        0.0,
                        2.0,
                        1.0,
                        static_cast<double>(g_animationMode.load()),
                        &OnAnimationModeChanged,
                        nullptr);
    g_uiApi->SetGridCell(page, "vampire_action", 0, 0);
    g_uiApi->SetGridCell(page, "enable_corpse_feeding", 1, 0);
    g_uiApi->SetGridCell(page, "feed_hotkey", 1, 1);
    g_uiApi->SetGridCell(page, "feeding_counter", 1, 2);
    g_uiApi->SetGridCell(page, "last_corpse_level", 1, 3);
    PushConfigToUI();
    g_uiApi->Refresh();

    HAG_INFO("HagVampire page registered");
}

void OnSaveLoaded() {
    ReloadSaveConfig("SKSE save context ready");
    RegisterFeedHotkey();
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

extern "C" __declspec(dllexport) void SkyrimMod_OnSaveLoaded() {
    hag::OnSaveLoaded();
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
