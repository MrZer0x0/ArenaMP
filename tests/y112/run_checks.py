#!/usr/bin/env python3
"""Reproduce the Y111 CI regressions and verify Y112 without an NDK build.

Runs the complete Android source patch pipeline (fresh and cached), then
compiles the real runApplication body for Android/desktop against small API
doubles. The Linux codec/path suite separately covers the short TGA failure.
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
def run(args, **kwargs):
    result = subprocess.run(args, text=True, capture_output=True, **kwargs)
    if result.returncode:
        raise RuntimeError('Failed: ' + repr(args) + '\n' + result.stdout[-6000:] + result.stderr[-6000:])
    return result.stdout

source = (ROOT / 'apps/openmw/main.cpp').read_text()
signature = 'int runApplication(int argc, char *argv[])'
start = source.index(signature)
opening = source.index('{', start)
depth, end = 1, opening + 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
body = source[start:end]
global_start = source.index('#ifdef ANDROID\n// Y112:')
declaration = source[global_start:start]
STUBS = r'''
#include <cassert>
#include <memory>
#include <string>
namespace Files {
struct ConfigurationManager {
 inline static int alive=0;
 ConfigurationManager(){++alive;}
 ~ConfigurationManager(){--alive;}
};
}
struct OSGLogHandler{};
namespace osg {inline void setNotifyHandler(OSGLogHandler* p){delete p;}}
namespace Resource::PortableOsg {inline void configure(){} inline bool checkImageCodecs(){return true;}}
'''
REST = r'''
namespace OMW {
class Engine {
 public: explicit Engine(Files::ConfigurationManager& cfg){
  (void)cfg;assert(Files::ConfigurationManager::alive==1);
#ifdef ANDROID
  assert(g_cfgMgr==&cfg);
#endif
 }
 void go(){}
};
}
inline bool parseOptions(int,char**,OMW::Engine&,Files::ConfigurationManager&){return false;}
'''
MAIN = r'''
int main(){
 char name[]="tes3mp";char* argv[]={name};
 assert(runApplication(1,argv)==0);assert(Files::ConfigurationManager::alive==0);
}
'''
with tempfile.TemporaryDirectory(prefix='arena-y112-main-') as folder:
    folder = Path(folder)
    cpp = folder / 'main.cpp'; cpp.write_text(STUBS + declaration + REST + body + MAIN)
    for mode, flags in [('android', ['-DANDROID']), ('desktop', [])]:
        binary = folder / mode
        run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-O2'] + flags + [str(cpp), '-o', str(binary)])
        run([str(binary)])
print('PASS actual shared runApplication: Android configuration bridge exists before Engine construction; desktop remains valid', flush=True)

prepare = ROOT / 'android/buildscripts/tool/prepare_source.py'
output = run([sys.executable, str(prepare), '--arch', 'arm64', '--force'], cwd=ROOT)
assert 'Prepared local ArenaMP:' in output and 'feature patchset verified' in output
stage = ROOT / 'android/buildscripts/build/arm64/arenamp-prefix/src/arenamp'
main = stage / 'apps/openmw/main.cpp'
assert main.read_bytes() == (ROOT / 'apps/openmw/main.cpp').read_bytes()
assert main.read_text().count('Files::ConfigurationManager* g_cfgMgr = nullptr;') == 1
assert main.read_text().count('g_cfgMgr = &cfgMgr;') == 1
assert (stage / '.arenamp_android_patchset').read_text().strip().endswith('enchant-v22')
mtime = main.stat().st_mtime_ns
cached = run([sys.executable, str(prepare), '--arch', 'arm64'], cwd=ROOT)
assert 'sources unchanged:' in cached and main.stat().st_mtime_ns == mtime
patches = ROOT / 'android/buildscripts/patches/openmw'
cached = run(['sh', str(patches / 'apply-arenamp-patches.sh'), str(stage)], cwd=ROOT)
assert 'already verified' in cached and main.read_bytes() == (ROOT / 'apps/openmw/main.cpp').read_bytes()
run([sys.executable, str(patches / '31-arenamp-y092-ui-ai-progression-enchant.py'), str(stage), '--verify-only'])
assert 'int tes3mpServerMain(int argc, char *argv[])' in (stage / 'apps/openmw-mp/main.cpp').read_text()
assert (stage / 'apps/openmw-mp/android_server_jni.cpp').read_bytes() == (patches / 'android_server_jni.cpp').read_bytes()
print('PASS complete Android preparation: fresh pipeline, unchanged fingerprint cache, cached patch pass, shared main/JNI/server entry point', flush=True)

output = run([sys.executable, str(ROOT / 'tests/y111/run_checks.py')], cwd=ROOT)
assert 'old 21-byte round-trip fails' in output and '56-byte 2x2 fixture and reader pass' in output
print(output, end='', flush=True)
print('PASS Y112 build regressions; no full engine/NDK build claimed', flush=True)
