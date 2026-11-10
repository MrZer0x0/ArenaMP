#!/usr/bin/env python3
"""Isolated Lua regression; optional fixture files are read only."""
from pathlib import Path
import ctypes, ctypes.util, os
ROOT = Path(__file__).resolve().parents[2]
os.chdir(ROOT)
lua = ctypes.CDLL(ctypes.util.find_library('lua5.4') or 'liblua5.4.so.0')
lua.luaL_newstate.restype = ctypes.c_void_p
for name, args in [('luaL_openlibs', [ctypes.c_void_p]),
                   ('luaL_loadfilex', [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p]),
                   ('lua_pcallk', [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_longlong, ctypes.c_void_p]),
                   ('lua_tolstring', [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p]),
                   ('lua_close', [ctypes.c_void_p])]:
    getattr(lua, name).argtypes = args
lua.lua_tolstring.restype = ctypes.c_char_p
state = lua.luaL_newstate()
lua.luaL_openlibs(state)
try:
    status = lua.luaL_loadfilex(state, b'tests/y105/regression.lua', None)
    if status:
        raise RuntimeError(lua.lua_tolstring(state, -1, None).decode())
    status = lua.lua_pcallk(state, 0, 0, 0, 0, None)
    if status:
        raise RuntimeError(lua.lua_tolstring(state, -1, None).decode())
finally:
    lua.lua_close(state)
