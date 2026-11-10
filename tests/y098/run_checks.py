#!/usr/bin/env python3
"""Y098 isolated regression checks; no production profiles or game plugins modified."""
from pathlib import Path
import ctypes,ctypes.util,hashlib,importlib.util,json,os,shutil,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
os.chdir(ROOT)
lib=ctypes.CDLL(ctypes.util.find_library('lua5.4'))
lib.luaL_newstate.restype=ctypes.c_void_p
for name,args in [('luaL_openlibs',[ctypes.c_void_p]),('luaL_loadfilex',[ctypes.c_void_p,ctypes.c_char_p,ctypes.c_char_p]),('lua_pcallk',[ctypes.c_void_p,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_longlong,ctypes.c_void_p]),('lua_tolstring',[ctypes.c_void_p,ctypes.c_int,ctypes.c_void_p]),('lua_close',[ctypes.c_void_p])]:getattr(lib,name).argtypes=args
lib.lua_tolstring.restype=ctypes.c_char_p
state=lib.luaL_newstate();lib.luaL_openlibs(state)
try:
 for result in [lib.luaL_loadfilex(state,b'tests/y098/server_test.lua',None)]:
  if result:raise RuntimeError(lib.lua_tolstring(state,-1,None).decode())
 result=lib.lua_pcallk(state,0,0,0,0,None)
 if result:raise RuntimeError(lib.lua_tolstring(state,-1,None).decode())
finally:lib.lua_close(state)

def function(source,signature):
 start=source.index(signature);i=source.index('{',start);depth=1;j=i+1
 while depth:
  if source[j]=='{':depth+=1
  if source[j]=='}':depth-=1
  j+=1
 return source[start:j]

native=(ROOT/'apps/openmw-mp/Script/Functions/Server.cpp').read_text()
names=['ReadFileUtf8','WriteFileUtf8','GetFileIoError','RenameFileUtf8','RemoveFileUtf8','ReplaceFileAtomic','GetFileStatusUtf8']
methods=[]
for name in names:
 import re
 signature=re.search(r'^[^\n]*ServerFunctions::'+name+r'\(',native,re.M).group()
 methods.append(function(native,signature))
