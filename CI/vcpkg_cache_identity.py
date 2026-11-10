#!/usr/bin/env python3
"""Compute a stable Windows vcpkg cache identity for ArenaMP's CI workflow.

The workflow appends the overlay triplet hash to this identity. The value is
stable across runner restarts yet changes when the compiler, SDK, toolset or
vcpkg executable changes. Print only the identity to stdout for PowerShell.
"""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import sys


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open('rb') as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--vcpkg-root', type=Path, required=True)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--sdk', required=True)
    parser.add_argument('--toolset', required=True)
    args = parser.parse_args()

    vcpkg_exe = args.vcpkg_root / 'vcpkg.exe'
    if not vcpkg_exe.is_file():
        parser.error(f'vcpkg executable missing: {vcpkg_exe}')
    if not args.compiler.is_file():
        parser.error(f'MSVC compiler missing: {args.compiler}')

    metadata = '\n'.join([
        'arenamp-vcpkg-cache-identity-v1',
        f'vcpkg-sha256={file_sha256(vcpkg_exe)}',
        f'compiler-sha256={file_sha256(args.compiler)}',
        f'sdk={args.sdk.strip()}',
        f'toolset={args.toolset.strip()}',
    ])
    print(hashlib.sha256(metadata.encode('utf-8')).hexdigest()[:32])
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except OSError as error:
        print(f'vcpkg cache identity: {error}', file=sys.stderr)
        sys.exit(1)
