#!/usr/bin/env python3
"""Subset regression checks; not a substitute for the full client/server build."""
from pathlib import Path
import ctypes, ctypes.util, subprocess, sys, tempfile, xml.etree.ElementTree as ET
root=Path(__file__).resolve().parents[2]
lua=ctypes.CDLL(ctypes.util.find_library('lua5.4'))
lua.luaL_newstate.restype=ctypes.c_void_p
lua.luaL_openlibs.argtypes=[ctypes.c_void_p]
lua.luaL_loadfilex.argtypes=[ctypes.c_void_p,ctypes.c_char_p,ctypes.c_char_p]
lua.luaL_loadstring.argtypes=[ctypes.c_void_p,ctypes.c_char_p]
lua.lua_pcallk.argtypes=[ctypes.c_void_p,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_longlong,ctypes.c_void_p]
lua.lua_tolstring.argtypes=[ctypes.c_void_p,ctypes.c_int,ctypes.c_void_p];lua.lua_tolstring.restype=ctypes.c_char_p
lua.lua_settop.argtypes=[ctypes.c_void_p,ctypes.c_int];lua.lua_close.argtypes=[ctypes.c_void_p]
state=lua.luaL_newstate();lua.luaL_openlibs(state)
def ok(status):
 if status: raise RuntimeError(lua.lua_tolstring(state,-1,None).decode())
files=list((root/'server/scripts').rglob('*.lua'))
for f in files:
 ok(lua.luaL_loadfilex(state,str(f).encode(),None));lua.lua_settop(state,0)
print(f'Lua syntax: {len(files)} files passed',flush=True)
import json
ok(lua.luaL_loadstring(state,('ROOT='+json.dumps(str(root))).encode()));ok(lua.lua_pcallk(state,0,0,0,0,None))
ok(lua.luaL_loadfilex(state,str(Path(__file__).with_name('server_regression.lua')).encode(),None));ok(lua.lua_pcallk(state,0,0,0,0,None))
lua.lua_close(state)
for f in (root/'files/mygui').iterdir():
 if f.suffix in ('.xml','.layout'): ET.parse(f)
layers=[x.attrib['name'] for x in ET.parse(root/'files/mygui/openmw_layers.xml').getroot()]
assert layers.index('MessageBox')<layers.index('Dialogue')<layers.index('LoadingScreen')
print('MyGUI XML and layer ordering passed',flush=True)
patch=root/'android/buildscripts/patches/openmw/31-arenamp-y092-ui-ai-progression-enchant.py'
with tempfile.TemporaryDirectory() as temp:
 subprocess.run([sys.executable,str(patch),temp],check=True)
 result=subprocess.run([sys.executable,str(patch),temp],check=True,text=True,capture_output=True)
 assert 'changes=0' in result.stdout
 subprocess.run([sys.executable,str(patch),temp,'--verify-only'],check=True)
 altered=Path(temp)/'apps/openmw/mwmechanics/character.cpp';altered.write_text('obsolete staging')
 assert subprocess.run([sys.executable,str(patch),temp,'--verify-only'],capture_output=True).returncode!=0
 subprocess.run([sys.executable,str(patch),temp],check=True)
print('Android stage31: parity, idempotence, stale-staging detection and repair passed',flush=True)
subprocess.run(['bash','-n',str(patch.with_name('apply-arenamp-patches.sh'))],check=True)
for f in patch.parent.glob('*.py'): compile(f.read_text(),str(f),'exec')
print('Android Python and shell syntax passed',flush=True)
# Static contract for the administrator command and anti-cheat source markers.
command=(root/'server/scripts/commandHandler.lua').read_text(encoding='utf-8')
anti=(root/'server/scripts/arenaAntiCheat.lua').read_text(encoding='utf-8')
assert 'cmd[1] == "addpoint"' in command and 'GrantSkillPoints' in command
assert 'Y092-SP: raw skillPoints' in anti and 'registerValidator("OnPlayerLevel"' in anti
print('Y092-SP command and skillPoints guard markers passed',flush=True)

# Y092 hotfix contracts: move-aside hidden/disabled, Unicode-safe profile storage,
# and CharGen stops after an account persistence failure.
dialogue=(root/'apps/openmw/mwgui/dialogue.cpp').read_text(encoding='utf-8')
dialogue_h=(root/'apps/openmw/mwgui/dialogue.hpp').read_text(encoding='utf-8')
event=(root/'server/scripts/eventHandler.lua').read_text(encoding='utf-8')
player_json=(root/'server/scripts/player/json.lua').read_text(encoding='utf-8')
player_base=(root/'server/scripts/player/base.lua').read_text(encoding='utf-8')
assert 'mMoveAsideLabel' not in dialogue and 'mMoveAsideLabel' not in dialogue_h
assert '@ArenaMoveAside:' not in dialogue and 'arenaMoveAside' not in event
assert '_utf8_' in player_json and 'GetSHA256Hash(accountName)' in player_json
assert 'return true' in player_json and 'return false' in player_json
assert 'if self:CreateAccount() ~= true then' in player_base
assert 'endCharGenOk == false' in event
print('Y092 hotfix move-aside disable and Unicode CharGen persistence markers passed',flush=True)
# Theft XP hotfix contract: one persistent source lock remains, but the reward
# itself is no longer lost to a 30% RNG after the lock has been consumed.
xp_cpp=(root/'apps/openmw/mwmechanics/xpleveling.cpp').read_text(encoding='utf-8')
theft=xp_cpp.split('void awardSuccessfulTheft',1)[1].split('void awardBookRead',1)[0]
assert 'Y092: theft source reward is deterministic once' in theft
assert 'stats.hasXpRewardKey(sourceKey.str())' in theft
assert 'stats.addXpRewardKey(sourceKey.str())' in theft
assert 'randomRewardRoll("theft bonus xp chance"' not in theft
assert 'nonNegativeSetting("theft bonus xp")' in theft
server_cfg=(root/'server/scripts/config.lua').read_text(encoding='utf-8')
default_cfg=(root/'files/settings-default.cfg').read_text(encoding='utf-8')
assert '["theft bonus xp chance"] = 1.00' in server_cfg
assert 'theft bonus xp chance = 1.00' in default_cfg
print('Y092 theft XP one-source/one-guaranteed-reward contract passed',flush=True)


