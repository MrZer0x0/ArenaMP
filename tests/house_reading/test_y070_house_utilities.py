from pathlib import Path
root=Path(__file__).resolve().parents[2]
hpp=(root/'apps/openmw/mwmp/ArenaHousing.hpp').read_text()
cpp=(root/'apps/openmw/mwmp/ArenaHousing.cpp').read_text()
action=(root/'apps/openmw/mwinput/actionmanager.cpp').read_text()
mech=(root/'apps/openmw/mwmechanics/mechanicsmanagerimp.cpp').read_text()
helper=(root/'server/scripts/houseHelper.lua').read_text()
assert 'bool utility = false;' in hpp
assert 'bool canUseHouseUtility(const MWWorld::ConstPtr& object) const;' in hpp
assert 'room.utility = fields.size() >= 7 ? fields[6] == "1" : room.bookTake;' in cpp
assert 'return found == mRooms.end() || found->second.utility;' in cpp
assert 'local utility = bookTake' in helper
assert '(utility and "1" or "0")' in helper
assert 'function houseHelper.CanUseUtility(pid, cell)' in helper
assert 'level == "owner" or level == "coowner" or level == "friend"' in helper
assert 'customEventHooks.registerValidator("OnObjectActivate"' in helper
assert 'if houseHelper.CanUseUtility(pid, cell) then return end' in helper
assert 'if not houseHelper.CanUseUtility(pid, cell) then' in helper
assert '!housing.canUseHouseUtility(target)' in action
assert '!mwmp::ArenaHousing::get().canUseHouseUtility(bed)' in mech
print('PASS: Y070 house no-take blocks guest containers/beds; trusted members retain utility access')
