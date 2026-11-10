#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

def text(rel):
    return (ROOT / rel).read_text(encoding='utf-8')

race = text('apps/openmw/mwgui/race.cpp')
for symbol in (
    'void RaceDialog::onFrame(float duration)',
    'void RaceDialog::onPreviewMousePressed',
    'void RaceDialog::onPreviewMouseDrag',
    'void RaceDialog::onPreviewMouseWheel',
):
    assert race.count(symbol) == 1, f'missing/duplicate RaceDialog definition: {symbol}'

for rel in ('apps/openmw/mwmp/LocalPlayer.cpp', 'apps/openmw/mwmp/DedicatedPlayer.cpp'):
    source = text(rel)
    assert 'stats.getAttackingOrSpell()' not in source, f'stale CreatureStats call: {rel}'
    assert 'getMechanicsManager()->isAttackingOrSpell(ptr)' in source, f'missing canonical attack/cast guard: {rel}'

stats_hpp = text('apps/openmw/mwmechanics/creaturestats.hpp')
assert 'getAttackingOrSpell() const' not in stats_hpp
assert 'setAttackingOrSpell(bool attackingOrSpell)' not in stats_hpp

launcher = text('apps/launcher/arenalinkclient.cpp')
assert '&QAbstractSocket::errorOccurred' in launcher
assert 'QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error)' not in launcher
assert 'static_cast<quint8>(AUTH_CODE)' in launcher

y070 = text('android/buildscripts/patches/openmw/24-arenamp-y070-house-cast-tab.py')
assert 'SUPERSEDED_MARKERS' in y070
apply_sh = text('android/buildscripts/patches/openmw/apply-arenamp-patches.sh')
assert 'ArenaMP Android Y070 semantic patcher v2 verified' in apply_sh

print('PASS: Y074 build/link + Y070 semantic patcher + launcher warning cleanup')
