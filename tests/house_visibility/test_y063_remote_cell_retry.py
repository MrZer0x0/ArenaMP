#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
dedicated = (ROOT / 'apps/openmw/mwmp/DedicatedPlayer.cpp').read_text()
loading = (ROOT / 'apps/openmw/mwgui/loadingscreen.cpp').read_text()
apply = (ROOT / 'android/buildscripts/patches/openmw/apply-arenamp-patches.sh').read_text()

assert 'ArenaMP Y063: a cell-local snapshot' in dedicated
assert 'Retrying DedicatedPlayer %s placement after target cell became active' in dedicated
assert 'ArenaMP Y063: BaseInfo/CellChange' in dedicated
assert 'RecordHelper::getPlaceholderInteriorCellName()' in dedicated
assert 'if (targetReady)\n        enableMarker();\n    else\n        removeMarker();' in dedicated
assert 'desktop in-game cell transitions use the same opaque' in loading
assert 'setupCopyFramebufferToTextureCallback();' not in loading.split('void LoadingScreen::loadingOn(bool visible)', 1)[1].split('void LoadingScreen::loadingOff()', 1)[0]
assert 'PATCHSET_ID="arenamp-android-proto809-' in apply
assert '19-arenamp-remote-player-cell-retry.py' in apply
print('Y063 remote-player cell retry + desktop black transition: PASS')
