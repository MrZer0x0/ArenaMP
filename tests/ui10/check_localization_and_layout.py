#!/usr/bin/env python3
"""Compile the production localization class with file-backed VFS/settings stubs.
Also validate edited layout geometry and the bindings between C++ and MyGUI.
This is not a substitute for compiling/rendering the full client.
"""
from pathlib import Path
import subprocess
import tempfile
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    headers = {
        'components/to_utf8/to_utf8.hpp': '#pragma once\nnamespace ToUTF8 { enum FromType { WINDOWS_1251, WINDOWS_1252 }; }',
        'components/debug/debuglog.hpp': '#pragma once\nnamespace Debug { enum Level {Warning,Error}; }\nstruct Log { Log(Debug::Level) {} template<class T> Log& operator<<(const T&) { return *this; } };',
        'components/settings/settings.hpp': '#pragma once\n#include <string>\nnamespace Settings { inline std::string language; struct Manager { static std::string getString(const char*,const char*) { return language; } }; }',
        'components/vfs/manager.hpp': '''#pragma once
#include <fstream>
#include <memory>
#include <string>
namespace VFS { struct Manager {
 std::string base;
 bool exists(const std::string& path) const { return bool(std::ifstream(base+path)); }
 std::unique_ptr<std::istream> get(const std::string& path) const { return std::make_unique<std::ifstream>(base+path); }
}; }
''',
    }
    for name, source in headers.items():
        p = tmp / name; p.parent.mkdir(parents=True, exist_ok=True); p.write_text(source)
    (tmp / 'test.cpp').write_text('''
#include "apps/openmw/mwgui/arenalocalization.hpp"
#include <components/settings/settings.hpp>
#include <components/vfs/manager.hpp>
#include <cassert>
#include <iostream>
int main(int argc, char** argv) {
 assert(argc == 2);
 VFS::Manager vfs{argv[1]};
 Settings::language = "ru";
 MWGui::ArenaLocalization englishGame(&vfs, ToUTF8::WINDOWS_1252);
 assert(englishGame.translate("dialogue.disposition_label") == u8"Отношение");
 assert(englishGame.translateGame("dialogue.disposition_label") == "Disposition");
 assert(englishGame.translateGame("archetype.warrior.name") == "Warrior");
 englishGame.setLanguage("ru");
 assert(englishGame.translateGame("archetype.mage.name") == "Mage");
 Settings::language = "en";
 MWGui::ArenaLocalization russianGame(&vfs, ToUTF8::WINDOWS_1251);
 assert(russianGame.translate("dialogue.disposition_label") == "Disposition");
 assert(russianGame.translateGame("dialogue.disposition_label") == u8"Отношение");
 assert(russianGame.translateGame("archetype.warrior.name") != "Warrior");
 russianGame.setLanguage("auto");
 assert(russianGame.getLanguage() == "ru");
 assert(englishGame.translateGame("missing.key") == "missing.key");
 std::cout << "Production localization / mixed account and game languages: PASS\\n";
}
''')
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-I'+str(tmp), '-I'+str(root),
                    str(root/'apps/openmw/mwgui/arenalocalization.cpp'), str(tmp/'test.cpp'),
                    '-o', str(tmp/'test')], check=True)
    subprocess.run([str(tmp/'test'), str(root/'files/vfs')+'/'], check=True)


def layout(name):
    return ET.parse(root/'files/mygui'/name).getroot()

def coord(widget):
    return tuple(map(int, widget.attrib['position'].split()))

stats = layout('openmw_stats_window.layout')
get = lambda name: stats.find('.//Widget[@name="'+name+'"]')
sections = [get(n) for n in ('VitalsBox', 'IdentityBox', 'AttributesBox', 'StatsSkillsBox')]
for a, b in zip(sections, sections[1:]):
    assert coord(a)[1] + coord(a)[3] <= coord(b)[1]
for name in ('HBar', 'MBar', 'FBar'):
    assert get(name).attrib['skin'].startswith('ArenaHUD_Bar_')
    assert coord(get(name))[3] == 21
    assert coord(get(name+'T'))[3] == 21

hud = layout('openmw_hud.layout')
for bar, text, height in [('Health','HealthText',18),('Magicka','MagickaText',17),('Stamina','StaminaText',17)]:
    a = hud.find('.//Widget[@name="'+bar+'"]')
    b = hud.find('.//Widget[@name="'+text+'"]')
    assert coord(a) == coord(b) and coord(a)[3] == height
    assert b.find('./Property[@key="FontHeight"]').attrib['value'] == '15'

load = layout('openmw_loading_screen.layout')
assert load.find('.//Widget[@name="LoadingPanel"]').attrib['skin'] == 'MW_Dialog'
assert load.find('.//Widget[@name="ProgressBar"]').attrib['type'] == 'ProgressBar'
assert load.find('.//Widget[@name="ProgressBar"]').attrib['skin'] == 'ArenaHUD_Bar_Magicka'
assert load.find('.//Widget[@name="LoadingTip"]/Property[@key="WordWrap"]').attrib['value'] == 'true'
for obsolete in ('LoadingText', 'CenterLoadingText', 'LoadingStatusStrip', 'LoadingTipStrip'):
    assert load.find('.//Widget[@name="'+obsolete+'"]') is None
# All skin references introduced by this patch are real, already shipped resources.
skins = {r.attrib['name'] for p in (root/'files/mygui').glob('*.xml')
         for r in ET.parse(p).getroot().iter('Resource') if 'name' in r.attrib}
for name in ('ArenaHUD_Bar_Health', 'ArenaHUD_Bar_Magicka', 'ArenaHUD_Bar_Stamina', 'MW_Dialog'):
    assert name in skins
print('MyGUI resource references, stat layout and HUD geometry: PASS')
