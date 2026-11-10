#!/usr/bin/env python3
"""Collect successful desktop packages and sources before publishing a release.

Run in a clean publication-job checkout. No GitHub write occurs here.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess


ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    label = os.environ["BUILD_LABEL"]
    if not re.fullmatch(r"[1-9][0-9]*-[1-9][0-9]*", label):
        raise ValueError("BUILD_LABEL must be <run number>-<attempt>, both positive")

    results = {
        "windows-x64": os.environ["WINDOWS_RESULT"],
        "linux-steamdeck-x64": os.environ["LINUX_RESULT"],
    }
    if any(result not in ("success", "skipped") for result in results.values()):
        raise ValueError("A selected build failed or was cancelled; refusing release")
    selected = [name for name, result in results.items() if result == "success"]
    if not selected:
        raise ValueError("No successful platform packages to publish")

    commit = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True
    ).strip()
    if commit != os.environ["GITHUB_SHA"]:
        raise ValueError("Publication checkout differs from the triggering commit")

    names = {
        "windows-x64": f"ArenaMP-Windows-x64-{label}.zip",
        "linux-steamdeck-x64": f"ArenaMP-Linux-x86_64-SteamDeck-{label}.tar.gz",
    }
    expected = {names[platform] for platform in selected}
    artifacts = ROOT / "release-artifacts"
    actual = {path.name for path in artifacts.iterdir() if path.is_file()}
    if actual != expected or any(not path.is_file() for path in artifacts.iterdir()):
        raise ValueError(f"Expected only {sorted(expected)}; found {sorted(actual)}")
    for name in expected:
        if (artifacts / name).stat().st_size == 0:
            raise ValueError(f"Empty package: {name}")

    changelog = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
    section = re.search(r"^## .+?(?=^## |\Z)", changelog, re.MULTILINE | re.DOTALL)
    if section is None:
        raise ValueError("CHANGELOG.md needs a public release section")

    assets = ROOT / "release-assets"
    assets.mkdir()  # Do not reuse files from a prior preparation attempt.
    for name in sorted(expected):
        shutil.copyfile(artifacts / name, assets / name)

    source = assets / f"ArenaMP-Source-{label}.tar.gz"
    subprocess.run(
        ["git", "archive", "--format=tar.gz", "--prefix=ArenaMP/",
         f"--output={source}", commit], cwd=ROOT, check=True,
    )
    info = {
        "project": "ArenaMP",
        "configuration": "Release",
        "commit": commit,
        "ref": os.environ.get("GITHUB_REF", ""),
        "repository": os.environ.get("GITHUB_REPOSITORY", ""),
        "run_id": os.environ.get("GITHUB_RUN_ID", ""),
        "build_label": label,
        "platforms": selected,
        "packages": sorted(expected),
        "source": source.name,
    }
    (assets / "BUILD_INFO.json").write_text(
        json.dumps(info, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )

    sums = []
    for path in sorted(assets.iterdir()):
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        sums.append(f"{digest.hexdigest()}  {path.name}\n")
    (assets / "SHA256SUMS.txt").write_text("".join(sums), encoding="utf-8")

    notes = section.group().strip()
    # Root-relative documentation links do not resolve from a GitHub release page.
    repository = os.environ.get("GITHUB_REPOSITORY", "")
    if repository:
        server = os.environ.get("GITHUB_SERVER_URL", "https://github.com").rstrip("/")
        notes = re.sub(
            r"\]\((?![a-zA-Z][a-zA-Z0-9+.-]*:|#|/)([^)]+)\)",
            lambda match: f"]({server}/{repository}/blob/{commit}/{match.group(1)})",
            notes,
        )
    notes += (
        f"\n\nBuilt from commit `{commit}` in **Release** configuration.\n\n"
        "Platforms: " + ", ".join(selected) + ".\n\n"
        "The attached source archive is the repository at that commit. "
        "See `BUILD_INFO.json` and `SHA256SUMS.txt` for provenance and checksums.\n"
    )
    (ROOT / "release-notes.md").write_text(notes, encoding="utf-8")
    print(f"Prepared {len(expected)} platform package(s), matching sources and checksums")


if __name__ == "__main__":
    main()
