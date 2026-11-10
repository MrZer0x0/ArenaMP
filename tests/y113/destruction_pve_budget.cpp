#include "../../apps/openmw/mwmechanics/arenacombatlimits.hpp"
#include <cassert>
#include <cmath>
#include <algorithm>
using namespace MWMechanics::ArenaCombatLimits;
static bool near(float a, float b) { return std::fabs(a - b) < 0.0002f; }
int main()
{
    // Symmetric relative level curve: Player->NPC and NPC->Player match.
    assert(near(pveLevelDamageFraction(20, 70), .10f));
    assert(near(pveLevelDamageFraction(70, 20), .60f));
    assert(near(pveLevelDamageFraction(50, 50), .35f));
    assert(near(pveLevelDamageFraction(60, 50), .40f));
    assert(near(pveLevelDamageFraction(50, 60), .30f));
    assert(near(pveLevelDamageFraction(1, 150), .10f));
    assert(near(pveLevelDamageFraction(150, 1), .60f));
    assert(near(pveLevelDamageFraction(1500, -99), .60f));

    // No one-cast kill even on a rat: 60% is the absolute max from full HP.
    auto fodder = makePveSpellBudget(20, 20, 1);
    assert(near(reservePveSpellDamage(80, 1, true, fodder), 8.9f));
    assert(near(fodder.total, 0.f));

    // Equal-level direct spell of 1000 damage -> at most 35% max HP.
    auto direct = makePveSpellBudget(100, 40, 40);
    assert(near(reservePveSpellDamage(1000, 1, false, direct), 35.f));
    assert(near(reservePveSpellDamage(1000, 1, false, direct), 0.f));

    // Instant and multi-DoT use ONE lifetime cap, no double-counting.
    auto mixed = makePveSpellBudget(1000, 75, 75);
    assert(near(reservePveSpellDamage(10000, 1, false, mixed), 350.f));
    assert(near(reservePveSpellDamage(10000, 5, true, mixed), 0.f));

    auto dots = makePveSpellBudget(400, 20, 20);
    float d1 = reservePveSpellDamage(1000, 10, true, dots);
    float d2 = reservePveSpellDamage(1000, 10, true, dots);
    assert(near(d1,14.f) && near(d2,0.f));
    assert(near(dots.total,0.f));

    auto boss = makePveSpellBudget(1000, 20, 90);
    assert(near(reservePveSpellDamage(5000, 1, false, boss), 100.f));
    auto weaker = makePveSpellBudget(1000, 90, 20);
    assert(near(reservePveSpellDamage(5000, 1, false, weaker), 600.f));

    // Negative/zero-time effects must not consume available damage.
    auto safe = makePveSpellBudget(100, 1, 1);
    assert(near(reservePveSpellDamage(-10, 4, true, safe), 0.f));
    assert(near(reservePveSpellDamage(10, 0, true, safe), 0.f));
    assert(near(safe.total,35.f));
}
