from pathlib import Path
import re
ROOT=Path(__file__).resolve().parents[2]
def text(p): return (ROOT/p).read_text(encoding='utf-8')
race=text('apps/openmw/mwgui/race.cpp')
race_h=text('apps/openmw/mwgui/race.hpp')
cc=text('apps/openmw/mwgui/charactercreation.cpp')
layout=text('files/mygui/openmw_chargen_race.layout')
hud=text('apps/openmw/mwgui/hud.cpp')
character=text('apps/openmw/mwmechanics/character.cpp')
anti=text('server/scripts/arenaAntiCheat.lua')
network_h=text('apps/openmw/mwmp/Networking.hpp')
network_cpp=text('apps/openmw/mwmp/Networking.cpp')
main=text('apps/openmw/mwmp/Main.cpp')
ru=text('files/vfs/l10n/arenamp/ru.ini')
en=text('files/vfs/l10n/arenamp/en.ini')
apply=text('android/buildscripts/patches/openmw/apply-arenamp-patches.sh')
assert 'name="TutorialPage"' in layout and 'name="TutorialAccept"' in layout
assert 'eventTutorialAccepted' in race_h and 'setTutorialVisible' in race_h
assert 'mArenaTutorialAccepted' in cc and 'onArenaTutorialAccepted' in cc
assert 'name="RaceDetailsPanel"' in layout and 'name="RacePowerArea"' in layout
assert 'createEffectWidgets(mRacePowerItems' in race and 'mData.mBonus' in race
assert 'changing race must not discard a manual preview zoom/pan' in race
assert 'preserve manual CharGen zoom/pan while browsing appearance choices' in race
# Browsing appearance must no longer call the reset helper.
selection=race[race.index('void RaceDialog::onSelectPreviousRace'):race.index('void RaceDialog::updateClasses()')]
assert 'resetPreviewView(false)' not in selection
assert 'Y071: player level packets can be transient/stale' in hud
assert 'PlayerList::isDedicatedPlayer(enemy)' in hud
assert 'Y071: shield-sheathing mods normally rely' in character
assert 'finishing a third-person sheathe must leave no upper-body' in character
assert 'rapid draw->sheathe reversals can change DrawState to Nothing' in character
assert 'forcestateupdate = true;' in character
assert 'showCarriedLeft(updateCarriedLeftVisible(mWeaponType))' in character
assert 'mAnimation->disable(mNativeShieldLayer);' in character
assert 'mAnimation->disable(mNativeBounceLayer);' in character
assert 'mAnimation->disable(mNativeTail);' in character
assert 'mUpperBodyState = UpperCharState_Nothing;' in character
assert 'anti-cheat enforcement is intentionally silent for players' in anti
notify=anti[anti.index('local function notify'):anti.index('local function copyMapOfBases')]
assert 'MessageBox' not in notify and 'SendMessage' not in notify
assert 'mRestoringConnection' in network_h and 'mRestoreRemaining = 30.f' in network_cpp
assert 'beginLoginSync()' in network_cpp and 'ID_LOADED' in network_cpp
assert 'getNetworking()->update(dt)' in main
assert 'network.reconnect.title' in hud and 'getRestoreRemaining()' in hud
for key in ('chargen.tutorial.title','chargen.race_details','chargen.racial_abilities','network.reconnect.title'):
    assert key in ru and key in en
assert '28 сочетаний' in ru and '28 combinations' in en
assert '«' not in ru and '»' not in ru
assert 'arenamp-android-proto809-y071-chargen-reconnect-shield-v2' in apply
assert 'X057/Y071 Android feature patchset verified' in apply
assert '25-arenamp-y071-chargen-reconnect-shield.py' in apply
print('Y071 CharGen/reconnect/shield/UI static validation: PASS')
