#include "../../components/loadinglistener/progress.hpp"
#include "../../apps/openmw/mwmechanics/arenacombatlimits.hpp"
#include <cassert>
#include <cmath>
#include <limits>
#include <iostream>

int main()
{
    using namespace MWMechanics::ArenaCombatLimits;
    // Typical novice: low skill must not deal full weapon damage, even when charged.
    const float novice = handToHandDamage(5.f, 40.f, 1.f);
    assert(novice >= 2.f && novice <= 3.f);
    for (int skill = 0; skill <= 500; ++skill)
    {
        const float tap = handToHandDamage(skill, 40.f, 0.f);
        const float charged = handToHandDamage(skill, 40.f, 1.f);
        const float oldDamage = 22.f * weaponScaling(skill, 40.f);
        assert(tap > 0.f && charged >= tap);
        assert(charged < oldDamage);
        assert(handToHandDamage(skill + 1.f, 40.f, 1.f) >= charged);
    }
    assert(handToHandDamage(500.f, 500.f, 1.f) < 14.f);
    assert(handToHandDamage(100.f, 100.f, -1.f) == handToHandDamage(100.f, 100.f, 0.f));
    assert(handToHandDamage(100.f, 100.f, 2.f) == handToHandDamage(100.f, 100.f, 1.f));

    Loading::Progress p;
    p.reset(true);
    double previous = 0.;
    // Multiple plugins and a range reset must not restart the overall progress.
    for (std::size_t plugin = 0; plugin < 7; ++plugin)
    {
        p.stage(Loading::Stage::Content, plugin, 7);
        p.range(1000);
        for (int value = 0; value <= 1000; value += 50)
        {
            p.set(value);
            assert(p.fraction() >= previous && p.fraction() <= .550001);
            previous = p.fraction();
        }
    }
    p.stage(Loading::Stage::World); p.range(3); p.set(3);
    assert(std::abs(p.fraction() - .65) < 1e-8);
    p.stage(Loading::Stage::Terrain);
    // Terrain work can grow dynamically: denominator increases are not regressions.
    p.range(10); p.set(8); previous = p.fraction();
    p.range(100); p.set(9); assert(p.fraction() >= previous);
    p.set(100);
    assert(std::abs(p.fraction() - .80) < 1e-8);
    p.stage(Loading::Stage::Objects); p.range(10);
    p.increase(std::numeric_limits<std::size_t>::max());
    assert(std::abs(p.fraction() - .98) < 1e-8);
    p.range(0); p.set(0); assert(p.fraction() < 1.);
    p.complete(); assert(p.fraction() == 1.);
    // A later cell transition owns a fresh budget, independent of startup.
    p.reset(); assert(p.fraction() == 0.);
    p.stage(Loading::Stage::Terrain); p.range(0); p.increase(1);
    assert(std::abs(p.fraction() - .35) < 1e-8);
    p.stage(Loading::Stage::Objects); p.range(2); p.set(1);
    assert(p.fraction() > .35 && p.fraction() < .98);
    p.set(2); p.complete(); assert(p.fraction() == 1.);
    std::cout << "Unarmed damage and aggregate loading progress: PASS\n"
              << "Novice charged base damage: " << novice << "\n";
}
