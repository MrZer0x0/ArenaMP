#!/usr/bin/env python3
"""vcpkg x-script provider for CMake release assets; preserve vcpkg's SHA512."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request

def candidates(url):
    parsed = urllib.parse.urlsplit(url)
    if parsed.scheme != 'https' or parsed.netloc.lower() != 'github.com' or parsed.query or parsed.fragment:
        return []
    match = re.fullmatch(r'/Kitware/CMake/releases/download/v(\d+)\.(\d+)\.(\d+)/(cmake-[A-Za-z0-9_.-]+)', parsed.path)
    if not match:
        return []
    major, minor, patch, name = match.groups()
    if not name.startswith(f'cmake-{major}.{minor}.{patch}-'):
        return []
    return [f'https://cmake.org/files/v{major}.{minor}/{name}', url]

def digest(path):
    h = hashlib.sha512()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()

def install_output(source, output):
    fd, name = tempfile.mkstemp(prefix='.vcpkg-asset-', dir=output.parent)
    os.close(fd)
    temporary = Path(name)
    try:
        shutil.copyfile(source, temporary)
        os.replace(temporary, output)
    finally:
        temporary.unlink(missing_ok=True)

def acquire(urls, expected, output, cache, attempts=3, timeout=90, pause=time.sleep):
    if not re.fullmatch(r'[0-9a-fA-F]{128}', expected):
        raise ValueError('a valid vcpkg SHA512 is required')
    expected = expected.lower()
    output, cache = Path(output), Path(cache)
    output.parent.mkdir(parents=True, exist_ok=True)
    cache.mkdir(parents=True, exist_ok=True)
    cached = cache / expected
    if output.is_file() and digest(output) == expected:
        return
    # Never leave an old invalid asset at the path vcpkg will consume.
    output.unlink(missing_ok=True)
    if cached.is_file():
        if digest(cached) == expected:
            install_output(cached, output)
            return
        cached.unlink()
    if not urls:
        raise ValueError('no supported CMake asset URL')
    last = None
    for attempt in range(attempts):
        if attempt:
            pause(2 * attempt)
        for url in urls:
            fd, name = tempfile.mkstemp(prefix='.asset-', dir=cache)
            os.close(fd)
            partial = Path(name)
            try:
                req = urllib.request.Request(url, headers={'User-Agent': 'ArenaMP-CI-v22'})
                deadline = time.monotonic() + timeout
                with urllib.request.urlopen(req, timeout=timeout) as response, partial.open('wb') as stream:
                    read = getattr(response, 'read1', response.read)
                    while True:
                        if time.monotonic() >= deadline:
                            raise OSError('asset download deadline exceeded')
                        chunk = read(1024 * 1024)
                        if not chunk:
                            break
                        stream.write(chunk)
                if digest(partial) != expected:
                    raise ValueError('downloaded asset SHA512 mismatch')
                os.replace(partial, cached)
                install_output(cached, output)
                return
            except (OSError, ValueError) as error:
                # No arbitrary URL/query strings in logs (other assets may be private).
                last = f'HTTP {error.code}' if isinstance(error, urllib.error.HTTPError) else type(error).__name__
            finally:
                partial.unlink(missing_ok=True)
    raise RuntimeError(f'CMake asset unavailable or invalid after retries ({last})')

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--url', required=True)
    parser.add_argument('--sha512', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    urls = candidates(args.url)
    if not urls:
        return 1  # vcpkg retains its ordinary source policy for unrelated assets.
    cache = os.environ.get('ARENAMP_VCPKG_ASSET_CACHE') or str(Path(__file__).resolve().parents[1] / '.cache/vcpkg-assets')
    try:
        acquire(urls, args.sha512, args.output, cache)
    except (OSError, ValueError, RuntimeError) as error:
        print(f'ArenaMP CMake asset provider: {error}', file=sys.stderr)
        return 1
    print('ArenaMP CMake asset restored; SHA512 verified', file=sys.stderr)
    return 0
if __name__ == '__main__':
    sys.exit(main())
