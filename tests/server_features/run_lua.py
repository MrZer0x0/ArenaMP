#!/usr/bin/env python3
"""Run server feature regressions with the system Lua 5.4 shared library.
No game server, real profiles or network connections are used.
"""
import ctypes
import ctypes.util
from pathlib import Path
import sys
root = Path(__file__).resolve().parents[2]
lib = ctypes.CDLL(ctypes.util.find_library('lua5.4') or 'liblua5.4.so.0')
lib.luaL_newstate.restype = ctypes.c_void_p
lib.luaL_openlibs.argtypes = [ctypes.c_void_p]
lib.luaL_loadfilex.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p]
lib.luaL_loadfilex.restype = ctypes.c_int
lib.lua_pcallk.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_longlong, ctypes.c_void_p]
lib.lua_pcallk.restype = ctypes.c_int
lib.lua_tolstring.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(ctypes.c_size_t)]
lib.lua_tolstring.restype = ctypes.c_char_p
lib.lua_settop.argtypes = [ctypes.c_void_p, ctypes.c_int]
lib.lua_close.argtypes = [ctypes.c_void_p]
L = lib.luaL_newstate()
lib.luaL_openlibs(L)
def check(result):
    if result:
        raise RuntimeError(lib.lua_tolstring(L, -1, None).decode())
try:
    paths = list((root/'server/scripts').rglob('*.lua')) + [root/'server/lib/lua/jsonInterface.lua']
    for path in paths:
        check(lib.luaL_loadfilex(L, str(path).encode(), None))
        lib.lua_settop(L, 0)
    print(f'Lua syntax: {len(paths)} files passed', flush=True)
    import os
    os.chdir(root)
    check(lib.luaL_loadfilex(L, str(root/'tests/server_features/regression.lua').encode(), None))
    check(lib.lua_pcallk(L, 0, 0, 0, 0, None))
    check(lib.luaL_loadfilex(L, str(root/'tests/server_features/house_items.lua').encode(), None))
    check(lib.lua_pcallk(L, 0, 0, 0, 0, None))
    check(lib.luaL_loadfilex(L, str(root/'tests/server_features/item_references.lua').encode(), None))
    check(lib.lua_pcallk(L, 0, 0, 0, 0, None))
    check(lib.luaL_loadfilex(L, str(root/'tests/server_features/house_cleanup.lua').encode(), None))
    check(lib.lua_pcallk(L, 0, 0, 0, 0, None))
finally:
    lib.lua_close(L)
