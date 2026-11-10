#!/usr/bin/env python3
"""Targeted Y110 regressions; does not build Windows/Linux/Android engine binaries."""
import ctypes
import ctypes.util
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
os.chdir(ROOT)
lua = ctypes.CDLL(ctypes.util.find_library('lua5.4') or 'liblua5.4.so.0')
lua.luaL_newstate.restype = ctypes.c_void_p
for name, args in [
    ('luaL_openlibs', [ctypes.c_void_p]),
    ('luaL_loadfilex', [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p]),
    ('luaL_loadstring', [ctypes.c_void_p, ctypes.c_char_p]),
    ('lua_pcallk', [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_longlong, ctypes.c_void_p]),
    ('lua_tolstring', [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p]),
    ('lua_settop', [ctypes.c_void_p, ctypes.c_int]),
    ('lua_close', [ctypes.c_void_p]),
]:
    getattr(lua, name).argtypes = args
lua.lua_tolstring.restype = ctypes.c_char_p

def check(code, state):
    if code:
        raise RuntimeError(lua.lua_tolstring(state, -1, None).decode())

roots = [ROOT / 'server']
if os.environ.get('ARENAMP_DEPLOYED_SERVER'):
    roots.append(Path(os.environ['ARENAMP_DEPLOYED_SERVER']).resolve())
for root in roots:
    state = lua.luaL_newstate()
    lua.luaL_openlibs(state)
    try:
        paths = list((root / 'scripts').rglob('*.lua'))
        for path in paths:
            check(lua.luaL_loadfilex(state, os.fsencode(path), None), state)
            lua.lua_settop(state, 0)
        # Paths here are local test data, encoded as a Lua long string.
        check(lua.luaL_loadstring(state, ('SERVER_ROOT = [[' + str(root) + ']]').encode()), state)
        check(lua.lua_pcallk(state, 0, 0, 0, 0, None), state)
        check(lua.luaL_loadfilex(state, b'tests/y110/login_sync.lua', None), state)
        check(lua.lua_pcallk(state, 0, 0, 0, 0, None), state)
        print('PASS Lua syntax and integration:', root, len(paths), 'modules', flush=True)
    finally:
        lua.lua_close(state)

with tempfile.TemporaryDirectory(prefix='arena-y110-') as temp:
    binary = Path(temp) / 'armor_wear'
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-O2', '-I', str(ROOT),
                    'tests/y110/armor_wear.cpp', '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)

subprocess.run(['python3', 'android/buildscripts/patches/openmw/31-arenamp-y092-ui-ai-progression-enchant.py',
                str(ROOT), '--verify-only'], check=True)
print('PASS Y110 targeted checks', flush=True)
