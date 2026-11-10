#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def npc_level(level):
    level = max(1, min(100, level))
    return 1.0 + (level - 1) / 99.0


def player_stamina(ratio):
    ratio = max(0.0, min(1.0, ratio))
    return 0.5 + 0.5 * ratio


assert abs(npc_level(1) - 1.0) < 1e-9
assert abs(npc_level(100) - 2.0) < 1e-9
assert 1.49 < npc_level(50) < 1.50
assert abs(player_stamina(0.0) - 0.5) < 1e-9
assert abs(player_stamina(0.5) - 0.75) < 1e-9
assert abs(player_stamina(1.0) - 1.0) < 1e-9
assert abs(20.0 * npc_level(100) - 40.0) < 1e-9
assert abs(40.0 / npc_level(100) - 20.0) < 1e-9

combat = (ROOT / 'apps/openmw/mwmechanics/combat.cpp').read_text()
npc = (ROOT / 'apps/openmw/mwclass/npc.cpp').read_text()
spell = (ROOT / 'apps/openmw/mwmechanics/spellcasting.cpp').read_text()
reflect = (ROOT / 'apps/openmw/mwmechanics/linkedeffects.cpp').read_text()
android = (ROOT / 'android/buildscripts/patches/openmw/16-arenamp-level-stamina-balance.py').read_text()
apply = (ROOT / 'android/buildscripts/patches/openmw/apply-arenamp-patches.sh').read_text()

assert 'kArenaAttackStatCoefficient = 0.50f' in combat
assert 'arenaPlayerStaminaDamageMultiplier(attacker)' in combat
assert 'arenaNpcLevelOffenseMultiplier(attacker)' in combat
assert 'arenaPlayerOffenseMultiplier' not in combat
assert 'Y061: NPC L1 keeps x1 incoming damage' in npc
assert 'isArenaNpcLevelActor(ptr)' in npc
assert 'Y061: player spell damage scales with stamina only' in spell
assert 'arenaPlayerStaminaSpellMultiplier(caster)' in spell
assert 'arenaNpcLevelMultiplier(caster)' in spell
assert "Y061: NPC level amplifies an existing Resist Paralysis source" in spell
assert 'Y061: NPC level amplifies an existing Reflect source' in reflect
assert 'target != getPlayer() && !mwmp::PlayerList::isDedicatedPlayer(target)' in reflect
assert '16-arenamp-level-stamina-balance.py' in apply
assert 'Y061 migration' in android
assert 'kArenaAttackStatCoefficient = 0.50f' in android

print('Y061 NPC-level/player-stamina balance: PASS')
