#!/usr/bin/env python3
"""Compile real security-controller methods against a small engine test double."""
from pathlib import Path
import subprocess, tempfile
ROOT=Path(__file__).resolve().parents[2]
def block(text, marker):
    a=text.index(marker);b=text.index('{',a);depth=1;i=b+1
    while depth:
        if text[i]=='{': depth+=1
        elif text[i]=='}': depth-=1
        i+=1
    return text[a:i]
source=(ROOT/'apps/openmw/mwmechanics/character.cpp').read_text()
header=(ROOT/'apps/openmw/mwmechanics/character.hpp').read_text()
a=header.index('    enum class SecurityPhase');b=header.index('\n    struct AnimationQueueEntry',a)
members=header[a:b].replace('    void updateSecurityAction(float duration);','    void updateSecurityAction(float duration);')
methods='\n\n'.join(block(source,'bool CharacterController::startSecurityAction(') if name=='startSecurityAction' else block(source,'void CharacterController::'+name+'(') for name in ['startSecurityAction','updateSecurityAction','finishSecurityAction'])
tool=block(source,'            else if(mWeaponType == ESM::Weapon::PickProbe)');tool=tool[tool.index('{')+1:-1]
stub=(ROOT/'tests/avatar_security/security_stub.cpp').read_text().replace('// INSERT_MEMBERS',members).replace('// INSERT_METHODS',methods).replace('// INSERT_TOOL_BRANCH',tool)
with tempfile.TemporaryDirectory(prefix='arenamp-security-') as d:
    path=Path(d)/'test.cpp';path.write_text(stub)
    binary=Path(d)/'test'
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-pedantic','-I',str(ROOT),str(path),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
for name in ['armor','clothing']:
    equip=block((ROOT/f'apps/openmw/mwclass/{name}.cpp').read_text(),f'    std::pair<int, std::string> {name.title()}::canBeEquipped(')
    assert 'sNotifyMessage13' not in equip
    assert 'ESM::PRT_LFoot' in equip and 'ESM::PRT_RFoot' in equip
# Check local/remote player gating and visual-height separation at the integration site.
body=(ROOT/'apps/openmw/mwphysics/actor.cpp').read_text()
assert 'mPlayerBody = MechanicsHelper::isPlayerPtr(ptr);' in body
assert 'ActorBodyRules::reducePlayerHeight(mPlayerBody,' in body
assert 'extents.z() = mRenderingHalfHeight;' in body
security=(ROOT/'apps/openmw/mwmechanics/security.cpp').read_text()
assert 'mechanics->startSecurityAction(actor, target, tool, std::string())' in security
assert 'if (!trapped && locked && spell' in security
assert 'stats.getSpells().hasSpell(spell)' in security
assert 'finishSecurityAction();' in block(source,'CharacterController::~CharacterController(')
assert 'finishSecurityAction();' in block(source,'void CharacterController::updatePtr(')
print('PASS security integration guards and beast helmet/footwear rules')
