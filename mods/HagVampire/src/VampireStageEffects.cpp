#include "PCH.h"
#include "VampireStageEffects.h"

#include "GameOffsets.h"
#include "Log.h"

#include <MinHook.h>

#include <array>
#include <cstddef>

namespace hag::vampire_stage {
namespace {

constexpr std::array<std::uint32_t, 4> kFrost{0x02E1C3, 0x0ED099, 0x0ED09D, 0x0ED09E};
constexpr std::array<std::uint32_t, 4> kDrain{0x08D5BF, 0x08D5C0, 0x08D5C1, 0x08D5C2};
constexpr std::array<std::uint32_t, 4> kServant{0x0ED0A4, 0x0ED0A5, 0x0ED0A6, 0x0ED0A7};
constexpr std::array<std::uint32_t, 4> kReanimateEffects{0x065BD6, 0x096D0B, 0x096D0C, 0x096D0D};
constexpr std::uint32_t kFrostEffect = 0x024315;
constexpr std::uint32_t kDrainHealthEffect = 0x0F5B57;
constexpr std::uint32_t kAshPileEffect = 0x0F52AB;
constexpr std::array<float, 4> kReverseFrost{50.0f, 40.0f, 30.0f, 20.0f};
constexpr std::array<float, 4> kReverseDrain{5.0f, 4.0f, 3.0f, 2.0f};
constexpr std::uint8_t kSpellFormType = 0x16;
constexpr std::uint16_t kGetLevelCondition = 80;
constexpr std::uint32_t kTargetTooPowerful = 2;

struct EffectCheckData {
    void* magicTarget;
    void* caster;
    float magnitude;
    void* baseEffect;
    void* spell;
    std::uint8_t usePerks;
    std::uint32_t failure;
};
static_assert(offsetof(EffectCheckData, magnitude) == 0x10);
static_assert(offsetof(EffectCheckData, baseEffect) == 0x18);
static_assert(offsetof(EffectCheckData, spell) == 0x20);
static_assert(offsetof(EffectCheckData, failure) == 0x2C);

using SpellArray = std::array<void*, 4>;
using ReanimateCheck = bool (*)(EffectCheckData*);
using EvaluateCondition = bool (*)(const void*, void*);

struct EffectMagnitude {
    float* value = nullptr;
    float original = 0.0f;

