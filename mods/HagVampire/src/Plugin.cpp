#include "PCH.h"

#include "HagLoaderAPI.h"
#include "HagUIAPI.h"
#include "GameOffsets.h"
#include "Log.h"
#include "SKSE_Min.h"
#include "SkyrimModAPI.h"

#include <MinHook.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

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
std::atomic_int g_bloodExtract{0};
std::atomic_int g_permanentHealthBonusAppliedLevel{0};

constexpr const char* kConfigName = "HagVampire";
constexpr const char* kFedCorpseSet = "fed_corpses_v2";
constexpr const char* kFeedHotkeyName = "feed_corpse";
constexpr std::int32_t kConfigScope = HAGLOADER_CONFIG_PERSAVE;
constexpr std::uint32_t kMaxFedCorpseEntries = 65536;
constexpr int kBloodStrengthMinLevel = 1;
constexpr int kBloodStrengthMaxLevel = 100;
constexpr int kBloodStrengthLevel50Extract = 5369;
constexpr int kBloodStrengthMaxExtract = 84000;
constexpr int kBloodStrengthLateExtract = kBloodStrengthMaxExtract - kBloodStrengthLevel50Extract;
constexpr int kBloodScentMinLevel = 1;
constexpr int kFreshBloodMinLevel = 1;
constexpr int kStalkerLevel = 10;
constexpr float kStalkerPermanentHealthBonus = 20.0f;
constexpr float kStalkerFreshBloodSpeedBonus = 5.0f;
constexpr float kStalkerFreshBloodLifestealFraction = 0.05f;
constexpr int kLifestealHealScale = 1'000'000;
constexpr std::uint32_t kFormFlagDeleted = 1u << 5;
constexpr std::uint32_t kFormFlagDisabled = 1u << 11;
constexpr std::uint8_t kFormTypeActorCharacter = 0x3E;
constexpr std::uint32_t kPlayerRefID = 0x14;
constexpr float kBloodScentBaseRange = 600.0f;
constexpr float kBloodScentStalkerRange = 850.0f;
constexpr float kBloodScentMovementRefreshDistance = 12.0f;
constexpr std::uint32_t kBloodScentEffectShader = 0x000DC209;  // LifeDetectedEnemy EFSH.
constexpr float kBloodScentShaderDuration = -1.0f;
constexpr float kFreshBloodBonusFraction = 0.10f;
constexpr float kFreshBloodMinBonus = 0.1f;
constexpr std::uint32_t kFreshBloodDurationMs = 5u * 60u * 1000u;
constexpr std::uint32_t kCorpseFeedCleanupDelayMs = 5500u;

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

struct VampireRank {
    const char* name;
    int level;
};

struct FreshBloodState {
    bool active = false;
    float magickaRateBonus = 0.0f;
    float staminaRateBonus = 0.0f;
    float speedMultBonus = 0.0f;
    float lifestealFraction = 0.0f;
};

struct FreshBloodTimerContext {
    HANDLE timer = nullptr;
    std::uint32_t generation = 0;
};

struct FeedCleanupTimerContext {
    HANDLE timer = nullptr;
    std::uint32_t generation = 0;
    std::uint32_t targetFormID = 0;
};

struct BSSpinLockRaw {
    volatile LONG owningThread = 0;
    volatile LONG lockCount = 0;
};

using BloodScentTargetMap = std::unordered_map<std::uint32_t, std::uint32_t>;  // form ID -> ObjectRefHandle

std::mutex g_freshBloodMutex;
FreshBloodState g_freshBlood{};
std::atomic<std::uint32_t> g_freshBloodGeneration{0};
std::atomic<std::uint32_t> g_feedCleanupGeneration{0};
std::mutex g_bloodScentMutex;
BloodScentTargetMap g_bloodScentActiveTargets;
BloodScentTargetMap g_bloodScentCandidateTargets;
std::mutex g_bloodScentMovementMutex;
NiPoint3 g_lastBloodScentScanPosition{};
bool g_hasLastBloodScentScanPosition = false;
using ModActorValueInternalFn = void (*)(void*, std::int32_t, std::uint32_t, float, void*);
ModActorValueInternalFn g_origModActorValueInternal = nullptr;
std::atomic_bool g_damageHookInstalled{false};
using ActorSetDeadStateFn = void (*)(void*, bool);
ActorSetDeadStateFn g_origActorSetDeadState = nullptr;
std::atomic_bool g_actorDeathHookInstalled{false};
using SetReferenceLocationFn = void (*)(void*, const NiPoint3*);
SetReferenceLocationFn g_origSetReferenceLocation = nullptr;
std::atomic_bool g_bloodScentMovementHookInstalled{false};
std::atomic_int g_lifestealHealRemainderMicro{0};
std::mutex g_lifestealAccumulatorMutex;
thread_local bool g_lifestealHealInProgress = false;

constexpr VampireRank kVampireRanks[] = {
    {"Fledgling", 1},
    {"Stalker", 10},
    {"Baron", 20},
    {"Viscount", 30},
    {"Count", 40},
    {"Marquis", 50},
    {"Duke", 60},
    {"Elder", 70},
    {"Ancient", 80},
    {"Progenitor", 100},
};

std::uintptr_t SkyrimBase() {
    return reinterpret_cast<std::uintptr_t>(::GetModuleHandleW(nullptr));
}

void* GetProcessListsGuarded() noexcept;
void ConfigSetInt(const char* key, int value);
void ApplyUnlockedRankRewards(const char* reason);
void QueueBloodScentGateRefresh(const char* reason);
void TrackBloodScentCandidateFromActor(void* actor, const char* reason);

int BloodStrengthTotalExtractForLevel(int level) {
    level = std::clamp(level, kBloodStrengthMinLevel, kBloodStrengthMaxLevel);
    if (level <= kBloodStrengthMinLevel) return 0;

    const double n = static_cast<double>(level - 1);
    const int baseExtract = static_cast<int>(std::llround((10.0 * std::pow(n, 1.58)) + (14.0 * n)));
    if (level <= 50) return baseExtract;

    const double t = static_cast<double>(level - 50) / 50.0;
    const int lateExtract = static_cast<int>(std::llround(static_cast<double>(kBloodStrengthLateExtract) * std::pow(t, 3.0)));
    return std::min(kBloodStrengthMaxExtract, baseExtract + lateExtract);
}

int BloodStrengthLevelForExtract(int extract) {
    extract = std::clamp(extract, 0, kBloodStrengthMaxExtract);
    int level = kBloodStrengthMinLevel;
    for (int candidate = kBloodStrengthMinLevel + 1; candidate <= kBloodStrengthMaxLevel; ++candidate) {
        if (extract < BloodStrengthTotalExtractForLevel(candidate)) break;
        level = candidate;
    }
    return level;
}

int BloodExtractForCorpseLevel(std::uint16_t corpseLevel) {
    const double level = static_cast<double>(std::max<std::uint16_t>(corpseLevel, 1));
    const int extract = 4 + static_cast<int>(std::floor(level * 0.30)) +
                        static_cast<int>(std::floor(2.0 * std::sqrt(level)));
    return std::clamp(extract, 6, 42);
}

const char* VampireRankNameForLevel(int level) {
    const char* rank = kVampireRanks[0].name;
    for (const auto& candidate : kVampireRanks) {
        if (level < candidate.level) break;
        rank = candidate.name;
    }
    return rank;
}

void SaveBloodExtract(int extract) {
    ConfigSetInt("blood_extract", std::clamp(extract, 0, kBloodStrengthMaxExtract));
}

