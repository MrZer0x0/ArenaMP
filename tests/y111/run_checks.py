#!/usr/bin/env python3
"""Linux OSG path/packaging regression checks; not a full OSG/engine build.

The production header is compiled unchanged against OSG API doubles. ELF
dependency closure uses real, locally compiled shared libraries and readelf.
The CI codec command runs the actual OSG codecs in a completed Linux build.
"""
from contextlib import redirect_stderr, redirect_stdout
import importlib.util
import io
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
def run(args, **kwargs):
    return subprocess.run(args, check=True, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, **kwargs)

IMAGE = r'''
#pragma once
#include <memory>
#include <vector>
#define GL_RGB 0x1907
#define GL_UNSIGNED_BYTE 0x1401
namespace osg {
class Image {
 int width=0,height=0; std::vector<unsigned char> bytes;
 public: void allocateImage(int x,int y,int,int,int){width=x;height=y;bytes.resize(x*y*3);}
 unsigned char* data(){return bytes.data();} int s()const{return width;} int t()const{return height;}
};
template<class T> class ref_ptr {std::shared_ptr<T> p;public:ref_ptr(T* v):p(v){} T* operator->()const{return p.get();} T& operator*()const{return *p;}};
}
'''
REGISTRY = r'''
#pragma once
#include <osg/Image>
#include <string>
#include <vector>
#include <istream>
#include <ostream>
#include <iterator>
namespace osgDB {
inline std::string failPlugin, failReader, failWrite, failRead;
inline int writes=0,reads=0;
class ReaderWriter {
 public: std::string extension;
 struct WriteResult {bool ok;bool success()const{return ok;}std::string message()const{return "write result";}};
 struct ReadResult {
  std::shared_ptr<osg::Image> image;
  bool success()const{return bool(image);}std::string message()const{return "read result";}
  int status()const{return image?4:3;}
  osg::Image* getImage(){return image.get();}
 };
 WriteResult writeImage(const osg::Image&, std::ostream& stream){
  ++writes;
  if(extension=="tga"){
   // Reproduce the real OSG 3.6.5 1x1 TGA writer: 18-byte header + 3-byte RGB.
   std::string data(21,'\0');data[2]=2;data[12]=1;data[14]=1;data[16]=24;
   stream.write(data.data(),data.size());
  }else stream<<"1x1";
  return{extension!=failWrite};
 }
 ReadResult readImage(std::istream& stream){
  ++reads;std::string data((std::istreambuf_iterator<char>(stream)),{});
  if(extension==failRead)return{};
  int size=1;
  if(extension=="tga"){
   // The distro decoder seeks/reads 26 footer bytes even for old-style TGAs.
   if(data.size()<26)return{};
   if(data.size()!=56||data.substr(38)!=std::string("TRUEVISION-XFILE.\0",18))return{};
   size=2;
  }else if(data!="1x1")return{};
  auto img=std::make_shared<osg::Image>();img->allocateImage(size,size,1,0,0);return{img};
 }
};
class Registry {
 std::vector<std::string> paths={"/system/plugins"}; ReaderWriter reader;
 public: enum LoadStatus{NOT_LOADED,PREVIOUSLY_LOADED,LOADED};
 std::vector<std::string> loaded;
 static Registry* instance(){static Registry r;return &r;}
 std::vector<std::string>& getLibraryFilePathList(){return paths;}
 LoadStatus loadLibrary(const std::string& p){loaded.push_back(p);return !failPlugin.empty()&&p.find(failPlugin)!=std::string::npos?NOT_LOADED:LOADED;}
 ReaderWriter* getReaderWriterForExtension(const std::string& e){reader.extension=e;return e==failReader?nullptr:&reader;}
};
}
'''
HARNESS = r'''
#include "components/resource/portableosg.hpp"
#include <cassert>
int main(int argc,char** argv){
 assert(argc==2);namespace fs=std::filesystem;namespace p=Resource::PortableOsg;
 if(std::string(argv[1])=="--dump-tga"){std::cout<<p::tgaReadFixture();return 0;}
 const fs::path expected(argv[1]); assert(p::portableRoot()==expected);
 const auto paths=p::pluginPaths(expected,"3.6.5");assert(paths.size()==2);
 assert(paths[0]==expected/"lib/osgPlugins-3.6.5");
 assert(paths[1]==expected/"lib/osgPlugins");
 p::configure();p::configure();auto* r=osgDB::Registry::instance();
 assert(r->getLibraryFilePathList().size()==3);
 assert(r->getLibraryFilePathList()[0]==paths[0].string());
 if(std::getenv("EXPECT_TGA_FAILURE")){assert(!p::checkImageCodecs());return 0;}
 assert(p::checkImageCodecs());assert(osgDB::writes==4&&osgDB::reads==5);
 osgDB::failWrite="tga";assert(p::checkImageCodecs());osgDB::failWrite.clear();
 for(const auto& path:r->loaded)assert(path.find("3.6.4")==std::string::npos);
 osgDB::failPlugin="osgdb_png.so";assert(!p::checkImageCodecs());osgDB::failPlugin.clear();
 osgDB::failReader="dds";assert(!p::checkImageCodecs());osgDB::failReader.clear();
 osgDB::failWrite="png";assert(!p::checkImageCodecs());osgDB::failWrite.clear();
 osgDB::failRead="tga";assert(!p::checkImageCodecs());osgDB::failRead.clear();
 fs::remove(paths[0]/"osgdb_png.so");assert(!p::checkImageCodecs());
}
'''

