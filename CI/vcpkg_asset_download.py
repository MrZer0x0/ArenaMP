#!/usr/bin/env python3
"""vcpkg x-script download provider with verified local SHA-512 asset caching.

Used by release-builds.yml with --url {url} --sha512 {sha512} --output {dst}.
Only valid checksummed files are installed into vcpkg's destination. On the
GitHub runner, downloaded assets are cached under .cache/vcpkg-assets.
"""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import shutil
import sys
import tempfile
import time
from urllib.parse import urlsplit
from urllib.request import Request, urlopen

BLOCK_SIZE = 1024 * 1024


def checksum(path: Path) -> str:
    digest = hashlib.sha512()
    with path.open('rb') as handle:
        for chunk in iter(lambda: handle.read(BLOCK_SIZE), b''):
            digest.update(chunk)
    return digest.hexdigest()


def install_cached(source: Path, dest: Path, expected: str) -> bool:
    if not source.is_file() or checksum(source) != expected:
        return False
    dest.parent.mkdir(parents=True, exist_ok=True)
    if source == dest:
        return True
    with tempfile.NamedTemporaryFile(dir=dest.parent, prefix='.arenamp-vcpkg-', delete=False) as handle:
        temporary = Path(handle.name)
    try:
        shutil.copyfile(source, temporary)
        os.replace(temporary, dest)
    finally:
        temporary.unlink(missing_ok=True)
    return True


def fetch(url: str, dest: Path, expected: str) -> None:
    request = Request(url, headers={'User-Agent': 'ArenaMP-vcpkg-asset-provider/1.0'})
    with urlopen(request, timeout=120) as response, dest.open('wb') as target:
        digest = hashlib.sha512()
        while True:
            block = response.read(BLOCK_SIZE)
            if not block:
                break
            digest.update(block)
            target.write(block)
    actual = digest.hexdigest()
    if actual != expected:
        raise ValueError(f'SHA-512 mismatch: expected {expected}, got {actual}')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True)
    parser.add_argument('--sha512', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    expected = args.sha512.lower().strip()
    if len(expected) != 128 or any(c not in '0123456789abcdef' for c in expected):
        parser.error('--sha512 must be exactly 128 hexadecimal characters')
    if urlsplit(args.url).scheme not in {'https', 'http'}:
        parser.error('--url must be an HTTP(S) URL')

    output = args.output.absolute()
    if install_cached(output, output, expected):
        return 0

    workspace = Path(os.environ.get('GITHUB_WORKSPACE', Path.cwd()))
    cache = workspace / '.cache' / 'vcpkg-assets'
    cache.mkdir(parents=True, exist_ok=True)
    cached = cache / expected
    if install_cached(cached, output, expected):
        return 0
    if cached.exists():
        cached.unlink()

    for attempt in range(1, 4):
        with tempfile.NamedTemporaryFile(dir=cache, prefix='.arenamp-download-', delete=False) as handle:
            temporary = Path(handle.name)
        try:
            fetch(args.url, temporary, expected)
            os.replace(temporary, cached)
            if not install_cached(cached, output, expected):
                raise RuntimeError('Cached download failed post-download verification')
            return 0
        except (OSError, ValueError, RuntimeError) as error:
            print(f'vcpkg asset download attempt {attempt}/3 failed: {error}', file=sys.stderr)
            if attempt == 3 or isinstance(error, ValueError):
                return 1
            time.sleep(attempt * 2)
        finally:
            temporary.unlink(missing_ok=True)
    return 1


if __name__ == '__main__':
    sys.exit(main())