source=r'''
#include "apps/openmw-mp/atomicfile.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
class ServerFunctions {public:
static const char* ReadFileUtf8(const char*) noexcept;
static bool WriteFileUtf8(const char*,const char*) noexcept;
static const char* GetFileIoError() noexcept;
static bool RenameFileUtf8(const char*,const char*) noexcept;
static bool RemoveFileUtf8(const char*) noexcept;
static bool ReplaceFileAtomic(const char*,const char*) noexcept;
static int GetFileStatusUtf8(const char*) noexcept;
};
static std::string fileContent,fileIoError;
'''+ '\n'.join(methods)+r'''
int main(int argc,char** argv){
 assert(argc==2);std::string dir=argv[1],path=dir+"/Игрок.json",temp=path+".tmp";
 assert(ServerFunctions::WriteFileUtf8(path.c_str(),"{\"v\":1}"));
 assert(std::string(ServerFunctions::ReadFileUtf8(path.c_str()))=="{\"v\":1}");
 assert(std::string(ServerFunctions::GetFileIoError()).empty());
 assert(ServerFunctions::WriteFileUtf8(temp.c_str(),"{\"v\":2}"));
 assert(ServerFunctions::ReplaceFileAtomic(temp.c_str(),path.c_str()));
 assert(std::string(ServerFunctions::ReadFileUtf8(path.c_str()))=="{\"v\":2}");
 assert(!ServerFunctions::RenameFileUtf8(path.c_str(),path.c_str()));
 const auto renamed=dir+"/Другое имя.json";
 assert(ServerFunctions::RenameFileUtf8(path.c_str(),renamed.c_str()));
 assert(ServerFunctions::GetFileStatusUtf8(path.c_str())==0);
 assert(!ServerFunctions::ReplaceFileAtomic(temp.c_str(),renamed.c_str()));
 assert(std::string(ServerFunctions::ReadFileUtf8(renamed.c_str()))=="{\"v\":2}");
 assert(ServerFunctions::RemoveFileUtf8(renamed.c_str()));
 ServerFunctions::ReadFileUtf8(renamed.c_str());assert(std::string(ServerFunctions::GetFileIoError())!="");
 std::string unchanged="old";assert(!ArenaFileOps::read(renamed,unchanged) && unchanged=="old");
 assert(!ArenaFileOps::write(std::string("a\0b",3),"bad"));
 assert(ServerFunctions::WriteFileUtf8(path.c_str(),""));
 assert(std::string(ServerFunctions::ReadFileUtf8(path.c_str())).empty() && std::string(ServerFunctions::GetFileIoError()).empty());
 std::cout<<"PASS actual native UTF-8 file API: read/write, atomic replace, no-overwrite migration, failure and removal\n";
}
'''
compiler=shutil.which('g++') or shutil.which('x86_64-linux-gnu-g++-13')
with tempfile.TemporaryDirectory(prefix='arenamp-y098-') as td:
 td=Path(td);cpp=td/'native.cpp';cpp.write_text(source);exe=td/'native'
 subprocess.run([compiler,'-std=c++17','-Wall','-Wextra','-Werror','-O2','-I',str(ROOT),str(cpp),'-o',str(exe)],check=True)
 subprocess.run([str(exe),str(td)],check=True)
 # Compile actual cell lookup functions against small cell fixtures.
 cell=(ROOT/'apps/openmw-mp/CellController.cpp').read_text()
 lookup='\n'.join(function(cell,sig) for sig in ['Cell *CellController::getCellByXY','Cell *CellController::getCellByName'])
 cpp.write_text(r'''
#include <algorithm>
#include <cassert>
#include <deque>
#include <string>
#include <iostream>
#define LOG_APPEND(...) ((void)0)
struct Desc {struct {int mX=0,mY=0;} mData;std::string mName;bool exterior=false;bool isExterior() const{return exterior;}};
struct Cell {Desc cell;};
class CellController {public: std::deque<Cell*> cells; Cell* getCellByXY(int,int);Cell* getCellByName(std::string);};
'''+lookup+r'''
int main(){Cell inside,outside;inside.cell.mName="House";outside.cell.exterior=true;outside.cell.mName="House";
CellController c;c.cells={&inside};assert(c.getCellByXY(0,0)==nullptr);c.cells.push_front(&outside);
assert(c.getCellByName("House")==&inside);assert(c.getCellByXY(0,0)==&outside);assert(c.getCellByName("Missing")==nullptr);
std::cout<<"PASS actual cell lookup: interior/exterior identity and missing cells\n";}
''')
 subprocess.run([compiler,'-std=c++17','-Wall','-Wextra','-Werror','-O2',str(cpp),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
 # Execute the actual receive-loop header; synthetic endless traffic and continue.
 network=(ROOT/'apps/openmw-mp/Networking.cpp').read_text();start=network.index('        const auto packetDeadline');end=network.index('\n        {',start)
 loop=network[start:end]
 cpp.write_text(r'''
#include <chrono>
#include <cassert>
#include <iostream>
struct Packet {};struct Peer {Packet item;int got=0,freed=0;Packet* Receive(){++got;return &item;}void DeallocatePacket(Packet*){++freed;}};
int main(){Peer p;auto peer=&p;Packet* packet=nullptr;
'''+loop+r'''
 {continue;}
 assert(p.got>0 && p.got<=256 && p.freed==p.got);
 std::cout<<"PASS actual receive loop: bounded work and every fetched packet released\n";
}
''')
 subprocess.run([compiler,'-std=c++17','-Wall','-Wextra','-Werror','-O2',str(cpp),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
 # Compile the production conditions for sneak eye clearance and helmet groups.
 camera=(ROOT/'apps/openmw/mwrender/camera.cpp').read_text()
 animation=(ROOT/'apps/openmw/mwrender/npcanimation.cpp').read_text()
 import re
 eye=re.search(r'if \((\(mImmersiveFirstPersonEnabled \|\| mSneakImmersiveFirstPersonActive\).*?!= 0.f)\)',camera,re.S).group(1)
 helmet=re.search(r'if \((mViewMode == VM_ImmersiveFirstPerson\s*&& slotlist\[i\].mSlot == MWWorld::InventoryStore::Slot_Helmet)\)',animation).group(1)
 cpp.write_text(r'''
#include <cassert>
#include <iostream>
namespace MWWorld {struct InventoryStore {enum {Slot_Helmet=0,Slot_Robe=11};};}
enum {VM_Normal,VM_FirstPerson,VM_ImmersiveFirstPerson};
int main(){for(int enabled=0;enabled<2;++enabled)for(int sneak=0;sneak<2;++sneak){
bool mImmersiveFirstPersonEnabled=enabled,mSneakImmersiveFirstPersonActive=sneak;
float mImmersiveFirstPersonForwardOffset=8.f;bool shifted=('''+eye+r''');assert(shifted==bool(enabled||sneak));}
struct {int mSlot;} slotlist[]={{0},{11}};
for(int mode=0;mode<3;++mode){int mViewMode=mode,drawn=0;
for(int i=0;i<2;++i){if('''+helmet+r'''){continue;}
++drawn;}
assert(drawn==(mode==VM_ImmersiveFirstPerson?1:2));}
std::cout<<"PASS actual render conditions: sneak/permanent eye clearance and helmet group visibility\n";}
''')
 subprocess.run([compiler,'-std=c++17','-Wall','-Wextra','-Werror','-O2',str(cpp),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
 # Offline migration on synthetic account copies: byte preservation and collisions.
 tool=ROOT/'server/tools/migrate_profile_filenames.py'
 directory=td/'player';directory.mkdir()
 raw=json.dumps({'login':{'name':'Игрок'},'inventory':[{'refId':'robe','count':1}]},ensure_ascii=False).encode()
 hashed='_utf8_'+hashlib.sha256('Игрок'.encode()).hexdigest()+'.json'
 (directory/hashed).write_bytes(raw);(directory/(hashed+'.bak')).write_bytes(raw)
 subprocess.run(['python3',str(tool),str(directory)],check=True,capture_output=True);assert (directory/hashed).exists()
 subprocess.run(['python3',str(tool),str(directory),'--apply'],check=True,capture_output=True)
 assert (directory/'Игрок.json').read_bytes()==raw and (directory/'Игрок.json.bak').read_bytes()==raw
 assert not (directory/hashed).exists()
 (directory/hashed).write_bytes(raw)
 result=subprocess.run(['python3',str(tool),str(directory),'--apply'],capture_output=True)
 assert result.returncode==2 and (directory/hashed).read_bytes()==raw and (directory/'Игрок.json').read_bytes()==raw
 print('PASS offline profile migration: dry run, backup, unchanged bytes and collision refusal',flush=True)
print('Y098 checks complete. Full Windows/RakNet/Android runtime not executed.',flush=True)
