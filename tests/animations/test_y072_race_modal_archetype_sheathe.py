#!/usr/bin/env python3
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
race_h=(ROOT/'apps/openmw/mwgui/race.hpp').read_text()
race=(ROOT/'apps/openmw/mwgui/race.cpp').read_text()
klass_h=(ROOT/'apps/openmw/mwgui/class.hpp').read_text()
klass=(ROOT/'apps/openmw/mwgui/class.cpp').read_text()
char=(ROOT/'apps/openmw/mwmechanics/character.cpp').read_text()
layout=(ROOT/'files/mygui/openmw_chargen_race_details.layout').read_text()
race_layout=(ROOT/'files/mygui/openmw_chargen_race.layout').read_text()
create_layout=(ROOT/'files/mygui/openmw_chargen_create_class.layout').read_text()
ru=(ROOT/'files/vfs/l10n/arenamp/ru.ini').read_text()
en=(ROOT/'files/vfs/l10n/arenamp/en.ini').read_text()
apply=(ROOT/'android/buildscripts/patches/openmw/apply-arenamp-patches.sh').read_text()

assert 'class RaceDetailsDialog : public WindowModal' in race_h
assert 'WindowModal("openmw_chargen_race_details.layout")' in race
assert 'MW_DialogNoTransp' in layout
assert 'setVScrollPosition(0)' in race
assert 'setViewOffset(MyGUI::IntPoint(0, 0))' in race
assert 'RaceDetailsPanel' not in race_layout
assert 'NextArchetypeButton' in race_layout and 'NextArchetypeButton' in create_layout
assert race.count('nextArchetypePair') >= 2
assert klass.count('nextArchetypePair') >= 2
assert 'pairs.reserve(28)' in race and 'pairs.reserve(28)' in klass
assert 'archetype.next = Следующий' in ru
assert 'archetype.next = Next' in en
assert 'Y072: atomically clear stale third-person upper-body transition' in char
for marker in ['mAnimation->disable("shield")','mAnimation->disable(mNativeTail)','mAnimation->showWeapons(false)']:
    assert marker in char, marker
assert '25-arenamp-y072-chargen-ui-sheathe.py' in apply
assert 'y072-race-modal-archetype-sheathe' in apply
print('PASS: Y072 modal race details + 28 archetype Next + sheathe self-heal')
