#!/usr/bin/env python3
"""Integrity checks plus real x86 build/run of restored maskedoc and PicoSHA2."""
from pathlib import Path
import subprocess,tempfile,os,shutil,json,hashlib
ROOT=Path(__file__).resolve().parents[2]
CMAKE=os.environ.get('CMAKE_COMMAND') or shutil.which('cmake')
assert CMAKE,'CMake is required'
with tempfile.TemporaryDirectory(prefix='arenamp-extern-') as d:
 p=Path(d)
 check=p/'check.cmake'
 check.write_text('include("'+(ROOT/'cmake/ArenaBundledDependencies.cmake').as_posix()+'")\nset(BUILD_OPENMW ON)\nset(BUILD_OPENMW_MP ON)\narena_check_bundled_sources("'+ROOT.as_posix()+'")\n')
 subprocess.run([CMAKE,'-P',str(check)],check=True)
 missing=p/'empty';missing.mkdir()
 check.write_text(check.read_text().replace(ROOT.as_posix()+'")',missing.as_posix()+'")'))
 r=subprocess.run([CMAKE,'-P',str(check)],text=True,capture_output=True)
 assert r.returncode!=0 and 'bundled source tree is incomplete' in r.stderr
 assert 'extern/maskedoc/CMakeLists.txt' in r.stderr
 (p/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.18)\nproject(BundledSmoke CXX)\nset(CMAKE_CXX_STANDARD 17)\n'
  +'set(OPENMW_USE_SYSTEM_BULLET ON)\nset(OPENMW_USE_SYSTEM_MYGUI ON)\nset(OPENMW_USE_SYSTEM_OSG ON)\nset(OPENMW_USE_SYSTEM_RECASTNAVIGATION ON)\n'
  +'add_subdirectory("'+(ROOT/'extern').as_posix()+'" extern_root)\n'
  +'add_subdirectory("'+(ROOT/'extern/maskedoc').as_posix()+'" moc)\n'
  +'add_subdirectory("'+(ROOT/'extern/Base64').as_posix()+'" base64)\n'
  +'add_executable(bundled_smoke "'+(ROOT/'tests/v21/bundled_smoke.cpp').as_posix()+'")\n'
  +'target_include_directories(bundled_smoke PRIVATE "'+(ROOT/'extern/PicoSHA2').as_posix()+'")\n'
  +'target_link_libraries(bundled_smoke PRIVATE maskedoc Base64)\n')
 subprocess.run([CMAKE,'-S',str(p),'-B',str(p/'build'),'-DCMAKE_BUILD_TYPE=Release'],check=True)
 subprocess.run([CMAKE,'--build',str(p/'build'),'--parallel','2'],check=True)
 subprocess.run([str(p/'build/bundled_smoke')],check=True)
 sources=json.loads((ROOT/'extern/ARENA_DEPENDENCY_SOURCES.json').read_text())
 assert all(len(x['commit'])==40 for x in sources['upstream_sources'])
 for name,sha in sources['files'].items():
  assert hashlib.sha256((ROOT/'extern'/name).read_bytes()).hexdigest()==sha,name
 assert (ROOT/'extern/breakpad/src/third_party/lss/linux_syscall_support.h').is_file()
 assert (ROOT/'extern/raknet/DependentExtensions/speex-1.1.12/include/speex/speex.h').is_file()
 assert not list((ROOT/'extern').rglob('.git'))
print('PASS bundled source integrity, missing-tree diagnostic, real maskedoc build and runtime smoke')
