from pathlib import Path
root=Path(__file__).resolve().parents[2]
cpp=(root/'apps/openmw/mwmp/ArenaHousing.cpp').read_text()
hpp=(root/'apps/openmw/mwmp/ArenaHousing.hpp').read_text()
helper=(root/'server/scripts/houseHelper.lua').read_text()
market=(root/'server/scripts/houseItemMarket.lua').read_text()
assert 'bool bookTake = false;' in hpp
assert 'room.bookTake = fields.size() >= 6 ? fields[5] == "1" : room.take;' in cpp
assert 'object.getTypeName() == typeid(ESM::Book).name()' in cpp
assert 'found->second.bookTake && !itemLocked(object)' in cpp
assert 'local bookTake = take or level == "friend"' in helper
assert '(bookTake and "1" or "0")' in helper
assert "access=='owner' or access=='coowner' or access=='friend'" in market
assert 'Y069: opening a BOOK is read-only for ordinary visitors' in market
print('PASS: Y069 client/server book permission transport')
