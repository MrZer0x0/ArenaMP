#!/usr/bin/env python3
"""Focused source regressions; no game/NDK build is performed."""
from pathlib import Path
import ctypes, ctypes.util, os, subprocess, tempfile, py_compile
ROOT=Path(__file__).resolve().parents[2]
os.chdir(ROOT)
lib=ctypes.CDLL(ctypes.util.find_library('lua5.4') or 'liblua5.4.so.0')
lib.luaL_newstate.restype=ctypes.c_void_p
for n,args in [('luaL_openlibs',[ctypes.c_void_p]),('luaL_loadfilex',[ctypes.c_void_p,ctypes.c_char_p,ctypes.c_char_p]),
 ('lua_pcallk',[ctypes.c_void_p,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_longlong,ctypes.c_void_p]),
 ('lua_tolstring',[ctypes.c_void_p,ctypes.c_int,ctypes.c_void_p]),('lua_close',[ctypes.c_void_p])]:getattr(lib,n).argtypes=args
lib.lua_tolstring.restype=ctypes.c_char_p
def lua(path,execute=False):
 state=lib.luaL_newstate();lib.luaL_openlibs(state)
 try:
  code=lib.luaL_loadfilex(state,os.fsencode(path),None)
  if not code and execute:code=lib.lua_pcallk(state,0,0,0,0,None)
  if code:raise AssertionError(lib.lua_tolstring(state,-1,None).decode())
 finally:lib.lua_close(state)
for file in (ROOT/'server/scripts').rglob('*.lua'):lua(file)
lua('tests/v18/server.lua',True)
security=(ROOT/'apps/openmw/mwmechanics/security.cpp').read_text()
a=security.index('        void consumeToolUse(')
b=security.index('\n    }\n\n    bool Security::autoActivate',a)
helper=security[a:b]
with tempfile.TemporaryDirectory(prefix='arenamp-v18-') as td:
 temp=Path(td)
 stub=(ROOT/'tests/v18/tool_stub.cpp').read_text().replace('// INSERT_ACTUAL_HELPER',helper)
 actual=temp/'tool.cpp';actual.write_text(stub)
 for file,name in [(ROOT/'tests/v18/pricing_body.cpp','pricing_body'),(actual,'tool')]:
  binary=temp/name
  subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-pedantic','-I',str(ROOT),str(file),'-o',str(binary)],check=True)
  subprocess.run([str(binary)],check=True)
# Check normal engine action paths remain connected; these are integration guards,
# not a replacement for a playtest with real locks, meshes and remote actors.
for name in ['door','container']:
 source=(ROOT/f'apps/openmw/mwclass/{name}.cpp').read_text()
 assert '!hasKey && MWMechanics::Security::autoActivate(ptr, actor)' in source
assert 'mechanics->startSecurityAction(actor, target, MWWorld::Ptr(), spellId)' in security
controller=(ROOT/'apps/openmw/mwmechanics/character.cpp').read_text()
assert 'Security(mPtr).probeTrap(target, item' in controller
assert 'milliseconds(550)' in security
body=(ROOT/'apps/openmw/mwmechanics/actors.cpp').read_text()
assert 'PlayerList::isDedicatedPlayer(ptr)' in body and 'isActiveDialogueTarget(ptr)' in body
assert 'isDead() || bodyStats.isParalyzed()' in body
# Android parity checks belong to the separately distributed mobile project.
print('PASS V18 focused regressions (full engine build not performed)')