# Consuming Animated regression: a naturally finished drinking/eating group must
# not remain in Animation::mStates and continue owning torso/arm bones.
consume=(root/'apps/openmw/mwmechanics/animationenhancements.cpp').read_text(encoding='utf-8')
assert 'Y092: consuming animation hard cleanup' in consume
assert 'animation->getInfo(state.mGroup, &complete, nullptr)' in consume
assert 'animation->disable(state.mGroup)' in consume
assert 'animation->play(spec.mGroup, priority, state.mBlendMask, true, speed' in consume
assert 'animation && animation->isPlaying(state.mGroup)' not in consume.split('void finishConsumingAnimation',1)[1].split('bool startConsumingAnimation',1)[0]
print('Y092 consuming-animation stale upper-body state cleanup contract passed',flush=True)

# Y092-EQ contract: NPC apparel survives ActorCellChange authority hand-off,
# while weapon/shield/ammunition slots remain dynamic.
cell_base=(root/'server/scripts/cell/base.lua').read_text(encoding='utf-8')
server_cfg=(root/'server/scripts/config.lua').read_text(encoding='utf-8')
assert 'local actorApparelGuard = require("actorApparelGuard")' in cell_base
assert 'APPAREL_SLOT_MAX = enumerations.equipment.BELT' in cell_base
assert 'actorEquipmentTransferGuards' in cell_base and 'self:LoadActorEquipment(visitorPid' in cell_base
assert 'actorEquipmentTransferGuardSeconds = 8' in server_cfg
print('Y092 NPC apparel cell-handoff preservation contract passed',flush=True)


# Y093 gameplay parity: first-person sneak temporarily uses the full-body
# renderer without mutating the user preference; skill raises stay in the
# right-side XP feed; Quick Loot uses the same server-authoritative container path.
camera=(root/'apps/openmw/mwrender/camera.cpp').read_text(encoding='utf-8')
quick=(root/'apps/openmw/mwgui/quickloot.cpp').read_text(encoding='utf-8')
quick_h=(root/'apps/openmw/mwgui/quickloot.hpp').read_text(encoding='utf-8')
npcstats=(root/'apps/openmw/mwmechanics/npcstats.cpp').read_text(encoding='utf-8')
npcanim=(root/'apps/openmw/mwrender/npcanimation.cpp').read_text(encoding='utf-8')
character=(root/'apps/openmw/mwmechanics/character.cpp').read_text(encoding='utf-8')
camera_hpp=(root/'apps/openmw/mwrender/camera.hpp').read_text(encoding='utf-8')
assert 'Y093: first-person body while sneaking' in camera
assert 'mImmersiveFirstPersonEnabled || mSneakImmersiveFirstPersonActive' in camera
assert 'osg::Vec3f(0.f, 0.f, -offset)' in camera
assert 'sneakHandsBack' not in camera
assert 'mSneakImmersiveFirstPersonActive' in camera_hpp
assert 'broken first-person dagger sneak clips' not in npcanim
assert 'first-person short-blade sneak fallback' not in character
assert 'Y093: XP mode skill raise notifications belong to the right-side ArenaMP' in npcstats
xp_raise=npcstats.split('Y093: XP mode skill raise notifications',1)[1].split('getSkill(skillIndex).setBase',1)[0]
assert 'hudExperienceNotification(0.f, message)' in xp_raise
assert 'if (xpLeveling)' in xp_raise
assert 'Y093: Quick Loot must obey the exact same access rules' in quick
assert 'ArenaHousing::get().canTakeHere()' in quick
assert 'BaseObjectList::DRAG' in quick and 'BaseObjectList::TAKE_ALL' in quick
assert 'objectList->sendContainer()' in quick
assert 'mModel->moveItem(item, count, playerModel)' not in quick
assert 'bool QuickLoot::ensureTrapTriggered()' in quick
assert 'return false;' in quick.split('bool QuickLoot::ensureTrapTriggered()',1)[1].split('bool QuickLoot::handleKeyPress',1)[0]
assert 'bool ensureTrapTriggered();' in quick_h
print('Y093 sneak full-body override, right-side skill notification and QuickLoot authority contracts passed',flush=True)
