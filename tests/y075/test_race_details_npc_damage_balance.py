#!/usr/bin/env python3
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]

def text(rel):
    return (ROOT / rel).read_text(encoding='utf-8')

race_layout = text('files/mygui/openmw_chargen_race.layout')
details_layout = text('files/mygui/openmw_chargen_race_details.layout')
main_size = re.search(r'name="_Main"[^\n]*position="0 0 (\d+) (\d+)"', race_layout)
detail_size = re.search(r'name="_Main"[^\n]*position="0 0 (\d+) (\d+)"', details_layout)
if not main_size:
    main_size = re.search(r'position="0 0 (\d+) (\d+)"[^\n]*name="_Main"', race_layout)
if not detail_size:
    detail_size = re.search(r'position="0 0 (\d+) (\d+)"[^\n]*name="_Main"', details_layout)
assert main_size and detail_size
assert main_size.groups() == ('780', '520')
assert detail_size.groups() == main_size.groups(), (main_size.groups(), detail_size.groups())
assert 'position="18 50 740 220"' in details_layout
assert 'position="18 312 740 188"' in details_layout

settings = text('files/settings-default.cfg')
config = text('server/scripts/config.lua')
npc = text('apps/openmw/mwclass/npc.cpp')
spell = text('apps/openmw/mwmechanics/spellcasting.cpp')
apply_sh = text('android/buildscripts/patches/openmw/apply-arenamp-patches.sh')
android = text('android/buildscripts/patches/openmw/27-arenamp-y075-race-npc-balance.py')

for marker in (
    'npc damage low level multiplier = 0.60',
    'npc damage level 100 multiplier = 1.20',
    'npc damage over 100 multiplier = 1.30',
):
    assert marker in settings

for marker in (
    'config.arenaNpcDamageLowLevelMultiplier = 0.60',
    'config.arenaNpcDamageLevel100Multiplier = 1.20',
    'config.arenaNpcDamageOver100Multiplier = 1.30',
    'setGameSetting("npc damage low level multiplier", config.arenaNpcDamageLowLevelMultiplier)',
    'setGameSetting("npc damage level 100 multiplier", config.arenaNpcDamageLevel100Multiplier)',
    'setGameSetting("npc damage over 100 multiplier", config.arenaNpcDamageOver100Multiplier)',
):
    assert marker in config

for source, call in ((npc, 'arenaNpcDamageToPlayerMultiplier(attacker)'),
                     (spell, 'arenaNpcDamageToPlayerMultiplier(caster)')):
    assert 'if (level <= 10)' in source
    assert 'if (level > 100)' in source
    assert 'static_cast<float>(level - 10) / 90.f' in source
    assert 'npc damage low level multiplier' in source
    assert 'npc damage level 100 multiplier' in source
    assert 'npc damage over 100 multiplier' in source
    assert call in source

assert 'ptr == MWMechanics::getPlayer()' in npc
assert 'isArenaNpcLevelActor(attacker)' in npc
assert 'target == player' in spell
assert 'isArenaNpcLevelActor(caster)' in spell
assert '!reflected' in spell
assert '27-arenamp-y075-race-npc-balance.py' in apply_sh
assert ('arenamp-android-proto809-y075-race-npc-balance-v1' in apply_sh
        or 'arenamp-android-proto809-y076-inventory-grid-setting-v1' in apply_sh)
assert 'files/mygui/openmw_chargen_race_details.layout' in android
assert 'apps/openmw/mwclass/npc.cpp' in android
assert 'apps/openmw/mwmechanics/spellcasting.cpp' in android

# Requested curve: L1-L10 fixed at 0.60; L11-L100 linearly reaches 1.20;
# any level above 100 uses 1.30.
def mult(level):
    if level <= 10:
        return 0.60
    if level > 100:
        return 1.30
    return 0.60 + ((level - 10) / 90.0) * (1.20 - 0.60)

expected = {
    1: 0.60,
    10: 0.60,
    25: 0.70,
    50: 0.8666666666666667,
    75: 1.0333333333333332,
    100: 1.20,
    101: 1.30,
    250: 1.30,
}
for level, value in expected.items():
    assert abs(mult(level) - value) < 1e-9, (level, mult(level), value)

print('PASS: Y075 race details 780x520 + NPC/creature level damage curve 0.60..1.20 / >100=1.30')
