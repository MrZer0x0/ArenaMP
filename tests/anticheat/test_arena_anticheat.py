#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
module = (ROOT / 'server/scripts/arenaAntiCheat.lua').read_text()
event = (ROOT / 'server/scripts/eventHandler.lua').read_text()
security = (ROOT / 'apps/openmw-mp/CoreArenaMPSecurity.cpp').read_text()
android = (ROOT / 'android/buildscripts/patches/openmw/17-arenamp-progression-anticheat.py').read_text()
apply = (ROOT / 'android/buildscripts/patches/openmw/apply-arenamp-patches.sh').read_text()

# Persistent server-authoritative state and checksums.
for marker in [
    'ArenaMPAntiCheatV2', 'LEGACY_STATE_KEY', 'baselineChecksum', 'trustedChecksum', 'lastRecoveryBackup',
    'GetSHA256Hash', 'skillInvestment', 'plausibleAgainstBaseline', 'plausibleTransition',
    'HARD_RECOVERY_STRIKES = 2', 'SKILL_POINTS_PER_LEVEL = 10', 'SKILL_BUDGET_TOLERANCE = 10',
    'pendingBaseline', 'finalizeBaseline', 'snapshotLooksComplete', 'detectCoopPreset', 'disableInCoop',
    'saveLevelCheckpoint', 'nearestLevelCheckpoint', 'nearestRollbackSnapshot', 'levelSnapshots',
    'healthCurrent', 'magickaBase', 'magickaCurrent', 'fatigueBase', 'fatigueCurrent',
]:
    assert marker in module, marker

# Y088: repeated anomalies recover to the nearest checksum-valid trusted /
# level-up checkpoint instead of destructively rebuilding the character to 1.
for marker in [
    'nearestRollbackSnapshot', 'previousTrusted', 'rollbackLevel',
    'CHECKPOINT RECOVERY', 'saveLevelCheckpoint(state, current)',
]:
    assert marker in module, marker
assert 'applySnapshot(pid, rebuilt, refund)' not in module
assert 'refundedSkillPoints = refund' not in module


# Y072: recovery remains server-authoritative but deliberately produces no client UI spam.
assert 'tes3mp.SendMessage(pid, text' not in module
assert 'tes3mp.MessageBox(pid, -1, text' not in module
assert 'Y072: deliberately silent client-side' in module

# Built-in, not optional.
assert 'local arenaAntiCheat = require("arenaAntiCheat")' in event
assert 'arenaAntiCheat.Enroll(pid, false)' in event
assert 'arenaAntiCheat.BeginCharGen(pid)' in event
assert 'arenaAntiCheat.Enroll(pid, true)' in event
assert 'arenaAntiCheat.RegisterHooks()' in event

# Native pre-Lua packet guard.
assert 'ArenaMP Y061 progression anti-cheat' in security
assert 'player.creatureStats.mLevel <= 105' in security
assert 'saneStatState(player.creatureStats.mAttributes[i], 110.0, false)' in security
assert 'saneStatState(player.npcStats.mSkills[i], 110.0, false)' in security
assert 'player.creatureStats.mLevel < state.level' in security
assert 'state.skillPoints) > 100LL' in security

# Android dedicated-server host receives the same layer.
assert '17-arenamp-progression-anticheat.py' in apply
assert 'PATCHSET_ID="arenamp-android-proto809-' in apply
assert '23-arenamp-y069-house-book-permissions.py' in apply
assert 'arenaAntiCheat.lua' in android
assert 'CoreArenaMPSecurity.cpp' in android

print('Y067 ArenaMP anti-cheat static validation: PASS')
