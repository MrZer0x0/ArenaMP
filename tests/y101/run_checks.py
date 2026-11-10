#!/usr/bin/env python3
"""Compile the real Script Types.hpp with host and simulated AArch64 va_list.
RakNet/Utils dependencies are stubbed; the templates under test are unchanged.
No claim of an Android NDK/APK build is made by these isolated checks.
"""
from pathlib import Path
import os,re,shutil,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
compiler=shutil.which('g++') or shutil.which('x86_64-linux-gnu-g++-13')
if not compiler: raise SystemExit('C++17 compiler is required')
TEST=r'''
#include <cstdarg>
#include "Types.hpp"
using ScriptFunc=unsigned long long(*)();
struct Any {void* value;};
int createTimer(ScriptFunc,int,const char*,va_list) noexcept {return 17;}
Any callPublic(const char*,va_list) noexcept {return {nullptr};}
constexpr ScriptFunctionData timer{"CreateTimerEx",createTimer};
constexpr ScriptFunctionData call{"CallPublic",callPublic};
static_assert(timer.func.numargs==4 && timer.func.ret=='q');
static_assert(timer.func.types[0]=='p' && timer.func.types[1]=='q' && timer.func.types[2]=='s');
static_assert(call.func.numargs==2 && call.func.types[0]=='s');
static_assert(TypeChar<bool,sizeof(bool)>::value=='b');
static_assert(TypeChar<const char*,sizeof(const char*)>::value=='s');
static_assert(TypeChar<char*,sizeof(char*)>::value=='s');
static_assert(TypeChar<double,sizeof(double)>::value=='f');
static_assert(TypeChar<void,sizeof_void<void>::value>::value=='v');
#ifdef EMULATE_ARM64_VA_LIST
static_assert(sizeof(va_list)==32);
static_assert(timer.func.types[3]=='p' && call.func.types[1]=='p');
#endif
int main(){
 ScriptFunctionAddress address{"CreateTimerEx",createTimer};
 decltype(&createTimer) restored=nullptr;
 std::memcpy(&restored,&address.addr,sizeof(restored));
 if(restored!=&createTimer)return 1;
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='arenamp-y101-') as tmp:
 tmp=Path(tmp);common=tmp/'common';common.mkdir()
 (common/'RakNetTypes.h').write_text('#pragma once\nnamespace RakNet {using NetworkID=unsigned long long;}\n')
 (common/'Utils.hpp').write_text('#pragma once\nnamespace Utils {template<unsigned long N> constexpr unsigned int hash(const char(&)[N]) {return 1;}}\n')
 real=(ROOT/'apps/openmw-mp/Script/Types.hpp').read_bytes()
 (common/'Types.hpp').write_bytes(real)
 cpp=tmp/'test.cpp';cpp.write_text(TEST)
 flags=[compiler,'-std=c++17','-Wall','-Wextra','-Werror','-O2','-I',str(common)]
 exe=tmp/'host'
 subprocess.run(flags+[str(cpp),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
 print('PASS real Types.hpp: host va_list, scalar/string metadata and unchanged native function address',flush=True)
 arm=tmp/'arm';arm.mkdir()
 (arm/'cstdarg').write_text('#pragma once\nnamespace std {struct alignas(8) va_list {unsigned long long slots[4];};}\nusing va_list=std::va_list;\n')
 armflags=flags+['-I',str(arm),'-DEMULATE_ARM64_VA_LIST']
 exe=tmp/'arm64-simulated'
 subprocess.run(armflags+[str(cpp),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
 print('PASS real Types.hpp: simulated 32-byte AArch64 va_list for CreateTimerEx/CallPublic',flush=True)
 unsupported=tmp/'unsupported.cpp';unsupported.write_text('#include "Types.hpp"\nstruct Other {char bytes[32];};\nint main(){return TypeChar<Other,sizeof(Other)>::value;}\n')
 result=subprocess.run(flags+['-fsyntax-only',str(unsupported)],capture_output=True,text=True)
 assert result.returncode!=0 and 'Unsupported type in variadic type list' in result.stderr
 print('PASS unsupported aggregate still rejected',flush=True)
 # Reproduce the logged failure with the previous primary template.
 before=real.decode();start=before.index('template<typename T, size_t t> struct TypeChar');end=before.index('template<> struct TypeChar<bool',start)
 before=before[:start]+'template<typename T, size_t t> struct TypeChar { static_assert(!t, "Unsupported type in variadic type list"); };\n'+before[end:]
 (common/'Types.hpp').write_text(before)
 result=subprocess.run(armflags+['-fsyntax-only',str(cpp)],capture_output=True,text=True)
 assert result.returncode!=0 and 'Unsupported type in variadic type list' in result.stderr
 print('PASS reproduced original failure with previous primary template and identical 32-byte fixture',flush=True)
# Real registration lists must cover every exported API category.
header=(ROOT/'apps/openmw-mp/Script/ScriptFunctions.hpp').read_text()
lua=(ROOT/'apps/openmw-mp/Script/LangLua/LangLua.cpp').read_text()
metadata=header.split('static constexpr ScriptFunctionData functions[]{',1)[1].split('\n    };',1)[0]
bindings=lua.split('const int registeredFunctions[] = {',1)[1].split('\n        };',1)[0]
api=lambda s:set(re.findall(r'\b[A-Z][A-Z0-9_]*API\b',s.replace('CONTENTCATALOGAPI', 'CONTENTAPI')))
assert api(metadata)==api(bindings),(api(metadata)-api(bindings),api(bindings)-api(metadata))
for name in ['CreateTimer','CreateTimerEx','MakePublic','CallPublic']:
 assert f'addCFunction("{name}", LangLua::{name})' in lua
print('PASS Lua API-category parity and dedicated timer/public-call wrappers',flush=True)
