#!/usr/bin/env python3
"""Exercise the production stepper and player shape with Bullet (no full engine)."""
from pathlib import Path
import argparse, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--deps',type=Path);args=p.parse_args()
step=(ROOT/'apps/openmw/mwphysics/stepper.cpp').read_text()
step=step[step.index('    static bool canStepDown'):].rsplit('\n}',1)[0]
stub=(ROOT/'tests/player_clearance/stairs.cpp').read_text().replace('// INSERT_STEPPER',step)
include=['-I',str(ROOT)];libraries=['-lBulletCollision','-lLinearMath']
if args.deps:
 base=args.deps.resolve();include+=['-isystem',str(base/'usr/include'),'-isystem',str(base/'usr/include/bullet')]
 lib=base/'usr/lib/x86_64-linux-gnu';libraries=['-L',str(lib),'-Wl,-rpath,'+str(lib)]+libraries
else:include+=['-I','/usr/include/bullet']
with tempfile.TemporaryDirectory(prefix='arenamp-stairs-') as td:
 f=Path(td)/'stairs.cpp';f.write_text(stub);out=Path(td)/'stairs'
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-pedantic',*include,str(f),*libraries,'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)

callback=(ROOT/'apps/openmw/mwphysics/actorconvexcallback.cpp').read_text()
assert 'ActorBodyRules::blocksMotion(dotCollision, mMotion.length(), mMinCollisionDot)' in callback
body=(ROOT/'apps/openmw/mwphysics/actor.cpp').read_text()
assert 'ActorBodyRules::reducePlayerWidth(mPlayerBody, mHalfExtents.x())' in body
assert 'extents.x() = mRenderingHalfWidth;' in body
solver=(ROOT/'apps/openmw/mwphysics/movementsolver.cpp').read_text()
assert solver.count('physicActor->getMovementOffset()') == 2
assert 'actor.mPosition -= collisionOffset;' in solver
print('PASS width, shape and movement-center integration guards')
