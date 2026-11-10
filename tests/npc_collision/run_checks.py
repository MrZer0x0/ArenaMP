from pathlib import Path
import subprocess
import tempfile

R = Path(__file__).resolve().parents[2]
actor = (R / 'apps/openmw/mwphysics/actor.cpp').read_text(encoding='utf-8')
callback = (R / 'apps/openmw/mwphysics/actorconvexcallback.cpp').read_text(encoding='utf-8')
launcher = (R / 'apps/launcher/advancedpage.cpp').read_text(encoding='utf-8')
ui = (R / 'files/ui/advancedpage.ui').read_text(encoding='utf-8')
settings = (R / 'files/settings-default.cfg').read_text(encoding='utf-8')

with tempfile.TemporaryDirectory() as d:
    exe = Path(d) / 'npc-collision-footprint'
    subprocess.run([
        'g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
        '-I', str(R), str(R / 'tests/npc_collision/footprint.cpp'), '-o', str(exe)
    ], check=True)
    subprocess.run([str(exe)], check=True)

assert 'mUseReducedNpcCollision = ptr.getClass().isNpc()' in actor
assert 'ptr != localPlayer' in actor
assert '!mwmp::PlayerList::isDedicatedPlayer(ptr)' in actor
assert 'applyHorizontalScale(\n        mScale, mUseReducedNpcCollision)' in actor
assert 'applyHorizontalScale(\n        osg::componentMultiply(mHalfExtents, mScale), mUseReducedNpcCollision)' in actor
assert 'applyHorizontalScale(mHalfExtents, mUseReducedNpcCollision)' in actor
assert 'compactActorSweep' not in callback
assert 'getActorPassScale' not in callback
assert 'npc player passage scale' not in launcher
assert 'actorCollisionProfileComboBox' not in launcher
assert 'actorCollisionProfileComboBox' not in ui
assert 'npc player passage scale = 0.95' in settings  # legacy compatibility only
assert 'current physics code ignores these values' in settings
print('PASS: permanent NPC footprint is 95% width/depth, full height; local and remote players are unchanged')
