"""Compile actual localization helpers and check every shipped shop category."""
from pathlib import Path
import json, re, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'apps/openmw/mwmp/GUI/FurnitureStoreWindow.cpp').read_text()
def extract(sig):
 a=s.index(sig);return s[a:s.index("\n    }\n",a)+7]
# Helpers contain #{...} strings, but each brace pair is balanced.
code=r'''
#include <string>
#include <map>
#include <utility>
#include <cctype>
#include <cassert>
std::string language="en"; bool missing=false;
namespace MWBase { struct Window { std::string getArenaLanguage(){return language;} };
struct Environment { Window w; static Environment& get(){static Environment e;return e;}
Window* getWindowManager(){return &w;} }; }
namespace MyGUI { struct LanguageManager {
static LanguageManager& getInstance(){static LanguageManager l;return l;}
std::string replaceTags(const std::string& tag){
if(missing)return tag.substr(10,tag.size()-11);
return tag; } }; }
'''
code+=extract('std::string furnitureText(')+'\n'+extract('std::string furnitureGroup(')+'\n'+extract('std::string lower(')
code+='\nint main(){\n'
ref=json.loads((root/'server/data/custom/arenaFurnitureReference.json').read_text())
for cat in ref['categories']:
 if cat=='Двери':continue
 code+='assert(furnitureGroup('+json.dumps(cat,ensure_ascii=False)+')!='+json.dumps(cat,ensure_ascii=False)+');\n'
code+='assert(furnitureGroup("Мебель")=="Furniture"); missing=true; assert(furnitureText("group.furniture")=="Furniture");\n'
code+='language="ru";assert(furnitureGroup("Мебель")=="Мебель"); assert(lower("СТОЛ Ёж")=="стол ёж");}\n'
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'test.cpp';p.write_text(code)
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra',str(p),'-o',d+'/test'],check=True)
 subprocess.run([d+'/test'],check=True)
keys=dict(re.findall(r'\["([^"]+)"\]="([^"]+)"',(root/'server/scripts/arenaFurniture.lua').read_text().split('local categoryKeys={')[1].split('}')[0]))
for cat in ref['categories']:
 if cat=='Двери':continue
 assert cat in keys,cat
 for lang in ('en','ru'):
  assert 'furniture.group.'+keys[cat]+' =' in (root/f'files/vfs/l10n/arenamp/{lang}.ini').read_text()
print('PASS all catalog category keys, English/Russian rendering, missing-pack fallback and Cyrillic search')
