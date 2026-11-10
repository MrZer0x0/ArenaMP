#!/usr/bin/env python3
"""Compile the actual ActionManager methods with small engine-state test doubles."""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
s=(ROOT/'apps/openmw/mwinput/actionmanager.cpp').read_text()
def block(pos):
 a=s.index('{',pos);depth=1;b=a+1
 while depth:
  if s[b]=='{':depth+=1
  if s[b]=='}':depth-=1
  b+=1
 return s[a:b]
toggle=block(s.index('void ActionManager::toggleSpell()'))
prepare=block(s.index('void ActionManager::prepareSpell()'))
press=block(s.index('else if (!mSpellKeyPending)',s.index('case A_ToggleSpell:')))
request=block(s.index('if (spellAllowed && mSpellLowerRequested)'))
source=r'''
#include <cassert>
#include <string>
#include <iostream>
namespace MWMechanics { enum DrawState_ { DrawState_Nothing, DrawState_Weapon, DrawState_Spell }; }
struct Fixture {
 bool gui=false,magic=true,controls=true,busy=false,transition=false,items=true,enchanted=false;
 int cleared=0;
 std::string spell="restore_health";
 MWMechanics::DrawState_ state=MWMechanics::DrawState_Nothing;
} f;
namespace MWWorld {
 struct Ptr;
 struct InventoryStore { int getSelectedEnchantItem(){return f.enchanted?1:0;} int end(){return 0;} } inv;
 struct Class {InventoryStore& getInventoryStore(const Ptr&){return inv;}} cls;
 struct Ptr {Class& getClass(){return cls;}} ptr;
 struct Player { Ptr& getPlayer(){return ptr;} MWMechanics::DrawState_ getDrawState(){return f.state;}
 void setDrawState(MWMechanics::DrawState_ state){f.state=state;} } player;
 struct World { Player& getPlayer(){return player;} } world;
}
namespace MWBase {
 struct MechanicsManager { bool isAttackingOrSpell(const MWWorld::Ptr&){return f.busy;}
 bool isWeaponTransition(const MWWorld::Ptr&){return f.transition;}
 void setAttackingOrSpell(const MWWorld::Ptr&,bool){++f.cleared;} } mechanics;
 struct WindowManager {bool isGuiMode(){return f.gui;} std::string getSelectedSpell(){return f.spell;}} window;
 struct InputManager {bool getControlSwitch(const std::string& key){return key=="playermagic"?f.magic:f.controls;}} input;
 struct Environment {
 static Environment& get(){static Environment e;return e;}
 WindowManager* getWindowManager(){return &window;}
 InputManager* getInputManager(){return &input;}
 MWWorld::World* getWorld(){return &MWWorld::world;}
 MechanicsManager* getMechanicsManager(){return &mechanics;}
 };
}
namespace MWInput {
struct ActionManager {
 bool mSpellKeyPending=false,mSpellKeyConsumed=false,mSpellLowerRequested=false;
 float mSpellKeyTime=0;
 bool checkAllowedToUseItems(){return f.items;}
 void toggleSpell();void prepareSpell();
 void press();void tick();
};
'''
source+='void ActionManager::toggleSpell()'+toggle+'\nvoid ActionManager::prepareSpell()'+prepare
source+='\nvoid ActionManager::press()'+press
source+='\nvoid ActionManager::tick(){auto* world=MWBase::Environment::get().getWorld(); if(mSpellLowerRequested)'+request+'}\n}\n'
source+=r'''
int main(){
 using namespace MWMechanics;
 MWInput::ActionManager a;
 a.prepareSpell();assert(f.state==DrawState_Spell);
 a.press();assert(a.mSpellKeyConsumed && a.mSpellLowerRequested);
 a.tick();assert(f.state==DrawState_Nothing && !a.mSpellLowerRequested);
 // Lower with no selected spell, item, or permission to USE an item.
 f.state=DrawState_Spell;f.spell.clear();f.items=false;
 a.toggleSpell();assert(f.state==DrawState_Nothing);
 // A pure equipment transition can be reversed immediately.
 f=Fixture();f.state=DrawState_Spell;f.busy=true;f.transition=true;
 a.toggleSpell();assert(f.state==DrawState_Nothing && f.cleared==1);
 // A real cast retains the lowering request until completion.
 f=Fixture();f.state=DrawState_Spell;f.busy=true;
 a.press();a.tick();assert(f.state==DrawState_Spell && a.mSpellLowerRequested && f.cleared==0);
 f.busy=false;a.tick();assert(f.state==DrawState_Nothing && !a.mSpellLowerRequested);
 // Preparing during a real attack is still forbidden.
 f=Fixture();f.state=DrawState_Weapon;f.busy=true;a.prepareSpell();assert(f.state==DrawState_Weapon);
 f.busy=false;f.transition=true;a.prepareSpell();assert(f.state==DrawState_Spell);
 // GUI/control locks and missing selections keep their ordinary behavior.
 f=Fixture();f.gui=true;a.prepareSpell();assert(f.state==DrawState_Nothing);
 f.gui=false;f.magic=false;a.prepareSpell();assert(f.state==DrawState_Nothing);
 f.magic=true;f.spell.clear();a.prepareSpell();assert(f.state==DrawState_Nothing);
 f.enchanted=true;a.prepareSpell();assert(f.state==DrawState_Spell);
 std::cout<<"PASS: actual R handlers, lower/prepare, missing selection, transitions, queued cast completion, control locks\n";
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(source)
 subprocess.run(['g++','-std=c++14','-Wall','-Wextra','-Werror',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
