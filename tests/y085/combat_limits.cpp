#include "../../apps/openmw/mwmechanics/arenacombatlimits.hpp"
#include <cassert>
#include <cmath>
using namespace MWMechanics::ArenaCombatLimits;
bool near(float a,float b) { return std::abs(a-b)<0.0001f; }
int main() {
 // Level no longer forces every strong enemy to take at least ten hits.
 assert(near(npcBurstFraction(1,1000),1.f));
 assert(near(npcBurstFraction(100,50),1.f));
 assert(near(npcBurstFraction(30,500),1.f));
 assert(near(npcBurstFraction(100,10000),1.f));
 assert(near(npcBurstFraction(20,300),1.f));
 float rate=2.5f,total=25.f;
 float a=reserveDot(100,2,rate,total);
 float b=reserveDot(100,60,rate,total);
 assert(near(a,2.5f) && near(b,0));
 rate=2.5f;total=25;
 a=reserveDot(100,60,rate,total);
 assert(near(a*60,25));
 for(int fps : {20,30,60,144}) {
   double dealt=0;
   for(int frame=0;frame<fps*60;++frame) dealt+=a/fps;
   assert(std::abs(dealt-25)<.001);
 }
 rate=3;total=30;
 a=reserveDot(1,5,rate,total); b=reserveDot(100,10,rate,total);
 assert(a+b<=3 && a*5+b*10<=30);
 rate=2;total=20;
 assert(reserveDot(0,5,rate,total)==0 && near(total,20));
}
