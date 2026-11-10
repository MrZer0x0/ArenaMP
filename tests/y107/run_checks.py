#!/usr/bin/env python3
"""Headless Y107 tests. This does not build the engine or an Android APK."""
from pathlib import Path
import ctypes, ctypes.util, os, subprocess, tempfile, xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[2]
os.chdir(ROOT)
lib=ctypes.CDLL(ctypes.util.find_library('lua5.4') or 'liblua5.4.so.0')
lib.luaL_newstate.restype=ctypes.c_void_p
for name,args in [('luaL_openlibs',[ctypes.c_void_p]),('luaL_loadfilex',[ctypes.c_void_p,ctypes.c_char_p,ctypes.c_char_p]),
 ('lua_pcallk',[ctypes.c_void_p,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_longlong,ctypes.c_void_p]),
 ('lua_tolstring',[ctypes.c_void_p,ctypes.c_int,ctypes.c_void_p]),('lua_close',[ctypes.c_void_p])]:
    getattr(lib,name).argtypes=args
lib.lua_tolstring.restype=ctypes.c_char_p
state=lib.luaL_newstate();lib.luaL_openlibs(state)
try:
    for code in [lib.luaL_loadfilex(state,b'tests/y107/server_test.lua',None)]:
        if code: raise RuntimeError(lib.lua_tolstring(state,-1,None).decode())
    code=lib.lua_pcallk(state,0,0,0,0,None)
    if code: raise RuntimeError(lib.lua_tolstring(state,-1,None).decode())
finally: lib.lua_close(state)
for name in ['openmw_trade_window','openmw_enchanting_dialog','openmw_itemselection_dialog','openmw_edit_effect']:
    tree=ET.parse(ROOT/'files/mygui'/f'{name}.layout')
    names=[w.get('name') for w in tree.iter('Widget') if w.get('name')]
    assert len(names)==len(set(names)), (name,'duplicate widget')
print('PASS MyGUI layouts: XML and unique widget names',flush=True)
with tempfile.TemporaryDirectory(prefix='arena-ce-') as temp:
    binary=Path(temp)/'protocol'
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-O2','-I',str(ROOT),
        'tests/y107/protocol_test.cpp','-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)

with tempfile.TemporaryDirectory(prefix='arena-ce-bridge-') as temp:
    temp=Path(temp)
    production=(ROOT/'apps/openmw/mwmp/CraftEconomic.cpp').read_text()
    body=production[production.index('namespace mwmp\n{'):]
    source=temp/'bridge.cpp'
    source.write_text('#include "tests/y107/bridge_harness.hpp"\n'+body+'\n'+(ROOT/'tests/y107/bridge_scenarios.cpp').read_text())
    binary=temp/'bridge'
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-O2','-I',str(ROOT),str(source),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)

with tempfile.TemporaryDirectory(prefix='arena-ce-extra-') as temp:
    temp=Path(temp)
    for name,extra in [('catalog',['apps/openmw-mp/content/ContentCatalog.cpp']),('updater',[])]:
        binary=temp/name
        subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-O2','-I',str(ROOT),
            'tests/y107/'+name+'_test.cpp',*extra,'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
# Same fixed flag table as the actual engine; no silently divergent copy.
import re
engine=(ROOT/'components/esm/loadmgef.cpp').read_text()
reader=(ROOT/'apps/openmw-mp/content/ContentCatalog.cpp').read_text()
flags=lambda t: re.findall(r'0x[0-9a-f]+',t)
a=flags(re.search(r'HardcodedFlags\[NumberOfHardcodedFlags\] = \{(.*?)\};',engine,re.S)[1])
b=flags(re.search(r'legacyEffectFlags\[\] = \{(.*?)\};',reader,re.S)[1])
assert len(a)==143 and a==b
# Android's pure merge uses exactly the same bootstrap values as desktop.
common=(ROOT/'components/misc/buildupdatedefaults.hpp').read_text()
android=(ROOT/'android/app/src/main/java/file/BuildManifest.kt').read_text()
for key,value in re.findall(r'\{"([^"]+)","([^"]*)"\}',common):
    assert f'"{key}" to "{value}"' in android
assert 'contentVersion = QStringLiteral("9999999");' in (ROOT/'components/config/buildmanifest.cpp').read_text()
assert 'var contentVersion: String = "9999999"' in android
assert 'contentVersion = existing?.contentVersion ?: "9999999"' in android
for file in ['apps/openmw-mp/Networking.cpp','apps/openmw/mwmp/Networking.cpp']:
    assert not re.search(r'(?<!::)\bIS_CONNECTED\b',(ROOT/file).read_text())
print('PASS engine flag table parity, Android update endpoint parity and CrabNet enum qualification',flush=True)

# Compile the production GUI lifecycle methods against model/widget doubles.
# The scenario frees the old inventory boundary between prepare and rebuild.
def method(source, signature):
    start=source.index(signature);opening=source.index('{',start);depth=1;end=opening+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
trade=(ROOT/'apps/openmw/mwgui/tradewindow.cpp').read_text()
with tempfile.TemporaryDirectory(prefix='arena-ce-ui-') as temp:
    temp=Path(temp)
    cpp=temp/'ui.cpp'
    cpp.write_text('#include "tests/y107/inventory_ui_harness.hpp"\nnamespace MWGui {\n'
        +method(trade,'    bool TradeWindow::prepareInventoryReplace(')+'\n'
        +method(trade,'    void TradeWindow::finishInventoryReplace(')+'\n}\n'+r"""
int main(){
 MWGui::TradeWindow t;t.finishInventoryReplace(true);assert(t.mTradeModel);
 t.mServerCommit=7;
 assert(!t.prepareInventoryReplace({99}) && t.mTradeModel);
 assert(t.prepareInventoryReplace({11}));assert(!t.mTradeModel && !t.mSortModel && t.mItemToSell==-1);
 assert(!MWBase::Environment::get().wm.count.shown);
 const int requests=t.requests;
 // Old item pointers may now be freed: neither trade view retains its old model.
 assert(t.view.source==nullptr);
 t.finishInventoryReplace(true);assert(t.mTradeModel && t.requests==requests && t.mServerCommit==7);
 t.mServerCommit=0;assert(t.prepareInventoryReplace({0}));t.finishInventoryReplace(true);
 assert(t.mTradeModel && t.requests==requests+1);
 t.view.setModel(nullptr);
 std::cout<<"PASS production UI inventory lifecycle: detach before replacement, discard stale count selection, rebuild, preserve pending commit\n";
}
""")
    binary=temp/'ui'
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-O2','-I',str(ROOT),str(cpp),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
local=(ROOT/'apps/openmw/mwmp/LocalPlayer.cpp').read_text()
body=method(local,'void LocalPlayer::setInventory()')
assert body.index('prepareInventoryReplace(ptrPlayer)')<body.index('ptrStore.clear()')<body.index('finishInventoryReplace(rebuildBarter)')
objects=(ROOT/'apps/openmw/mwmp/ObjectList.cpp').read_text()
body=method(objects,'void ObjectList::editContainers(')
assert body.index('prepareInventoryReplace(ptrFound)')<body.index('containerStore.clear()')<body.index('finishInventoryReplace(rebuildBarter)')
enchant=(ROOT/'apps/openmw/mwgui/enchantingdialog.cpp').read_text()
assert 'if(mServerCommit){setItem(MWWorld::Ptr());setSoulGem(MWWorld::Ptr());}' in enchant
print('PASS native inventory replacement hook ordering and enchanting Ptr release',flush=True)
