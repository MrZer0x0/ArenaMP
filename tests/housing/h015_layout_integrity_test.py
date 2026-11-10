from pathlib import Path
from xml.etree import ElementTree as ET
import json, re
root=Path(__file__).resolve().parents[2]
layout=ET.parse(root/'files/mygui/arenamp_furniture.layout').getroot()
widgets={x.attrib['name']:x.attrib['type'] for x in layout.iter('Widget') if 'name' in x.attrib}
cpp=(root/'apps/openmw/mwmp/GUI/FurnitureStoreWindow.cpp').read_text()
for name in re.findall(r'getWidget\(\w+,"([^"]+)"\)',cpp):
    assert name in widgets,(name,'missing MyGUI widget')
assert widgets['StyleButton']=='Button'
# The value editor is intentionally a separate popup constructed in C++ so it
# cannot cover the embedded catalogue list/preview.
assert 'AdminCategoryEdit' in cpp and 'createWidget<MyGUI::Button>' in cpp
assert widgets['AdminCaptureButton']=='Button' and widgets['AdminRefreshButton']=='Button'
assert 'SubcategoryList' not in widgets
assert 'FurniturePreview::' not in cpp or 'getInstance(' in cpp
assert 'recreateShaders(node, "objects", true)' in cpp
for lang in ('ru','en'):
    locale=(root/f'files/vfs/l10n/arenamp/{lang}.ini').read_text()
    for key in ('style.nordic','style.dunmer','style.imperial','group.furniture','group.trading','admin.edit','admin.capture','admin.refresh','admin.name.ru','admin.name.en','admin.save'):
        assert 'furniture.'+key+' =' in locale,(lang,key)
reference=json.loads((root/'server/data/custom/arenaFurnitureReference.json').read_text())
assert len(reference['items'])==len({r['refId'].lower() for r in reference['items']})
assert all(r['nameRu'] and r['nameEn'] and r['price']>0 for r in reference['items'])
assert not any(re.search('[А-Яа-яЁё]',r['nameEn']) for r in reference['items'])
housing=(root/'apps/openmw/mwmp/ArenaHousing.cpp').read_text()
server=(root/'server/scripts/houseHelper.lua').read_text()
assert 'UNLOCK_HOME' in housing and 'UNLOCK_HOME' in server
assert 'typeid(ESM::Container)' in housing and 'unlockPrivateContainers' in housing
print('PASS H015 UI/source checks: MyGUI fields, compact categories, RU/EN keys, unique bilingual reference, private-container unlock wiring')
