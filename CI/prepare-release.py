#!/usr/bin/env python3
"""Stage only verified Release binaries, exact Git source, checksums, and notes.

Run by the publish-release GitHub Actions job AFTER all selected builds succeed.
Does not write to GitHub. Signing credentials are forbidden in Release assets.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PLATFORMS = {
    "WINDOWS_RESULT": ("windows", ".zip"),
    "LINUX_RESULT": ("linux", ".tar.gz"),
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "release-input")
    parser.add_argument("--destination", type=Path, default=ROOT / "release-assets")
    parser.add_argument("--tag", required=True)
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,100}", args.tag):
        parser.error("Invalid release tag")
    source = args.source.resolve()
    output = args.destination.resolve()
    if not source.is_dir() or output.exists():
        parser.error("Release input must exist, output must be empty/not yet created")
    selected = []
    for envname, (folder, suffix) in PLATFORMS.items():
        status = os.environ.get(envname, "skipped")
        if status not in {"success", "skipped"}:
            parser.error(f"A selected platform is not successful: {envname}={status}")
        if status == "success":
            matches = sorted((source / folder).glob("*" + suffix))
            if len(matches) != 1 or not matches[0].is_file() or matches[0].is_symlink() or matches[0].stat().st_size < 100:
                parser.error(f"Expected exactly one non-empty {suffix} in {folder}; found {matches}")
            selected.append((folder, matches[0]))
        elif (source / folder).exists():
            parser.error(f"Unselected platform has unexpected files: {folder}")
    if not selected:
        parser.error("No successful packages were selected")
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    if os.environ.get("GITHUB_SHA") and commit != os.environ["GITHUB_SHA"]:
        parser.error("Checkout commit differs from the triggering GitHub commit")
    # This is a PUBLIC desktop-only repository. Never export private identities.
    tracked = subprocess.check_output(["git", "ls-files"], cwd=ROOT, text=True).splitlines()
    private = sorted(name for name in tracked if
        name.startswith(("android/", "signing-local/")) or
        name.lower().endswith((".jks", ".keystore", ".p12", ".pfx", "signing.properties")))
    if private:
        parser.error("Private/Android files found in public source index: " + ", ".join(private))
    output.mkdir(parents=True)
    releases = []
    for platform, src in selected:
        target = output / src.name
        if target.exists():
            parser.error("Package collision: " + src.name)
        shutil.copyfile(src, target)
        releases.append({"platform": platform, "file": target.name, "bytes": target.stat().st_size})

    source_tar = output / f"ArenaMP-Source-{args.tag}.tar.gz"
    subprocess.run(["git", "archive", "--format=tar.gz", "--prefix=ArenaMP/", f"--output={source_tar}", "HEAD"],
                   cwd=ROOT, check=True)
    if source_tar.stat().st_size < 100:
        parser.error("Generated source archive is empty")
    # Verify export-ignore rules; never trust archive configuration blindly.
    import tarfile
    with tarfile.open(source_tar, "r:gz") as tf:
        names = [f.name for f in tf.getmembers() if f.isfile()]
    if any(name.startswith("ArenaMP/android/") or
           name.lower().endswith((".jks", ".keystore", ".p12", ".pfx", "signing.properties"))
           for name in names):
        parser.error("Private or Android files unexpectedly present in source archive")
    info = {
        "project": "ArenaMP",
        "release": args.tag,
        "commit": commit,
        "github_run_id": os.environ.get("GITHUB_RUN_ID", "local"),
        "configuration": "Release",
        "script_runtime": "vendored Rubic0n LuaJIT 2.1",
        "packages": releases,
    }
    (output / "BUILD_INFO.json").write_text(json.dumps(info, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    checksums = []
    for path in sorted(output.iterdir()):
        if path.is_file():
            checksums.append(f"{sha256(path)}  {path.name}")
    (output / "SHA256SUMS.txt").write_text("\n".join(checksums) + "\n", encoding="utf-8")
    changelog = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
    (output / "RELEASE_NOTES.md").write_text(changelog, encoding="utf-8")
    print("Prepared release", args.tag, "commit", commit, "platforms", [p for p, _ in selected])
    return 0


if __name__ == "__main__":
    sys.exit(main())
