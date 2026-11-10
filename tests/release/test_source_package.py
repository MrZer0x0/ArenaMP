#!/usr/bin/env python3
"""Run the current Release packager in an isolated disposable Git repository."""
from pathlib import Path
from zipfile import ZipFile
import subprocess, tempfile, shutil, tarfile, json, os
SRC = Path(__file__).resolve().parents[2]
def run(*args,root,env=None):
    return subprocess.run(args,cwd=root,env=env,check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True).stdout
with tempfile.TemporaryDirectory(prefix='arena-source-test-') as tmp:
    root=Path(tmp);(root/'CI').mkdir()
    shutil.copy2(SRC/'CI/prepare-release.py',root/'CI/prepare-release.py')
    shutil.copy2(SRC/'.gitattributes',root/'.gitattributes')
    (root/'CHANGELOG.md').write_text('# First public source release\n')
    (root/'test.cpp').write_text('int main(){return 0;}\n')
    run('git','init','-q',root=root)
    run('git','add','-A',root=root)
    run('git','-c','user.name=Source test','-c','user.email=test@example.invalid','commit','-qm','fixture',root=root)
    source=root/'release-input';source.mkdir()
    (source/'windows').mkdir()
    (source/'windows'/'ArenaMP-Windows-x64-fixture.zip').write_bytes(b'winpkg'+b'0'*200)
    env={**os.environ,'WINDOWS_RESULT':'success','LINUX_RESULT':'skipped'}
    run('python3','CI/prepare-release.py','--source','release-input','--destination','release-assets','--tag','v24-fixture',root=root,env=env)
    assets=root/'release-assets'
    with tarfile.open(assets/'ArenaMP-Source-v24-fixture.tar.gz') as archive:
        names=archive.getnames()
        assert 'ArenaMP/test.cpp' in names
        assert not any('signing-local' in n or '/android/' in n for n in names)
    info=json.loads((assets/'BUILD_INFO.json').read_text())
    assert info['configuration']=='Release' and len(info['packages'])==1
    assert (assets/'SHA256SUMS.txt').is_file()
    print('PASS Release asset checks, source tar sanitization, metadata and SHA-256')
