#!/usr/bin/env python3
"""Compile the production charge helper + school selection with mocked engine access."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'apps/openmw/mwmechanics/spellutil.cpp').read_text()
def function(name):
 a=s.index(name);a=s.rfind('\n',0,a)+1;b=s.index('{',a);depth=1;i=b+1
 while depth:
  depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]
prefix=r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <iostream>
#include <limits>
#include <vector>
namespace ESM {
struct Skill {enum SkillEnum {Alteration,Conjuration,Destruction,Illusion,Mysticism,Restoration};};
struct MagicEffect {enum {AppliedOnce=1};struct {int mSchool=0;int mFlags=0;float mBaseCost=5;}mData;};
struct ENAMstruct {int mEffectID=0,mDuration=1,mMagnMin=1,mMagnMax=1,mArea=0,mRange=0;};
struct Effects {std::vector<ENAMstruct>mList;};
struct Spell {struct {int mCost=0;}mData;Effects mEffects;};
struct Enchantment {enum {WhenUsed,WhenStrikes};struct {int mCost=0,mType=WhenUsed;}mData;Effects mEffects;};
enum {RT_Target=2};
struct GameSetting {struct Value {float getFloat() const {return 1.f;}}mValue;};
}
std::array<ESM::MagicEffect,6> effects;
struct Stat {float getModified() const {return 50.f;}};
namespace ESM {struct Attribute {enum {Willpower,Luck};};}
namespace MWMechanics {struct CreatureStats {Stat getAttribute(int) {return {};}};}
namespace MWWorld {
struct Ptr;
struct Class {float getSkill(const Ptr&,int) const;MWMechanics::CreatureStats& getCreatureStats(const Ptr&) const;};
struct Ptr {int id=0;std::array<float,6> skills;const Class& getClass() const {static Class c;return c;}bool operator==(const Ptr&o)const{return id==o.id;}bool operator!=(const Ptr&o)const{return !(*this==o);}};
float Class::getSkill(const Ptr&p,int i)const{return p.skills.at(i);}
MWMechanics::CreatureStats& Class::getCreatureStats(const Ptr&)const{static MWMechanics::CreatureStats c;return c;}
template<class T>struct Store;
template<>struct Store<ESM::MagicEffect>{const ESM::MagicEffect*find(int i)const{return &effects.at(i);}};
template<>struct Store<ESM::GameSetting>{const ESM::GameSetting*find(const char*)const{static ESM::GameSetting g;return &g;}};
struct ESMStore {template<class T>Store<T>get()const{return {};}};
}
namespace MWBase {struct World {MWWorld::ESMStore getStore()const{return {};}};struct Environment {static Environment&get(){static Environment e;return e;}World*getWorld(){static World w;return &w;}};}
namespace MWMechanics {
MWWorld::Ptr getPlayer(){return {0,{}};}
float getBaseGameEffectCost(int){return 5.f;} // Mock NPC ESM base cost; fire damage is 5.
float calcSpellBaseSuccessChance(const ESM::Spell*,const MWWorld::Ptr&,int*);
'''
main=r'''
}
int main(){
 using namespace MWMechanics;
 for(int i=0;i<6;i++)effects[i].mData.mSchool=i;
 ESM::Enchantment e;e.mData.mCost=100;e.mEffects.mList.push_back({2,10,5,5,0,0});
 MWWorld::Ptr player{0,{50,50,20,50,50,50}};
 int previous=1000;
 for(float skill:{0.f,20.f,50.f,100.f,120.f,150.f}){
  player.skills[2]=skill;int cost=getEffectiveEnchantedItemUseCost(e,player);
  assert(cost>=1&&cost<=previous);previous=cost;
 }
 player.skills[2]=100;int cap=getEffectiveEnchantedItemUseCost(e,player);
 player.skills[2]=150;assert(getEffectiveEnchantedItemUseCost(e,player)==cap);
 for(int base:{0,1,5,25,100})for(float skill:{0.f,20.f,50.f,100.f,150.f}){
  e.mData.mCost=base;player.skills[2]=skill;
  for(int type:{ESM::Enchantment::WhenUsed,ESM::Enchantment::WhenStrikes}){
   e.mData.mType=type;assert(getEffectiveEnchantedItemUseCost(e,player)>=1);
  }
 }
 e.mData.mCost=100;e.mData.mType=ESM::Enchantment::WhenUsed;
 e.mEffects.mList.push_back({5,10,5,5,0,0});player.skills[2]=100;player.skills[5]=20;
 int mixed=getEffectiveEnchantedItemUseCost(e,player);
 std::reverse(e.mEffects.mList.begin(),e.mEffects.mList.end());
 assert(mixed==getEffectiveEnchantedItemUseCost(e,player));
 player.skills[5]=100;assert(getEffectiveEnchantedItemUseCost(e,player)<mixed);
 MWWorld::Ptr npc=player;npc.id=1;
 assert(getEffectiveEnchantedItemUseCost(e,npc)>=1);
 std::cout<<"C++ production charge helper and school-selection tests passed (mock engine)\n";
}
'''
code=prefix+function('ESM::Skill::SkillEnum spellSchoolToSkill')+'\n'+function('int getEffectiveEnchantedItemUseCost')+'\n'+function('float calcSpellBaseSuccessChance (')+main
with tempfile.TemporaryDirectory() as tmp:
 src=Path(tmp)/'charge.cpp';exe=Path(tmp)/'charge';src.write_text(code)
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-fsanitize=undefined',str(src),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
