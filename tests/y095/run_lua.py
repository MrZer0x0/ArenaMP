#!/usr/bin/env python3
"""Run actual Lua server regressions through the installed Lua 5.4 library."""
import ctypes
import ctypes.util
import os
from pathlib import Path
root = Path(__file__).resolve().parents[2]
os.chdir(root)
lib=ctypes.CDLL(ctypes.util.find_library('lua5.4') or 'liblua5.4.so.0')
lib.luaL_newstate.restype=ctypes.c_void_p
lib.luaL_openlibs.argtypes=[ctypes.c_void_p]
lib.luaL_loadfilex.argtypes=[ctypes.c_void_p,ctypes.c_char_p,ctypes.c_char_p]
lib.lua_pcallk.argtypes=[ctypes.c_void_p,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_longlong,ctypes.c_void_p]
lib.lua_tolstring.argtypes=[ctypes.c_void_p,ctypes.c_int,ctypes.c_void_p]
lib.lua_tolstring.restype=ctypes.c_char_p
lib.lua_settop.argtypes=[ctypes.c_void_p,ctypes.c_int]
lib.lua_close.argtypes=[ctypes.c_void_p]
state=lib.luaL_newstate();lib.luaL_openlibs(state)
def check(code):
    if code: raise RuntimeError(lib.lua_tolstring(state,-1,None).decode())
try:
    files=list((root/'server/scripts').rglob('*.lua'))
    for path in files:
        check(lib.luaL_loadfilex(state,os.fsencode(path),None));lib.lua_settop(state,0)
    print(f'PASS Lua syntax: {len(files)} server scripts',flush=True)
    check(lib.luaL_loadfilex(state,b'tests/y095/server_regression.lua',None))
    check(lib.lua_pcallk(state,0,0,0,0,None))
finally:lib.lua_close(state)
