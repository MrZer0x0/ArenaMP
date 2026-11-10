#include "apps/openmw/mwmechanics/armorwearrules.hpp"
#include <cassert>
#include <iostream>
#include <limits>

using namespace MWMechanics::ArmorWearRules;

int main()
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    assert(expectedWear(-1, 1000, 0, 0) == 0);
    assert(expectedWear(nan, 1000, 0, 0) == 0);
    assert(expectedWear(10, 0, 0, 0) == 0);
    assert(expectedWear(10, 1000, 0, 0) == 2.f);
    assert(expectedWear(10, 1000, 25, 0) == 1.f);
    assert(std::abs(expectedWear(10, 1000, 100, 0) - .4f) < 1e-5f);
    assert(std::abs(expectedWear(10, 1000, 100, 100) - .2f) < 1e-5f);
    assert(expectedWear(10, 1000, 200, 200) == expectedWear(10, 1000, 100, 100));
    assert(expectedWear(10, 1000, nan, nan) == expectedWear(10, 1000, 0, 0));
    assert(roundWear(0, 0) == 0);
    assert(roundWear(.2f, .1f) == 1 && roundWear(.2f, .8f) == 0);
    assert(roundWear(2.f, 0) == 2 && roundWear(2.f, 1) == 2);

    for (int health : {1, 25, 100, 500, 6000, 65535})
        for (float damage : {0.f, .01f, 1.f, 100.f, 100000.f})
            for (float armorer : {0.f, 50.f, 100.f})
            {
                float previous = expectedWear(damage, health, 0, armorer);
                for (int skill = 0; skill <= 150; ++skill)
                {
                    const float wear = expectedWear(damage, health, float(skill), armorer);
                    assert(wear >= 0 && wear <= health * .01f && wear <= previous + 1e-5f);
                    previous = wear;
                    // A sampled uniform distribution must keep fractional wear
                    // instead of charging at least one point for every light hit.
                    double total = 0;
                    for (int i = 0; i < 1000; ++i)
                        total += roundWear(wear, (i + .5f) / 1000);
                    assert(std::abs(total / 1000 - wear) < .0011);
                }
            }
    std::cout << "PASS armour skills, maintenance, burst cap, fractional wear and invalid inputs\n";
}
