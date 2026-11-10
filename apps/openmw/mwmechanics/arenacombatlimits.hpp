#ifndef OPENMW_MWMECHANICS_ARENACOMBATLIMITS_H
#define OPENMW_MWMECHANICS_ARENACOMBATLIMITS_H
#include <algorithm>
#include <cmath>
namespace MWMechanics { namespace ArenaCombatLimits {
// Shared deterministic rules for physical attacks and magic. Record damage,
// speed, reach and elemental resistances remain authoritative inputs.
constexpr int MaxPlayerLevel = 150;
inline float softStat(float value)
{
    value = std::clamp(value, 0.f, 300.f);
    return std::min(value, 40.f) + std::clamp(value - 40.f, 0.f, 40.f) * .6f
        + std::clamp(value - 80.f, 0.f, 70.f) * .25f
        + std::max(0.f, value - 150.f) * .05f;
}
inline float offense(int level)
{
    return 1.f + .6f * (std::clamp(level, 1, 150) - 1) / 149.f;
}
inline float defense(int level)
{
    return 1.f - .3f * (std::clamp(level, 1, 150) - 1) / 149.f;
}
inline float stamina(float ratio)
{
    return .35f + .65f * std::clamp(ratio / .30f, 0.f, 1.f);
}
inline float weaponScaling(float skill, float attribute)
{
    return .70f + .009f * softStat(skill) + .004f * softStat(attribute);
}
inline float armorMultiplier(float rating)
{
    rating = std::max(0.f, rating);
    return std::max(.25f, 150.f / (150.f + rating));
}
inline float unarmoredRating(float skill)
{
    return .55f * softStat(skill);
}
// V24: player-only cost/regen multipliers. Values <= 40 keep the new baseline;
// fortification beyond 100 cannot make attacks free or amplify regen endlessly.
inline float playerAttackCostMultiplier(float endurance)
{
    return .5f * (1.f - .2f * std::clamp((endurance - 40.f) / 60.f, 0.f, 1.f));
}
inline float playerFatigueRegenMultiplier(float willpower)
{
    return 1.f + .15f * std::clamp((willpower - 40.f) / 60.f, 0.f, 1.f);
}
inline float attackCost(float weight, float charge, float load)
{
    return (6.f + std::clamp(weight, 0.f, 80.f) * .65f)
        * (1.f + .5f * std::clamp(charge, 0.f, 1.f))
        * (1.f + .4f * std::clamp(load, 0.f, 1.f));
}
// Y114: the PvE peak-damage ceiling is symmetrical. For attacker minus
// defender level = -50 / 0 / +50 the cap is 10% / 35% / 60% of target
// *maximum* health per hit or full spell application. Soft linear interpolation
// avoids an abrupt transition at a single level. Regular damage formulas,
// armour and resistances still decide actual HP loss.
inline float pveLevelDamageFraction(int attackerLevel, int defenderLevel)
{
    const int gap = std::clamp(attackerLevel, 1, MaxPlayerLevel)
        - std::clamp(defenderLevel, 1, MaxPlayerLevel);
    return std::clamp(.35f + .005f * static_cast<float>(gap), .10f, .60f);
}
inline float npcBurstFraction(int, float) { return 1.f; }
// Historical helper retained for old call sites; the old per-hit 100% bypass
// must NOT return for a PvE level gap.
inline float levelGapBurstFraction(float, float, int attackerLevel, int defenderLevel)
{
    return pveLevelDamageFraction(attackerLevel, defenderLevel);
}
// Y114: shared health budget for every direct-damage effect in one spell
// application on the same target. In particular, an instant + a 10-second
// Damage Health effect cannot each spend 35% at equal levels. Per-second safety
// is identical to the total cap; longer spells are limited by total/duration.
struct PveSpellBudget
{
    float instant = 0.f;
    float perSecond = 0.f;
    float total = 0.f;
};
inline PveSpellBudget makePveSpellBudget(float maxHealth, int casterLevel, int targetLevel)
{
    const float ceiling = std::max(0.f, maxHealth) * pveLevelDamageFraction(casterLevel, targetLevel);
    return {ceiling, ceiling, ceiling};
}
inline float reservePveSpellDamage(float magnitude, float duration, bool timed, PveSpellBudget& budget)
{
    if (magnitude <= 0.f) return 0.f;
    if (timed)
    {
        if (duration <= 0.f) return 0.f;
        const float allowed = std::max(0.f, std::min({magnitude, budget.perSecond, budget.total / duration}));
        budget.perSecond = std::max(0.f, budget.perSecond - allowed);
        budget.total = std::max(0.f, budget.total - allowed * duration);
        return allowed;
    }
    const float allowed = std::max(0.f, std::min({magnitude, budget.instant, budget.total}));
    budget.instant = std::max(0.f, budget.instant - allowed);
    budget.total = std::max(0.f, budget.total - allowed);
    return allowed;
}
// Reserve both a per-second rate and an entire-application amount. Native
// active effects integrate magnitude over dt; no frame count enters this rule.
inline float reserveDot(float magnitude, float duration, float& rate, float& total)
{
    if (magnitude <= 0.f || duration <= 0.f) return 0.f;
    const float allowed = std::max(0.f, std::min({magnitude, rate, total / duration}));
    rate = std::max(0.f, rate - allowed);
    total = std::max(0.f, total - allowed * duration);
    return allowed;
}
}}
#endif
