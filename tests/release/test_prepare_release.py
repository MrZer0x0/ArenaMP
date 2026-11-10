#!/usr/bin/env python3
"""Exercise release payload and refusal cases using a temporary Git repository."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[2] / "CI" / "prepare-release.py"
LABEL = "12-2"
WINDOWS = f"ArenaMP-Windows-x64-{LABEL}.zip"
LINUX = f"ArenaMP-Linux-x86_64-SteamDeck-{LABEL}.tar.gz"


class ReleasePreparationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="arenamp-release-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "CI").mkdir()
        shutil.copyfile(SCRIPT, self.root / "CI/prepare-release.py")
        (self.root / "CHANGELOG.md").write_text(
            "# Changelog\n\n## First public release\n\n"
            "See [status](PACKAGE_STATUS.md).\n\n## Older release\nExcluded.\n"
        )
        (self.root / "PACKAGE_STATUS.md").write_text("# Source status\n")
        (self.root / "source.txt").write_text("committed source\n")
        (self.root / "extern/LuaJIT/src").mkdir(parents=True)
        (self.root / "extern/LuaJIT/src/Makefile").write_text("# vendor source\n")
        self.git("init", "-q")
        self.git("add", "-A")
        self.git("-c", "user.name=Release test", "-c", "user.email=test@example.invalid",
                 "commit", "-qm", "Source fixture")
        self.commit = self.git("rev-parse", "HEAD").strip()
        (self.root / "release-artifacts").mkdir()
        self.env = {
            **os.environ,
            "BUILD_LABEL": LABEL,
            "WINDOWS_RESULT": "success",
            "LINUX_RESULT": "success",
            "GITHUB_SHA": self.commit,
            "GITHUB_REF": "refs/heads/main",
            "GITHUB_REPOSITORY": "owner/arenamp",
            "GITHUB_SERVER_URL": "https://github.com",
            "GITHUB_RUN_ID": "1234",
        }

    def git(self, *args):
        return subprocess.check_output(
            ["git", *args], cwd=self.root, text=True, stderr=subprocess.PIPE
        )

    def packages(self, *names):
        # Preparation treats packages as opaque outputs of the platform checks.
        for name in names:
            (self.root / "release-artifacts" / name).write_bytes(b"package fixture\n")

    def prepare(self, success=True):
        result = subprocess.run(
            ["python3", "CI/prepare-release.py"], cwd=self.root, env=self.env,
            text=True, capture_output=True,
        )
        if success:
            self.assertEqual(result.returncode, 0, result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse((self.root / "release-assets").exists())
        return result

    def test_both_packages_sources_and_checksums(self):
        self.packages(WINDOWS, LINUX)
        (self.root / "source.txt").write_text("uncommitted change\n")
        self.prepare()
        assets = self.root / "release-assets"
        expected = {WINDOWS, LINUX, f"ArenaMP-Source-{LABEL}.tar.gz",
                    "BUILD_INFO.json", "SHA256SUMS.txt"}
        self.assertEqual({p.name for p in assets.iterdir()}, expected)
        info = json.loads((assets / "BUILD_INFO.json").read_text())
        self.assertEqual(info["commit"], self.commit)
        self.assertEqual(info["configuration"], "Release")
        self.assertEqual(set(info["packages"]), {WINDOWS, LINUX})
        with tarfile.open(assets / info["source"]) as source:
            self.assertEqual(source.extractfile("ArenaMP/source.txt").read(),
                             b"committed source\n")
            self.assertIn("ArenaMP/extern/LuaJIT/src/Makefile", source.getnames())
            self.assertFalse(any(".git" in Path(p).parts for p in source.getnames()))
            self.assertFalse(any("release-assets" in Path(p).parts for p in source.getnames()))
        sums = (assets / "SHA256SUMS.txt").read_text().splitlines()
        self.assertEqual(len(sums), 4)
        for line in sums:
            digest, name = line.split("  ", 1)
            self.assertEqual(digest, hashlib.sha256((assets / name).read_bytes()).hexdigest())
        notes = (self.root / "release-notes.md").read_text()
        self.assertIn(f"https://github.com/owner/arenamp/blob/{self.commit}/PACKAGE_STATUS.md", notes)
        self.assertNotIn("Older release", notes)

    def test_windows_only(self):
        self.env["LINUX_RESULT"] = "skipped"
        self.packages(WINDOWS)
        self.prepare()
        info = json.loads((self.root / "release-assets/BUILD_INFO.json").read_text())
        self.assertEqual(info["platforms"], ["windows-x64"])

    def test_linux_only(self):
        self.env["WINDOWS_RESULT"] = "skipped"
        self.packages(LINUX)
        self.prepare()
        info = json.loads((self.root / "release-assets/BUILD_INFO.json").read_text())
        self.assertEqual(info["platforms"], ["linux-steamdeck-x64"])

    def test_missing_selected_package(self):
        self.packages(WINDOWS)
        self.prepare(success=False)

    def test_unexpected_diagnostics(self):
        self.packages(WINDOWS, LINUX, "diagnostics.zip")
        self.prepare(success=False)

    def test_empty_package(self):
        self.packages(WINDOWS, LINUX)
        (self.root / "release-artifacts" / LINUX).write_bytes(b"")
        self.prepare(success=False)

    def test_failed_cancelled_or_no_selected_build(self):
        self.packages(WINDOWS, LINUX)
        for result in ("failure", "cancelled"):
            with self.subTest(result=result):
                self.env["LINUX_RESULT"] = result
                self.prepare(success=False)
        self.env.update(WINDOWS_RESULT="skipped", LINUX_RESULT="skipped")
        self.prepare(success=False)

    def test_wrong_checkout(self):
        self.packages(WINDOWS, LINUX)
        self.env["GITHUB_SHA"] = "0" * 40
        self.prepare(success=False)


if __name__ == "__main__":
    unittest.main()
