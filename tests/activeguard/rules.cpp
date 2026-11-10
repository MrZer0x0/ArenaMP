#include "../../apps/openmw/mwmechanics/guardrules.hpp"
#include <cassert>
#include <limits>
#include <iostream>
int main()
{
    using MWMechanics::GuardRules::resolve;
    const auto shield = resolve(100, 200, 50, 1, true, false);
    const auto weapon = resolve(100, 200, 50, 1, false, false);
    assert(shield.kind == 1 && shield.damage < weapon.damage && shield.damage > 0);
    const auto perfect = resolve(100, 200, 50, .05f, true, false);
    assert(perfect.kind == 2 && perfect.damage == 0 && perfect.wear == 0);
    const auto broken = resolve(100, 1, 50, .05f, true, false);
    assert(broken.kind == 3 && broken.fatigue == 1 && broken.damage > shield.damage);
    assert(resolve(100, 200, 0, 0, true, false).kind == 1);
    assert(resolve(100, 200, 100, 1, false, true).kind == 0);
    assert(resolve(100, 200, 100, .05f, false, true).kind == 2);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    assert(resolve(nan,200,50,0,true,false).kind == 0);
    assert(resolve(100,nan,50,0,true,false).kind == 0);
    assert(resolve(100,200,nan,0,true,false).kind == 0);
    assert(resolve(100,200,50,nan,true,false).kind == 0);
    assert(resolve(100,0,50,0,true,false).kind == 0);
    assert(resolve(0,200,50,0,true,false).kind == 0);
    // Bounds and monotonic protection across the supported balance domain.
    for (float damage : {1.f, 5.f, 100.f, 1000.f, 100000.f})
        for (float fatigue : {1.f, 30.f, 500.f, 2000.f})
            for (bool isShield : {false,true})
                for (float age : {0.f,.1f,.23f,1.f})
                    for (int skill=0; skill<=150; ++skill)
                    {
                        const auto r=resolve(damage,fatigue,float(skill),age,isShield,false);
                        assert(r.kind >= 1 && r.kind <= 3);
                        assert(r.damage >= 0 && r.damage <= damage);
                        assert(r.fatigue >= 0 && r.fatigue <= fatigue && r.fatigue <= 500);
                        assert(r.wear >= 0 && r.wear <= 50);
                        if (r.kind==2) assert(r.damage==0 && r.wear==0);
                    }
    std::cout << "ActiveGuard rules: edge cases and 24,160 balance cases passed\n";
}
