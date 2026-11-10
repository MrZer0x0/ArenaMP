#!/usr/bin/env python3
"""Stable dependency-cache identity: excludes game sources and commit SHA."""
import argparse
import hashlib
import json
from pathlib import Path

def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda:f.read(1024*1024),b''):h.update(chunk)
    return h.hexdigest()

def identity(root, compiler, sdk, toolset):
    root,compiler=Path(root),Path(compiler)
    # New GitHub Windows runner images may omit the legacy vcpkgTools.xml.
    # Validate build-critical files, but only hash optional metadata when present.
    required=['vcpkg.exe','triplets/x64-windows.cmake',
              'ports/openssl/portfile.cmake','ports/openssl/vcpkg.json']
    optional=['scripts/vcpkgTools.xml','scripts/vcpkgTools.json',
              'scripts/buildsystems/vcpkg.cmake']
    for name in required:
        if not (root/name).is_file():raise ValueError('vcpkg cache input is missing: '+name)
    if not compiler.is_file() or not sdk or not toolset:raise ValueError('MSVC compiler, SDK and toolset are required')
    files={root/p for p in required}
    files.update(root/p for p in optional if (root/p).is_file())
    for name in ['scripts/cmake','ports/openssl','ports/vcpkg-cmake','ports/vcpkg-cmake-config','ports/vcpkg-cmake-get-vars']:
        files.update(p for p in (root/name).rglob('*') if p.is_file())
    inputs={p.relative_to(root).as_posix():sha(p) for p in sorted(files)}
    compilers={name:sha(compiler.parent/name) for name in [compiler.name,'c1xx.dll','c2.dll'] if (compiler.parent/name).is_file()}
    payload={'schema':1,'triplet':'x64-windows','sdk':sdk,'toolset':toolset,
             'compiler':compilers,'vcpkg':inputs,'identity_script':sha(Path(__file__))}
    return hashlib.sha256(json.dumps(payload,sort_keys=True,separators=(',',':')).encode()).hexdigest()

def main():
    p=argparse.ArgumentParser();p.add_argument('--vcpkg-root',required=True);p.add_argument('--compiler',required=True)
    p.add_argument('--sdk',required=True);p.add_argument('--toolset',required=True);a=p.parse_args()
    print(identity(a.vcpkg_root,a.compiler,a.sdk,a.toolset))
if __name__=='__main__':main()
