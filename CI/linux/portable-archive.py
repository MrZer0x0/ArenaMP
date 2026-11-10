#!/usr/bin/env python3
"""Build-host link normalization and offline repair of ArenaMP portable TARs.

materialize is for TRUSTED build output only; it can read host library bytes.
repair never extracts entries or reads link targets from the host filesystem.
"""
import argparse
import copy
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import sys
import tarfile
import tempfile


def clean_name(value):
    if '\\' in value or '\x00' in value or value.startswith('/') or re.match(r'^[A-Za-z]:', value):
        raise ValueError(f'Unsafe archive name: {value!r}')
    parts = []
    for part in value.split('/'):
        if part in ('', '.'):
            continue
        if part == '..':
            raise ValueError(f'Parent traversal in entry: {value!r}')
        parts.append(part)
    return '/'.join(parts)


def materialize(package):
    root = Path(package).resolve(strict=True)
    changed = 0
    for parent, dirs, files in os.walk(root, followlinks=False):
        for name in dirs:
            p = Path(parent) / name
            if p.is_symlink():
                raise ValueError(f'Directory link is not portable: {p.relative_to(root)}')
        for name in files:
            p = Path(parent) / name
            if not p.is_symlink():
                continue
            source = p.resolve(strict=True)
            if not source.is_file():
                raise ValueError(f'Link target is not a regular file: {p}')
            rel = p.relative_to(root)
            if root not in source.parents and rel.parts[0] != 'lib':
                raise ValueError(f'External non-library link: {rel}')
            fd, temp = tempfile.mkstemp(prefix='.portable-', dir=p.parent)
            os.close(fd)
            try:
                shutil.copy2(source, temp)
                os.replace(temp, p)  # Replace the link itself, never its host target.
            finally:
                if os.path.exists(temp):
                    os.unlink(temp)
            changed += 1
    print(f'Portable library links materialized: {changed}')


def check(package):
    root = Path(package).resolve(strict=True)
    for parent, dirs, files in os.walk(root, followlinks=False):
        for name in dirs + files:
            p = Path(parent) / name
            if p.is_symlink():
                raise ValueError(f'Unnormalized package link: {p.relative_to(root)}')
    print('Portable package link check passed')


def link_name(entry, link, hard):
    if '\\' in link or '\x00' in link or re.match(r'^[A-Za-z]:', link):
        raise ValueError(f'Unsafe link target: {link!r}')
    if link.startswith('/'):
        # Old Linux releases copied distro aliases verbatim. Rebind ONLY to a
        # same-directory library already present in the archive, never /lib on
        # the machine performing this repair.
        system_lib = any(link.startswith(prefix) for prefix in
                         ('/lib/', '/lib64/', '/usr/lib/', '/usr/lib64/', '/usr/local/lib/'))
        if not system_lib or 'lib' not in PurePosixPath(entry).parts[:-1] or '..' in link.split('/'):
            raise ValueError(f'Unsupported external link: {entry} -> {link}')
        if not re.fullmatch(r'lib[^/]+\.so(?:\.[A-Za-z0-9_-]+)*', PurePosixPath(link).name):
            raise ValueError(f'Not a shared-library alias: {link}')
        return str(PurePosixPath(entry).parent / PurePosixPath(link).name)
    parts = [] if hard else list(PurePosixPath(entry).parent.parts)
    for part in link.split('/'):
        if part in ('', '.'):
            continue
        if part == '..':
            if not parts:
                raise ValueError(f'Escaping link: {entry} -> {link}')
            parts.pop()
        else:
            parts.append(part)
    return clean_name('/'.join(parts))


def repair(source, output):
    source, output = Path(source), Path(output)
    if source.resolve() == output.resolve() or output.exists():
        raise ValueError('Choose a new output filename; input and existing files are never overwritten')
    with tarfile.open(source, 'r:*') as src:
        entries = {}
        for member in src:
            name = clean_name(member.name)
            if not name and member.isdir():
                continue
            if not name or name in entries:
                raise ValueError(f'Empty or duplicate archive entry: {member.name!r}')
            if not (member.isfile() or member.isdir() or member.issym() or member.islnk()):
                raise ValueError(f'Unsupported special entry: {name}')
            entries[name] = member
        for name in entries:
            for parent in PurePosixPath(name).parents:
                ancestor = entries.get(str(parent))
                if ancestor is not None and not ancestor.isdir():
                    raise ValueError(f'Non-directory ancestor of {name}: {parent}')

        resolved = {}
        def target(name, seen):
            if name in resolved:
                return resolved[name]
            if name in seen or len(seen) >= 64:
                raise ValueError(f'Link cycle or excessive depth: {name}')
            member = entries.get(name)
            if member is None:
                raise ValueError(f'Link target missing inside archive: {name}; rebuild from sources')
            if member.isfile():
                resolved[name] = member
                return member
            if not (member.issym() or member.islnk()):
                raise ValueError(f'Link to non-file: {name}')
            result = target(link_name(name, member.linkname, member.islnk()), seen | {name})
            resolved[name] = result
            return result

        # Preflight every link before creating the output. Bounded expansion
        # prevents an alias-heavy input multiplying its contents without limit.
        total = 0
        original = sum(m.size for m in entries.values() if m.isfile())
        for name, member in entries.items():
            if not member.isdir():
                total += target(name, set()).size
        if total > max(original * 8, original + 256 * 1024 * 1024):
            raise ValueError('Excessive link expansion; rebuild from sources')
        output.parent.mkdir(parents=True, exist_ok=True)
        fd, temp = tempfile.mkstemp(prefix='.arena-repair-', dir=output.parent)
        os.close(fd)
        try:
            with tarfile.open(temp, 'w:gz', format=tarfile.PAX_FORMAT) as dst:
                for name, member in entries.items():
                    info = copy.copy(member)
                    info.name = name
                    info.pax_headers = {}  # Do not retain stale path/linkpath overrides.
                    info.uid = info.gid = 0
                    info.uname = info.gname = ''
                    info.mode &= 0o777
                    if member.isdir():
                        info.size = 0
                        dst.addfile(info)
                    else:
                        data_member = target(name, set())
                        info.type = tarfile.REGTYPE
                        info.linkname = ''
                        info.size = data_member.size
                        info.mode = data_member.mode & 0o777
                        stream = src.extractfile(data_member)
                        if stream is None:
                            raise ValueError(f'Missing data: {name}')
                        with stream:
                            dst.addfile(info, stream)
            # Exclusive publication even if another process created OUTPUT meanwhile.
            os.link(temp, output)
        finally:
            if os.path.exists(temp):
                os.unlink(temp)
    print(f'Repaired archive: {output}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subs = parser.add_subparsers(dest='command', required=True)
    for command in ('materialize', 'check'):
        subs.add_parser(command).add_argument('package')
    p = subs.add_parser('repair')
    p.add_argument('source')
    p.add_argument('output')
    args = parser.parse_args()
    try:
        if args.command == 'repair':
            repair(args.source, args.output)
        elif args.command == 'check':
            check(args.package)
        else:
            materialize(args.package)
    except (OSError, ValueError, RuntimeError, tarfile.TarError) as error:
        print(f'ERROR: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
