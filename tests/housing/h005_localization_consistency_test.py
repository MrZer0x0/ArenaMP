"""Static localization / protocol checks. Run from project root."""
from pathlib import Path
import re
from xml.etree import ElementTree
root=Path(__file__).resolve().parents[2]
client=(root/'apps/openmw/mwmp/GUI/FurnitureStoreWindow.cpp').read_text()
server=(root/'server/scripts/arenaFurniture.lua').read_text()
layout=ElementTree.parse(root/'files/mygui/arenamp_furniture.layout')
widgets={w.get('name') for w in layout.findall('.//Widget')}
for widget in ['FurnitureTitle','CloseButton','ShopTab','InventoryTab','PlacedTab',
    'PreviousButton','NextButton','CategoryLabel','StyleButton','ItemList','SearchEdit']:
    assert widget in widgets,widget
for lang in ('ru','en'):
    entries={}
    for line in (root/f'files/vfs/l10n/arenamp/{lang}.ini').read_text().splitlines():
        if line.startswith('furniture.'):
            key,_,val=line.partition('=')
            assert key.strip() not in entries,('duplicate key',lang,key)
            entries[key.strip()]=val.strip()
    assert len(entries)>=43,(lang,len(entries))
    for ref in re.findall(r'furnitureText\("([^"+]+)"\)',client):
        if ref!='group.':assert 'furniture.'+ref in entries,(lang,ref)
    for key in ('furniture.title','furniture.shop','furniture.inventory','furniture.placed','furniture.missing.model','furniture.button'):
        assert entries[key],(lang,key)
    print('PASS',lang,'furniture dictionary keys:',len(entries))
assert 'deliver(pid,"FS_STOCK",refId,math.floor(stock[refId]),item and localName(pid,item) or "",' in server
assert 'if tonumber(count) and count>0 then refs[#refs+1]=refId end' in server
assert 'mStock[f[1]]' in client and 'f.size()>=4' in client
assert 'getArenaLanguage' in client # use the active UI language for bilingual item payloads
print('PASS H005 GUI resources, bilingual dictionaries, orphan furniture protocol invariants')
