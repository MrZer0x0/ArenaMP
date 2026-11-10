#!/usr/bin/env python3
"""Collect the non-host ELF DT_NEEDED closure for ArenaMP Linux portable.

Build-time only.  The target host owns glibc, libstdc++ and the graphics driver
stack; application/runtime libraries are copied beside ArenaMP.
"""

from __future__ import annotations

import fnmatch
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from collections import deque

HOST_PATTERNS = (
    "libc.so.*", "libm.so.*", "libpthread.so.*", "librt.so.*", "libdl.so.*",
    "libresolv.so.*", "libgcc_s.so.*", "libstdc++.so.*", "ld-linux*.so*",
    "libGL.so.*", "libGLX.so.*", "libGLdispatch.so.*", "libOpenGL.so.*",
    "libEGL.so.*", "libGLES*.so.*", "libGLX_mesa.so.*", "libEGL_mesa.so.*",
    "libdrm.so.*", "libgbm.so.*", "libvulkan.so.*",
    # Y079: fontconfig configuration syntax follows the target distro.  A
    # bundled Jammy libfontconfig parsing Arch/SteamOS /etc/fonts produces
    # invalid-attribute/constant errors, so the host owns fontconfig too.
    "libfontconfig.so.*",
)

SEARCH_DIRS = (
    Path("/lib/x86_64-linux-gnu"),
    Path("/usr/lib/x86_64-linux-gnu"),
    Path("/usr/local/lib/x86_64-linux-gnu"),
    Path("/lib"),
    Path("/usr/lib"),
    Path("/usr/local/lib"),
    Path("/usr/local/lib64"),
    # PulseAudio private DSOs are intentionally absent from ldconfig's cache.
    Path("/usr/lib/x86_64-linux-gnu/pulseaudio"),
    Path("/usr/lib/pulseaudio"),
)

NEEDED_RE = re.compile(r"Shared library: \[([^]]+)\]")
RUNPATH_RE = re.compile(r"Library (?:runpath|rpath): \[([^]]*)\]", re.IGNORECASE)
LDCONFIG_RE = re.compile(r"^\s*(\S+)\s+\(([^)]*)\)\s+=>\s+(\S+)\s*$")


def is_host_owned(name: str) -> bool:
    return any(fnmatch.fnmatch(name, pattern) for pattern in HOST_PATTERNS)


def is_elf(path: Path) -> bool:
    try:
        with path.open("rb") as stream:
            return stream.read(4) == b"\x7fELF"
    except OSError:
        return False


def needed(path: Path) -> list[str]:
    proc = subprocess.run(
        ["readelf", "-d", str(path)],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
    )
    if proc.returncode != 0:
        return []
    return NEEDED_RE.findall(proc.stdout)


def ldconfig_map() -> dict[str, Path]:
    result: dict[str, Path] = {}
    proc = subprocess.run(
        ["ldconfig", "-p"],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
    )
    for line in proc.stdout.splitlines():
        match = LDCONFIG_RE.match(line)
        if not match:
            continue
        soname, abi, raw_path = match.groups()
        path = Path(raw_path)
        if not path.is_file():
            continue
        # GitHub Linux x64 packages must never accidentally select a 32-bit lib.
        if "x86-64" not in abi and "64bit" not in abi and os.uname().machine == "x86_64":
            continue
        result.setdefault(soname, path)
    return result


def build_package_index(lib_root: Path) -> dict[str, Path]:
    result: dict[str, Path] = {}
    if not lib_root.exists():
        return result
    for path in lib_root.rglob("*"):
        if path.is_file():
            result.setdefault(path.name, path)
    return result


def dynamic_runpaths(path: Path) -> list[Path]:
    proc = subprocess.run(
        ["readelf", "-d", str(path)],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
    )
    result: list[Path] = []
    for raw in RUNPATH_RE.findall(proc.stdout):
        for entry in raw.split(":"):
            if not entry:
                continue
            entry = entry.replace("${ORIGIN}", str(path.parent)).replace("$ORIGIN", str(path.parent))
            result.append(Path(entry))
    return result


def resolve_host(name: str, cache: dict[str, Path], requester: Path | None = None) -> Path | None:
    # Honor the requesting DSO's own runtime first (private ABI).
    if requester is not None:
        for directory in dynamic_runpaths(requester):
            candidate = directory / name
            if candidate.is_file():
                return candidate
    path = cache.get(name)
    if path is not None and path.is_file():
        return path
    for directory in SEARCH_DIRS:
        candidate = directory / name
        if candidate.is_file():
            return candidate
    return None


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {Path(sys.argv[0]).name} PACKAGE_DIR", file=sys.stderr)
        return 2

    package = Path(sys.argv[1]).resolve()
    lib_root = package / "lib"
    lib_root.mkdir(parents=True, exist_ok=True)

    cache = ldconfig_map()
    package_index = build_package_index(lib_root)
    # Preserve build-host origins while traversing relocated libraries. $ORIGIN
    # must resolve next to the source, not next to its flattened package copy.
    origins: dict[Path, Path] = {}
    for local in package_index.values():
        source = resolve_host(local.name, cache)
        if source is not None and source.resolve() != local.resolve():
            if source.read_bytes() == local.read_bytes():
                origins[local.resolve()] = source.resolve()
    queue: deque[Path] = deque()
    queued_real: set[str] = set()
    processed_real: set[str] = set()

    def enqueue(path: Path) -> None:
        if not path.is_file() or not is_elf(path):
            return
        try:
            key = str(path.resolve())
        except OSError:
            key = str(path)
        if key in queued_real or key in processed_real:
            return
        queued_real.add(key)
        queue.append(path)

    # Every executable, runtime library and dynamic plugin already selected by
    # package-portable.sh is a dependency root.
    for path in package.rglob("*"):
        if path.is_file():
            enqueue(path)

    copied = 0
    while queue:
        current = queue.popleft()
        try:
            current_key = str(current.resolve())
        except OSError:
            current_key = str(current)
        queued_real.discard(current_key)
        if current_key in processed_real:
            continue
        processed_real.add(current_key)

        for soname in needed(current):
            if is_host_owned(soname):
                continue

            local = package_index.get(soname)
            if local is not None and local.is_file():
                enqueue(local)
                continue

            requester = origins.get(current.resolve(), current)
            source = resolve_host(soname, cache, requester)
            if source is None:
                rel = current.relative_to(package) if package in current.parents else current
                print(
                    f"ERROR: cannot resolve portable dependency {soname} needed by {rel}",
                    file=sys.stderr,
                )
                return 1

            destination = lib_root / soname
            # Store bytes under the exact DT_NEEDED SONAME. This avoids relying
            # on target-distro symlink layouts (e.g. libtheoraenc.so.1).
            shutil.copy2(source.resolve(), destination)
            package_index[soname] = destination
            origins[destination.resolve()] = source.resolve()
            copied += 1
            enqueue(destination)

    print(
        f"ArenaMP portable dependency closure: copied={copied}, "
        f"processed_elf={len(processed_real)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
