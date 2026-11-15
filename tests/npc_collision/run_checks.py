from pathlib import Path
import subprocess
import tempfile

R = Path(__file__).resolve().parents[2]
actor = (R / 'apps/openmw/mwphysics/actor.cpp').read_text(encoding='utf-8')
callback = (R / 'apps/openmw/mwphysics/actorconvexcallback.cpp').read_text(encoding='utf-8')
launcher = (R / 'apps/launcher/advancedpage.cpp').read_text(encoding='utf-8')
ui = (R / 'files/ui/advancedpage.ui').read_text(encoding='utf-8')
settings = (R / 'files/settings-default.cfg').read_text(encoding='utf-8')
footprint = (R / 'apps/openmw/mwphysics/npccollisionfootprint.hpp').read_text(encoding='utf-8')

with tempfile.TemporaryDirectory() as d:
    exe = Path(d) / 'npc-collision-footprint'
    subprocess.run([
        'g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
        '-I', str(R), str(R / 'tests/npc_collision/footprint.cpp'), '-o', str(exe)
    ], check=True)
    subprocess.run([str(exe)], check=True)

assert 'NpcHorizontalScale = 0.95f' in footprint
assert 'PlayerHorizontalScale = 1.f' in footprint
assert 'const bool isPlayerActor = ptr == localPlayer || mwmp::PlayerList::isDedicatedPlayer(ptr);' in actor
assert 'const bool isOrdinaryNpc = ptr.getClass().isNpc() && !isPlayerActor;' in actor
assert 'mHorizontalCollisionScale = isOrdinaryNpc' in actor
assert 'NpcCollisionFootprint::NpcHorizontalScale' in actor
assert 'NpcCollisionFootprint::PlayerHorizontalScale' in actor
assert "applyHorizontalScale(\n        mScale, mHorizontalCollisionScale)" in actor
assert "applyHorizontalScale(\n        osg::componentMultiply(mHalfExtents, mScale), mHorizontalCollisionScale)" in actor
assert 'applyHorizontalScale(mHalfExtents, mHorizontalCollisionScale)' in actor
assert 'compactActorSweep' not in callback
assert 'getActorPassScale' not in callback
assert 'npc player passage scale' not in launcher
assert 'actorCollisionProfileComboBox' not in launcher
assert 'actorCollisionProfileComboBox' not in ui
assert 'npc player passage scale = 0.95' in settings  # legacy compatibility only
assert 'current physics code ignores these values' in settings
print('PASS: permanent actor footprints active: NPC 95% width/depth, players native OpenMW extents, full height; no launcher collision profile remains')
