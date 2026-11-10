// The runner injects the real controller methods and native PickProbe branch.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include <iostream>
#include <typeinfo>
#include <apps/openmw/mwmechanics/drawstate.hpp>
namespace ESM {struct Probe {}; struct Lockpick {}; struct Weapon {enum {None, PickProbe, Spell};};}
namespace MWWorld {
struct CellStore {};
struct Class;
struct Row {int count=1,health=10,type=0; int getCount() const{return count;}};
struct Ptr {
 std::shared_ptr<Row> row; CellStore* cell=nullptr;
 bool isEmpty() const{return !row;} Row& getRefData() const{return *row;}
 CellStore* getCell() const{return cell;}
 bool operator==(const Ptr& x)const{return row==x.row;}
 bool operator!=(const Ptr& x)const{return !(*this==x);}
 const Class& getClass()const;
 std::string getTypeName() const {return row->type==1?typeid(ESM::Probe).name():typeid(ESM::Lockpick).name();}
};
using ContainerStoreIterator = std::vector<Ptr>::iterator;
struct InventoryStore {
 static constexpr int Slot_CarriedRight=16;
 std::vector<Ptr> items; Ptr right,enchanted;
 int equipCount=0; bool mergePrevious=false;
 auto begin(){return items.begin();} auto end(){return items.end();}
 auto getSlot(int){return std::find(items.begin(),items.end(),right);}
 auto getSelectedEnchantItem(){return std::find(items.begin(),items.end(),enchanted);}
 void setSelectedEnchantItem(std::vector<Ptr>::iterator it){enchanted=it==end()?Ptr():*it;}
 auto unequipSlot(int,const Ptr&,bool=true){auto old=getSlot(16);right={};
  if(mergePrevious && old!=end()) {mergePrevious=false;old->row->count=0;return std::find_if(begin(),end(),[](const Ptr& p){return p.row->type==4;});}
  return old;}
 void equip(int,std::vector<Ptr>::iterator it,const Ptr&){assert(it!=end());assert(it->row->count>0);right=*it;++equipCount;}
};
}
using MWMechanics::DrawState_; using MWMechanics::DrawState_Nothing;using MWMechanics::DrawState_Weapon;using MWMechanics::DrawState_Spell;
struct Spells {std::set<std::string> known={"open","old"};std::string selected="old";
 bool hasSpell(const std::string& s) const{return known.count(s)>0;} std::string getSelectedSpell()const{return selected;}
 void setSelectedSpell(const std::string& s){selected=s;}};
struct Stats {bool dead=false,paralyzed=false;struct{float stagger=0;}mGuard;DrawState_ draw=DrawState_Weapon;Spells spells;
 bool isDead()const{return dead;}bool isParalyzed()const{return paralyzed;}Spells& getSpells(){return spells;}
 DrawState_ getDrawState()const{return draw;}void setDrawState(DrawState_ x){draw=x;}} stats;
MWWorld::InventoryStore inv;
namespace MWWorld {struct Class {
 bool hasInventoryStore(const Ptr&)const{return true;}Stats& getCreatureStats(const Ptr&)const{return stats;}
 InventoryStore& getInventoryStore(const Ptr&)const{return inv;}int getItemHealth(const Ptr& p)const{return p.row->health;}
}; const Class& Ptr::getClass()const{static Class c;return c;}}
MWWorld::Ptr player;
namespace MWRender {struct Animation {enum {BlendMask_All}; bool playing=false; int plays=0;
 bool isPlaying(const std::string&)const{return playing;}void disable(const std::string&){playing=false;}
 template<class... T>void play(T...){playing=true;++plays;}};}
struct PlayerControl {bool attacking=false;void setAttackingOrSpell(bool b){attacking=b;}};
struct World {MWWorld::Ptr faced;float distance=40,maxDistance=100;PlayerControl pc;
 PlayerControl& getPlayer(){return pc;}MWWorld::Ptr getFacedObject(){return faced;}
 float getDistanceToFacedObject()const{return distance;}float getMaxActivationDistance()const{return maxDistance;}} world;
