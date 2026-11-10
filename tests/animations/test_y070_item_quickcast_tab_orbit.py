from pathlib import Path
root=Path(__file__).resolve().parents[2]
action=(root/'apps/openmw/mwinput/actionmanager.cpp').read_text()
ah=(root/'apps/openmw/mwinput/actionmanager.hpp').read_text()
char=(root/'apps/openmw/mwmechanics/character.cpp').read_text()
ch=(root/'apps/openmw/mwmechanics/character.hpp').read_text()
assert 'inventory.getSelectedEnchantItem() == inventory.end()' in action
assert 'short R quick-casts either a selected spell OR a selected' in action
assert 'mQuickCastIsItem' in ch and 'mQuickCastItemId' in ch
assert 'const ESM::Enchantment* enchantment = nullptr;' in char
assert 'ESM::Enchantment::WhenUsed' in char and 'ESM::Enchantment::CastOnce' in char
assert 'inv.setSelectedEnchantItem(item);' in char
assert 'world->castSpell(mPtr, false);' in char
assert 'localCast->type = mwmp::Cast::REGULAR;' in char  # regular spells unchanged
assert 'if (localRelease && !isMagicItem)' in char       # ITEM does not fake REGULAR press packet
assert 'mPovKeyPending' in ah and 'mPovHoldPreviewActive' in ah
assert 'restore classic hold-TAB third-person orbit' in action
assert 'mPovKeyTime >= .25f && !world->isFirstPerson()' in action
assert 'world->togglePreviewMode(true);' in action
assert 'world->togglePreviewMode(false);' in action
assert 'if (!mPovKeyConsumed)' in action and 'world->togglePOV();' in action
print('PASS: Y070 enchanted-item left quick-cast + hold-TAB third-person orbit')
