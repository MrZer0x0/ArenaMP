import importlib.util
import io
from pathlib import Path
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('portable_archive', ROOT / 'CI/linux/portable-archive.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

class PortableTests(unittest.TestCase):
    def archive(self, path, links, extra=None):
        with tarfile.open(path, 'w:gz') as out:
            root = tarfile.TarInfo('./'); root.type = tarfile.DIRTYPE; out.addfile(root)
            lib = tarfile.TarInfo('./lib'); lib.type = tarfile.DIRTYPE; out.addfile(lib)
            data = tarfile.TarInfo('./lib/libtinfo.so.6.3'); data.size = 7; data.mode = 0o755
            out.addfile(data, io.BytesIO(b'ELFtest'))
            for name, target, kind in links:
                item = tarfile.TarInfo(name);item.linkname = target;item.type = kind;out.addfile(item)
            if extra:
                out.addfile(extra)

    def test_old_absolute_alias_relative_chain_and_hardlink(self):
        with tempfile.TemporaryDirectory() as tmp:
            src, dst = Path(tmp)/'old.tar.gz',Path(tmp)/'fixed.tar.gz'
            self.archive(src,[('lib/libtinfo.so','/lib/x86_64-linux-gnu/libtinfo.so.6',tarfile.SYMTYPE),
                              ('lib/libtinfo.so.6','libtinfo.so.6.3',tarfile.SYMTYPE),
                              ('lib/libalias.so','./lib/libtinfo.so.6.3',tarfile.LNKTYPE)])
            before = src.read_bytes();m.repair(src,dst);self.assertEqual(before,src.read_bytes())
            with tarfile.open(dst) as fixed:
                for name in ['lib/libtinfo.so','lib/libtinfo.so.6','lib/libalias.so']:
                    member=fixed.getmember(name);self.assertTrue(member.isfile())
                    self.assertEqual(fixed.extractfile(member).read(),b'ELFtest')
                    self.assertEqual(member.mode,0o755)
            with self.assertRaises(ValueError):m.repair(src,dst)

    def test_reject_unsafe_missing_cyclic_links_without_output(self):
        for name,target in [('lib/x','/etc/passwd'),('lib/x','../../etc/passwd'),
                            ('lib/x','/lib/libmissing.so.9'),('lib/x','x'),
                            ('../escape','libtinfo.so.6.3')]:
            with self.subTest(target=target),tempfile.TemporaryDirectory() as tmp:
                src,dst=Path(tmp)/'a.tar.gz',Path(tmp)/'b.tar.gz'
                self.archive(src,[(name,target,tarfile.SYMTYPE)])
                with self.assertRaises(ValueError):m.repair(src,dst)
                self.assertFalse(dst.exists())

    def test_reject_link_parent(self):
        with tempfile.TemporaryDirectory() as tmp:
            src,dst=Path(tmp)/'a.tar.gz',Path(tmp)/'b.tar.gz'
            extra=tarfile.TarInfo('lib/alias/child');extra.size=0
            self.archive(src,[('lib/alias','libtinfo.so.6.3',tarfile.SYMTYPE)],extra)
            with self.assertRaises(ValueError):m.repair(src,dst)

    def test_materialize_does_not_modify_host_library(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)/'package';(root/'lib').mkdir(parents=True)
            host=Path(tmp)/'host.so';host.write_bytes(b'host');host.chmod(0o755)
            alias=root/'lib/libtinfo.so';alias.symlink_to(host)
            with self.assertRaises(ValueError):m.check(root)
            m.materialize(root);m.check(root)
            self.assertFalse(alias.is_symlink());self.assertEqual(alias.read_bytes(),b'host')
            alias.write_bytes(b'patched');self.assertEqual(host.read_bytes(),b'host')
            m.materialize(root)

    def test_directory_alias_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)/'package';root.mkdir();(root/'lib').symlink_to(Path(tmp),target_is_directory=True)
            with self.assertRaises(ValueError):m.materialize(root)

if __name__ == '__main__':unittest.main()
