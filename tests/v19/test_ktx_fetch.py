#!/usr/bin/env python3
"""Exercise the real CMake downloader against a local HTTP server; no GitHub requests."""
import io,tarfile,tempfile,subprocess,threading,sys,os,unittest,shutil
from pathlib import Path
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
ROOT=Path(__file__).resolve().parents[2]
CMAKE=os.environ.get('CMAKE_COMMAND') or shutil.which('cmake')
class FetchTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  if not CMAKE:raise unittest.SkipTest('cmake is required')
  f=io.BytesIO()
  with tarfile.open(fileobj=f,mode='w:gz') as t:
   b=b'pinned test source';r=tarfile.TarInfo('ktx-test/lib/texture2.c');r.size=len(b);t.addfile(r,io.BytesIO(b))
  cls.archive=f.getvalue();cls.calls=[]
  class Handler(BaseHTTPRequestHandler):
   def log_message(self,*args):pass
   def do_GET(self):
    cls.calls.append(self.path)
    status=200;body=cls.archive
    if self.path=='/503':status=503;body=b'unavailable'
    elif self.path=='/corrupt':body=b'not an archive'
    elif self.path=='/retry' and cls.calls.count('/retry')<2:status=503;body=b'temporary'
    self.send_response(status);self.end_headers();self.wfile.write(body)
  cls.server=ThreadingHTTPServer(('127.0.0.1',0),Handler)
  cls.thread=threading.Thread(target=cls.server.serve_forever,daemon=True);cls.thread.start()
 @classmethod
 def tearDownClass(cls):cls.server.shutdown();cls.server.server_close();cls.thread.join()
 def setUp(self):self.tmp=tempfile.TemporaryDirectory();self.path=Path(self.tmp.name);self.calls.clear()
 def tearDown(self):self.tmp.cleanup()
 def fetch(self,*paths,success=True):
  urls=' '.join('"http://127.0.0.1:%d%s"'%(self.server.server_port,p) for p in paths)
  script=self.path/'fetch.cmake';archive=self.path/'cache/archive.tar.gz'
  script.write_text('include("'+(ROOT/'cmake/ArenaFetchArchive.cmake').as_posix()+'")\narena_fetch_archive(result "'+archive.as_posix()+'" '+urls+')\n')
  result=subprocess.run([CMAKE,'-P',str(script)],capture_output=True,text=True,timeout=30)
  self.assertEqual(result.returncode==0,success,result.stdout+result.stderr)
  self.assertFalse(Path(str(archive)+'.part').exists())
  return archive,result
 def test_mirror_fallback_and_cache(self):
  archive,_=self.fetch('/503','/ok');self.assertEqual(self.calls,['/503','/ok']);self.assertEqual(archive.read_bytes(),self.archive)
  self.calls.clear();self.fetch('/503');self.assertEqual(self.calls,[])
 def test_retry_same_revision(self):
  self.fetch('/retry');self.assertEqual(self.calls,['/retry','/retry'])
 def test_invalid_download_and_invalid_cache(self):
  a=self.path/'cache/archive.tar.gz';a.parent.mkdir();a.write_bytes(b'broken cache')
  self.fetch('/corrupt','/ok');self.assertEqual(a.read_bytes(),self.archive)
 def test_persistent_outage_fails(self):
  archive,result=self.fetch('/503',success=False)
  self.assertEqual(self.calls,['/503']*3);self.assertFalse(archive.exists())
  self.assertIn('Source archive unavailable after retries',result.stderr)
 def test_real_module_offline_and_overrides(self):
  # Configure ArenaKtx.cmake itself with harmless source stubs; do not compile a decoder.
  import re
  module=(ROOT/'cmake/ArenaKtx.cmake').read_text()
  files=re.search(r'set\(_arena_ktx_sources\s+(.*?)\)',module,re.S)[1].split()
  src=self.path/'ktx'
  for name in files+['include/ktx.h','LICENSE.md','NOTICE.md','LICENSES/test.txt']:
   p=src/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('')
  project=self.path/'project';project.mkdir()
  (project/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.18)\nproject(FetchTest C CXX)\ninclude("'+(ROOT/'cmake/ArenaKtx.cmake').as_posix()+'")\n')
  for i,var in enumerate(['ARENA_KTX_SOURCE_DIR','FETCHCONTENT_SOURCE_DIR_ARENA_KTX']):
   p=subprocess.run([CMAKE,'-S',str(project),'-B',str(self.path/('build'+str(i))),'-D'+var+'='+str(src)],capture_output=True,text=True,timeout=30)
   self.assertEqual(p.returncode,0,p.stdout+p.stderr)
  build=self.path/'offline';cached=build/'_deps/arena_ktx-src';shutil.copytree(src,cached)
  p=subprocess.run([CMAKE,'-S',str(project),'-B',str(build),'-DFETCHCONTENT_FULLY_DISCONNECTED=ON'],capture_output=True,text=True,timeout=30)
  self.assertEqual(p.returncode,0,p.stdout+p.stderr)
  # Exercise the normal FetchContent branch using its already-downloaded cache.
  build=self.path/'from-archive';cache=build/'extern/fetched/ktx/91ace88675ac59a97e55d0378a6602a9ae6b98bd.tar.gz'
  cache.parent.mkdir(parents=True)
  with tarfile.open(cache,'w:gz') as t:t.add(src,arcname='ktx-pinned')
  p=subprocess.run([CMAKE,'-S',str(project),'-B',str(build)],capture_output=True,text=True,timeout=30)
  self.assertEqual(p.returncode,0,p.stdout+p.stderr)
  self.assertTrue((build/'_deps/arena_ktx-src/lib/texture2.c').exists())
  self.assertEqual(self.calls,[])
if __name__=='__main__':unittest.main()
