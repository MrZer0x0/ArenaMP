#!/usr/bin/env python3
"""ArenaMP X058 quest-item phasing regression checks.

No server runtime is required.  The behavioral checks model the canonical rules
used by questItemPhasing.lua and the source checks make sure the X058 classifier
and identity-ordering fixes cannot silently disappear from a changed-files patch.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def visible_count(total: int, phase_count: int, claimed: int) -> int:
    total = max(0, total)
    phase_count = max(0, min(total, phase_count))
    claimed = max(0, claimed)
    if phase_count <= 0 or claimed <= 0:
        return total
    return max(0, total - min(claimed, phase_count))


def claim(phase_count: int, already_claimed: int, requested: int) -> int:
    available = max(0, phase_count - already_claimed)
    return min(max(0, requested), available)


def test_personal_world_container_semantics() -> None:
    # One canonical quest copy remains available to every other character.
    phase = 1
    a = claim(phase, 0, 1)
    assert a == 1
    assert visible_count(1, phase, a) == 0
    assert visible_count(1, phase, 0) == 1

    # The same character cannot mint another personal copy from the same source.
    assert claim(phase, a, 1) == 0

    # Mixed stack: the canonical quest unit phases, later shared drops stay shared.
    assert visible_count(3, 1, 1) == 2
    assert visible_count(3, 1, 0) == 3


def test_classifier_fix_present() -> None:
    text = (ROOT / "apps/openmw/mwmp/QuestItemIndex.cpp").read_text(encoding="utf-8")
    assert "directWorldQuestItem" in text
    assert "candidate.sources == 0 && candidate.evidence >= 7" in text


def test_index_schema_rebuild_present() -> None:
    text = (ROOT / "server/scripts/questIndexStore.lua").read_text(encoding="utf-8")
    assert "INDEX_SCHEMA_VERSION = 2" in text
    assert "schemaVersion = INDEX_SCHEMA_VERSION" in text
    assert "tonumber(data.schemaVersion) ~= INDEX_SCHEMA_VERSION" in text


def test_world_identity_is_set_before_classification() -> None:
    text = (ROOT / "server/scripts/eventHandler.lua").read_text(encoding="utf-8")
    assignment = text.index("object.uniqueIndex = uniqueIndex", text.index('packetType == "ObjectDelete"'))
    classification = text.index("questItemPhasing.IsWorldQuestSource", assignment)
    assert assignment < classification


if __name__ == "__main__":
    test_personal_world_container_semantics()
    test_classifier_fix_present()
    test_index_schema_rebuild_present()
    test_world_identity_is_set_before_classification()
    print("ArenaMP X058 quest-item phasing regression checks: PASS")
