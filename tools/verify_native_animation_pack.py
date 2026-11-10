#!/usr/bin/env python3
"""Validate shipped NIF 4.x text-key tracks against ArenaMP's actual attack selector."""
from pathlib import Path
import struct
import subprocess
import tempfile
import xml.etree.ElementTree as ET
import re

ROOT = Path(__file__).resolve().parents[1]
VFS = ROOT / 'files/vfs'

def textkeys(path):
    data = path.read_bytes()
    assert data.startswith(b'NetImmerse File Format, Version 4.'), path
    marker = b'NiTextKeyExtraData'
    pos = data.index(marker) + len(marker) + 8
    count, = struct.unpack_from('<I', data, pos)
    pos += 4
    assert 0 < count < 100000, (path, count)
    result = {}
    for _ in range(count):
        time, length = struct.unpack_from('<fI', data, pos)
        pos += 8
        text = data[pos:pos+length].decode('latin1').lower()
        pos += length
        assert pos <= len(data), path
        for line in text.splitlines():
            result[line.strip()] = time
    return result

def main():
    sources = sorted((VFS/'animations/arenamp/reanimation/xbase_anim.1st').glob('*.kf'))
    tracks = [textkeys(p) for p in sources]
    assert len(tracks) > 80
    # Use the C++ production selector, then verify each chosen attack's event pipeline
    # exists in the shipped assets, not just in a duplicated list of expected strings.
    cpp = r'''
#include "apps/openmw/mwmechanics/nativeanimation.hpp"
#include <iostream>
int main() {
 for (auto base : {"weapononehand", "weapontwohand", "weapontwowide", "handtohand", "throwweapon", "throwweaponstar"})
  for (auto id : {"iron_claymore", "steel_katana", "daedric_scythe", "throwing_star"})
   for (auto attack : {"chop", "slash", "thrust", "shoot"}) {
    bool thrown = std::string(base).find("throwweapon") == 0;
    if (thrown != (std::string(attack) == "shoot")) continue;
    for (unsigned int seq=0; seq<40; ++seq)
     std::cout << MWMechanics::NativeAnimation::attackGroup(base, attack, id, seq) << " " << attack << "\n";
   }
}
'''
    with tempfile.TemporaryDirectory() as td:
        src=Path(td)/'selector.cpp'; exe=Path(td)/'selector';src.write_text(cpp)
        subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-I',str(ROOT),str(src),'-o',str(exe)],check=True)
        choices=subprocess.check_output([str(exe)],text=True).splitlines()
    checks=0
    for choice in choices:
        group,attack=choice.split()
        endings=['start','min attack','max attack','min hit','release' if attack=='shoot' else 'hit']
        if attack=='shoot': endings += ['follow start','follow stop']
        else: endings += [size+' follow '+point for size in ['small','medium','large'] for point in ['start','stop']]
        keys=[group+': '+attack+' '+end for end in endings]
        assert any(all(k in track for k in keys) for track in tracks), (group,attack,'missing attack track')
        checks+=1
    # Third-person alt pack must expose the base + alternate groups that the
    # render-time adapter expects. Check both normal and beast skeleton families.
    for family in ['xbase_anim', 'xbase_animkna']:
        third = VFS/'animations/arenamp/thirdperson_alt'/family
        assert third.is_dir(), third
        required = {
            'xone_h_attack.kf': ['weapononehand', 'weapononehand1'],
            'xtwo_h_attack.kf': ['weapontwohand', 'weapontwohand1'],
            'xtwo_h_attack_wide.kf': ['weapontwowide', 'weapontwowide1'],
            'xthrow_weapon.kf': ['throwweapon', 'throwweapon1'],
            'xmagic.kf': ['spellcast'],
        }
        for name, groups in required.items():
            keys = textkeys(third/name)
            lowered = {k.split(':', 1)[0].strip() for k in keys}
            for group in groups:
                assert group in lowered, (family, name, group)

    native=(ROOT/'apps/openmw/mwmechanics/nativeanimation.hpp').read_text()
    for canonical, third in [('weapontwohandalt','weapontwohand1'),
                             ('weapontwowidealt','weapontwowide1'),
                             ('throwweaponalt','throwweapon1')]:
        assert canonical in native and third in native, (canonical, third)

    for p in (VFS/'animations/arenamp/ossc').rglob('*quickcast*.kf'):
        if p.name == 'xquickcastpose1h.kf': continue
        keys=textkeys(p)
        for group in ['quickcast','quickbuff','qcconj','qcill','qcalt','qcsnap','qctouch','qcdrain']:
            assert keys[group+': start'] <= keys[group+': release'] <= keys[group+': stop'], (p,group)
    for p in (VFS/'animations/arenamp/mao').glob('*.kf'):
        keys=textkeys(p)
        groups = ['walkforward','runforward'] if 'female' in p.name else ['idle','walkforward','runforward','jump','spellcast','weapononehand','bowandarrow']
        for group in groups:
            assert any(k.startswith(group+': ') for k in keys), (p,group)
    source=(ROOT/'apps/openmw/mwrender/npcanimation.cpp').read_text()
    # Y062: third person must not explicitly load the OSSC full-body quickcast KF.
    # It falls back to thirdperson_alt SpellCast, while first person keeps the
    # dedicated .1st OSSC clip.
    assert source.count('animations/arenamp/ossc/xbase_anim.1st/xquickcast.1st.kf') == 1
    for forbidden in [
        'addSingleAnimSource("animations/arenamp/ossc/xbase_anim/xquickcast.kf"',
        'addSingleAnimSource("animations/arenamp/ossc/xbase_anim_female/xquickcast_female.kf"',
        'addSingleAnimSource("animations/arenamp/ossc/xbase_animkna/xquickcastkna.kf"',
    ]:
        assert forbidden not in source, forbidden
    controller=(ROOT/'apps/openmw/mwmechanics/character.cpp').read_text()
    assert 'group = "spellcast";' in controller
    assert 'mAttackType + " release"' in controller
    assert 'mAttackType + " stop"' in controller
    cmake=(VFS/'CMakeLists.txt').read_text()
    assert 'list(FILTER BUILTIN_VFS_FILES EXCLUDE REGEX' in cmake
    assert r'xbase_anim/xquickcast\\.kf' in cmake

    for name in re.findall(r'"(animations/arenamp/[^"]+\.kf)"',source):
        assert (VFS/name).is_file(), name
        assert name == name.lower(), name
    for path in ['files/mygui/openmw_loading_screen.layout','android/app/src/main/res/values/strings.xml','android/app/src/main/res/values-ru/strings.xml']:
        ET.parse(ROOT/path)
    loading=(ROOT/'apps/openmw/mwgui/loadingscreen.cpp').read_text()
    # Exercise the actual preprocessor boundary. Android contains no GPU capture
    # implementation. Desktop may retain the helper implementation for upstream
    # compatibility, but Y063 gameplay transitions must not invoke it: both mobile
    # and desktop use the opaque black widget outside startup/title loading.
    body = '\n'.join(line for line in loading.splitlines() if not line.startswith('#include'))
    android = subprocess.check_output(['g++','-E','-P','-x','c++','-D__ANDROID__','-'], input=body, text=True)
    for token in ['copyTexImage2D','waitTillDone','setupCopyFramebufferToTextureCallback','mSceneImage']:
        assert token not in android, token
    assert 'changeWallpaper();' in android
    assert 'mBlackBackground->setVisible' in android
    desktop = subprocess.check_output(['g++','-E','-P','-x','c++','-DOSG_VERSION_GREATER_OR_EQUAL(a,b,c)=1','-'], input=body, text=True)
    assert 'copyTexImage2D' in desktop
    assert 'setupCopyFramebufferToTextureCallback();' not in desktop
    assert 'mSceneSnapshotReady = false;' in desktop
    assert 'mSceneImage->setVisible(false);' in desktop
    print(f'PASS: {checks} attack choices, {len(tracks)} ReAnimation tracks, third-person humanoid/KNA aliases, OSSC release timing, MAO groups, asset paths and UI XML')

if __name__=='__main__': main()
