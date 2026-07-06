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
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
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
constexpr std::uint32_t kFormFlagDeleted = 1u << 5;
constexpr std::uint32_t kFormFlagDisabled = 1u << 11;
constexpr std::uint8_t kFormTypeActorCharacter = 0x3E;
constexpr std::uint32_t kPlayerRefID = 0x14;
constexpr std::uint32_t kIdleCannibalFeedCrouching = 0x000FE09F;
constexpr std::uint32_t kIdleVampireFeedingBedrollLeft = 0x00023622;
constexpr float kCorpseFeedDistance = 40.0f;
constexpr float kBloodScentRange = 700.0f;
constexpr std::uint32_t kBloodScentEffectShader = 0x000DC209;  // LifeDetectedEnemy EFSH.
constexpr float kBloodScentShaderDuration = -1.0f;
constexpr float kFreshBloodBonusFraction = 0.10f;
constexpr float kFreshBloodMinBonus = 0.1f;
constexpr std::uint32_t kFreshBloodDurationMs = 5u * 60u * 1000u;
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

struct VampireRank {
    const char* name;
    int level;
};

struct FreshBloodState {
    bool active = false;
    float magickaRateBonus = 0.0f;
    float staminaRateBonus = 0.0f;
};

struct FreshBloodTimerContext {
    HANDLE timer = nullptr;
    std::uint32_t generation = 0;
};

struct BloodScentEffect {
    void* effect = nullptr;
    void* controller = nullptr;
};

struct BSSpinLockRaw {
    volatile LONG owningThread = 0;
    volatile LONG lockCount = 0;
};

std::mutex g_freshBloodMutex;
FreshBloodState g_freshBlood{};
std::atomic<std::uint32_t> g_freshBloodGeneration{0};
std::mutex g_bloodScentMutex;
std::unordered_set<std::uint32_t> g_bloodScentActiveForms;
std::unordered_map<std::uint32_t, BloodScentEffect> g_bloodScentEffects;

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
    HAG_INFO("config loaded: enable_corpse_feeding={} animation_mode={} feed_hotkey={:#x} blood_extract={} level={} rank={}",
             g_corpseFeedingEnabled.load(),
             g_animationMode.load(),
             g_feedHotkey.load(),
             g_bloodExtract.load(),
             BloodStrengthLevelForExtract(g_bloodExtract.load()),
             VampireRankNameForLevel(BloodStrengthLevelForExtract(g_bloodExtract.load())));
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

bool DistanceBetweenRefsGuarded(void* a, void* b, float* distance) noexcept {
    if (distance) *distance = 0.0f;
    if (!a || !b || !distance) return false;

    NiPoint3 apos{};
    NiPoint3 bpos{};
    if (!ReadPoint3Guarded(a, game::refr::DataLocation, &apos) ||
        !ReadPoint3Guarded(b, game::refr::DataLocation, &bpos)) {
        return false;
    }

    const float dx = apos.x - bpos.x;
    const float dy = apos.y - bpos.y;
    const float dz = apos.z - bpos.z;
    *distance = std::sqrt((dx * dx) + (dy * dy) + (dz * dz));
    return true;
}

