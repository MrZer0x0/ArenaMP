#include <components/misc/economypricing.hpp>
#include <apps/openmw/mwphysics/actorbodyrules.hpp>
#include <apps/openmw/mwmechanics/approachrules.hpp>
#include <cassert>
#include <limits>
#include <iostream>
int main() {
 using namespace ArenaCraft;using namespace ArenaEconomy;
 auto p=Parser(R"({"buy":1.2,"sell":0.8,"services":{"trade":1.5,"training":2},"items":{"iron":{"buy":0.5,"sell":3}}})").parse();
 assert(price(100,p,"trade","IRON",true)==90);
 assert(price(100,p,"trade","iron",false)==90); // capped to purchase multiplier
 assert(price(100,p,"training","",true)==240);
 assert(price(1,p,"trade","IRON",true)==1);
 assert(price(1,p,"trade","iron",false)==0);
 assert(price(0,p,"trade","iron",true)==0);
 p["buy"]=100;p["services"]["trade"]=100;
 assert(price(2147483647,p,"trade","iron",true)==1000000000);
 assert(coefficient(Value(std::numeric_limits<double>::quiet_NaN()))==1);
 assert(coefficient(Value(-4))==0.01);
 assert(price(99,Value{},"trade","",true)==99);
 using namespace MWPhysics::ActorBodyRules;
 assert(fit(50,20)==25);assert(fit(50,40)==40);assert(fit(50,100)==50);
 assert(fit(50,0)==50);assert(fit(50,std::numeric_limits<float>::quiet_NaN())==50);
 assert(roundFootprint(20,21));assert(!roundFootprint(20,50));assert(!roundFootprint(0,20));
 using MWMechanics::ApproachRules::collisionTime;
 assert(std::abs(collisionTime(0,200,0,-200,40,2)-.8f)<1e-6);
 assert(std::abs(collisionTime(0,-200,0,200,40,2)-.8f)<1e-6); // running at an NPC from behind
 assert(collisionTime(0,200,0,200,40,2)==2); // moving away
 assert(collisionTime(0,200,200,0,40,2)==2); // crossing safely
 assert(collisionTime(0,200,0,0,40,2)==2);
 std::cout<<"pricing/body rules passed\n";
}
