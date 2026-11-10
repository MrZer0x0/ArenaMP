import ctypes, sys
from pathlib import Path
lib=ctypes.CDLL('liblua5.4.so.0')
lib.luaL_newstate.restype=ctypes.c_void_p
L=lib.luaL_newstate()
lib.luaL_openlibs.argtypes=[ctypes.c_void_p];lib.luaL_openlibs(L)
lib.luaL_loadfilex.argtypes=[ctypes.c_void_p,ctypes.c_char_p,ctypes.c_char_p]
lib.lua_pcallk.argtypes=[ctypes.c_void_p,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_longlong,ctypes.c_void_p]
lib.lua_tolstring.argtypes=[ctypes.c_void_p,ctypes.c_int,ctypes.c_void_p];lib.lua_tolstring.restype=ctypes.c_char_p
lib.lua_settop.argtypes=[ctypes.c_void_p,ctypes.c_int]
for name in sys.argv[1:]:
 status=lib.luaL_loadfilex(L,str(name).encode(),None)
 if not status and len(sys.argv)==2: status=lib.lua_pcallk(L,0,-1,0,0,None)
 if status: raise RuntimeError(lib.lua_tolstring(L,-1,None).decode())
 lib.lua_settop(L,0)
print('PASS:',len(sys.argv)-1,'Lua file(s)')