bool ChangeActorValueByDeltaGuarded(void* actor, std::uint32_t actorValue, float delta) noexcept {
    float current = 0.0f;
    if (!GetActorValueGuarded(actor, actorValue, &current)) return false;
    return SetActorValueGuarded(actor, actorValue, current + delta);
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

    if (!ChangeActorValueByDeltaGuarded(player, game::actor::AV_MagickaRate, magickaBonus)) {
        HAG_WARN("Fresh Blood skipped: could not apply magicka regen bonus");
        return;
    }
    if (!ChangeActorValueByDeltaGuarded(player, game::actor::AV_StaminaRate, staminaBonus)) {
        ChangeActorValueByDeltaGuarded(player, game::actor::AV_MagickaRate, -magickaBonus);
        HAG_WARN("Fresh Blood skipped: could not apply stamina regen bonus");
        return;
    }

    const auto generation = g_freshBloodGeneration.fetch_add(1) + 1;
    g_freshBlood.active = true;
    g_freshBlood.magickaRateBonus = magickaBonus;
    g_freshBlood.staminaRateBonus = staminaBonus;

    if (!ScheduleFreshBloodRevert(generation)) {
        RemoveFreshBloodBonusesLocked(player);
        HAG_WARN("Fresh Blood skipped: could not create expiry timer");
        return;
    }

    HAG_INFO("Fresh Blood applied: sourceForm={:#x} magickaRate +{} staminaRate +{} durationMs={} generation={}",
             sourceFormID,
             magickaBonus,
             staminaBonus,
             kFreshBloodDurationMs,
             generation);
}

bool QueueConsoleCommand(const char* command) {
    if (!command || !*command || !g_loaderApi || !g_loaderApi->QueueConsoleCommand) return false;
    const bool queued = g_loaderApi->QueueConsoleCommand(command);
    if (!queued) {
        HAG_WARN("Blood Scent console command could not be queued: {}", command);
    }
    return queued;
}

