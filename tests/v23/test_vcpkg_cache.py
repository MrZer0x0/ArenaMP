#!/usr/bin/env python3
import importlib.util,tempfile,unittest,shutil
from pathlib import Path
import yaml
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('cache_id',ROOT/'CI/vcpkg_cache_identity.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class CacheTests(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.p=Path(self.tmp.name);self.root=self.p/'vcpkg';self.compiler=self.p/'msvc/cl.exe'
  for name in ['vcpkg.exe','scripts/vcpkgTools.xml','triplets/x64-windows.cmake','ports/openssl/portfile.cmake','ports/openssl/vcpkg.json','scripts/cmake/example.cmake']:
   p=self.root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(name)
  self.compiler.parent.mkdir();self.compiler.write_text('compiler');(self.compiler.parent/'c2.dll').write_text('backend')
 def tearDown(self):self.tmp.cleanup()
 def key(self,sdk='10.0.26100.0'):return m.identity(self.root,self.compiler,sdk,'14.44')
 def test_game_changes_do_not_invalidate(self):
  k=self.key();(self.p/'game.cpp').write_text('new game source');self.assertEqual(self.key(),k)
 def test_openssl_and_toolchain_invalidate(self):
  k=self.key();p=self.root/'ports/openssl/portfile.cmake';p.write_text('new port');self.assertNotEqual(self.key(),k)
  k=self.key();(self.compiler.parent/'c2.dll').write_text('new backend');self.assertNotEqual(self.key(),k)
  self.assertNotEqual(self.key('10.0.99999.0'),self.key())
 def test_location_does_not_invalidate(self):
  clone=self.p/'other-vcpkg';shutil.copytree(self.root,clone)
  cc=self.p/'other-msvc';shutil.copytree(self.compiler.parent,cc)
  self.assertEqual(self.key(),m.identity(clone,cc/'cl.exe','10.0.26100.0','14.44'))
 def test_missing_input_fails(self):
  (self.root/'vcpkg.exe').unlink()
  with self.assertRaises(ValueError):self.key()
 def test_workflow_save_before_app_build(self):
  d=yaml.load((ROOT/'.github/workflows/release-builds.yml').read_text(),Loader=yaml.BaseLoader)
  steps=d['jobs']['windows-x64']['steps'];names=[s.get('name') for s in steps]
  restore=next(s for s in steps if s.get('id')=='vcpkg-binary-cache')
  install=next(s for s in steps if s.get('id')=='publisher-crypto')
  save=next(s for s in steps if s.get('name')=='Save compiled Windows crypto packages')
  self.assertEqual(restore['uses'],'actions/cache/restore@v5');self.assertEqual(save['uses'],'actions/cache/save@v5')
  self.assertEqual(restore['with']['key'],save['with']['key'])
  self.assertNotIn('github.sha',restore['with']['key']);self.assertIn('always()',save['if'])
  self.assertIn("steps.publisher-crypto.outcome == 'success'",save['if'])
  self.assertLess(names.index(save['name']),names.index('Configure dependencies and project'))
  self.assertIn('VCPKG_BINARY_SOURCES',install['run']);self.assertIn('VCPKG_ASSET_SOURCES',install['run'])
  self.assertIn("{ 'write' } else { 'readwrite' }",install['run'])
if __name__=='__main__':unittest.main()
