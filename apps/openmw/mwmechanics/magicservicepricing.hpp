#ifndef MWMECHANICS_MAGICSERVICEPRICING_H
#define MWMECHANICS_MAGICSERVICEPRICING_H

#include <algorithm>
#include <cmath>
#include <limits>

namespace MWMechanics
{
    namespace MagicServicePricing
    {
        // Base skill only: temporary Fortify/Drain must not change service prices.
        inline double schoolMultiplier(double baseSkill)
        {
            if (!std::isfinite(baseSkill))
                baseSkill = 0.0;
            const double deficit = 1.0 - std::max(0.0, std::min(100.0, baseSkill)) / 100.0;
            return 1.0 + 3.0 * deficit * deficit;
        }

        inline double spellEffectPoints(double points, bool onTarget)
        {
            return std::max(1.0, points) * (onTarget ? 1.5 : 1.0);
        }

        inline double enchantmentEffectPoints(double minMagnitude, double maxMagnitude,
            double duration, double area, double baseCost, double effectCostMultiplier)
        {
            // Zero-duration effects must still pay for their magnitude.
            return ((std::max(1.0, minMagnitude) + std::max(1.0, maxMagnitude))
                * std::max(1.0, duration) + std::max(1.0, area))
                * baseCost * effectCostMultiplier * 0.05;
        }

        // Round once, upwards; reserve headroom for the legacy barter formula.
        inline int gold(double amount)
        {
            const int limit = std::numeric_limits<int>::max() / 16;
            if (!std::isfinite(amount) || amount >= limit)
                return limit;
            return static_cast<int>(std::ceil(std::max(1.0, amount)));
        }
    }
}

#endif
