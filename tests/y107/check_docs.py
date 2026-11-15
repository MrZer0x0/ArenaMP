#!/usr/bin/env python3
"""Configure the actual documentation install block without engine dependencies."""
from pathlib import Path
import argparse, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cmake',required=True)
args=parser.parse_args()
source=(ROOT/'CMakeLists.txt').read_text()
block=source[source.index('# Y107: ordinary client-folder documents'):source.index('# Doxygen Target --')]
with tempfile.TemporaryDirectory(prefix='arena-ce-docs-') as temp:
    temp=Path(temp)
    for mode in ['desktop','android']:
        case=temp/mode;case.mkdir()
        (case/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.16)\nproject(Docs NONE)\n'
            f'set(OpenMW_SOURCE_DIR "{ROOT.as_posix()}")\nset(OpenMW_BINARY_DIR "${{CMAKE_BINARY_DIR}}")\n'
            f'set(ANDROID {"ON" if mode=="android" else "OFF"})\nset(DATAROOTDIR "${{CMAKE_INSTALL_PREFIX}}/share")\n'+block)
        subprocess.run([args.cmake,'-S',str(case),'-B',str(case/'build'),'-DCMAKE_INSTALL_PREFIX='+str(case/'install')],check=True,capture_output=True)
        subprocess.run([args.cmake,'--install',str(case/'build')],check=True,capture_output=True)
        resource=case/'build/resources/doc/ArenaMP-Craft-and-Economic'
        installed=case/'install'/('doc' if mode=='android' else 'share/doc/arenamp')/'ArenaMP-Craft-and-Economic'
        for name in ['README_RU.md','API.md','AUTHORS_AND_LICENSES.md','LICENSE.txt','CREDITS.md','THIRD_PARTY_NOTICES.md']:
            assert (resource/name).is_file() and (installed/name).read_bytes()==(resource/name).read_bytes()
        assert (resource/'LICENSE.txt').read_bytes()==(ROOT/'LICENSE').read_bytes()
print('PASS actual CMake documentation block: desktop install, Android resource folder, authors, API and GPL text')
