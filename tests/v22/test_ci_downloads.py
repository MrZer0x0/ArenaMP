#!/usr/bin/env python3
"""Local network/provider tests and Android cached-source parity regression."""
import hashlib,importlib.util,io,os,subprocess,sys,tempfile,threading,unittest
from pathlib import Path
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('arena_assets',ROOT/'CI/vcpkg_asset_download.py')
assets=importlib.util.module_from_spec(spec);spec.loader.exec_module(assets)
class AssetTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.payload=b'pinned CMake fixture';cls.sha=hashlib.sha512(cls.payload).hexdigest();cls.calls=[]
  class Handler(BaseHTTPRequestHandler):
   def log_message(self,*args):pass
   def do_GET(self):
    cls.calls.append(self.path)
    if self.path=='/fail' or self.path=='/retry' and cls.calls.count('/retry')==1:
     self.send_response(504);self.end_headers();self.wfile.write(b'gateway timeout');return
    self.send_response(200);self.end_headers();self.wfile.write(b'wrong asset' if self.path=='/wrong' else cls.payload)
  cls.server=ThreadingHTTPServer(('127.0.0.1',0),Handler);cls.thread=threading.Thread(target=cls.server.serve_forever,daemon=True);cls.thread.start()
 @classmethod
 def tearDownClass(cls):cls.server.shutdown();cls.server.server_close();cls.thread.join()
 def setUp(self):self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name);self.calls.clear()
 def tearDown(self):self.tmp.cleanup()
 def url(self,suffix):return f'http://127.0.0.1:{self.server.server_port}{suffix}'
 def get(self,paths,**kw):
  assets.acquire([self.url(p) for p in paths],self.sha,self.root/'out/tool.zip',self.root/'cache',pause=lambda _:None,**kw)
 def test_mirror_is_same_version(self):
  origin='https://github.com/Kitware/CMake/releases/download/v4.4.4/cmake-4.4.4-windows-x86_64.zip'
  self.assertEqual(assets.candidates(origin),['https://cmake.org/files/v4.4/cmake-4.4.4-windows-x86_64.zip',origin])
  for url in ['https://evil.test/Kitware/CMake/releases/download/v4.4.4/cmake-4.4.4.zip',origin+'?token=x',origin.replace('cmake-4.4.4','cmake-4.3.0'),'https://github.com/openssl/openssl/archive/x.zip']:
   self.assertEqual(assets.candidates(url),[])
 def test_504_fallback_then_cache(self):
  self.get(['/fail','/good']);self.assertEqual(self.calls,['/fail','/good'])
  (self.root/'out/tool.zip').unlink();self.calls.clear();self.get(['/fail']);self.assertEqual(self.calls,[])
  self.assertEqual((self.root/'out/tool.zip').read_bytes(),self.payload)
 def test_retry(self):
  self.get(['/retry']);self.assertEqual(self.calls,['/retry','/retry'])
 def test_bad_hash_never_installed(self):
  with self.assertRaises(RuntimeError):self.get(['/wrong'])
  self.assertFalse((self.root/'out/tool.zip').exists());self.assertFalse((self.root/'cache'/self.sha).exists())
  self.assertEqual(list((self.root/'cache').glob('.asset-*')),[])
 def test_corrupt_cache_recovered(self):
  (self.root/'cache').mkdir();(self.root/'cache'/self.sha).write_bytes(b'bad')
  self.get(['/good']);self.assertEqual((self.root/'out/tool.zip').read_bytes(),self.payload)
 def test_invalid_checksum_rejected(self):
  with self.assertRaises(ValueError):assets.acquire([self.url('/good')],'invalid',self.root/'out',self.root/'cache')
  self.assertEqual(self.calls,[])
 def test_provider_cli_with_cached_asset_and_spaced_path(self):
  cache=self.root/'cache';cache.mkdir();(cache/self.sha).write_bytes(self.payload)
  output=self.root/'folder with spaces/tool.zip'
  env=dict(os.environ,ARENAMP_VCPKG_ASSET_CACHE=str(cache))
  p=subprocess.run([sys.executable,str(ROOT/'CI/vcpkg_asset_download.py'),'--url','https://github.com/Kitware/CMake/releases/download/v4.4.4/cmake-4.4.4-windows-x86_64.zip','--sha512',self.sha,'--output',str(output)],env=env,capture_output=True,text=True)
  self.assertEqual(p.returncode,0,p.stderr);self.assertEqual(output.read_bytes(),self.payload)
  self.assertEqual(self.calls,[])
 def test_permanent_failure_does_not_skip_dependency(self):
  with self.assertRaisesRegex(RuntimeError,'HTTP 504'):self.get(['/fail'])
  self.assertEqual(self.calls,['/fail']*3);self.assertFalse((self.root/'out/tool.zip').exists())
@unittest.skipUnless((ROOT/'android').is_dir(), 'Windows-only source tree has no mobile staging')
class AndroidParityTests(unittest.TestCase):
 def test_cached_dependency_file_repaired_and_verified(self):
  patcher=ROOT/'android/buildscripts/patches/openmw/31-arenamp-y092-ui-ai-progression-enchant.py'
  with tempfile.TemporaryDirectory(prefix='arena-parity-') as d:
   stage=Path(d)
   subprocess.run([sys.executable,str(patcher),str(stage)],check=True,stdout=subprocess.DEVNULL)
   target=stage/'extern/CMakeLists.txt'
   self.assertIn('arena_fetch_archive(_arena_recastnavigation_archive',target.read_text())
   target.write_text('# stale cached direct URL\n')
   p=subprocess.run([sys.executable,str(patcher),str(stage),'--verify-only'],capture_output=True)
   self.assertNotEqual(p.returncode,0)
   subprocess.run([sys.executable,str(patcher),str(stage)],check=True,stdout=subprocess.DEVNULL)
   self.assertEqual(target.read_bytes(),(ROOT/'extern/CMakeLists.txt').read_bytes())
   subprocess.run([sys.executable,str(patcher),str(stage),'--verify-only'],check=True,stdout=subprocess.DEVNULL)
   for name in ['ArenaFetchArchive.cmake','ArenaKtx.cmake','ArenaBundledDependencies.cmake']:
    self.assertEqual((stage/'cmake'/name).read_bytes(),(ROOT/'cmake'/name).read_bytes())
if __name__=='__main__':unittest.main()
