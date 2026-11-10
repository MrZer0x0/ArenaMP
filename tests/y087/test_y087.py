from pathlib import Path
root = Path(__file__).resolve().parents[2]
actor = (root/'apps/openmw/mwrender/actoranimation.cpp').read_text()
creature = (root/'apps/openmw/mwclass/creature.cpp').read_text()
spell = (root/'apps/openmw/mwmechanics/spellcasting.cpp').read_text()
launcher = (root/'apps/launcher/maindialog.cpp').read_text()
assert 'carriedLeft->getTypeName() == typeid(ESM::Light).name()' in actor
assert 'getDrawState() != MWMechanics::DrawState_Weapon' in actor
assert 'damage *= 0.80f;' in creature
assert 'magnitude *= 0.80f;' in spell
assert '#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)' in launcher
assert 'writeEndpointFile(QStringLiteral("tes3mp-client.cfg"));' in launcher
assert 'address = mServerDialog->localConnectAddress();' in launcher
print('Y087 source checks: OK')
