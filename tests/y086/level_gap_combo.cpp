#include "../../apps/openmw/mwmechanics/arenacombatlimits.hpp"
#include "../../apps/openmw/mwmechanics/arenacombo.hpp"
#include <cassert>
#include <cmath>
#include <limits>
using namespace MWMechanics;
bool near(float a,float b) { return std::abs(a-b)<.0001f; }
int main() {
 using namespace ArenaCombatLimits;
 // Strength and defense still scale normally; PvE burst cap uses relative level.
 for (int level=1; level<=150; ++level) {
   assert(offense(level) >= 1.f && offense(level) <= 1.601f);
   assert(defense(level) >= .699f && defense(level) <= 1.f);
   assert(near(levelGapBurstFraction(.45f,.45f,level,1),pveLevelDamageFraction(level,1)));
 }
 assert(near(offense(150),1.6f) && near(defense(150),.7f));
 assert(near(offense(151),offense(150)));
 assert(weaponScaling(80,80)>weaponScaling(40,40));
 assert(armorMultiplier(300)<armorMultiplier(100));
 assert(unarmoredRating(150)<50);
 assert(attackCost(25,1,1)>attackCost(5,0,0));
 ArenaCombo::Chain c;
 assert(near(c.multiplier(1,"sword"),1));
 assert(!c.landed(1,"sword",10));
 c.update(1);
 assert(!c.landed(1,"sword",10));
 assert(near(c.multiplier(1,"sword"),1.35f));
 assert(near(c.multiplier(2,"sword"),1));
 assert(c.landed(1,"sword",10));
 assert(near(c.multiplier(1,"sword"),1));
 c.landed(1,"sword",10);c.landed(1,"sword",10);c.update(2.51f);
 assert(near(c.multiplier(1,"sword"),1));
 c.landed(1,"sword",10);c.landed(1,"sword",10);c.checkWeapon("axe");
 assert(near(c.multiplier(1,"sword"),1));
 c.landed(1,"sword",10);c.landed(1,"sword",10);assert(!c.landed(1,"sword",0));
 assert(near(c.multiplier(1,"sword"),1));
 c.landed(1,"sword",10);c.landed(1,"sword",10);c.update(std::numeric_limits<float>::quiet_NaN());
 assert(near(c.multiplier(1,"sword"),1));
 for(int fps : {20,30,60,144}) {
   c.reset(); c.landed(1,"sword",10);c.landed(1,"sword",10);
   for(int i=0;i<fps*3;++i)c.update(1.f/fps);
   assert(near(c.multiplier(1,"sword"),1));
 }
 ArenaCombo::local().landed(1,"sword",10);ArenaCombo::local().landed(1,"sword",10);
 { ArenaCombo::Attempt missed(true); }
 assert(near(ArenaCombo::local().multiplier(1,"sword"),1));
 { ArenaCombo::Attempt hit(true); assert(!hit.complete(1,"sword",10)); }
 { ArenaCombo::Attempt hit(true); assert(!hit.complete(1,"sword",10)); }
 { ArenaCombo::Attempt remote(false); }
 assert(near(ArenaCombo::local().multiplier(1,"sword"),1.35f));
 { ArenaCombo::Attempt hit(true); assert(hit.complete(1,"sword",3)); }
 assert(near(ArenaCombo::local().multiplier(1,"sword"),1));
 // Finisher is still clipped at the level-difference ceiling.
 assert(near(std::min(1000.f*1.35f,1000.f*pveLevelDamageFraction(50,50)),350.f));
}