    void Apply(bool reverse, float reversed) const noexcept {
        if (value) *value = reverse ? reversed : original;
    }
};

SpellArray g_frostSpells{};
std::array<EffectMagnitude, 4> g_frost{};
std::array<std::array<EffectMagnitude, 3>, 4> g_drain{};
SpellArray g_servantSpells{};
SpellArray g_servantBaseEffects{};
const void* g_ashPileLevelCheck = nullptr;
ReanimateCheck g_originalReanimate = nullptr;
EvaluateCondition g_originalCondition = nullptr;

std::uintptr_t GameBase() noexcept {
    return reinterpret_cast<std::uintptr_t>(::GetModuleHandleW(nullptr));
}

template <typename T>
T& Field(void* object, std::size_t offset) noexcept {
    return *reinterpret_cast<T*>(static_cast<std::byte*>(object) + offset);
}

void* Lookup(std::uint32_t formID) noexcept {
    return reinterpret_cast<void* (*)(std::uint32_t)>(GameBase() + game::form::LookupByID)(formID);
}

std::uint32_t FormID(void* form) noexcept {
    return form ? Field<std::uint32_t>(form, game::form::FormID) : 0;
}

void** SpellEffects(void* spell, std::uint32_t& count) noexcept {
    if (!spell || Field<std::uint8_t>(spell, game::form::FormType) != kSpellFormType) return nullptr;
    count = Field<std::uint32_t>(spell, game::magic::MagicItemEffectCount);
    if (!count || count > 16) return nullptr;
    return Field<void**>(spell, game::magic::MagicItemEffects);
}

void* Player() noexcept {
    return *reinterpret_cast<void**>(GameBase() + game::actor::PlayerSingletonPtr);
}

std::uint16_t Level(void* actor) noexcept {
    return reinterpret_cast<std::uint16_t (*)(void*)>(GameBase() + game::actor::GetLevel)(actor);
}

EffectMagnitude CaptureMagnitude(void* effect) noexcept {
    auto& value = Field<float>(effect, game::magic::EffectMagnitude);
    return {&value, value};
}

bool CaptureFrost() noexcept {
    SpellArray spells{};
    decltype(g_frost) effects{};
    for (std::size_t i = 0; i < spells.size(); ++i) {
        spells[i] = Lookup(kFrost[i]);
        std::uint32_t count = 0;
        void** list = SpellEffects(spells[i], count);
        if (!list || count != 1 || !list[0] ||
            FormID(Field<void*>(list[0], game::magic::EffectBaseEffect)) != kFrostEffect) return false;
        effects[i] = CaptureMagnitude(list[0]);
    }
    g_frostSpells = spells;
    g_frost = effects;
    return true;
}

bool CaptureDrain() noexcept {
    decltype(g_drain) effects{};
    for (std::size_t i = 0; i < effects.size(); ++i) {
        std::uint32_t count = 0;
        void** list = SpellEffects(Lookup(kDrain[i]), count);
        if (!list) return false;
        for (std::uint32_t j = 0; j < count; ++j) {
            if (!list[j]) continue;
            const auto id = FormID(Field<void*>(list[j], game::magic::EffectBaseEffect));
            if (id == kDrainHealthEffect) effects[i][0] = CaptureMagnitude(list[j]);
            // Vanilla Dawnguard's optional Chalice/Redwater effects; leave their conditions intact.
            else if (id == 0x0201419C) effects[i][1] = CaptureMagnitude(list[j]);
            else if (id == 0x0201419A) effects[i][2] = CaptureMagnitude(list[j]);
        }
        if (!effects[i][0].value) return false;
    }
    g_drain = effects;
    return true;
}

void ApplyMagnitudes(bool reverse) {
    for (std::size_t i = 0; i < kReverseFrost.size(); ++i) {
        g_frost[i].Apply(reverse, kReverseFrost[i]);
        for (const auto& effect : g_drain[i]) effect.Apply(reverse, kReverseDrain[i]);
    }
    HAG_INFO("Vampire progression applied: reverse={} frost={} drain={}",
             reverse, g_frost[0].value != nullptr, g_drain[0][0].value != nullptr);
}

bool ResolveServantRecords() noexcept {
    for (std::size_t i = 0; i < g_servantSpells.size(); ++i) {
        g_servantSpells[i] = Lookup(kServant[i]);
        std::uint32_t count = 0;
        void** list = SpellEffects(g_servantSpells[i], count);
        if (!list) return false;
        for (std::uint32_t j = 0; j < count; ++j) {
            if (!list[j]) continue;
            void* base = Field<void*>(list[j], game::magic::EffectBaseEffect);
            const std::uint32_t id = FormID(base);
            if (id == kReanimateEffects[i]) g_servantBaseEffects[i] = base;
            if (i != 0 || id != kAshPileEffect) continue;
            for (void* item = Field<void*>(list[j], game::magic::EffectConditions); item;
                 item = Field<void*>(item, 0)) {
                if (Field<std::uint16_t>(item, game::magic::ConditionFunctionID) == kGetLevelCondition &&
                    Field<std::uint8_t>(item, game::magic::ConditionFlags) == 0xA0 &&
                    Field<float>(item, game::magic::ConditionComparisonValue) == 6.0f) {
                    g_ashPileLevelCheck = item;
                    break;
                }
            }
        }
        if (!g_servantBaseEffects[i]) return false;
    }
    return g_ashPileLevelCheck != nullptr;
}

bool CheckReanimate(EffectCheckData* data) {
    if (!data || !data->caster || data->caster != Player()) return g_originalReanimate(data);
    bool servant = false;
    for (std::size_t i = 0; i < g_servantSpells.size(); ++i) {
        if (data->spell == g_servantSpells[i] && data->baseEffect == g_servantBaseEffects[i]) {
            servant = true;
            break;
        }
    }
    if (!servant) return g_originalReanimate(data);

    const auto toActor = reinterpret_cast<void* (*)(void*)>(GameBase() + game::magic::MagicTargetToActor);
    const auto canReanimate = reinterpret_cast<bool (*)(void*)>(GameBase() + game::magic::CanReanimateActor);
    void* target = data->magicTarget ? toActor(data->magicTarget) : nullptr;
    if (!target || !canReanimate(target)) return false;

    const auto cap = Level(data->caster);
    const auto targetLevel = Level(target);
    const bool allowed = targetLevel <= cap;
    if (!allowed) data->failure = kTargetTooPowerful;
    HAG_INFO("Vampire's Servant native check: spell={:#x} target={:#x} level={} cap={} allowed={}",
             FormID(data->spell), FormID(target), targetLevel, cap, allowed);
    return allowed;
}

bool CheckAshPileCondition(const void* item, void* params) {
    if (item != g_ashPileLevelCheck || !params) return g_originalCondition(item, params);
    void* player = Player();
    if (!player || (Field<void*>(params, 0) != player && Field<void*>(params, 0x08) != player)) {
        return g_originalCondition(item, params);
    }
    alignas(void*) std::array<std::byte, game::magic::ConditionItemSize> copy{};
    std::memcpy(copy.data(), item, copy.size());
    const float cap = static_cast<float>(Level(player));
    std::memcpy(copy.data() + game::magic::ConditionComparisonValue, &cap, sizeof(cap));
    return g_originalCondition(copy.data(), params);
}

bool InstallServantHooks() {
    if (g_originalReanimate) return true;
    if (!ResolveServantRecords()) return false;
    auto* reanimate = reinterpret_cast<void*>(GameBase() + game::magic::CheckReanimateTarget);
    auto* condition = reinterpret_cast<void*>(GameBase() + game::magic::EvaluateConditionItem);
    const auto slot = reinterpret_cast<void**>(GameBase() + game::magic::ReanimateCheckSlot);
    constexpr std::uint8_t reanimateEntry[]{0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20};
    constexpr std::uint8_t conditionEntry[]{0x48, 0x89, 0x4C, 0x24, 0x08, 0x55, 0x53, 0x56, 0x57};
    if (*slot != reanimate || std::memcmp(reanimate, reanimateEntry, sizeof(reanimateEntry)) != 0 ||
        std::memcmp(condition, conditionEntry, sizeof(conditionEntry)) != 0) return false;

    const auto init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) return false;
    auto status = MH_CreateHook(reanimate, reinterpret_cast<void*>(&CheckReanimate),
                                reinterpret_cast<void**>(&g_originalReanimate));
    if (status != MH_OK) return false;
    status = MH_CreateHook(condition, reinterpret_cast<void*>(&CheckAshPileCondition),
                           reinterpret_cast<void**>(&g_originalCondition));
    if (status == MH_OK) {
        if (MH_EnableHook(condition) == MH_OK && MH_EnableHook(reanimate) == MH_OK) return true;
        MH_RemoveHook(condition);
    }
    MH_RemoveHook(reanimate);
    g_originalCondition = nullptr;
    g_originalReanimate = nullptr;
    return false;
}

void RefreshPlayerFrost() {
    void* player = Player();
    if (!player || !g_frostSpells[0]) return;
    using SpellFn = bool (*)(void*, void*);
    const auto hasSpell = reinterpret_cast<SpellFn>(GameBase() + game::actor::HasSpell);
    const auto removeSpell = reinterpret_cast<SpellFn>(GameBase() + game::actor::RemoveSpell);
    const auto addSpell = reinterpret_cast<SpellFn>(GameBase() + game::actor::AddSpell);
    // An active ability can retain its old magnitude after a mode change or load.
    for (void* spell : g_frostSpells) {
        if (!hasSpell(player, spell) || !removeSpell(player, spell)) continue;
        if (addSpell(player, spell)) HAG_INFO("Refreshed loaded Vampire frost ability {:#x}", FormID(spell));
        else HAG_WARN("Failed to reapply loaded Vampire frost ability {:#x}", FormID(spell));
    }
}

}  // namespace

void OnDataLoaded(bool reverse) {
    if (!CaptureFrost()) HAG_WARN("Vampire frost resistance unchanged: unexpected spell records");
    if (!CaptureDrain()) HAG_WARN("Vampiric Drain unchanged: unexpected spell records");
    ApplyMagnitudes(reverse);
    if (InstallServantHooks()) HAG_INFO("Vampire's Servant uses the native player-level cap");
    else HAG_WARN("Vampire's Servant unchanged: native level checks did not install");
}

void ApplyProgression(bool reverse) {
    ApplyMagnitudes(reverse);
    RefreshPlayerFrost();
}

}  // namespace hag::vampire_stage
