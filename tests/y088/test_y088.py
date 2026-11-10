from pathlib import Path
root=Path('.').resolve()
xp=(root/'apps/openmw/mwmechanics/xpleveling.cpp').read_text()
actor=(root/'apps/openmw/mwrender/actoranimation.cpp').read_text()
chat=(root/'apps/openmw/mwmp/GUI/GUIChat.cpp').read_text()
base=(root/'server/scripts/player/base.lua').read_text()
house=(root/'server/scripts/houseHelper.lua').read_text()
group=(root/'server/scripts/groupHelper.lua').read_text()
event=(root/'server/scripts/eventHandler.lua').read_text()
anti=(root/'server/scripts/arenaAntiCheat.lua').read_text()
sec=(root/'apps/openmw-mp/CoreArenaMPSecurity.cpp').read_text()
apply=(root/'android/buildscripts/patches/openmw/apply-arenamp-patches.sh').read_text()
patch17=(root/'android/buildscripts/patches/openmw/17-arenamp-progression-anticheat.py').read_text()
assert 'isClass(victim, "Guard")' in xp
assert 'ArenaMP Y088: guards are an anti-farm' in xp
assert 'do not make an equipped shield disappear' in actor
assert 'if(stats.getDrawState() != MWMechanics::DrawState_Weapon)\n                        return true;' in actor
assert 'removeXpRewardKeysWithPrefix("amp-xp|")' in chat
assert 'PlayerEquipment is a delta packet' in base
assert 'packetType ~= "PlayerEquipment"' in base
assert 'IsRegressiveLevelPacket' in base and 'IsRegressiveLevelPacket(playerPacket)' in event
assert 'EQUIPMENT_GUARD_SECONDS = 10' in house
assert 'equipmentTransitionGuards[pid] = nil\n    end\nend' not in house[house.index('function HouseHelper_EquipmentRefreshTimer'):house.index('function houseHelper.EvictOriginalVisitor')]
assert 'local dispatched = false' in group and 'return dispatched' in group
assert 'local xpDispatched = false' in event and 'if not xpDispatched then' in event
assert 'player.creatureStats.mLevel < state.level' in sec
assert 'player.creatureStats.mLevel < state.level' in patch17
for m in ['healthCurrent','magickaBase','magickaCurrent','fatigueBase','fatigueCurrent','saveLevelCheckpoint','nearestLevelCheckpoint','nearestRollbackSnapshot','trueLevelUp','CHECKPOINT RECOVERY']:
    assert m in anti, m
assert 'level = 1,\n        levelProgress = 0' not in anti
assert 'arenamp-android-proto809-y088-xp-checkpoint-equipment-v1' in apply
print('Y088 source checks: OK')