bool ApplyEffectShaderGuarded(void* target, std::uint32_t shaderFormID, float duration, void** effectOut) noexcept {
    if (effectOut) *effectOut = nullptr;
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
        using ApplyEffectShaderFn = void* (*)(void*, void*, float, void*, bool, bool, void*, bool);
        auto applyEffectShader = reinterpret_cast<ApplyEffectShaderFn>(
            SkyrimBase() + game::refr::ApplyEffectShader);
        void* effect = applyEffectShader(target, shader, duration, nullptr, false, false, nullptr, false);
        if (effectOut) *effectOut = effect;
        return effect != nullptr;
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

    void* effect = nullptr;
    const bool ok = ApplyEffectShaderGuarded(target, kBloodScentEffectShader, kBloodScentShaderDuration, &effect);
    if (!ok) {
        HAG_WARN("Blood Scent shader apply failed: target={:#x} shader={:#x}",
                 formID,
                 kBloodScentEffectShader);
        return false;
    }

    const BloodScentEffect tracked{
        effect,
        ReadPtrGuarded(effect, game::effect::ReferenceEffect_Controller)
    };
    {
        std::lock_guard lock(g_bloodScentMutex);
        g_bloodScentEffects[formID] = tracked;
    }

    HAG_INFO("Blood Scent shader applied: target={:#x} shader={:#x} effect={} controller={}",
             formID,
             kBloodScentEffectShader,
             tracked.effect,
             tracked.controller);
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

bool DecNiRefObjectGuarded(void* object) noexcept {
    if (!object) return false;
    __try {
        auto* refCount = reinterpret_cast<volatile LONG*>(
            static_cast<std::uint8_t*>(object) + game::effect::NiRefObject_RefCount);
        const LONG remaining = ::InterlockedDecrement(refCount);
        if (remaining == 0) {
            auto** vtbl = *reinterpret_cast<void***>(object);
            using DeleteThisFn = void (*)(void*);
            auto deleteThis = reinterpret_cast<DeleteThisFn>(
                vtbl[game::effect::NiRefObject_VSlot_DeleteThis]);
            deleteThis(object);
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
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

std::uint32_t ClearTrackedMagicEffectsGuarded(void** effects,
                                              std::uint32_t size,
                                              const BloodScentEffect& tracked,
                                              std::uint32_t* releaseFailures) noexcept {
    if (releaseFailures) *releaseFailures = 0;
    if (!effects || (!tracked.effect && !tracked.controller)) return 0;

    __try {
        std::uint32_t removed = 0;
        std::uint32_t failedReleases = 0;
        for (std::uint32_t i = 0; i < size; ++i) {
            void* effect = effects[i];
            if (!effect) continue;
            const bool matchByEffect = tracked.effect && effect == tracked.effect;
            const bool matchByController = tracked.controller &&
                ReadPtrGuarded(effect, game::effect::ReferenceEffect_Controller) == tracked.controller;
            if (matchByEffect || matchByController) {
                effects[i] = nullptr;
                if (!DecNiRefObjectGuarded(effect)) {
                    ++failedReleases;
                }
                ++removed;
            }
        }
        if (releaseFailures) *releaseFailures = failedReleases;
        return removed;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return UINT32_MAX;
    }
}

bool ClearBloodScentShaderNative(std::uint32_t formID) {
    BloodScentEffect tracked{};
    {
        std::lock_guard lock(g_bloodScentMutex);
        const auto it = g_bloodScentEffects.find(formID);
        if (it == g_bloodScentEffects.end()) {
            HAG_WARN("Blood Scent native clear had no tracked effect: target={:#x}", formID);
            return false;
        }
        tracked = it->second;
        g_bloodScentEffects.erase(it);
    }

    if (!tracked.effect && !tracked.controller) {
        HAG_WARN("Blood Scent native clear had empty tracked effect: target={:#x}", formID);
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
    std::uint32_t releaseFailures = 0;
    const auto removed = ClearTrackedMagicEffectsGuarded(effects, size, tracked, &releaseFailures);
    UnlockBSSpinLock(lock);

    if (removed == UINT32_MAX) {
        HAG_WARN("Blood Scent native clear faulted while scanning effects: target={:#x}", formID);
        return false;
    }
    if (releaseFailures != 0) {
        HAG_WARN("Blood Scent native clear removed {} effect(s) but {} refcount release(s) failed: target={:#x}",
                 removed,
                 releaseFailures,
                 formID);
    }

    HAG_INFO("Blood Scent native clear: target={:#x} removed={} trackedEffect={} trackedController={}",
             formID,
             removed,
             tracked.effect,
             tracked.controller);
    return removed != 0;
}

bool QueueTargetEffectShaderStopCommand(std::uint32_t formID) {
    if (formID == 0) return false;

    char prid[32];
    char sms[32];
    std::snprintf(prid, sizeof(prid), "prid %08X", formID);
    std::snprintf(sms, sizeof(sms), "sms %08X", kBloodScentEffectShader);

    bool ok = QueueConsoleCommand(prid);
    ok = QueueConsoleCommand(sms) && ok;
    return ok;
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

bool IsBloodScentCandidate(void* player, const TargetInfo& target, float* outDistance) {
    if (outDistance) *outDistance = 0.0f;
    if (!player || !target.actor || target.formID == 0) return false;
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

    float distance = 0.0f;
    if (!DistanceBetweenRefsGuarded(player, target.actor, &distance)) {
        return false;
    }
    if (distance > kBloodScentRange) {
        return false;
    }

    if (outDistance) *outDistance = distance;
    return true;
}

std::unordered_set<std::uint32_t> FindBloodScentTargets(void* player, std::uint32_t* scannedCount) {
    if (scannedCount) *scannedCount = 0;

    std::unordered_set<std::uint32_t> desired;
    if (!player || !g_loaderApi || !g_loaderApi->SaveStorageAvailable || !g_loaderApi->SaveStorageAvailable()) {
        return desired;
    }

    std::unordered_set<std::uint32_t> seenForms;
    const auto handles = CollectProcessListActorHandles();
    if (scannedCount) *scannedCount = static_cast<std::uint32_t>(handles.size());

    for (const auto handle : handles) {
        void* actor = ResolveRefHandleRawGuarded(handle);
        if (!actor) continue;

        TargetInfo target{};
        target.handle = handle;
        target.actor = actor;
        target.formID = ReadU32Guarded(actor, 0x14);
        target.formType = ReadU8Guarded(actor, game::form::FormType);
        if (target.formID == 0 || !seenForms.insert(target.formID).second) continue;

        float distance = 0.0f;
        if (IsBloodScentCandidate(player, target, &distance)) {
            desired.insert(target.formID);
            HAG_INFO("Blood Scent target: form={:#x} distance={}", target.formID, distance);
        }
    }

    return desired;
}

void ApplyBloodScentSet(const std::unordered_set<std::uint32_t>& desired, const char* reason) {
    std::vector<std::uint32_t> toApply;
    std::vector<std::uint32_t> toClear;

    {
        std::lock_guard lock(g_bloodScentMutex);
        for (const auto formID : desired) {
            if (!g_bloodScentActiveForms.contains(formID)) {
                toApply.push_back(formID);
            }
        }
        for (const auto formID : g_bloodScentActiveForms) {
            if (!desired.contains(formID)) {
                toClear.push_back(formID);
            }
        }
        g_bloodScentActiveForms = desired;
    }

    for (const auto formID : toApply) {
        ApplyBloodScentShader(formID);
    }
    for (const auto formID : toClear) {
        if (!ClearBloodScentShaderNative(formID)) {
            QueueTargetEffectShaderStopCommand(formID);
        }
    }

    if (!toApply.empty() || !toClear.empty()) {
        QueueConsoleCommand("prid 00000014");
    }

    HAG_INFO("Blood Scent refreshed ({}): active={} applied={} cleared={}",
             reason ? reason : "unspecified",
             desired.size(),
             toApply.size(),
             toClear.size());
}

void ClearBloodScentHighlights(const char* reason) {
    const std::unordered_set<std::uint32_t> empty;
    ApplyBloodScentSet(empty, reason);
}

void RefreshBloodScentHighlights(const char* reason) {
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
    auto desired = FindBloodScentTargets(player, &scanned);
    HAG_INFO("Blood Scent scan ({}): scannedActorHandles={} desired={}",
             reason ? reason : "unspecified",
             scanned,
             desired.size());
    ApplyBloodScentSet(desired, reason);
}

void RefreshBloodScentTask(void* user) {
    RefreshBloodScentHighlights(static_cast<const char*>(user));
}

void QueueBloodScentRefresh(const char* reason) {
    if (!g_loaderApi || !g_loaderApi->QueueMainThreadTask) return;
    if (!g_loaderApi->QueueMainThreadTask(&RefreshBloodScentTask, const_cast<char*>(reason))) {
        HAG_WARN("Blood Scent refresh queue failed ({})", reason ? reason : "unspecified");
    }
}

void OnCellChange(void*) {
    QueueBloodScentRefresh("cell change");
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
    RefreshBloodScentHighlights("feed task start");

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
    AwardBloodExtract(hasCorpseLevel ? corpseLevel : 1, target.formID);
    ApplyFreshBloodBuff(player, target.formID);
    RefreshBloodScentHighlights("after feed");
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
    if (!g_loaderApi || !g_loaderApi->QueueConsoleCommand ||
        !g_loaderApi->QueuePapyrusStaticCallWithCallback || !g_loaderApi->QueueMainThreadTask ||
        !g_loaderApi->SaveStorageAvailable || !g_loaderApi->SaveFormIDSetContainsForModule ||
        !g_loaderApi->SaveFormIDSetAddForModule || !g_loaderApi->SaveFormIDSetCountForModule ||
        !g_loaderApi->RegisterHotkeyForModule || !g_loaderApi->RegisterCellChangeCallbackForModule) {
        throw std::runtime_error("HagVampire requires HagLoader queue/config/save/hotkey APIs");
    }
    LoadConfig();
    RegisterFeedHotkey();
    if (!g_loaderApi->RegisterCellChangeCallbackForModule(g_selfModule, &OnCellChange, nullptr)) {
        HAG_WARN("Blood Scent cell-change callback registration failed");
    }
    QueueBloodScentRefresh("init");

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
    RegisterFeedHotkey();
    QueueBloodScentRefresh("save loaded");
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
