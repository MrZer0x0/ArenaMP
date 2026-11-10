from pathlib import Path
import subprocess
source=Path('builder/ArenaMP_Builder/apps/openmw/mwmp/ArenaHousing.cpp')
if not source.exists(): source=Path('apps/openmw/mwmp/ArenaHousing.cpp')
src=source.read_text()
def extract(signature):
 start=src.index(signature);brace=src.index('{',start);depth=1;i=brace+1
 while depth:
  if src[i]=='{':depth+=1
  elif src[i]=='}':depth-=1
  i+=1
 return src[start:i]

header=Path('apps/openmw/mwmp/ArenaHousing.hpp').read_text().replace('#include <components/esm/defs.hpp>','').replace('private:', 'public:')
pre=r'''
#include <vector>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cassert>
#include <iostream>
#include <string>
#define LOG_MESSAGE_SIMPLE(...) ((void)0)
namespace ESM {struct Position {float pos[3]={},rot[3]={};};}
namespace Misc {namespace StringUtils {std::string lowerCase(std::string s){for(auto& c:s)c=std::tolower((unsigned char)c);return s;}bool ciEqual(const std::string&a,const std::string&b){return a==b;}}}
namespace MWWorld {
struct Ref {struct Num{int mIndex=0;};Num getRefNum()const{return {};}int getMpNum()const{return 0;}std::string getRefId()const{return "x";}};
struct Cell {Cell* getCell(){return this;}std::string getDescription(){return "House";}};
struct ConstPtr {bool isEmpty()const{return true;}bool isInCell()const{return false;}Cell* getCell()const{static Cell c;return &c;}Ref getCellRef()const{return {};}};
struct Ptr:ConstPtr{};
}
namespace MWMechanics {MWWorld::Ptr getPlayer(){return {};}}
namespace MWBase {struct World {MWWorld::Ptr getFacedObject(){return {};}void beginPhysicsGrab(const MWWorld::Ptr&){}};
struct Environment {World w;static Environment& get(){static Environment e;return e;}World* getWorld(){return &w;}};}
int teleports=0;
'''
code=pre+header+r'''
namespace mwmp {
struct GUIController{void receiveFurnitureMessage(const std::string&){}};
struct Main {static Main& get(){static Main m;return m;}GUIController* getGUIController(){return nullptr;}};
std::string networkCellName(const MWWorld::Ptr&){return "House";}
bool ArenaHousing::ownsPlace(const MWWorld::ConstPtr&)const{return false;}
void ArenaHousing::purgeCell(const std::string&){}
void ArenaHousing::unlockPrivateContainers(const std::string&){}
void ArenaHousing::performHeldTeleport(const std::string&) { ++teleports; cancelDoor(); }
constexpr float sPurgeDelay=.75f;
'''
for sig in ['std::string toKey(', 'std::vector<std::string> splitOn(', 'std::string unescapeHint(', 'bool ArenaHousing::isManaged(', 'const ArenaHousing::Entry* ArenaHousing::find(', 'void ArenaHousing::reset()', 'bool ArenaHousing::handleControlMessage(', 'void ArenaHousing::applyState(', 'void ArenaHousing::cancelDoor()', 'std::string ArenaHousing::displayName(']:code+=extract(sig)+'\n'
# Compile actual timers; discovery of world doors has its separate H019 harness.
update=extract('void ArenaHousing::update(')
code+='void ArenaHousing::update(float dt) {\n'+update[update.index('        if (mHasPendingDoor &&'):]+ '\n'
code+=r'''
}
int main() {
 mwmp::ArenaHousing a;
 a.mHasPendingDoor=true;a.mPendingTimeout=4;
 a.handleControlMessage("WAIT");a.update(60);
 assert(teleports==0 && a.mHasPendingDoor);
 a.handleControlMessage("CANCEL");a.update(60);
 assert(teleports==0 && !a.mHasPendingDoor);
 a.mHasPendingDoor=true;a.mPendingTimeout=4;a.update(5);
 assert(teleports==0 && !a.mHasPendingDoor);
 a.mHasPendingDoor=true;a.handleControlMessage("GO\tHouse");assert(teleports==1);
 a.handleControlMessage("STATE\tHouse^mine^Home\tBasement^mine^Home");
 assert(a.mEntries.size()==2);
 assert(a.isManaged("House"));
 a.handleControlMessage("STATE\tHouse^locked^House (Bob)");
 assert(a.find("House")->hint=="House (Bob)" && !a.isManaged("House"));
 a.handleControlMessage("OWN\t1");assert(a.mOwnsHome);
 a.handleControlMessage("ROOM\tHouse - Home of Alice\tTavern, owner Bob\t0");
 assert(a.displayName("House - Home of Alice")=="Tavern, owner Bob");
 assert(!a.mRooms.begin()->second.take);
 a.reset();assert(!a.mOwnsHome && a.mRooms.empty() && a.mEntries.empty());
 std::cout<<"PASS: actual client protocol methods: wait, cancel, timeout, GO, rooms, title, ownership, reset\n";
}
'''

import tempfile
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'protocol.cpp';p.write_text(code)
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra',str(p),'-o',d+'/test'],check=True)
 subprocess.run([d+'/test'],check=True)
