#!/usr/bin/env python3
"""Check permanent NPC collision footprint and OpenSSL Release-only contract."""
from pathlib import Path
import re
import sys
R=Path(__file__).resolve().parents[2]
s=(R/'.github/workflows/release-builds.yml').read_text()
trip=(R/'CI/vcpkg-triplets/x64-windows-release.cmake').read_text()
assert 'set(VCPKG_BUILD_TYPE release)' in trip
assert 'openssl:x64-windows-release' in s
assert 'C:/vcpkg/installed/x64-windows-release' in s
assert not re.search(r'install --classic openssl:x64-windows(?:\s|$)',s)
actor = (R/'apps/openmw/mwphysics/actor.cpp').read_text()
footprint = (R/'apps/openmw/mwphysics/npccollisionfootprint.hpp').read_text()
assert (R/'apps/openmw/mwphysics/actorconvexcallback.cpp').is_file()
assert 'HorizontalScale = 0.95f' in footprint
assert 'applyHorizontalScale' in actor
assert 'compactActorSweep' not in (R/'apps/openmw/mwphysics/actorconvexcallback.cpp').read_text()
print('PASS: permanent 95% NPC width/depth collision; publisher OpenSSL uses Release-only vcpkg triplet')