void AwardBloodExtract(std::uint16_t corpseLevel, std::uint32_t corpseFormID) {
    const int feedExtract = BloodExtractForCorpseLevel(corpseLevel);
    const int previousExtract = std::clamp(g_bloodExtract.load(), 0, kBloodStrengthMaxExtract);
    const int previousLevel = BloodStrengthLevelForExtract(previousExtract);
    const int nextExtract = std::min(kBloodStrengthMaxExtract, previousExtract + feedExtract);
    const int nextLevel = BloodStrengthLevelForExtract(nextExtract);
    g_bloodExtract.store(nextExtract);
    SaveBloodExtract(nextExtract);

    HAG_INFO("bloodstrength feed awarded: form={:#x} corpseLevel={} bloodExtract={} totalBloodExtract={}/{} level={} rank={}",
             corpseFormID,
             corpseLevel,
             feedExtract,
             nextExtract,
             kBloodStrengthMaxExtract,
             nextLevel,
             VampireRankNameForLevel(nextLevel));
    if (nextLevel > previousLevel) {
        HAG_INFO("bloodstrength level advanced: {} -> {} ({})",
                 previousLevel,
                 nextLevel,
                 VampireRankNameForLevel(nextLevel));
    }
    ApplyUnlockedRankRewards(nextLevel > previousLevel ? "bloodstrength level up" : "bloodstrength feed");
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

int CurrentBloodStrengthLevel() {
    return BloodStrengthLevelForExtract(g_bloodExtract.load());
}

bool HasVampireBonusLevel(int minLevel) {
    return IsPlayerVampire() && CurrentBloodStrengthLevel() >= minLevel;
}

float EffectiveBloodScentRange() {
    return CurrentBloodStrengthLevel() >= kStalkerLevel ? kBloodScentStalkerRange : kBloodScentBaseRange;
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
    g_bloodExtract.store(ConfigGetInt("blood_extract", 0, 0, kBloodStrengthMaxExtract));
    g_permanentHealthBonusAppliedLevel.store(ConfigGetInt("permanent_health_bonus_applied_level", 0, 0, kBloodStrengthMaxLevel));
    g_lifestealHealRemainderMicro.store(ConfigGetInt("lifesteal_heal_remainder_micro", 0, 0, kLifestealHealScale - 1));
    HAG_INFO("config loaded: enable_corpse_feeding={} animation_mode={} feed_hotkey={:#x} blood_extract={} level={} rank={} permanentHealthBonusAppliedLevel={} lifestealRemainder={}",
             g_corpseFeedingEnabled.load(),
             g_animationMode.load(),
             g_feedHotkey.load(),
             g_bloodExtract.load(),
             BloodStrengthLevelForExtract(g_bloodExtract.load()),
             VampireRankNameForLevel(BloodStrengthLevelForExtract(g_bloodExtract.load())),
             g_permanentHealthBonusAppliedLevel.load(),
             static_cast<double>(g_lifestealHealRemainderMicro.load()) / static_cast<double>(kLifestealHealScale));
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

float DistanceSquared(const NiPoint3& a, const NiPoint3& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return (dx * dx) + (dy * dy) + (dz * dz);
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

bool GetVampireFeedStateGuarded(void* actor, bool* active) noexcept {
    if (active) *active = false;
    if (!actor || !active) return false;
    __try {
        using GetVampireFeedFn = bool (*)(void*);
        auto** vtbl = *reinterpret_cast<void***>(actor);
        auto getVampireFeed = reinterpret_cast<GetVampireFeedFn>(
            vtbl[game::actor::VSlot_GetVampireFeed]);
        *active = getVampireFeed(actor);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SetVampireFeedStateGuarded(void* actor, bool active) noexcept {
    if (!actor) return false;
    __try {
        using SetVampireFeedFn = void (*)(void*, bool);
        auto** vtbl = *reinterpret_cast<void***>(actor);
        auto setVampireFeed = reinterpret_cast<SetVampireFeedFn>(
            vtbl[game::actor::VSlot_SetVampireFeed]);
        setVampireFeed(actor, active);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool GetCannibalStateGuarded(void* actor, bool* active) noexcept {
    if (active) *active = false;
    if (!actor || !active) return false;
    __try {
        using GetCannibalFn = bool (*)(void*);
        auto** vtbl = *reinterpret_cast<void***>(actor);
        auto getCannibal = reinterpret_cast<GetCannibalFn>(
            vtbl[game::actor::VSlot_GetCannibal]);
        *active = getCannibal(actor);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SetCannibalStateGuarded(void* actor, bool active) noexcept {
    if (!actor) return false;
    __try {
        using SetCannibalFn = void (*)(void*, bool);
        auto** vtbl = *reinterpret_cast<void***>(actor);
        auto setCannibal = reinterpret_cast<SetCannibalFn>(
            vtbl[game::actor::VSlot_SetCannibal]);
        setCannibal(actor, active);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool InitiateCannibalPackageGuarded(void* actor, void* target) noexcept {
    if (!actor || !target) return false;
    __try {
        using InitiateCannibalPackageFn = void (*)(void*, void*);
        auto** vtbl = *reinterpret_cast<void***>(actor);
        auto initiateCannibalPackage = reinterpret_cast<InitiateCannibalPackageFn>(
            vtbl[game::actor::VSlot_InitiateCannibalPackage]);
        initiateCannibalPackage(actor, target);
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

bool GetPermanentActorValueGuarded(void* actor, std::uint32_t actorValue, float* out) noexcept {
    if (out) *out = 0.0f;
    if (!actor || !out) return false;
    __try {
        auto* avOwner = static_cast<std::uint8_t*>(actor) + game::actor::ActorValueOwnerOffset;
        auto** vtbl = *reinterpret_cast<void***>(avOwner);
        using GetPermanentActorValueFn = float (*)(void*, std::uint32_t);
        auto getPermanentActorValue = reinterpret_cast<GetPermanentActorValueFn>(
            vtbl[game::actor::AVOwner_GetPermanentActorValue]);
        *out = getPermanentActorValue(avOwner, actorValue);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool GetBaseActorValueGuarded(void* actor, std::uint32_t actorValue, float* out) noexcept {
    if (out) *out = 0.0f;
    if (!actor || !out) return false;
    __try {
        auto* avOwner = static_cast<std::uint8_t*>(actor) + game::actor::ActorValueOwnerOffset;
        auto** vtbl = *reinterpret_cast<void***>(avOwner);
        using GetBaseActorValueFn = float (*)(void*, std::uint32_t);
        auto getBaseActorValue = reinterpret_cast<GetBaseActorValueFn>(
            vtbl[game::actor::AVOwner_GetBaseActorValue]);
        *out = getBaseActorValue(avOwner, actorValue);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SetBaseActorValueGuarded(void* actor, std::uint32_t actorValue, float value) noexcept {
    if (!actor) return false;
    __try {
        auto* avOwner = static_cast<std::uint8_t*>(actor) + game::actor::ActorValueOwnerOffset;
        auto** vtbl = *reinterpret_cast<void***>(avOwner);
        using SetBaseActorValueFn = void (*)(void*, std::uint32_t, float);
        auto setBaseActorValue = reinterpret_cast<SetBaseActorValueFn>(
            vtbl[game::actor::AVOwner_SetBaseActorValue]);
        setBaseActorValue(avOwner, actorValue, value);
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

bool DistanceBetweenRefsGuarded(void* a, void* b, float* distance) noexcept {
    if (distance) *distance = 0.0f;
    if (!a || !b || !distance) return false;

    NiPoint3 apos{};
    NiPoint3 bpos{};
    if (!ReadPoint3Guarded(a, game::refr::DataLocation, &apos) ||
        !ReadPoint3Guarded(b, game::refr::DataLocation, &bpos)) {
        return false;
    }

    *distance = std::sqrt(DistanceSquared(apos, bpos));
    return true;
}

bool ChangeActorValueByDeltaGuarded(void* actor, std::uint32_t actorValue, float delta) noexcept {
    float current = 0.0f;
    if (!GetActorValueGuarded(actor, actorValue, &current)) return false;
    return SetActorValueGuarded(actor, actorValue, current + delta);
}

float CurrentFreshBloodLifestealFraction() {
    std::lock_guard lock(g_freshBloodMutex);
    return g_freshBlood.active ? g_freshBlood.lifestealFraction : 0.0f;
}

bool HealPlayerFromLifesteal(void* player, void* target, float appliedDamage, float fraction) {
    if (!player || !target || appliedDamage <= 0.0f || fraction <= 0.0f) return false;

    const float requestedHeal = appliedDamage * fraction;
    if (!std::isfinite(requestedHeal) || requestedHeal <= 0.0f) return false;

    const auto requestedMicro = static_cast<std::int64_t>(
        std::llround(static_cast<double>(requestedHeal) * static_cast<double>(kLifestealHealScale)));
    if (requestedMicro <= 0) return false;

    int previousRemainderMicro = 0;
    int nextRemainderMicro = 0;
    int wholeHeal = 0;
    {
        std::lock_guard lock(g_lifestealAccumulatorMutex);
        previousRemainderMicro = std::clamp(g_lifestealHealRemainderMicro.load(), 0, kLifestealHealScale - 1);
        const auto totalMicro = static_cast<std::int64_t>(previousRemainderMicro) + requestedMicro;
        wholeHeal = static_cast<int>(totalMicro / kLifestealHealScale);
        nextRemainderMicro = static_cast<int>(totalMicro % kLifestealHealScale);
        g_lifestealHealRemainderMicro.store(nextRemainderMicro);
        ConfigSetInt("lifesteal_heal_remainder_micro", nextRemainderMicro);
    }

    if (wholeHeal <= 0) return false;

    float playerHealth = 0.0f;
    float playerMaxHealth = 0.0f;
    if (!GetActorValueGuarded(player, game::actor::AV_Health, &playerHealth) ||
        !GetPermanentActorValueGuarded(player, game::actor::AV_Health, &playerMaxHealth)) {
        HAG_WARN("Fresh Blood lifesteal saved fractional remainder but skipped whole heal: could not read player health");
        return false;
    }

    if (playerMaxHealth <= 0.0f || playerHealth >= playerMaxHealth) {
        return false;
    }

    const float appliedHeal = std::min(static_cast<float>(wholeHeal), playerMaxHealth - playerHealth);
    if (appliedHeal <= 0.0f) return false;

    g_lifestealHealInProgress = true;
    const bool healed = SetActorValueGuarded(player, game::actor::AV_Health, playerHealth + appliedHeal);
    g_lifestealHealInProgress = false;

    if (!healed) {
        HAG_WARN("Fresh Blood lifesteal saved fractional remainder but skipped whole heal: could not write player health");
        return false;
    }

    const std::uint32_t targetFormID = ReadU32Guarded(target, 0x14);
    HAG_INFO("Fresh Blood lifesteal: target={:#x} damage={} requestedHeal={} wholeHeal={} appliedHeal={} fraction={} savedRemainder={} previousRemainder={}",
             targetFormID,
             appliedDamage,
             requestedHeal,
             wholeHeal,
             appliedHeal,
             fraction,
             static_cast<double>(nextRemainderMicro) / static_cast<double>(kLifestealHealScale),
             static_cast<double>(previousRemainderMicro) / static_cast<double>(kLifestealHealScale));
    return true;
}

void Detour_ModActorValueInternal(void* actor,
                                  std::int32_t modifier,
                                  std::uint32_t actorValue,
                                  float delta,
                                  void* source) {
    if (!g_origModActorValueInternal) return;

    float beforeHealth = 0.0f;
    void* player = nullptr;
    bool trackOutgoingPlayerDamage = false;

    if (!g_lifestealHealInProgress &&
        modifier == game::actor::AVModifier_Damage &&
        actorValue == game::actor::AV_Health &&
        delta < 0.0f &&
        actor &&
        source) {
        const float lifestealFraction = CurrentFreshBloodLifestealFraction();
        if (lifestealFraction > 0.0f) {
            player = GetPlayerActorGuarded();
            trackOutgoingPlayerDamage = player &&
                                        source == player &&
                                        actor != player &&
                                        GetActorValueGuarded(actor, game::actor::AV_Health, &beforeHealth) &&
                                        beforeHealth > 0.0f;
        }
    }

    g_origModActorValueInternal(actor, modifier, actorValue, delta, source);

    if (!trackOutgoingPlayerDamage || !player) return;

    float afterHealth = 0.0f;
    if (!GetActorValueGuarded(actor, game::actor::AV_Health, &afterHealth)) {
        return;
    }

    const float appliedDamage = std::clamp(beforeHealth - afterHealth, 0.0f, beforeHealth);
    const float lifestealFraction = CurrentFreshBloodLifestealFraction();
    if (appliedDamage > 0.0f && lifestealFraction > 0.0f) {
        HealPlayerFromLifesteal(player, actor, appliedDamage, lifestealFraction);
    }
}

void Detour_ActorSetDeadState(void* actor, bool dead) {
    bool wasDead = false;
    const bool hadPreDeathState = IsDeadGuarded(actor, &wasDead);

    if (g_origActorSetDeadState) {
        g_origActorSetDeadState(actor, dead);
    }

    if (dead && (!hadPreDeathState || !wasDead)) {
        TrackBloodScentCandidateFromActor(actor, "actor death");
    }
}

bool InstallDamageHook() {
    if (g_damageHookInstalled.load()) return true;

    const auto initStatus = MH_Initialize();
    if (initStatus != MH_OK && initStatus != MH_ERROR_ALREADY_INITIALIZED) {
        HAG_ERR("Fresh Blood damage hook failed: MH_Initialize status={}", static_cast<int>(initStatus));
        return false;
    }

    const auto target = SkyrimBase() + game::actor::Actor_ModActorValueInternal;
    const auto createStatus = MH_CreateHook(reinterpret_cast<LPVOID>(target),
                                            reinterpret_cast<LPVOID>(&Detour_ModActorValueInternal),
                                            reinterpret_cast<LPVOID*>(&g_origModActorValueInternal));
    if (createStatus != MH_OK) {
        HAG_ERR("Fresh Blood damage hook failed: MH_CreateHook target={:#x} status={}",
                target,
                static_cast<int>(createStatus));
        return false;
    }

    const auto enableStatus = MH_EnableHook(reinterpret_cast<LPVOID>(target));
    if (enableStatus != MH_OK && enableStatus != MH_ERROR_ENABLED) {
        HAG_ERR("Fresh Blood damage hook failed: MH_EnableHook target={:#x} status={}",
                target,
                static_cast<int>(enableStatus));
        return false;
    }

    g_damageHookInstalled.store(true);
    HAG_INFO("Fresh Blood damage hook installed: ActorValueModifierInternal @{:#x}", target);
    return true;
}

bool InstallActorDeathHook() {
    if (g_actorDeathHookInstalled.load()) return true;

    const auto initStatus = MH_Initialize();
    if (initStatus != MH_OK && initStatus != MH_ERROR_ALREADY_INITIALIZED) {
        HAG_ERR("Blood Scent actor-death hook failed: MH_Initialize status={}", static_cast<int>(initStatus));
        return false;
    }

    const auto target = SkyrimBase() + game::actor::Actor_SetDeadState;
    const auto createStatus = MH_CreateHook(reinterpret_cast<LPVOID>(target),
                                            reinterpret_cast<LPVOID>(&Detour_ActorSetDeadState),
                                            reinterpret_cast<LPVOID*>(&g_origActorSetDeadState));
    if (createStatus != MH_OK) {
        HAG_ERR("Blood Scent actor-death hook failed: MH_CreateHook target={} status={}",
                reinterpret_cast<void*>(target),
                static_cast<int>(createStatus));
        return false;
    }

    const auto enableStatus = MH_EnableHook(reinterpret_cast<LPVOID>(target));
    if (enableStatus != MH_OK && enableStatus != MH_ERROR_ENABLED) {
        HAG_ERR("Blood Scent actor-death hook failed: MH_EnableHook target={} status={}",
                reinterpret_cast<void*>(target),
                static_cast<int>(enableStatus));
        return false;
    }

    g_actorDeathHookInstalled.store(true);
    HAG_INFO("Blood Scent actor-death hook installed: Actor_SetDeadState @{}",
             reinterpret_cast<void*>(target));
    return true;
}

bool ApplyPermanentHealthBonus(void* player, float amount, const char* label) {
    if (!player || amount <= 0.0f) return false;

    float baseHealth = 0.0f;
    if (!GetBaseActorValueGuarded(player, game::actor::AV_Health, &baseHealth)) {
        HAG_WARN("{} skipped: could not read player base health", label ? label : "permanent health bonus");
        return false;
    }

    const float nextBaseHealth = baseHealth + amount;
    if (!SetBaseActorValueGuarded(player, game::actor::AV_Health, nextBaseHealth)) {
        HAG_WARN("{} skipped: could not set player base health {} -> {}",
                 label ? label : "permanent health bonus",
                 baseHealth,
                 nextBaseHealth);
        return false;
    }

    HAG_INFO("{} applied: baseHealth {} -> {} (+{})",
             label ? label : "permanent health bonus",
             baseHealth,
             nextBaseHealth,
             amount);
    return true;
}

void ApplyUnlockedRankRewards(const char* reason) {
    const int level = CurrentBloodStrengthLevel();
    int appliedLevel = g_permanentHealthBonusAppliedLevel.load();
    if (level < kStalkerLevel || appliedLevel >= kStalkerLevel) {
        return;
    }
    if (!IsPlayerVampire()) {
        HAG_INFO("Stalker permanent health reward deferred ({}): player is not currently a vampire",
                 reason ? reason : "unspecified");
        return;
    }

    void* player = GetPlayerActorGuarded();
    if (!player) {
        HAG_WARN("Stalker permanent health reward deferred ({}): player actor unavailable",
                 reason ? reason : "unspecified");
        return;
    }

    if (!ApplyPermanentHealthBonus(player, kStalkerPermanentHealthBonus, "Stalker permanent health")) {
        return;
    }

    appliedLevel = kStalkerLevel;
    g_permanentHealthBonusAppliedLevel.store(appliedLevel);
    ConfigSetInt("permanent_health_bonus_applied_level", appliedLevel);
    HAG_INFO("rank reward recorded: permanentHealthBonusAppliedLevel={} ({}) reason={}",
             appliedLevel,
             VampireRankNameForLevel(appliedLevel),
             reason ? reason : "unspecified");
}

bool RemoveFreshBloodBonusesLocked(void* player) {
    if (!g_freshBlood.active) return true;

    bool ok = true;
    if (g_freshBlood.magickaRateBonus != 0.0f) {
        ok = ChangeActorValueByDeltaGuarded(player, game::actor::AV_MagickaRate, -g_freshBlood.magickaRateBonus) && ok;
    }
    if (g_freshBlood.staminaRateBonus != 0.0f) {
        ok = ChangeActorValueByDeltaGuarded(player, game::actor::AV_StaminaRate, -g_freshBlood.staminaRateBonus) && ok;
    }
    if (g_freshBlood.speedMultBonus != 0.0f) {
        ok = ChangeActorValueByDeltaGuarded(player, game::actor::AV_SpeedMult, -g_freshBlood.speedMultBonus) && ok;
    }

    g_freshBlood = {};
    return ok;
}

void RevertFreshBloodTask(void* user);

void CALLBACK FreshBloodTimerProc(PVOID user, BOOLEAN) {
    auto* ctx = static_cast<FreshBloodTimerContext*>(user);
    if (!ctx) return;

    const HANDLE timer = ctx->timer;
    ctx->timer = nullptr;
    if (timer) {
        ::DeleteTimerQueueTimer(nullptr, timer, nullptr);
    }

    if (g_loaderApi && g_loaderApi->QueueMainThreadTask &&
        g_loaderApi->QueueMainThreadTask(&RevertFreshBloodTask, ctx)) {
        return;
    }

    HAG_WARN("Fresh Blood revert could not be queued; clearing timer context without touching actor values");
    delete ctx;
}

bool ScheduleFreshBloodRevert(std::uint32_t generation) {
    auto* ctx = new FreshBloodTimerContext();
    ctx->generation = generation;

    HANDLE timer = nullptr;
    if (!::CreateTimerQueueTimer(&timer,
                                 nullptr,
                                 &FreshBloodTimerProc,
                                 ctx,
                                 kFreshBloodDurationMs,
                                 0,
                                 WT_EXECUTEDEFAULT)) {
        delete ctx;
        return false;
    }

    ctx->timer = timer;
    return true;
}

void RevertFreshBloodTask(void* user) {
    std::unique_ptr<FreshBloodTimerContext> ctx(static_cast<FreshBloodTimerContext*>(user));
    if (!ctx) return;

    if (ctx->generation != g_freshBloodGeneration.load()) {
        HAG_INFO("Fresh Blood stale revert ignored: generation={} current={}",
                 ctx->generation,
                 g_freshBloodGeneration.load());
        return;
    }

    void* player = GetPlayerActorGuarded();
    if (!player) {
        HAG_WARN("Fresh Blood revert skipped: player actor unavailable");
        return;
    }

    std::lock_guard lock(g_freshBloodMutex);
    const bool ok = RemoveFreshBloodBonusesLocked(player);
    HAG_INFO("Fresh Blood expired: reverted={} generation={}", ok, ctx->generation);
}

bool CleanupCorpseFeedState(void* player, const char* reason) {
    if (!player) return false;

    bool wasVampireFeeding = false;
    const bool vampireReadOk = GetVampireFeedStateGuarded(player, &wasVampireFeeding);
    const bool vampireClearOk = SetVampireFeedStateGuarded(player, false);

    bool wasCannibal = false;
    const bool cannibalReadOk = GetCannibalStateGuarded(player, &wasCannibal);
    const bool cannibalClearOk = SetCannibalStateGuarded(player, false);

    bool afterVampireFeeding = false;
    const bool vampireRereadOk = GetVampireFeedStateGuarded(player, &afterVampireFeeding);
    bool afterCannibal = false;
    const bool cannibalRereadOk = GetCannibalStateGuarded(player, &afterCannibal);

    HAG_INFO("native corpse-feed cleanup ({}): vampireReadOk={} wasVampireFeed={} vampireClearOk={} vampireRereadOk={} nowVampireFeed={} cannibalReadOk={} wasCannibal={} cannibalClearOk={} cannibalRereadOk={} nowCannibal={}",
             reason ? reason : "unspecified",
             vampireReadOk,
             wasVampireFeeding,
             vampireClearOk,
             vampireRereadOk,
             afterVampireFeeding,
             cannibalReadOk,
             wasCannibal,
             cannibalClearOk,
             cannibalRereadOk,
             afterCannibal);
    return vampireClearOk && cannibalClearOk &&
           (!vampireRereadOk || !afterVampireFeeding) &&
           (!cannibalRereadOk || !afterCannibal);
}

void FeedCleanupTask(void* user);

void CALLBACK FeedCleanupTimerProc(PVOID user, BOOLEAN) {
    auto* ctx = static_cast<FeedCleanupTimerContext*>(user);
    if (!ctx) return;

    const HANDLE timer = ctx->timer;
    ctx->timer = nullptr;
    if (timer) {
        ::DeleteTimerQueueTimer(nullptr, timer, nullptr);
    }

    if (g_loaderApi && g_loaderApi->QueueMainThreadTask &&
        g_loaderApi->QueueMainThreadTask(&FeedCleanupTask, ctx)) {
        return;
    }

    HAG_WARN("native corpse-feed cleanup could not be queued; dropping cleanup context");
    delete ctx;
}

bool ScheduleFeedCleanup(std::uint32_t generation, std::uint32_t targetFormID) {
    auto* ctx = new FeedCleanupTimerContext();
    ctx->generation = generation;
    ctx->targetFormID = targetFormID;

    HANDLE timer = nullptr;
    if (!::CreateTimerQueueTimer(&timer,
                                 nullptr,
                                 &FeedCleanupTimerProc,
                                 ctx,
                                 kCorpseFeedCleanupDelayMs,
                                 0,
                                 WT_EXECUTEDEFAULT)) {
        delete ctx;
        return false;
    }

    ctx->timer = timer;
    return true;
}

void FeedCleanupTask(void* user) {
    std::unique_ptr<FeedCleanupTimerContext> ctx(static_cast<FeedCleanupTimerContext*>(user));
    if (!ctx) return;

    if (ctx->generation != g_feedCleanupGeneration.load()) {
        HAG_INFO("native corpse-feed stale cleanup ignored: generation={} current={} target={:#x}",
                 ctx->generation,
                 g_feedCleanupGeneration.load(),
                 ctx->targetFormID);
        return;
    }

    void* player = GetPlayerActorGuarded();
    if (!player) {
        HAG_WARN("native corpse-feed cleanup skipped: player actor unavailable target={:#x}",
                 ctx->targetFormID);
        return;
    }

    const bool cleaned = CleanupCorpseFeedState(player, "timer");
    HAG_INFO("native corpse-feed cleanup completed: cleaned={} target={:#x} generation={}",
             cleaned,
             ctx->targetFormID,
             ctx->generation);
}

void FeedCleanupNowTask(void* user) {
    const char* reason = static_cast<const char*>(user);
    void* player = GetPlayerActorGuarded();
    if (!player) {
        HAG_INFO("native corpse-feed cleanup skipped ({}): player actor unavailable",
                 reason ? reason : "unspecified");
        return;
    }
    CleanupCorpseFeedState(player, reason ? reason : "queued");
}

void QueueFeedCleanup(const char* reason) {
    if (!g_loaderApi || !g_loaderApi->QueueMainThreadTask) return;
    if (!g_loaderApi->QueueMainThreadTask(&FeedCleanupNowTask, const_cast<char*>(reason))) {
        HAG_WARN("native corpse-feed cleanup queue failed ({})", reason ? reason : "unspecified");
    }
}

void ApplyFreshBloodBuff(void* player, std::uint32_t sourceFormID) {
    if (!player) return;
    if (!HasVampireBonusLevel(kFreshBloodMinLevel)) {
        HAG_INFO("Fresh Blood skipped: vampire bonus level unavailable");
        return;
    }

    std::lock_guard lock(g_freshBloodMutex);
    if (g_freshBlood.active && !RemoveFreshBloodBonusesLocked(player)) {
        HAG_WARN("Fresh Blood refresh could not fully remove previous bonuses");
    }

    float magickaRate = 0.0f;
    float staminaRate = 0.0f;
    if (!GetActorValueGuarded(player, game::actor::AV_MagickaRate, &magickaRate) ||
        !GetActorValueGuarded(player, game::actor::AV_StaminaRate, &staminaRate)) {
        HAG_WARN("Fresh Blood skipped: could not read player regen actor values");
        return;
    }

    const float magickaBonus = std::max(std::fabs(magickaRate) * kFreshBloodBonusFraction, kFreshBloodMinBonus);
    const float staminaBonus = std::max(std::fabs(staminaRate) * kFreshBloodBonusFraction, kFreshBloodMinBonus);
    const float speedBonus = HasVampireBonusLevel(kStalkerLevel) ? kStalkerFreshBloodSpeedBonus : 0.0f;
    const float lifestealFraction = HasVampireBonusLevel(kStalkerLevel) ? kStalkerFreshBloodLifestealFraction : 0.0f;

    if (!ChangeActorValueByDeltaGuarded(player, game::actor::AV_MagickaRate, magickaBonus)) {
        HAG_WARN("Fresh Blood skipped: could not apply magicka regen bonus");
        return;
    }
    if (!ChangeActorValueByDeltaGuarded(player, game::actor::AV_StaminaRate, staminaBonus)) {
        ChangeActorValueByDeltaGuarded(player, game::actor::AV_MagickaRate, -magickaBonus);
        HAG_WARN("Fresh Blood skipped: could not apply stamina regen bonus");
        return;
    }
    if (speedBonus != 0.0f &&
        !ChangeActorValueByDeltaGuarded(player, game::actor::AV_SpeedMult, speedBonus)) {
        ChangeActorValueByDeltaGuarded(player, game::actor::AV_StaminaRate, -staminaBonus);
        ChangeActorValueByDeltaGuarded(player, game::actor::AV_MagickaRate, -magickaBonus);
        HAG_WARN("Fresh Blood skipped: could not apply Stalker speed bonus");
        return;
    }

    const auto generation = g_freshBloodGeneration.fetch_add(1) + 1;
    g_freshBlood.active = true;
    g_freshBlood.magickaRateBonus = magickaBonus;
    g_freshBlood.staminaRateBonus = staminaBonus;
    g_freshBlood.speedMultBonus = speedBonus;
    g_freshBlood.lifestealFraction = lifestealFraction;

    if (!ScheduleFreshBloodRevert(generation)) {
        RemoveFreshBloodBonusesLocked(player);
        HAG_WARN("Fresh Blood skipped: could not create expiry timer");
        return;
    }

    HAG_INFO("Fresh Blood applied: sourceForm={:#x} magickaRate +{} staminaRate +{} speedMult +{} lifesteal {} durationMs={} generation={} level={} rank={}",
             sourceFormID,
             magickaBonus,
             staminaBonus,
             speedBonus,
             lifestealFraction,
             kFreshBloodDurationMs,
             generation,
             CurrentBloodStrengthLevel(),
             VampireRankNameForLevel(CurrentBloodStrengthLevel()));
}

bool ApplyEffectShaderGuarded(void* target, std::uint32_t shaderFormID, float duration, std::uintptr_t* resultOut) noexcept {
    if (resultOut) *resultOut = 0;
    if (!target || shaderFormID == 0) return false;

    void* shader = LookupFormByIDGuarded(shaderFormID);
    if (!shader) {
        HAG_WARN("Blood Scent shader apply failed: shader form {:#x} was not found", shaderFormID);
        return false;
    }
    constexpr std::uint8_t kFormTypeEffectShader = 0x55;
    const auto shaderType = ReadU8Guarded(shader, game::form::FormType);
    if (shaderType != kFormTypeEffectShader) {
        HAG_WARN("Blood Scent shader apply failed: form {:#x} type {:#x} is not EFSH",
                 shaderFormID,
                 shaderType);
        return false;
    }

    __try {
        using ApplyEffectShaderFn = std::uintptr_t (*)(void*, void*, float, void*, bool, bool, void*, bool);
        auto applyEffectShader = reinterpret_cast<ApplyEffectShaderFn>(
            SkyrimBase() + game::refr::ApplyEffectShader);
        const auto result = applyEffectShader(target, shader, duration, nullptr, false, false, nullptr, false);
        if (resultOut) *resultOut = result;
        return result != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ApplyBloodScentShader(std::uint32_t formID) {
    if (formID == 0) return false;

    void* target = LookupFormByIDGuarded(formID);
    if (!target) {
        HAG_WARN("Blood Scent shader apply skipped: target form {:#x} was not found", formID);
        return false;
    }

    std::uintptr_t result = 0;
    const bool ok = ApplyEffectShaderGuarded(target, kBloodScentEffectShader, kBloodScentShaderDuration, &result);
    if (!ok) {
        HAG_WARN("Blood Scent shader apply failed: target={:#x} shader={:#x}",
                 formID,
                 kBloodScentEffectShader);
        return false;
    }

    HAG_INFO("Blood Scent shader applied: target={:#x} shader={:#x} result={:#x}",
             formID,
             kBloodScentEffectShader,
             result);
    return true;
}

void LockBSSpinLock(BSSpinLockRaw* lock) noexcept {
    if (!lock) return;
    const auto threadID = static_cast<LONG>(::GetCurrentThreadId());
    if (lock->owningThread == threadID) {
        ::InterlockedIncrement(&lock->lockCount);
        return;
    }

    std::uint32_t spins = 0;
    while (::InterlockedCompareExchange(&lock->lockCount, 1, 0) != 0) {
        if (++spins < 10000) {
            YieldProcessor();
        } else {
            ::Sleep(spins < 20000 ? 0 : 1);
        }
    }
    lock->owningThread = threadID;
}

void UnlockBSSpinLock(BSSpinLockRaw* lock) noexcept {
    if (!lock) return;
    const auto threadID = static_cast<LONG>(::GetCurrentThreadId());
    if (lock->owningThread != threadID) return;

    if (lock->lockCount == 1) {
        lock->owningThread = 0;
        ::MemoryBarrier();
        ::InterlockedCompareExchange(&lock->lockCount, 0, 1);
    } else {
        ::InterlockedDecrement(&lock->lockCount);
    }
}

bool ReadBSTPointerArrayHeaderGuarded(void* array, void*** data, std::uint32_t* size) noexcept {
    if (data) *data = nullptr;
    if (size) *size = 0;
    if (!array || !data || !size) return false;

    __try {
        auto* bytes = static_cast<std::uint8_t*>(array);
        auto* rawData = *reinterpret_cast<void***>(bytes + game::process::BSTArray_Data);
        const auto capacity = *reinterpret_cast<std::uint32_t*>(bytes + game::process::BSTArray_Capacity);
        const auto rawSize = *reinterpret_cast<std::uint32_t*>(bytes + game::process::BSTArray_Size);
        if (rawSize > capacity || rawSize > game::process::MaxMagicEffects) return false;
        *data = rawData;
        *size = rawSize;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::uint32_t MarkBloodScentEffectsFinishedGuarded(void** effects,
                                                   std::uint32_t size,
                                                   std::uint32_t targetHandle,
                                                   void* shader,
                                                   std::uint32_t* targetMatches,
                                                   std::uint32_t* shaderMatches) noexcept {
    if (targetMatches) *targetMatches = 0;
    if (shaderMatches) *shaderMatches = 0;
    if (!effects || targetHandle == 0 || !shader) return 0;

    __try {
        std::uint32_t marked = 0;
        std::uint32_t matchedTarget = 0;
        std::uint32_t matchedShader = 0;
        for (std::uint32_t i = 0; i < size; ++i) {
            void* effect = effects[i];
            if (!effect) continue;
            if (ReadU32Guarded(effect, game::effect::ReferenceEffect_Target) != targetHandle) continue;
            ++matchedTarget;
            if (ReadPtrGuarded(effect, game::effect::ShaderReferenceEffect_EffectData) != shader) continue;
            ++matchedShader;
            *reinterpret_cast<std::uint8_t*>(
                static_cast<std::uint8_t*>(effect) + game::effect::ReferenceEffect_Finished) = 1;
            ++marked;
        }
        if (targetMatches) *targetMatches = matchedTarget;
        if (shaderMatches) *shaderMatches = matchedShader;
        return marked;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return UINT32_MAX;
    }
}

bool ClearBloodScentShaderNative(std::uint32_t formID, std::uint32_t targetHandle) {
    if (formID == 0 || targetHandle == 0) {
        HAG_WARN("Blood Scent native clear had no tracked handle: target={:#x} handle={:#x}",
                 formID,
                 targetHandle);
        return false;
    }

    void* shader = LookupFormByIDGuarded(kBloodScentEffectShader);
    if (!shader) {
        HAG_WARN("Blood Scent native clear failed: shader form {:#x} was not found", kBloodScentEffectShader);
        return false;
    }

    void* processLists = GetProcessListsGuarded();
    if (!processLists) {
        HAG_WARN("Blood Scent native clear failed: ProcessLists unavailable target={:#x}", formID);
        return false;
    }

    auto* base = static_cast<std::uint8_t*>(processLists);
    void** effects = nullptr;
    std::uint32_t size = 0;
    if (!ReadBSTPointerArrayHeaderGuarded(base + game::process::ProcessLists_MagicEffects, &effects, &size) ||
        !effects) {
        HAG_WARN("Blood Scent native clear failed: magic effects array unavailable target={:#x}", formID);
        return false;
    }

    auto* lock = reinterpret_cast<BSSpinLockRaw*>(base + game::process::ProcessLists_MagicEffectsLock);
    LockBSSpinLock(lock);
    std::uint32_t targetMatches = 0;
    std::uint32_t shaderMatches = 0;
    const auto marked = MarkBloodScentEffectsFinishedGuarded(effects,
                                                             size,
                                                             targetHandle,
                                                             shader,
                                                             &targetMatches,
                                                             &shaderMatches);
    UnlockBSSpinLock(lock);

    if (marked == UINT32_MAX) {
        HAG_WARN("Blood Scent native clear faulted while scanning effects: target={:#x}", formID);
        return false;
    }

    HAG_INFO("Blood Scent native clear: target={:#x} handle={:#x} markedFinished={} targetMatches={} shaderMatches={}",
             formID,
             targetHandle,
             marked,
             targetMatches,
             shaderMatches);
    return marked != 0;
}

bool ReadBSTActorHandleArrayHeaderGuarded(void* array, std::uint32_t** data, std::uint32_t* size) noexcept {
    if (data) *data = nullptr;
    if (size) *size = 0;
    if (!array || !data || !size) return false;

    __try {
        auto* bytes = static_cast<std::uint8_t*>(array);
        auto* rawData = *reinterpret_cast<std::uint32_t**>(bytes + game::process::BSTArray_Data);
        const auto capacity = *reinterpret_cast<std::uint32_t*>(bytes + game::process::BSTArray_Capacity);
        const auto rawSize = *reinterpret_cast<std::uint32_t*>(bytes + game::process::BSTArray_Size);
        if (rawSize > capacity || rawSize > game::process::MaxActorHandlesPerList) return false;
        *data = rawData;
        *size = rawSize;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ReadActorHandleAtGuarded(std::uint32_t* data, std::uint32_t index, std::uint32_t* handle) noexcept {
    if (handle) *handle = 0;
    if (!data || !handle) return false;

    __try {
        *handle = data[index];
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void* GetProcessListsGuarded() noexcept {
    __try {
        return *reinterpret_cast<void**>(SkyrimBase() + game::process::ProcessListsPtr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void CollectActorHandlesFromArray(void* array, std::vector<std::uint32_t>& handles) {
    std::uint32_t* data = nullptr;
    std::uint32_t size = 0;
    if (!ReadBSTActorHandleArrayHeaderGuarded(array, &data, &size) || !data || size == 0) return;

    for (std::uint32_t i = 0; i < size; ++i) {
        std::uint32_t handle = 0;
        if (ReadActorHandleAtGuarded(data, i, &handle) && handle != 0) {
            handles.push_back(handle);
        }
    }
}

std::vector<std::uint32_t> CollectProcessListActorHandles() {
    std::vector<std::uint32_t> handles;
    void* processLists = GetProcessListsGuarded();
    if (!processLists) return handles;

    auto* base = static_cast<std::uint8_t*>(processLists);
    CollectActorHandlesFromArray(base + game::process::ProcessLists_HighActorHandles, handles);
    CollectActorHandlesFromArray(base + game::process::ProcessLists_MiddleHighActorHandles, handles);
    CollectActorHandlesFromArray(base + game::process::ProcessLists_MiddleLowActorHandles, handles);
    CollectActorHandlesFromArray(base + game::process::ProcessLists_LowActorHandles, handles);
    return handles;
}

bool IsFormFed(std::uint32_t formID) {
    return g_loaderApi && g_loaderApi->SaveFormIDSetContainsForModule &&
           g_loaderApi->SaveFormIDSetContainsForModule(g_selfModule, kFedCorpseSet, formID);
}

bool BuildBloodScentTargetFromHandle(std::uint32_t handle,
                                     std::uint32_t expectedFormID,
                                     TargetInfo* out) {
    if (!out || handle == 0) return false;

    void* actor = ResolveRefHandleRawGuarded(handle);
    if (!actor) return false;

    TargetInfo target{};
    target.handle = handle;
    target.actor = actor;
    target.formID = ReadU32Guarded(actor, 0x14);
    target.formType = ReadU8Guarded(actor, game::form::FormType);
    if (target.formID == 0) return false;
    if (expectedFormID != 0 && target.formID != expectedFormID) return false;

    *out = target;
    return true;
}

bool IsBloodScentBaseCandidate(const TargetInfo& target) {
    if (!target.actor || target.formID == 0) return false;
    if (target.formType != kFormTypeActorCharacter || target.formID == kPlayerRefID) return false;

    const std::uint32_t flags = ReadU32Guarded(target.actor, 0x10);
    if ((flags & (kFormFlagDeleted | kFormFlagDisabled)) != 0) return false;

    bool has3D = false;
    if (!HasCurrent3DGuarded(target.actor, &has3D) || !has3D) return false;

    bool dead = false;
    if (!IsDeadGuarded(target.actor, &dead) || !dead) return false;

    float consumedMarker = 0.0f;
    if (GetActorValueGuarded(target.actor, game::actor::AV_Variable08, &consumedMarker) &&
        consumedMarker >= 8.5f) {
        return false;
    }

    if (IsFormFed(target.formID)) return false;
    return true;
}

bool IsBloodScentCandidateInRange(void* player, const TargetInfo& target, float* outDistance) {
    if (outDistance) *outDistance = 0.0f;
    if (!player || !IsBloodScentBaseCandidate(target)) return false;

    float distance = 0.0f;
    if (!DistanceBetweenRefsGuarded(player, target.actor, &distance)) {
        return false;
    }
    if (distance > EffectiveBloodScentRange()) {
        return false;
    }

    if (outDistance) *outDistance = distance;
    return true;
}

BloodScentTargetMap RebuildBloodScentCandidateCache(std::uint32_t* scannedCount) {
    if (scannedCount) *scannedCount = 0;

    BloodScentTargetMap candidates;
    if (!g_loaderApi || !g_loaderApi->SaveStorageAvailable || !g_loaderApi->SaveStorageAvailable()) {
        return candidates;
    }

    std::unordered_set<std::uint32_t> seenForms;
    const auto handles = CollectProcessListActorHandles();
    if (scannedCount) *scannedCount = static_cast<std::uint32_t>(handles.size());

    for (const auto handle : handles) {
        TargetInfo target{};
        if (!BuildBloodScentTargetFromHandle(handle, 0, &target)) continue;
        if (target.formID == 0 || !seenForms.insert(target.formID).second) continue;

        if (IsBloodScentBaseCandidate(target)) {
            candidates.emplace(target.formID, target.handle);
        }
    }

    {
        std::lock_guard lock(g_bloodScentMutex);
        g_bloodScentCandidateTargets = candidates;
    }
    return candidates;
}

BloodScentTargetMap CopyBloodScentCandidateCache() {
    std::lock_guard lock(g_bloodScentMutex);
    return g_bloodScentCandidateTargets;
}

BloodScentTargetMap FindBloodScentTargetsFromCandidates(void* player,
                                                        const BloodScentTargetMap& candidates,
                                                        std::uint32_t* evaluatedCount,
                                                        std::uint32_t* staleCount) {
    if (evaluatedCount) *evaluatedCount = 0;
    if (staleCount) *staleCount = 0;

    BloodScentTargetMap desired;
    if (!player || !g_loaderApi || !g_loaderApi->SaveStorageAvailable || !g_loaderApi->SaveStorageAvailable()) {
        return desired;
    }

    std::vector<std::uint32_t> staleForms;
    for (const auto& [formID, handle] : candidates) {
        if (evaluatedCount) ++(*evaluatedCount);

        TargetInfo target{};
        if (!BuildBloodScentTargetFromHandle(handle, formID, &target) || !IsBloodScentBaseCandidate(target)) {
            staleForms.push_back(formID);
            continue;
        }

        float distance = 0.0f;
        if (IsBloodScentCandidateInRange(player, target, &distance)) {
            desired.emplace(target.formID, target.handle);
            HAG_INFO("Blood Scent target: form={:#x} handle={:#x} distance={}",
                     target.formID,
                     target.handle,
                     distance);
        }
    }

    if (!staleForms.empty()) {
        std::lock_guard lock(g_bloodScentMutex);
        for (const auto formID : staleForms) {
            g_bloodScentCandidateTargets.erase(formID);
        }
        if (staleCount) *staleCount = static_cast<std::uint32_t>(staleForms.size());
    }

    return desired;
}

std::uint32_t FindProcessListHandleForActor(void* actor, std::uint32_t formID) {
    if (!actor && formID == 0) return 0;

    const auto handles = CollectProcessListActorHandles();
    for (const auto handle : handles) {
        void* candidate = ResolveRefHandleRawGuarded(handle);
        if (!candidate) continue;
        if (candidate == actor) return handle;
        if (formID != 0 && ReadU32Guarded(candidate, 0x14) == formID) return handle;
    }
    return 0;
}

void TrackBloodScentCandidateFromActor(void* actor, const char* reason) {
    if (!actor || !g_corpseFeedingEnabled.load()) return;

    TargetInfo target{};
    target.actor = actor;
    target.formID = ReadU32Guarded(actor, 0x14);
    target.formType = ReadU8Guarded(actor, game::form::FormType);
    if (!IsBloodScentBaseCandidate(target)) return;

    target.handle = FindProcessListHandleForActor(actor, target.formID);
    if (target.handle == 0) {
        HAG_WARN("Blood Scent candidate add skipped ({}): no process-list handle for form={:#x}",
                 reason ? reason : "unspecified",
                 target.formID);
        return;
    }

    bool inserted = false;
    {
        std::lock_guard lock(g_bloodScentMutex);
        inserted = !g_bloodScentCandidateTargets.contains(target.formID);
        g_bloodScentCandidateTargets[target.formID] = target.handle;
    }

    HAG_INFO("Blood Scent candidate {} ({}): form={:#x} handle={:#x}",
             inserted ? "added" : "updated",
             reason ? reason : "unspecified",
             target.formID,
             target.handle);
    QueueBloodScentGateRefresh(reason ? reason : "actor death");
}

void ApplyBloodScentSet(const BloodScentTargetMap& desired, const char* reason) {
    std::vector<std::pair<std::uint32_t, std::uint32_t>> toApply;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> toClear;
    BloodScentTargetMap previous;

    {
        std::lock_guard lock(g_bloodScentMutex);
        previous = g_bloodScentActiveTargets;
    }

    for (const auto& [formID, handle] : desired) {
        if (!previous.contains(formID)) {
            toApply.emplace_back(formID, handle);
        }
    }
    for (const auto& [formID, handle] : previous) {
        if (!desired.contains(formID)) {
            toClear.emplace_back(formID, handle);
        }
    }

    std::vector<std::pair<std::uint32_t, std::uint32_t>> applied;
    std::vector<std::uint32_t> cleared;
    for (const auto& [formID, handle] : toApply) {
        if (ApplyBloodScentShader(formID)) {
            applied.emplace_back(formID, handle);
        }
    }
    for (const auto& [formID, handle] : toClear) {
        if (ClearBloodScentShaderNative(formID, handle)) {
            cleared.push_back(formID);
        }
    }

    std::size_t activeCount = 0;
    {
        std::lock_guard lock(g_bloodScentMutex);
        for (const auto& [formID, handle] : desired) {
            if (previous.contains(formID)) {
                g_bloodScentActiveTargets[formID] = handle;
            }
        }
        for (const auto& [formID, handle] : applied) {
            g_bloodScentActiveTargets[formID] = handle;
        }
        for (const auto formID : cleared) {
            g_bloodScentActiveTargets.erase(formID);
        }
        activeCount = g_bloodScentActiveTargets.size();
    }

    HAG_INFO("Blood Scent refreshed ({}): active={} applied={} cleared={}",
             reason ? reason : "unspecified",
             activeCount,
             applied.size(),
             cleared.size());
}

void ClearBloodScentHighlights(const char* reason) {
    const BloodScentTargetMap empty;
    ApplyBloodScentSet(empty, reason);
}

void RefreshBloodScentHighlights(const char* reason, bool rebuildCandidates = true) {
    if (!g_corpseFeedingEnabled.load()) {
        ClearBloodScentHighlights("corpse feeding disabled");
        return;
    }

    if (!HasVampireBonusLevel(kBloodScentMinLevel)) {
        ClearBloodScentHighlights("vampire bonus unavailable");
        return;
    }

    void* player = GetPlayerActorGuarded();
    if (!player) {
        ClearBloodScentHighlights("player unavailable");
        return;
    }

    std::uint32_t scanned = 0;
    std::uint32_t evaluated = 0;
    std::uint32_t stale = 0;
    BloodScentTargetMap candidates = rebuildCandidates
        ? RebuildBloodScentCandidateCache(&scanned)
        : CopyBloodScentCandidateCache();
    auto desired = FindBloodScentTargetsFromCandidates(player, candidates, &evaluated, &stale);
    {
        NiPoint3 position{};
        if (ReadPoint3Guarded(player, game::refr::DataLocation, &position)) {
            std::lock_guard lock(g_bloodScentMovementMutex);
            g_lastBloodScentScanPosition = position;
            g_hasLastBloodScentScanPosition = true;
        }
    }
    if (rebuildCandidates) {
        HAG_INFO("Blood Scent candidate rebuild ({}): scannedActorHandles={} candidates={}",
                 reason ? reason : "unspecified",
                 scanned,
                 candidates.size());
    }
    HAG_INFO("Blood Scent gate eval ({}): cachedCandidates={} evaluated={} stale={} desired={}",
             reason ? reason : "unspecified",
             candidates.size(),
             evaluated,
             stale,
             desired.size());
    ApplyBloodScentSet(desired, reason);
}

void RefreshBloodScentTask(void* user) {
    RefreshBloodScentHighlights(static_cast<const char*>(user), true);
}

void RefreshBloodScentGateTask(void* user) {
    RefreshBloodScentHighlights(static_cast<const char*>(user), false);
}

void QueueBloodScentRefresh(const char* reason) {
    if (!g_loaderApi || !g_loaderApi->QueueMainThreadTask) return;
    if (!g_loaderApi->QueueMainThreadTask(&RefreshBloodScentTask, const_cast<char*>(reason))) {
        HAG_WARN("Blood Scent refresh queue failed ({})", reason ? reason : "unspecified");
    }
}

void QueueBloodScentGateRefresh(const char* reason) {
    if (!g_loaderApi || !g_loaderApi->QueueMainThreadTask) return;
    if (!g_loaderApi->QueueMainThreadTask(&RefreshBloodScentGateTask, const_cast<char*>(reason))) {
        HAG_WARN("Blood Scent gate refresh queue failed ({})", reason ? reason : "unspecified");
    }
}

void OnCellChange(void*) {
    QueueBloodScentRefresh("cell change");
}

bool ShouldRefreshBloodScentForMovement(const NiPoint3& position) {
    if (!g_corpseFeedingEnabled.load()) return false;
    if (CurrentBloodStrengthLevel() < kBloodScentMinLevel) return false;

    std::lock_guard lock(g_bloodScentMovementMutex);
    if (!g_hasLastBloodScentScanPosition) {
        g_lastBloodScentScanPosition = position;
        g_hasLastBloodScentScanPosition = true;
        return true;
    }

    constexpr float kRefreshDistanceSquared =
        kBloodScentMovementRefreshDistance * kBloodScentMovementRefreshDistance;
    if (DistanceSquared(position, g_lastBloodScentScanPosition) < kRefreshDistanceSquared) {
        return false;
    }

    g_lastBloodScentScanPosition = position;
    return true;
}

void Detour_SetReferenceLocation(void* ref, const NiPoint3* position) {
    bool playerMoved = false;
    NiPoint3 nextPosition{};
    __try {
        void* player = GetPlayerActorGuarded();
        if (player && ref == player && position) {
            nextPosition = *position;
            playerMoved = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        playerMoved = false;
    }

    if (g_origSetReferenceLocation) {
        g_origSetReferenceLocation(ref, position);
    }

    if (playerMoved && ShouldRefreshBloodScentForMovement(nextPosition)) {
        QueueBloodScentGateRefresh("player movement");
    }
}

bool InstallBloodScentMovementHook() {
    if (g_bloodScentMovementHookInstalled.load()) return true;

    const auto initStatus = MH_Initialize();
    if (initStatus != MH_OK && initStatus != MH_ERROR_ALREADY_INITIALIZED) {
        HAG_ERR("Blood Scent movement hook failed: MH_Initialize status={}", static_cast<int>(initStatus));
        return false;
    }

    const auto target = SkyrimBase() + game::refr::SetLocation;
    const auto createStatus = MH_CreateHook(reinterpret_cast<LPVOID>(target),
                                            reinterpret_cast<LPVOID>(&Detour_SetReferenceLocation),
                                            reinterpret_cast<LPVOID*>(&g_origSetReferenceLocation));
    if (createStatus != MH_OK) {
        HAG_ERR("Blood Scent movement hook failed: MH_CreateHook target={:#x} status={}",
                target,
                static_cast<int>(createStatus));
        return false;
    }

    const auto enableStatus = MH_EnableHook(reinterpret_cast<LPVOID>(target));
    if (enableStatus != MH_OK && enableStatus != MH_ERROR_ENABLED) {
        HAG_ERR("Blood Scent movement hook failed: MH_EnableHook target={:#x} status={}",
                target,
                static_cast<int>(enableStatus));
        return false;
    }

    g_bloodScentMovementHookInstalled.store(true);
    HAG_INFO("Blood Scent movement hook installed: TESObjectREFR::SetLocation @{:#x}", target);
    return true;
}

bool RunNativeCorpseFeedGuarded(void* player, void* target, int animationMode) noexcept {
    if (!player || !target) return false;

    if (animationMode == 2) {
        HAG_INFO("native corpse-feed aborted: custom animation mode is not implemented yet");
        return false;
    }

    if (animationMode != 0) {
        HAG_INFO("native corpse-feed animation mode {} routed through engine cannibal package", animationMode);
    }

    if (!InitiateCannibalPackageGuarded(player, target)) {
        HAG_ERR("native corpse-feed failed: InitiateCannibalPackage faulted");
        CleanupCorpseFeedState(player, "package start failed");
        return false;
    }

    if (!SetActorValueGuarded(target, game::actor::AV_Variable08, 9.0f)) {
        HAG_ERR("native corpse-feed failed: could not mark target Variable08");
        CleanupCorpseFeedState(player, "target marker failed");
        return false;
    }

    bool cannibalActive = false;
    const bool cannibalReadOk = GetCannibalStateGuarded(player, &cannibalActive);
    HAG_INFO("native corpse-feed action started: package=InitiateCannibalPackage positioning=engine cannibalReadOk={} cannibalActive={}",
             cannibalReadOk,
             cannibalActive);
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
    CleanupCorpseFeedState(player, "pre feed");
    RefreshBloodScentHighlights("feed task start", false);

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

    if (!RunNativeCorpseFeedGuarded(player, target.actor, mode)) {
        HAG_ERR("native corpse-feed failed: native corpse action did not start");
        return;
    }
    if (!g_loaderApi->SaveFormIDSetAddForModule(g_selfModule, kFedCorpseSet, target.formID, kMaxFedCorpseEntries)) {
        HAG_ERR("native corpse-feed action started but failed to record fed corpse form={:#x}; refusing future unsafe repeats depends on save storage",
                target.formID);
        return;
    }
    AwardBloodExtract(hasCorpseLevel ? corpseLevel : 1, target.formID);
    ApplyFreshBloodBuff(player, target.formID);
    RefreshBloodScentHighlights("after feed", false);
    const auto cleanupGeneration = g_feedCleanupGeneration.fetch_add(1) + 1;
    if (!ScheduleFeedCleanup(cleanupGeneration, target.formID)) {
        HAG_WARN("native corpse-feed cleanup timer failed; clearing feed state immediately");
        CleanupCorpseFeedState(player, "cleanup timer failed");
    }
    if (g_uiApi && g_uiApi->Refresh) {
        g_uiApi->Refresh();
    }
    HAG_INFO("native corpse-feed completed: form={:#x} positioning=engine package",
             target.formID);
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
    QueueBloodScentRefresh("corpse feeding config");
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

const char* VampireRankText(void*) {
    const int level = BloodStrengthLevelForExtract(g_bloodExtract.load());
    return VampireRankNameForLevel(level);
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
    if (!g_loaderApi ||
        !g_loaderApi->QueuePapyrusStaticCallWithCallback || !g_loaderApi->QueueMainThreadTask ||
        !g_loaderApi->SaveStorageAvailable || !g_loaderApi->SaveFormIDSetContainsForModule ||
        !g_loaderApi->SaveFormIDSetAddForModule || !g_loaderApi->SaveFormIDSetCountForModule ||
        !g_loaderApi->RegisterHotkeyForModule || !g_loaderApi->RegisterCellChangeCallbackForModule) {
        throw std::runtime_error("HagVampire requires HagLoader queue/config/save/hotkey APIs");
    }
    LoadConfig();
    if (!InstallDamageHook()) {
        HAG_WARN("Fresh Blood lifesteal unavailable: damage hook did not install");
    }
    if (!InstallActorDeathHook()) {
        HAG_WARN("Blood Scent actor-death candidate tracking unavailable");
    }
    if (!InstallBloodScentMovementHook()) {
        HAG_WARN("Blood Scent will only refresh on save load/cell change/feed events");
    }
    RegisterFeedHotkey();
    if (!g_loaderApi->RegisterCellChangeCallbackForModule(g_selfModule, &OnCellChange, nullptr)) {
        HAG_WARN("Blood Scent cell-change callback registration failed");
    }
    QueueBloodScentRefresh("init");
    QueueFeedCleanup("init");

    auto getApi = reinterpret_cast<HagUI_GetAPIFn>(h ? ::GetProcAddress(h, "HagUI_GetAPI") : nullptr);
    g_uiApi = getApi ? getApi(HAGUI_ABI_VERSION) : nullptr;
    g_page = page;
    if (!g_uiApi || !page) {
        throw std::runtime_error("HagVampire requires HagLoader HagUI API/page");
    }

    if (!g_uiApi->AddDynamicButton || !g_uiApi->SetIntState || !g_uiApi->AddHotkey ||
        !g_uiApi->AddCounter || !g_uiApi->SetGridCell || !g_uiApi->SetDoublePage) {
        throw std::runtime_error("HagVampire requires HagUI dynamic button/state/hotkey/counter/grid/double-page API");
    }

    g_uiApi->SetDoublePage(page, true);
    g_uiApi->AddDynamicButton(page,
                              "vampire_action",
                              "Transform into a Vampire",
                              &VampireActionLabel,
                              &OnVampireActionClicked,
                              nullptr);
    g_uiApi->AddCounter(page,
                        "vampire_rank",
                        "Vampire rank",
                        &VampireRankText,
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
    g_uiApi->SetGridCell(page, "vampire_rank", 0, 1);
    g_uiApi->SetGridCell(page, "enable_corpse_feeding", 1, 0);
    g_uiApi->SetGridCell(page, "feed_hotkey", 1, 1);
    g_uiApi->SetGridCell(page, "feeding_counter", 1, 2);
    PushConfigToUI();
    g_uiApi->Refresh();

    HAG_INFO("HagVampire page registered");
}

void OnSaveLoaded() {
    ReloadSaveConfig("SKSE save context ready");
    InstallActorDeathHook();
    RegisterFeedHotkey();
    ApplyUnlockedRankRewards("save loaded");
    QueueBloodScentRefresh("save loaded");
    QueueFeedCleanup("save loaded");
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
