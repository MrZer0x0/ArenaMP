// Y114 numerical regression: max 10-60% HP per full spell application or hit.
#include "../apps/openmw/mwmechanics/arenacombatlimits.hpp"
#include <cassert>
#include <cmath>
using namespace MWMechanics::ArenaCombatLimits;
int main()
{
    for (int a = 1; a <= 150; ++a)
        for (int d = 1; d <= 150; ++d)
        {
            const auto cap = pveLevelDamageFraction(a, d);
            assert(cap >= 0.09999f && cap <= 0.60001f);
            assert(std::fabs(cap - pveLevelDamageFraction(d, a) - .0f) <= .5001f);
            const float maxHealth = 1000.f;
            auto spell = makePveSpellBudget(maxHealth, a, d);
            const float burst = reservePveSpellDamage(1.e8f, 1.f, false, spell);
            assert(burst <= maxHealth * cap + .01f);
            assert(spell.total <= .0001f);
        }
}
