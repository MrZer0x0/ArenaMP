"""Compile the production NPC door search with lightweight engine stand-ins."""
from pathlib import Path
import subprocess,tempfile
s=Path('apps/openmw/mwmechanics/aicombat.cpp').read_text();a=s.index('    template <class Predicate>\n    MWWorld::Ptr findTeleportDoor');b=s.index('\n}\n',a)
code=r'''
#include <vector>
#include <string>
#include <cmath>
#include <cassert>
namespace osg {struct Vec3f {float x=0;};}
namespace ESM {struct Door {};}
namespace MWWorld {
struct Position {float x=0;osg::Vec3f asVec3()const{return {x};}};
struct Data {int count=1;bool enabled=true;Position pos;int getCount()const{return count;}bool isEnabled()const{return enabled;}const Position&getPosition()const{return pos;}};
struct Ref {bool teleport=true;int lock=0;std::string trap;bool getTeleport()const{return teleport;}int getLockLevel()const{return lock;}const std::string&getTrap()const{return trap;}};
template<class T>struct LiveCellRef {Data mData;Ref ref;};
template<class T>struct CellRefList {std::vector<LiveCellRef<T>>mList;};
struct CellStore {CellRefList<ESM::Door>doors;const auto&getReadOnlyDoors(){return doors;}};
struct Ptr {LiveCellRef<ESM::Door>*p=nullptr;CellStore*c=nullptr;Ptr()=default;Ptr(LiveCellRef<ESM::Door>*v,CellStore*w):p(v),c(w){}CellStore*getCell()const{return c;}const Data&getRefData()const{return p->mData;}const Ref&getCellRef()const{return p->ref;}};
}
namespace MWMechanics {float distanceIgnoreZ(osg::Vec3f a,osg::Vec3f b){return std::abs(a.x-b.x);}}
'''+s[a:b]+r'''
int main(){using namespace MWWorld;CellStore c;LiveCellRef<ESM::Door>actor;c.doors.mList.resize(6);
for(int i=0;i<6;i++)c.doors.mList[i].mData.pos.x=i+1;
c.doors.mList[0].ref.lock=10;c.doors.mList[1].ref.trap="fire";c.doors.mList[2].mData.enabled=false;c.doors.mList[3].mData.count=0;c.doors.mList[4].ref.teleport=false;
auto all=[](const Ptr&){return true;};Ptr p(&actor,&c);
assert(findTeleportDoor(p,20,all).p==&c.doors.mList[5]);assert(findTeleportDoor(p,5,all).p==nullptr);
c.doors.mList[5].ref.lock=1;assert(findTeleportDoor(p,20,all).p==nullptr);
c.doors.mList[0].ref.lock=0;assert(findTeleportDoor(p,20,all).p==&c.doors.mList[0]);
assert(findTeleportDoor(p,20,[](const Ptr&){return false;}).p==nullptr);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'test.cpp';p.write_text(code)
 subprocess.run(['g++','-std=c++17',str(p),'-o',d+'/test'],check=True)
 subprocess.run([d+'/test'],check=True)
print('PASS Z012: NPC locked/trapped/disabled/deleted/nonteleport doors, range and predicate')
