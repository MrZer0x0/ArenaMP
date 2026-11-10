from pathlib import Path
import ctypes, ctypes.util, os, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
lua=ctypes.CDLL(ctypes.util.find_library('lua5.4'))
lua.luaL_newstate.restype=ctypes.c_void_p
for name,args in [('luaL_openlibs',[ctypes.c_void_p]),('luaL_loadfilex',[ctypes.c_void_p,ctypes.c_char_p,ctypes.c_char_p]),('lua_pcallk',[ctypes.c_void_p,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_longlong,ctypes.c_void_p]),('lua_tolstring',[ctypes.c_void_p,ctypes.c_int,ctypes.c_void_p]),('lua_close',[ctypes.c_void_p])]:getattr(lua,name).argtypes=args
lua.lua_tolstring.restype=ctypes.c_char_p
os.environ['ARENAMP_TEST_ROOT']=str(root/'server')
L=lua.luaL_newstate();lua.luaL_openlibs(L)
r=lua.luaL_loadfilex(L,str(Path(__file__).with_name('server_regression.lua')).encode(),None)
if not r:r=lua.lua_pcallk(L,0,0,0,0,None)
if r:raise RuntimeError(lua.lua_tolstring(L,-1,None).decode())
lua.lua_close(L)
source=(root/'apps/openmw/mwworld/inventorystore.cpp').read_text()
start=source.index('    bool canAutoEquipOwnedItem(');left=source.index('{',start);level=1;i=left+1
while level:
 if source[i]=='{':level+=1
 if source[i]=='}':level-=1
 i+=1
function=source[start:i]
source=(root/'apps/openmw/mwmp/ObjectList.cpp').read_text()
start=source.index('            // Y094: equipped apparel');end=source.index('            // If we are setting the entire contents',start)
capture=source[start:end]
cpp=r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include <cctype>
namespace MWWorld {struct Ptr;struct InventoryStore;
struct Class { bool npc=true;int services=1;bool isNpc() const{return npc;}
int getServices(const Ptr&) const{return services;}
InventoryStore& getInventoryStore(const Ptr&) const;};
struct Ref {std::string id,owner;const std::string& getRefId()const{return id;}
const std::string& getOwner()const{return owner;}};
struct Data {int count=1;int getCount()const{return count;}};
struct Node {Ref ref;Data data;Class cls;InventoryStore* inventory=nullptr;};
struct Ptr {Node* node=nullptr;Class& getClass()const{return node->cls;}
Ref& getCellRef()const{return node->ref;}Data& getRefData()const{return node->data;}
bool operator==(const Ptr& p)const{return node==p.node;}};
struct InventoryStore {static constexpr int Slot_Belt=15;std::vector<Ptr> items;
std::array<int,19> slots;InventoryStore(){slots.fill(-1);}
auto end(){return items.end();}auto getSlot(int slot){return slots[slot]<0?end():items.begin()+slots[slot];}};
InventoryStore& Class::getInventoryStore(const Ptr& p)const{return *p.node->inventory;}}
namespace MWMechanics {MWWorld::Ptr player;MWWorld::Ptr getPlayer(){return player;}}
namespace ESM {struct NPC {enum {Weapon=1,Armor=2,Clothing=4,MagicItems=8};};}
namespace Misc {struct StringUtils {static std::string lowerCase(std::string s){for(auto& c:s)c=std::tolower(static_cast<unsigned char>(c));return s;}
static bool ciEqual(const std::string& a,const std::string& b){return lowerCase(a)==lowerCase(b);}};}
struct BaseObjectList {enum {SET=0};};
'''+function+r'''
std::map<std::string,int> captureOwned(const MWWorld::Ptr& ptrFound,std::map<std::string,int> ownedCounts){
bool merchant=true,hasActorEquipment=true;int action=BaseObjectList::SET;
'''+capture+r'''
return ownedCounts;}
int main(){
MWWorld::InventoryStore inv;
MWWorld::Node actorNode{{"merchant",""},{1},{true,1},&inv};MWWorld::Ptr actor{&actorNode};
MWWorld::Node clothing{{"shirt",""},{1},{},nullptr},stock{{"stock",""},{1},{},nullptr};
MWWorld::Ptr shirt{&clothing},goods{&stock};inv.items={shirt,goods};inv.slots[8]=0;
assert(canAutoEquipOwnedItem(actor,shirt));assert(!canAutoEquipOwnedItem(actor,goods));
stock.ref.owner="MERCHANT";assert(canAutoEquipOwnedItem(actor,goods));stock.ref.owner="";
inv.slots[8]=-1;inv.slots[16]=0;assert(!canAutoEquipOwnedItem(actor,shirt));
inv.slots[16]=-1;inv.slots[8]=0;
auto counts=captureOwned(actor,{});assert(counts["shirt"]==1&&counts.count("stock")==0);
counts=captureOwned(actor,{{"shirt",1}});assert(counts["shirt"]==1);
inv.slots[12]=0;inv.slots[13]=1;stock.ref.id="shirt";inv.slots[8]=-1;
counts=captureOwned(actor,{});assert(counts["shirt"]==2);
actorNode.cls.services=0;assert(canAutoEquipOwnedItem(actor,goods));
actorNode.cls.services=1;MWMechanics::player=actor;assert(canAutoEquipOwnedItem(actor,goods));
std::cout<<"C++ production merchant filter / SET capture: 9 checks passed\n";
}
'''
with tempfile.TemporaryDirectory() as temp:
 p=Path(temp)/'merchant.cpp';p.write_text(cpp);exe=Path(temp)/'merchant'
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror',str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print('Y094 Lua and C++ regression checks passed; engine APIs mocked')
