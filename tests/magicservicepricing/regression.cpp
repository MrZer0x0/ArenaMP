// Standalone: g++ -std=c++14 -Wall -Wextra -Werror -I. tests/magicservicepricing/regression.cpp -o /tmp/magic-pricing-test
#include "apps/openmw/mwmechanics/magicservicepricing.hpp"
#include <array>
#include <cassert>
#include <iostream>
using namespace MWMechanics::MagicServicePricing;
int main()
{
    assert(schoolMultiplier(0) == 4);
    assert(schoolMultiplier(50) == 1.75);
    assert(schoolMultiplier(100) == 1);
    assert(schoolMultiplier(-100) == 4);
    assert(schoolMultiplier(1000) == 1);
    assert(schoolMultiplier(std::numeric_limits<double>::quiet_NaN()) == 4);
    for (int skill = 1; skill <= 100; ++skill)
        assert(schoolMultiplier(skill) < schoolMultiplier(skill - 1));

    // Mixed schools: a cheap expert effect must not discount an expensive novice effect.
    const double novice = spellEffectPoints(100, false) * schoolMultiplier(0);
    const double expert = spellEffectPoints(1, false) * schoolMultiplier(100);
    assert(novice + expert == 401);

    // Target multipliers belong to their own effect, independent of list order.
    std::array<int, 3> order{{0, 1, 2}};
    const double points[] = {10, 20, 30};
    const bool target[] = {true, false, true};
    do {
        double total = 0;
        for (int i : order) total += spellEffectPoints(points[i], target[i]);
        assert(total == 80);
    } while (std::next_permutation(order.begin(), order.end()));

    // A zero-duration powerful enchant must not collapse to the area-only price.
    assert(enchantmentEffectPoints(100, 100, 0, 1, 1, 1)
        == enchantmentEffectPoints(100, 100, 1, 1, 1, 1));
    assert(enchantmentEffectPoints(100, 100, 0, 1, 1, 1)
        > enchantmentEffectPoints(1, 1, 0, 1, 1, 1));
    assert(enchantmentEffectPoints(100, 100, 2, 1, 1, 1)
        > enchantmentEffectPoints(100, 100, 1, 1, 1, 1));
    assert(gold(0) == 1);
    assert(gold(1.01) == 2);
    assert(gold(100 * 20 * 0.05) == 100);
    assert(gold(std::numeric_limits<double>::infinity()) > 0);
    assert(gold(1e100) == gold(std::numeric_limits<double>::infinity()));
    std::cout << "PASS: school curve, skill bounds, mixed schools, all effect permutations, zero duration, rounding and overflow guard\n";
}
