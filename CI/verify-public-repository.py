#!/usr/bin/env python3
"""Prevent private signing material and non-desktop jobs in the public branch."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = ROOT / '.github/workflows/release-builds.yml'


def main() -> int:
    errors = []
    for relative in ('android', 'CI/macos-arm64'):
        if (ROOT / relative).exists():
            errors.append(f'Unexpected platform build directory: {relative}')
    job_text = WORKFLOW.read_text(encoding='utf-8')
    for forbidden in ('  macos-arm64:', '  android-arm64:', '      build_android:',
                      '      build_macos_arm64:', 'MACOS_RESULT:', 'ANDROID_RESULT:'):
        if forbidden in job_text:
            errors.append(f'Unexpected platform build setting: {forbidden.strip()}')
    for job in ('  windows-x64:', '  linux-steamdeck-x64:', '  publish-release:'):
        if job not in job_text:
            errors.append(f'Required job is missing: {job.strip()}')

    def private_path(name: str) -> bool:
        low = name.lower()
        return (name.startswith(('android/', 'signing-local/')) or
                low.endswith(('.jks', '.keystore', '.p12', '.pfx', 'signing.properties')))

    for path in ROOT.rglob('*'):
        if '.git' in path.parts:
            continue
        if path.is_file() and private_path(path.relative_to(ROOT).as_posix()):
            errors.append(f'Private credential in source tree: {path.relative_to(ROOT)}')
    tracked = subprocess.run(['git', 'ls-files'], cwd=ROOT,
                             capture_output=True, text=True, check=False)
    if tracked.returncode == 0:
        for name in tracked.stdout.splitlines():
            if private_path(name):
                errors.append(f'Private credential tracked in Git: {name}')
    else:
        errors.append('Unable to verify Git index in public repository')
    if errors:
        for error in errors:
            print(f'::error::{error}', file=sys.stderr)
        return 1
    print('Verified public repository scope: only Windows/Linux jobs; no private signing material')
    return 0


if __name__ == '__main__':
    sys.exit(main())
