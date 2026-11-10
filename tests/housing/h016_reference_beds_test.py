"""Regression checks for curated bed entries in the ArenaMP furniture catalog."""
from pathlib import Path
import json

ROOT = Path(__file__).resolve().parents[2]
reference = json.loads((ROOT / "server/data/custom/arenaFurnitureReference.json").read_text(encoding="utf-8"))
lua = (ROOT / "server/scripts/arenaFurniture.lua").read_text(encoding="utf-8")
auto = (ROOT / "server/scripts/arenaFurnitureAutoCatalog.lua").read_text(encoding="utf-8")
client = (ROOT / "apps/openmw/mwmp/GUI/FurnitureStoreWindow.cpp").read_text(encoding="utf-8")

beds = [item for item in reference["items"] if item.get("category") == "Кровати"]
assert "Кровати" in reference.get("categories", [])
assert len(beds) == 48, f"expected 48 curated bed entries, got {len(beds)}"
assert 'original.category=="Кровати" and record==nil' in lua
assert "referenceBedCandidate(record)" in lua
assert "function M.referenceBedCandidate(record)" in auto
assert '(record.kind~="ACTI" and record.kind~="STAT" and record.kind~="MISC")' in auto
assert 'record.script and record.script' not in auto.split('function M.referenceBedCandidate(record)', 1)[1].split('end\nlocal function sourceDirs', 1)[0]
assert '"curated scripted ACTI bed must be accepted' in (ROOT / 'tests/housing/h003_auto_catalog_test.lua').read_text(encoding='utf-8')
assert 'if (const auto* r=store.get<ESM::Static>().search(refId))' in client
assert 'if (const auto* r=store.get<ESM::Miscellaneous>().search(refId))' in client
assert 'hasLocalFurnitureRecord(item.id)' in client
assert 'furnitureText("unavailable")' in client
print("PASS H016: all curated scripted/unscripted beds retained with safe local-model checks; local model check remains enforced")

android_path = ROOT / "android/buildscripts/patches/openmw/31-arenamp-y092-ui-ai-progression-enchant.py"
if android_path.is_file():
    android = android_path.read_text(encoding="utf-8")
    assert "NpcCollisionFootprint::applyHorizontalScale" in android
    assert "FILES += ['apps/openmw/mwphysics/npccollisionfootprint.hpp']" in android
    assert "'apps/openmw/mwphysics/npcpassagerules.hpp'" in android.split('for obsolete in ')[1]
    print("PASS Android Y092 parity: obsolete passage dependency removed, real collider mirrored")
