from pathlib import Path
import subprocess,tempfile
src=Path('apps/openmw/mwmp/ArenaHousing.cpp').read_text()
def extract(sig):
 a=src.index(sig);i=src.index('{',a)+1;depth=1
 while depth:
  if src[i]=='{':depth+=1
  elif src[i]=='}':depth-=1
  i+=1
 return src[a:i]
code=r'''
#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <vector>
#include <sstream>
#include <cassert>
#define LOG_MESSAGE_SIMPLE(...) ((void)0)
namespace Misc {namespace StringUtils {std::string lowerCase(std::string v){for(auto&c:v)c=std::tolower((unsigned char)c);return v;}bool ciEqual(std::string a,std::string b){return lowerCase(a)==lowerCase(b);}}}
namespace ESM {struct Position{float pos[3]={1,2,3},rot[3]={0,0,0};};}
int teleports=0;
namespace MWWorld {struct Ptr{bool isEmpty()const{return false;}bool isInCell()const{return true;}};struct CellRef{int lock=50;ESM::Position dest;int getLockLevel()const{return lock;}const auto&getDoorDest()const{return dest;}};struct ConstPtr {std::string dest="Town, House";CellRef ref;const auto&getCellRef()const{return ref;}};struct ActionTeleport{ActionTeleport(const std::string&,const ESM::Position&,bool){}void execute(Ptr&){++teleports;}};}
namespace MWMechanics{MWWorld::Ptr getPlayer(){return {};}}
namespace MWBase{struct Translation{std::string translateCellName(const std::string&v)const{return v=="Town, House"?"Город, Дом":v;}};struct Window{Translation tr;const auto&getTranslationDataStorage(){return tr;}void messageBox(const std::string&){} };struct Environment{Window w;static auto&get(){static Environment e;return e;}auto*getWindowManager(){return &w;}auto*getWorld(){return this;}};}
namespace mwmp {
struct LocalPlayer{bool isLoggedIn(){return true;}};struct Main{LocalPlayer p;static auto&get(){static Main m;return m;}auto*getLocalPlayer(){return &p;}};
std::string networkCellName(const MWWorld::Ptr&){return "Town, Outside";}
std::string resolveDestination(const MWWorld::ConstPtr&d){return d.dest;}
constexpr float sRequestTimeout=4;std::string sent;
void sendControl(const std::string&s){sent=s;}
struct ArenaHousing {
enum Status{Status_None,Status_Locked,Status_Free,Status_Owned,Status_Mine};
struct Entry{Status status;std::string cell,hint;bool titleIsCell=false,trusted=false,originalCell=false;};
std::map<std::string,Entry>mEntries;
bool mHasPendingDoor=false,mPendingOriginalLocked=false;
float mPendingTimeout=0;std::string mPendingDoorCell;ESM::Position mPendingDoorDest;
const Entry*find(const std::string&)const;void applyState(const std::string&);
std::string hintForDoor(const MWWorld::ConstPtr&)const;
bool canBypassDoorLock(const MWWorld::ConstPtr&)const;
bool requestForDoor(const MWWorld::ConstPtr&,bool,bool=false);
void performHeldTeleport(const std::string&),cancelDoor();
};
'''
for sig in ['std::string toKey(', 'std::vector<std::string> splitOn(', 'std::string unescapeHint(', 'void ArenaHousing::applyState(', 'const ArenaHousing::Entry* ArenaHousing::find(', 'std::string ArenaHousing::hintForDoor(', 'bool ArenaHousing::canBypassDoorLock(', 'bool ArenaHousing::requestForDoor(', 'void ArenaHousing::performHeldTeleport(', 'void ArenaHousing::cancelDoor(']:code+=extract(sig)+'\n'
code+=r'''
}
int main(){mwmp::ArenaHousing a;MWWorld::ConstPtr door;
a.applyState("STATE\tTown, House^mine^Town, House\\nДом (Alice)^celltitle^1");
assert(a.hintForDoor(door)=="Город, Дом\nДом (Alice)");assert(a.canBypassDoorLock(door));
a.applyState("STATE\tTown, House^owned^Town, House\\nДом (Bob)^customtitle^0");
assert(a.hintForDoor(door)=="Town, House\nДом (Bob)");assert(!a.canBypassDoorLock(door));
a.applyState("STATE\tTown, House^mine^Town, House\\nДом (Alice)");assert(a.hintForDoor(door)=="Город, Дом\nДом (Alice)");
a.applyState("STATE\tTown, House^owned^House^customtitle^0");
a.requestForDoor(door,false);assert(a.mPendingOriginalLocked);assert(mwmp::sent.back()=='1');a.performHeldTeleport("Town, House");assert(teleports==0&&!a.mHasPendingDoor);
a.requestForDoor(door,false,true);assert(!a.mPendingOriginalLocked);a.performHeldTeleport("Wrong Cell");assert(teleports==0&&a.mHasPendingDoor);a.performHeldTeleport("Town, House");assert(teleports==1&&!a.mHasPendingDoor);
door.ref.lock=0;a.requestForDoor(door,true);assert(mwmp::sent.back()=='0');a.cancelDoor();a.performHeldTeleport("Town, House");assert(teleports==1);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'test.cpp';p.write_text(code)
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra',str(p),'-o',d+'/test'],check=True)
 subprocess.run([d+'/test'],check=True)
print('PASS Z010: CEL localization, custom titles, old state, trusted flag, lock protocol, locked GO refusal, key entry, cancellation')
