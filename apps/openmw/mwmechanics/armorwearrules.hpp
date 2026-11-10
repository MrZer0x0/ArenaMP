#ifndef OPENMW_MWMECHANICS_ARMORWEARRULES_HPP
#define OPENMW_MWMECHANICS_ARMORWEARRULES_HPP

#include <algorithm>
#include <cmath>

namespace MWMechanics::ArmorWearRules
{
    // Y110: armour skill protects its own equipment type. Difficulty changes
    // combat damage, but must not multiply equipment wear a second time.
    inline float expectedWear(float damage, int maxHealth, float armorSkill, float armorerSkill)
    {
        if (!std::isfinite(damage) || damage <= 0.f || maxHealth <= 0)
            return 0.f;
        armorSkill = std::isfinite(armorSkill) ? std::clamp(armorSkill, 0.f, 100.f) : 0.f;
        armorerSkill = std::isfinite(armorerSkill) ? std::clamp(armorerSkill, 0.f, 100.f) : 0.f;
        const float skillDivisor = 1.f + armorSkill / 25.f;
        const float maintenance = 1.f - armorerSkill * .005f;
        return std::min(damage * .2f / skillDivisor * maintenance, maxHealth * .01f);
    }

    // Condition is an integer on disk and on the wire. Stochastic rounding
    // preserves fractional wear without a forced one-point cost on every hit,
    // and without a runtime remainder that could be reset by re-equipping.
    inline int roundWear(float wear, float roll)
    {
        if (!std::isfinite(wear) || wear <= 0.f || !std::isfinite(roll))
            return 0;
        const int whole = static_cast<int>(wear);
        return whole + (std::clamp(roll, 0.f, 1.f) < wear - whole ? 1 : 0);
    }
}

#endif
