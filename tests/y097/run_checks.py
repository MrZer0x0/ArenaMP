#!/usr/bin/env python3
"""Actual headless catalog + Lua API tests. No real player data or plugins used."""
from pathlib import Path
import ctypes, ctypes.util, json, os, shutil, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[2]
compiler=shutil.which('g++') or shutil.which('g++-13') or shutil.which('x86_64-linux-gnu-g++-13')
if not compiler: raise SystemExit('C++17 compiler required')
with tempfile.TemporaryDirectory(prefix='arenamp-content-test-') as temp:
    exe=Path(temp)/'catalog_test'
    subprocess.run([compiler,'-std=c++17','-Wall','-Wextra','-Werror','-O2','-I',str(ROOT),
        str(ROOT/'apps/openmw-mp/content/ContentCatalog.cpp'),str(ROOT/'tests/y097/catalog_test.cpp'),'-o',str(exe)],check=True)
    output=subprocess.check_output([str(exe)],text=True)
    lines=output.splitlines()
    record,robe,info=map(json.loads,lines[:3])
    assert record['source']=='Mod.esp' and record['inventory']==[{'refId':'robe','count':1}]
    assert robe['name']=='Роба' and robe['slots']==[11]
    assert info['plugins'][1]['masters']==['Base.esm']
    print(lines[3],flush=True)
    print('PASS native catalog JSON output and Unicode',flush=True)
lib=ctypes.CDLL(ctypes.util.find_library('lua5.4') or 'liblua5.4.so.0')
lib.luaL_newstate.restype=ctypes.c_void_p
for name,args in [('luaL_openlibs',[ctypes.c_void_p]),('luaL_loadfilex',[ctypes.c_void_p,ctypes.c_char_p,ctypes.c_char_p]),
 ('lua_pcallk',[ctypes.c_void_p,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_longlong,ctypes.c_void_p]),
 ('lua_tolstring',[ctypes.c_void_p,ctypes.c_int,ctypes.c_void_p]),('lua_settop',[ctypes.c_void_p,ctypes.c_int]),('lua_close',[ctypes.c_void_p])]:
    getattr(lib,name).argtypes=args
lib.lua_tolstring.restype=ctypes.c_char_p
state=lib.luaL_newstate();lib.luaL_openlibs(state)
def check(code):
    if code: raise RuntimeError(lib.lua_tolstring(state,-1,None).decode())
os.chdir(ROOT)
try:
    paths=list((ROOT/'server/scripts').rglob('*.lua'))
    for path in paths:
        check(lib.luaL_loadfilex(state,os.fsencode(path),None));lib.lua_settop(state,0)
    print(f'PASS Lua syntax: {len(paths)} scripts',flush=True)
    check(lib.luaL_loadfilex(state,b'tests/y097/content_api_test.lua',None));check(lib.lua_pcallk(state,0,0,0,0,None))
finally:lib.lua_close(state)
