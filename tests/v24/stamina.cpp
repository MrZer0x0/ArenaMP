#include "apps/openmw/mwmechanics/arenacombatlimits.hpp"
#include <cassert>
#include <cmath>
using namespace MWMechanics::ArenaCombatLimits;
bool near(float a,float b){return std::abs(a-b)<.0001f;}
int main(){
    assert(near(playerAttackCostMultiplier(40),.5f));
    assert(near(playerAttackCostMultiplier(70),.45f));
    assert(near(playerAttackCostMultiplier(100),.4f));
    assert(near(playerAttackCostMultiplier(-100),.5f));
    assert(near(playerAttackCostMultiplier(10000),.4f));
    assert(near(playerFatigueRegenMultiplier(40),1.f));
    assert(near(playerFatigueRegenMultiplier(70),1.075f));
    assert(near(playerFatigueRegenMultiplier(100),1.15f));
    assert(near(playerFatigueRegenMultiplier(10000),1.15f));
    assert(near(playerFatigueRegenMultiplier(-100),1.f));
    for(float w:{0.f,1.f,10.f,80.f})for(float c:{0.f,.5f,1.f})for(float l:{0.f,.5f,1.f}){
        float old=attackCost(w,c,l);
        assert(near(old*playerAttackCostMultiplier(40),old/2));
        assert(old*playerAttackCostMultiplier(100)>0);
    }
    // Holding charge is integrated over time: rate does not depend on FPS.
    for(int fps:{30,60,144}){
        float total=0;for(int frame=0;frame<fps*10;++frame)total+=8.f*playerAttackCostMultiplier(40)/fps;
        assert(std::abs(total-40.f)<.01f);
    }
}
