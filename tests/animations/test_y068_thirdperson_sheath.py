#!/usr/bin/env python3
from pathlib import Path
root = Path(__file__).resolve().parents[2]
action = (root/'apps/openmw/mwinput/actionmanager.cpp').read_text()
char = (root/'apps/openmw/mwmechanics/character.cpp').read_text()
char_h = (root/'apps/openmw/mwmechanics/character.hpp').read_text()
actors = (root/'apps/openmw/mwmechanics/actors.cpp').read_text()
base = (root/'apps/openmw/mwbase/mechanicsmanager.hpp').read_text()
imp = (root/'apps/openmw/mwmechanics/mechanicsmanagerimp.cpp').read_text()
assert 'mGuardKeyTime >= .30f' in action
assert 'if (MWMechanics::ActiveGuard::raised(player))' in action
assert 'const bool weaponTransition = mechanics->isWeaponTransition(player.getPlayer());' in action
assert 'mechanics->isAttackingOrSpell(player.getPlayer()) && !weaponTransition' in action
assert 'Y068: allow F to reverse/finish a pure equip/unequip transition' in action
assert 'bool CharacterController::isWeaponTransition() const' in char
assert 'nativeStats.getDrawState() != DrawState_Nothing' in char
assert 'bool isWeaponTransition() const;' in char_h
assert 'bool Actors::isWeaponTransition' in actors
assert 'virtual bool isWeaponTransition' in base
assert 'bool MechanicsManager::isWeaponTransition' in imp
print('Y068 third-person sheath / ActiveGuard gesture: PASS')
