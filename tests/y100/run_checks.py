#!/usr/bin/env python3
"""Run isolated Lua regressions. No server data is read or written."""
from pathlib import Path
import ctypes,ctypes.util,os
ROOT=Path(__file__).resolve().parents[2];os.chdir(ROOT)
lib=ctypes.CDLL(ctypes.util.find_library('lua5.4') or 'liblua5.4.so.0')
lib.luaL_newstate.restype=ctypes.c_void_p
for name,args in [('luaL_openlibs',[ctypes.c_void_p]),('luaL_loadfilex',[ctypes.c_void_p,ctypes.c_char_p,ctypes.c_char_p]),('lua_pcallk',[ctypes.c_void_p,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_longlong,ctypes.c_void_p]),('lua_tolstring',[ctypes.c_void_p,ctypes.c_int,ctypes.c_void_p]),('lua_close',[ctypes.c_void_p])]:getattr(lib,name).argtypes=args
lib.lua_tolstring.restype=ctypes.c_char_p
for file in ['tests/y100/server_regression.lua','tests/y100/housing_regression.lua','tests/y100/authority_regression.lua','tests/housing/h019_cell_interest_test.lua']:
 state=lib.luaL_newstate();lib.luaL_openlibs(state)
 try:
  result=lib.luaL_loadfilex(state,file.encode(),None)
  if result:raise RuntimeError(lib.lua_tolstring(state,-1,None).decode())
  result=lib.lua_pcallk(state,0,0,0,0,None)
  if result:raise RuntimeError(lib.lua_tolstring(state,-1,None).decode())
 finally:lib.lua_close(state)