struct Window {std::string selected="old";MWWorld::Ptr enchanted;bool gui=false;
 bool isGuiMode()const{return gui;}std::string getSelectedSpell()const{return selected;}
 MWWorld::Ptr getSelectedEnchantItem()const{return enchanted;}
 void setSelectedSpell(const std::string& s,int){selected=s;enchanted={};}void unsetSelectedSpell(){selected.clear();}
 void setSelectedEnchantItem(const MWWorld::Ptr& p){enchanted=p;selected.clear();}void messageBox(const std::string&){} } window;
struct Sound{void playSound3D(const MWWorld::Ptr&,const std::string&,float,float){}} sound;
namespace MWBase {struct Environment {
 static Environment& get(){static Environment e;return e;}World* getWorld(){return &world;}
 Window* getWindowManager(){return &window;}Sound* getSoundManager(){return &sound;}};}
namespace MWMechanics {
using CreatureStats = ::Stats;
MWWorld::Ptr getPlayer(){return player;}
namespace ActiveGuard {bool raised(const MWWorld::Ptr&){return false;}}
float getSpellSuccessChance(const std::string&,const MWWorld::Ptr&){return 80;}
int attempts=0;
struct Security {explicit Security(const MWWorld::Ptr&){}void pickLock(const MWWorld::Ptr&,const MWWorld::Ptr& tool,std::string&,std::string&){++attempts;if(--tool.row->health==0){tool.row->count=0;inv.right={};}}void probeTrap(const MWWorld::Ptr& t,const MWWorld::Ptr& p,std::string& a,std::string& b){pickLock(t,p,a,b);}};
enum Upper {UpperCharState_Nothing,UpperCharState_EquipingWeap,UpperCharState_UnEquipingWeap,UpperCharState_WeapEquiped,UpperCharState_FollowStartToFollowStop,UpperCharState_CastingSpell};
struct CharacterController {
 MWWorld::Ptr mPtr; MWRender::Animation* mAnimation;
 // INSERT_MEMBERS
 int mWeaponType=ESM::Weapon::None; Upper mUpperBodyState=UpperCharState_WeapEquiped;
 std::string mCurrentWeapon="pickprobe";bool mAttackingOrSpell=false,mCastingManualSpell=false,mQuickCastActive=false;
 bool knocked=false,recovery=false;
 bool isKnockedDown()const{return knocked;}bool isKnockedOut()const{return false;}bool isRecovery()const{return recovery;}
 bool isWeaponTransition()const{return mUpperBodyState==UpperCharState_EquipingWeap || mUpperBodyState==UpperCharState_UnEquipingWeap;}
 bool isAttackingOrSpell()const{return mQuickCastActive || mUpperBodyState!=UpperCharState_WeapEquiped;}
 void castSpell(const std::string& s,bool){mAttackingOrSpell=true;stats.spells.selected=s;stats.draw=DrawState_Spell;}
 bool startSecurityAction(const MWWorld::Ptr&,const MWWorld::Ptr&,const std::string&);
 bool nativeToolAttempt(){bool forcestateupdate=false;int priorityWeapon=1;
 // INSERT_TOOL_BRANCH
 return forcestateupdate;}
};
// INSERT_METHODS
}
MWWorld::Ptr item(int type=0,int health=10){return {std::make_shared<MWWorld::Row>(MWWorld::Row{1,health,type}),nullptr};}
int main(){
 using namespace MWMechanics;
 auto weapon=item(2),tool=item(),target=item(),enchantment=item(3),merged=item(4);
 MWWorld::CellStore cell,other;player=item();player.cell=&cell;
 MWRender::Animation anim;CharacterController c;c.mPtr=player;c.mAnimation=&anim;
 auto reset=[&](){c.finishSecurityAction();stats=Stats{};window=Window{};world=World{};anim={};inv={};
 inv.items={weapon,tool,enchantment,merged};inv.right=weapon;weapon.row->count=1;tool.row->health=10;tool.row->count=1;
 world.faced=target;c.mWeaponType=ESM::Weapon::None;c.mUpperBodyState=UpperCharState_WeapEquiped;c.knocked=false;attempts=0;};
 auto use=[&](){c.mWeaponType=ESM::Weapon::PickProbe;c.updateSecurityAction(.016f);assert(c.mAttackingOrSpell);c.nativeToolAttempt();assert(attempts==1);assert(anim.plays==1);assert(!c.mAttackingOrSpell);};
 auto complete=[&](){anim.playing=false;c.mUpperBodyState=UpperCharState_WeapEquiped;c.updateSecurityAction(.016f);assert(c.mSecurityPhase==CharacterController::SecurityPhase::None);};
 reset();assert(c.startSecurityAction(target,tool,""));assert(inv.right==tool);assert(c.startSecurityAction(target,tool,""));use();complete();assert(inv.right==weapon&&stats.draw==DrawState_Weapon);assert(attempts==1);
 reset();inv.right=tool;assert(c.startSecurityAction(target,tool,""));use();int equips=inv.equipCount;complete();assert(inv.right==tool&&inv.equipCount==equips);
 reset();stats.draw=DrawState_Nothing;inv.right={};assert(c.startSecurityAction(target,tool,""));use();complete();assert(inv.right.isEmpty()&&stats.draw==DrawState_Nothing);
 reset();tool.row->type=1;tool.row->health=1;assert(c.startSecurityAction(target,tool,""));use();complete();assert(inv.right==weapon&&tool.row->count==0);tool.row->type=0;
 reset();assert(c.startSecurityAction(target,tool,""));world.faced={};c.updateSecurityAction(.016f);assert(inv.right==weapon&&attempts==0);
 reset();assert(c.startSecurityAction(target,tool,""));inv.right=enchantment;c.updateSecurityAction(.016f);assert(inv.right==enchantment);
 reset();assert(c.startSecurityAction(target,tool,""));world.distance=200;c.updateSecurityAction(.016f);assert(inv.right==weapon&&attempts==0);
 reset();assert(c.startSecurityAction(target,tool,""));stats.paralyzed=true;c.updateSecurityAction(.016f);assert(inv.right==weapon&&attempts==0);
 reset();assert(c.startSecurityAction(target,tool,""));c.mPtr.cell=&other;c.updateSecurityAction(.016f);assert(inv.right==weapon&&attempts==0);c.mPtr.cell=&cell;
 reset();inv.mergePrevious=true;assert(c.startSecurityAction(target,tool,""));use();complete();assert(inv.right==merged&&weapon.row->count==0);
 reset();assert(c.startSecurityAction(target,tool,""));weapon.row->count=0;use();complete();assert(inv.right.isEmpty());
 reset();c.mUpperBodyState=UpperCharState_EquipingWeap;assert(c.startSecurityAction(target,tool,""));c.finishSecurityAction();assert(inv.right==weapon);
 reset();c.mUpperBodyState=UpperCharState_FollowStartToFollowStop;assert(!c.startSecurityAction(target,tool,""));assert(inv.right==weapon);
 reset();assert(c.startSecurityAction(target,{},"open"));assert(stats.draw==DrawState_Spell);assert(window.selected=="open");c.mSecurityAnimationStarted=true;anim.playing=false;c.updateSecurityAction(.016f);assert(stats.draw==DrawState_Weapon&&window.selected=="old"&&stats.spells.selected=="old");
 reset();window.selected.clear();window.enchanted=enchantment;assert(c.startSecurityAction(target,{},"open"));c.finishSecurityAction();assert(window.enchanted==enchantment&&inv.enchanted==enchantment&&stats.draw==DrawState_Weapon);
 reset();window.selected.clear();window.enchanted=weapon;inv.enchanted=weapon;inv.mergePrevious=true;assert(c.startSecurityAction(target,tool,""));use();complete();assert(inv.right==merged&&inv.enchanted==merged&&window.enchanted==merged);
 reset();assert(c.startSecurityAction(target,{},"open"));window.selected="old";c.updateSecurityAction(.016f);assert(window.selected=="old");
 reset();assert(c.startSecurityAction(target,tool,""));c.updateSecurityAction(9);assert(inv.right==weapon&&attempts==0);
 std::cout<<"PASS actual security controller methods, native tool branch, equipment restore\n";
}
