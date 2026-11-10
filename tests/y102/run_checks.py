#!/usr/bin/env python3
"""Compile the real Content API registration and run it with real Lua/LuaBridge.

Engine functions are stubs: this verifies Lua binding, not network animation.
Requires C++17, LuaBridge 2 headers and Lua 5.1 source files (no downloads).
"""
from pathlib import Path
import argparse, re, shutil, subprocess, tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--luabridge', type=Path, default=ROOT / 'extern/LuaBridge')
parser.add_argument('--lua-source', type=Path, required=True, help='Lua 5.1 src directory')
args = parser.parse_args()
source = (ROOT / 'apps/openmw-mp/Script/LangLua/LangLua.cpp').read_text()
binding = source.split('\nnamespace\n{', 1)[1].split('\nvoid LangLua::LoadProgram', 1)[0]
binding = 'namespace\n{' + binding
content = (ROOT / 'apps/openmw-mp/Script/Functions/Content.hpp').read_text()
entries = re.findall(r'SCRIPT_API_ENTRY\("([^"]+)"', content)
assert len(entries) == len(set(entries)) == 14
assert 'tes3mp.addCFunction("SendContentActorAnimation", sendContentActorAnimation);' in binding
definitions = []
for ret, name, params in re.findall(r'static\s+(unsigned int|bool|const char\*)\s+(\w+)\((.*?)\) noexcept;', content, re.S):
    if name == 'SendActorAnimation':
        body = '''++calls;
        assert(pid == 65530 && std::string(cell) == "Home");
        assert(refNum == 4000000000U && mpNum == 123456);
        assert(std::string(refId) == "arrille" && std::string(group) == "idle2");
        assert(mode == 2 && count == 17); return persist;'''
    else:
        body = 'return ' + {'unsigned int': '101', 'bool': 'true', 'const char*': '"ok"'}[ret] + ';'
    definitions.append(f'{ret} ContentFunctions::{name}({params}) noexcept {{{body}}}')
macros = set(re.findall(r'\b[A-Z][A-Z0-9_]*API\b', binding)) - {'CONTENTCATALOGAPI', 'CONTENTAPI'}
prelude = '''#include <cassert>
#include <iostream>
#include <string>
#include "lua.hpp"
#include <LuaBridge/LuaBridge.h>
#include "Content.hpp"
int calls = 0;
struct LangLua {
 static int CreateTimer(lua_State*) { return 0; }
 static int CreateTimerEx(lua_State*) { return 0; }
 static int MakePublic(lua_State*) { return 0; }
 static int CallPublic(lua_State*) { return 0; }
};
struct ScriptFunctions {
 static void StartTimer() {} static void StopTimer() {}
 static void RestartTimer() {} static void FreeTimer() {}
 static bool IsTimerElapsed() { return true; }
};
'''
main = '''
int main() {
 lua_State* L = luaL_newstate(); luaL_openlibs(L);
 {
   auto ns = luabridge::getGlobalNamespace(L).beginNamespace("tes3mp");
   registerLuaFunctions(ns); ns.endNamespace();
 }
 const char* script = R"LUA(
   assert(tes3mp.ContentCatalogGetVersion() == 101)
   assert(tes3mp.ContentCatalogGetSubrecordChunk("NPC_", "arrille", "NPCO", 0, 1, 2) == "ok")
   assert(tes3mp.ContentCatalogGetCellReference("Shop", 42, "chest") == "ok")
   local f = tes3mp.SendContentActorAnimation
   assert(f(65530, "Home", 4000000000, 123456, "arrille", "idle2", 2, 17, true) == true)
   assert(f(65530, "Home", 4000000000, 123456, "arrille", "idle2", 2, 17, false) == false)
   assert(not pcall(f, {}, "Home", 4000000000, 123456, "arrille", "idle2", 2, 17, true))
 )LUA";
 if (luaL_dostring(L, script)) {std::cerr << lua_tostring(L, -1); return 1;}
 assert(calls == 2);
 for (const char* name : {NAMES}) {
   lua_getglobal(L, "tes3mp"); lua_getfield(L, -1, name);
   assert(lua_isfunction(L, -1)); lua_pop(L, 2);
 }
 lua_close(L);
}
'''.replace('NAMES', ', '.join('"' + name + '"' for name in entries))
with tempfile.TemporaryDirectory(prefix='arenamp-y102-') as tmp:
    tmp = Path(tmp)
    bridge = tmp / 'LuaBridge'
    shutil.copytree(args.luabridge, bridge)
    # Upstream TES3MP headers may lack the noexcept forwarding specialization
    # already present in ArenaMP. Add it only to this temporary test copy.
    traits = bridge / 'detail/FuncTraits.h'
    text = traits.read_text()
    if 'struct FuncTraits <R (*) (P...) noexcept, D>' not in text:
        text = text.replace('} // namespace luabridge', '''
template<class R, class... P, class D>
struct FuncTraits <R (*) (P...) noexcept, D> : FuncTraits <R (*) (P...), D> {};
} // namespace luabridge''')
        traits.write_text(text)
    (tmp / 'Content.hpp').write_text(content)
    lua_sources = sorted(p for p in args.lua_source.glob('*.c') if p.name not in ('lua.c', 'luac.c', 'print.c'))
    assert lua_sources and (args.lua_source / 'lua.h').is_file()
    (tmp / 'lua.hpp').write_text('extern "C" {\n#include "lua.h"\n#include "lauxlib.h"\n#include "lualib.h"\n}\n')
    objects = []
    for p in lua_sources:
        obj = tmp / (p.stem + '.o')
        subprocess.run(['cc', '-O2', '-c', str(p), '-o', str(obj)], check=True, capture_output=True)
        objects.append(str(obj))
    cpp = tmp / 'binding.cpp'
    code = prelude + '\n'.join('#define ' + m + ' 0' for m in macros) + '\n' + '\n'.join(definitions) + '\n' + binding + main
    cpp.write_text(code)
    flags = ['g++', '-std=c++17', '-O2', '-I', str(tmp), '-I', str(args.lua_source)]
    exe = tmp / 'binding'
    subprocess.run(flags + [str(cpp)] + objects + ['-lm', '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    print('PASS real LuaBridge + Lua 5.1: all 14 Content functions registered; nine typed arguments, true/false result and bad argument handling', flush=True)
    cpp.write_text(code.replace('            CONTENTCATALOGAPI,', '            CONTENTAPI,'))
    failed = subprocess.run(flags + ['-fsyntax-only', str(cpp)], capture_output=True, text=True)
    assert failed.returncode != 0 and 'ReturnType' in failed.stderr
    print('PASS reproduced original LuaBridge ReturnType failure by restoring direct nine-argument binding', flush=True)
