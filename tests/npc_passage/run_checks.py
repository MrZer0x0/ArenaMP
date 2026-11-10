from pathlib import Path
import subprocess,tempfile
R=Path(__file__).resolve().parents[2]
s=(R/'apps/openmw/mwphysics/actorconvexcallback.cpp').read_text()
a=s.index('        bool compactActorSweep(');b=s.index('\n    }\n\n    class ActorOverlapTester',a)
actual=s[a:b]
stub=(R/'tests/npc_passage/sweep.cpp').read_text().replace('// INSERT_REAL_SWEEP',actual)
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'sweep.cpp';p.write_text(stub);exe=Path(d)/'sweep'
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-I',str(R),str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
assert 'npc player passage scale = 0.2' in (R/'files/settings-default.cfg').read_text()
assert 'PlayerList::isDedicatedPlayer(a)' in s
assert 'mShape.reset(new btBoxShape' in (R/'apps/openmw/mwphysics/actor.cpp').read_text()
print('PASS real compact sweep, 20% passage, open corridor, vertical separation, local/remote players, PvP and creature exclusions')
