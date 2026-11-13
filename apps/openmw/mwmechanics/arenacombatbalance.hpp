#ifndef OPENMW_MWMECHANICS_ARENACOMBATBALANCE_HPP
#define OPENMW_MWMECHANICS_ARENACOMBATBALANCE_HPP

#include <algorithm>
#include <cmath>
#include <exception>

#include <components/settings/settings.hpp>

/// ArenaMP CB1: combat balance formulas. Pure math (no game state), so they can
/// be unit-tested and tuned in one place. Callers gather the inputs.
namespace MWMechanics::ArenaCombatBalance
{
    // ---------------------------------------------------------------- auto block
    // A drawn shield or weapon now and then blocks a hit on its own, without
    // holding Active Guard. The chance grows with defence: the guard skill
    // (Block for a shield, weapon skill mixed with Block for a weapon, Hand To
    // Hand for fists) and the armour rating. It stays a "now and then" event:
    // the caps keep Active Guard the real way to defend.
    struct AutoBlockCaps
    {
        static constexpr float shield = .25f;
        static constexpr float weapon = .15f;
        static constexpr float hands = .08f;
    };

    inline float autoBlockChance(float guardSkill, float armorRating, bool shield, bool unarmed, float fatigueRatio)
    {
        if (!std::isfinite(guardSkill) || !std::isfinite(armorRating) || !std::isfinite(fatigueRatio))
            return 0.f;
        guardSkill = std::clamp(guardSkill, 0.f, 150.f);
        armorRating = std::clamp(armorRating, 0.f, 200.f);
        // Percent points per skill point / per armour point.
        float chance;
        float cap;
        if (shield)
        {
            chance = guardSkill * .20f + armorRating * .10f;
            cap = AutoBlockCaps::shield;
        }
        else if (unarmed)
        {
            chance = guardSkill * .08f + armorRating * .04f;
            cap = AutoBlockCaps::hands;
        }
        else
        {
            chance = guardSkill * .12f + armorRating * .06f;
            cap = AutoBlockCaps::weapon;
        }
        // An exhausted fighter reacts worse: half the chance at zero fatigue.
        const float fatigue = .5f + .5f * std::clamp(fatigueRatio, 0.f, 1.f);
        return std::clamp(chance * .01f * fatigue, 0.f, cap);
    }

    /// A passive block absorbs this share of what a raised guard would absorb.
    constexpr float autoBlockEfficiency = .75f;

    // ----------------------------------------------------------------- evasion
    // The fork removed the vanilla invisible hit roll, so Sanctuary did nothing
    // and Light ("Светоч") had no combat use. Both now give a chance to dodge a
    // physical hit completely (melee and ranged): Light 0.6% per point, Sanctuary
    // 1% per point, together at most 35%. Spells are not dodged.
    constexpr float evasionCap = .35f;

    inline float evasionChance(float lightMagnitude, float sanctuaryMagnitude, float fatigueRatio)
    {
        if (!std::isfinite(lightMagnitude) || !std::isfinite(sanctuaryMagnitude) || !std::isfinite(fatigueRatio))
            return 0.f;
        const float points = std::max(0.f, lightMagnitude) * .6f + std::max(0.f, sanctuaryMagnitude);
        const float fatigue = .5f + .5f * std::clamp(fatigueRatio, 0.f, 1.f);
        return std::clamp(points * .01f * fatigue, 0.f, evasionCap);
    }

    // ------------------------------------------------ creature attack speed
    // Creatures (not NPCs or players) swing at 0.5x .. 1.5x speed by level:
    // level 1 -> min, "full level" and above -> max, linear in between. The AI
    // pause between attacks shrinks by the same factor.
    // Tunable in settings.cfg [Game]: creature attack speed min / max / full level.
    struct CreatureSpeedSettings
    {
        float min = .5f;
        float max = 1.5f;
        int fullLevel = 50;
    };

    inline float creatureAttackSpeed(int level, const CreatureSpeedSettings& s)
    {
        const float lo = std::clamp(s.min, .1f, 3.f);
        const float hi = std::clamp(s.max, lo, 3.f);
        const int full = std::max(2, s.fullLevel);
        const float t = std::clamp((static_cast<float>(level) - 1.f) / static_cast<float>(full - 1), 0.f, 1.f);
        return lo + (hi - lo) * t;
    }

    inline const CreatureSpeedSettings& creatureSpeedSettings()
    {
        // Read once: settings.cfg changes apply after a restart.
        static const CreatureSpeedSettings settings = []
        {
            CreatureSpeedSettings s;
            try { s.min = Settings::Manager::getFloat("creature attack speed min", "Game"); } catch (const std::exception&) {}
            try { s.max = Settings::Manager::getFloat("creature attack speed max", "Game"); } catch (const std::exception&) {}
            try { s.fullLevel = Settings::Manager::getInt("creature attack speed full level", "Game"); } catch (const std::exception&) {}
            return s;
        }();
        return settings;
    }

    inline float creatureAttackSpeed(int level)
    {
        return creatureAttackSpeed(level, creatureSpeedSettings());
    }
}

#endif