with tempfile.TemporaryDirectory(prefix='arena-y111-') as temp:
    temp = Path(temp)
    headers = temp / 'headers'
    (headers / 'osg').mkdir(parents=True)
    (headers / 'osgDB').mkdir()
    (headers / 'osg/Image').write_text(IMAGE)
    (headers / 'osg/ref_ptr').write_text('#pragma once\n#include <osg/Image>\n')
    (headers / 'osg/Version').write_text('#pragma once\ninline const char* osgGetVersion(){return "3.6.5";}\n')
    (headers / 'osgDB/Registry').write_text(REGISTRY)
    source = temp / 'probe.cpp'; source.write_text(HARNESS)
    binary = temp / 'probe'
    run(['g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(headers), '-I', str(ROOT), str(source), '-o', str(binary)])
    fixture = subprocess.run([str(binary), '--dump-tga'], check=True, stdout=subprocess.PIPE).stdout
    import struct
    assert len(fixture) == 56 and struct.unpack('<HH', fixture[12:16]) == (2, 2)
    assert fixture[2] == 2 and fixture[16] == 24 and fixture[18:30] == bytes([50, 100, 200]) * 4
    assert fixture[-26:-18] == bytes(8) and fixture[-18:] == b'TRUEVISION-XFILE.\x00'
    def bundle(name):
        game = temp / name
        (game / 'resources').mkdir(parents=True)
        for version in ['osgPlugins-3.6.5', 'osgPlugins-3.6.4', 'osgPlugins']:
            (game / 'lib' / version).mkdir(parents=True)
        for name in ['dds', 'png', 'tga', 'jpeg', 'bmp', 'freetype', 'osg', 'serializers_osg']:
            (game / 'lib/osgPlugins-3.6.5' / ('osgdb_' + name + '.so')).write_bytes(b'fixture')
        return game
    game = bundle('Игра $cash `tick` with spaces')
    env = dict(os.environ, ARENAMP_PORTABLE_ROOT=str(game))
    run([str(binary), str(game)], env=env, cwd='/')
    # Direct ELF must discover its own directory even with an inherited wrong root.
    direct = bundle('direct ELF'); shutil.copy2(binary, direct / 'tes3mp.x86_64')
    env['ARENAMP_PORTABLE_ROOT'] = '/definitely/wrong'
    run([str(direct / 'tes3mp.x86_64'), str(direct)], env=env, cwd='/')
    # Regression variant: restore the old TGA writer round-trip. The actual
    # decoder's minimum-footer behavior rejects its 21 bytes, reproducing CI.
    legacy = temp / 'legacy-include/components/resource/portableosg.hpp'
    legacy.parent.mkdir(parents=True)
    production = (ROOT / 'components/resource/portableosg.hpp').read_text()
    predicate = 'if (std::string(extension) == "tga")'
    assert production.count(predicate) == 1
    legacy.write_text(production.replace(predicate, 'if (false)', 1))
    legacy_binary = temp / 'legacy-probe'
    run(['g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(temp / 'legacy-include'),
         '-I', str(headers), '-I', str(ROOT), str(source), '-o', str(legacy_binary)])
    legacy_game = bundle('legacy TGA reproduction')
    result = run([str(legacy_binary), str(legacy_game)], env=dict(os.environ,
                 ARENAMP_PORTABLE_ROOT=str(legacy_game), EXPECT_TGA_FAILURE='1'), cwd='/')
    assert 'FAIL image read: tga:' in result.stderr and 'bytes=21' in result.stderr
    print('PASS Y112 TGA regression: old 21-byte round-trip fails; valid 56-byte 2x2 fixture and reader pass', flush=True)
    print('PASS production C++ header: versioned plugin paths, special paths, direct ELF, idempotence and codec failure paths', flush=True)

    # Runtime workaround: no rewrites of the archive, real argument/environment
    # propagation, explicit refusal for a missing plugin or unresolved dependency.
    game = bundle('runtime helper')
    script = game / 'arena_mp_osg_fix.sh'
    shutil.copyfile(ROOT / 'CI/linux/arena_mp_osg_fix.sh', script)
    tools = temp / 'tools'; tools.mkdir()
    ldd = tools / 'ldd'
    ldd.write_text('#!/bin/bash\nif [[ ${FAIL_DEP:-0} == 1 ]]; then echo "libpng16.so.16 => not found"; else echo "libpng16.so.16 => /bundle/libpng16.so.16"; fi\n')
    ldd.chmod(0o755)
    capture = temp / 'capture'
    (game / 'arena_mp_start.sh').write_text('#!/bin/bash\nprintf "%s\\n" "$OSG_LIBRARY_PATH" "$@" > "$CAPTURE"\n')
    env = dict(os.environ, PATH=str(tools) + ':' + os.environ['PATH'], CAPTURE=str(capture))
    run(['bash', str(script), 'argument with spaces'], env=env, cwd='/')
    assert capture.read_text().splitlines() == [str(game / 'lib/osgPlugins-3.6.5'), 'argument with spaces']
    capture.unlink(); run(['bash', str(script), '--check'], env=env)
    assert not capture.exists()
    failure = subprocess.run(['bash', str(script), '--check'], env=dict(env, FAIL_DEP='1'), capture_output=True, text=True)
    assert failure.returncode == 2 and 'libpng16.so.16 => not found' in failure.stderr
    (game / 'lib/osgPlugins-3.6.5/osgdb_png.so').unlink()
    failure = subprocess.run(['bash', str(script)], env=env, capture_output=True, text=True)
    assert failure.returncode == 2 and 'plugins are missing' in failure.stderr and not capture.exists()
    print('PASS existing-build workaround: scoped OSG path, arguments, check-only, missing plugins and missing dependencies', flush=True)

    # Real shared-object chain: a dynamically loaded plugin needs a codec that
    # in turn needs another DSO. These are not linked into a game executable.
    vendor = temp / 'vendor'; vendor.mkdir()
    base = vendor / 'libarenay111base.so.1'
    codec = vendor / 'libarenay111codec.so.1'
    (temp / 'base.cpp').write_text('extern "C" int base(){return 7;}\n')
    (temp / 'codec.cpp').write_text('extern "C" int base();extern "C" int codec(){return base();}\n')
    (temp / 'plugin.cpp').write_text('extern "C" int codec();extern "C" int plugin(){return codec();}\n')
    run(['g++', '-shared', '-fPIC', str(temp / 'base.cpp'), '-Wl,-soname,libarenay111base.so.1', '-o', str(base)])
    run(['g++', '-shared', '-fPIC', str(temp / 'codec.cpp'), str(base), '-Wl,-soname,libarenay111codec.so.1', '-o', str(codec)])
    plugin = temp / 'osgdb_png.so'
    run(['g++', '-shared', '-fPIC', str(temp / 'plugin.cpp'), str(codec), '-o', str(plugin)])
    spec = importlib.util.spec_from_file_location('arena_deps', ROOT / 'CI/linux/collect-portable-deps.py')
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    module.ldconfig_map = lambda: {base.name: base, codec.name: codec}
    module.SEARCH_DIRS = ()
    pkg = temp / 'closure'; (pkg / 'lib/osgPlugins-3.6.5').mkdir(parents=True)
    shutil.copyfile(plugin, pkg / 'lib/osgPlugins-3.6.5/osgdb_png.so')
    saved_args = sys.argv
    try:
        sys.argv = ['collect-portable-deps.py', str(pkg)]
        with redirect_stdout(io.StringIO()): assert module.main() == 0
        assert (pkg / 'lib' / base.name).read_bytes() == base.read_bytes()
        assert (pkg / 'lib' / codec.name).read_bytes() == codec.read_bytes()
        pkg2 = temp / 'missing-closure'; (pkg2 / 'lib/osgPlugins-3.6.5').mkdir(parents=True)
        shutil.copyfile(plugin, pkg2 / 'lib/osgPlugins-3.6.5/osgdb_png.so')
        module.ldconfig_map = lambda: {codec.name: codec}
        sys.argv = ['collect-portable-deps.py', str(pkg2)]
        errors = io.StringIO()
        with redirect_stderr(errors): assert module.main() == 1
        assert base.name in errors.getvalue()
    finally:
        sys.argv = saved_args
    print('PASS real ELF plugins: complete recursive dependency closure and rejection of an unresolved secondary codec DSO', flush=True)

for name in ['package-portable.sh', 'check-portable-runtime.sh', 'arena_mp_osg_fix.sh']:
    run(['bash', '-n', str(ROOT / 'CI/linux' / name)])
main = (ROOT / 'apps/openmw/main.cpp').read_text()
assert main.index('PortableOsg::configure()') < main.index('Files::ConfigurationManager cfgMgr;', main.index('int runApplication'))
package = (ROOT / 'CI/linux/package-portable.sh').read_text()
assert package.index('collect-portable-deps.py" "$package_dir"') > package.index('required OSG image/runtime plugin missing')
assert package.index('wrap_binary "$bin"') < package.index('"$portable_root/tes3mp.x86_64" --check-image-codecs') < package.index('tar --numeric-owner')
arch = (ROOT / 'CI/linux/check-portable-runtime.sh').read_text()
assert '(cd / && timeout 30s env -u ARENAMP_PORTABLE_ROOT' in arch
assert '--check-image-codecs' in arch
print('PASS Linux shell syntax, image check ordering, missing-plugin guard and clean-Arch direct-ELF check', flush=True)
