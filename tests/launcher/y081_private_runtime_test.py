"""Build real ELF fixtures: recursive $ORIGIN dependencies survive relocation."""
from pathlib import Path
import importlib.util
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('collector', root/'CI/linux/collect-portable-deps.py')
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)
def run(*args):
    subprocess.run(args, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

with tempfile.TemporaryDirectory(prefix='arenamp-private-') as tmp:
    base = Path(tmp)
    host = base/'host'
    private = host/'private'
    nested = private/'nested'
    nested.mkdir(parents=True)
    pkg = base/'package'
    (pkg/'lib').mkdir(parents=True)
    def shared(name, folder, body, *flags):
        src = base/(name+'.c')
        src.write_text(body)
        run('gcc', '-shared', '-fPIC', str(src), '-o', str(folder/name),
            '-Wl,-soname,'+name, *flags)
    shared('libleaf.so.1', nested, 'int leaf(void) {return 42;}')
    shared('libprivate.so.1', private, 'extern int leaf(void); int helper(void) {return leaf();}',
           '-L'+str(nested), '-l:libleaf.so.1', '-Wl,-rpath,$ORIGIN/nested')
    shared('libpublic.so.1', host, 'extern int helper(void); int api(void) {return helper();}',
           '-L'+str(private), '-l:libprivate.so.1', '-Wl,-rpath,$ORIGIN/private')
    shutil.copy2(host/'libpublic.so.1', pkg/'lib/libpublic.so.1')
    c.ldconfig_map = lambda: {'libpublic.so.1':host/'libpublic.so.1'}
    c.SEARCH_DIRS = () # No global search can hide a broken provenance chain.
    sys.argv = ['collector',str(pkg)]
    assert c.main() == 0
    assert (pkg/'lib/libprivate.so.1').read_bytes() == (private/'libprivate.so.1').read_bytes()
    assert (pkg/'lib/libleaf.so.1').read_bytes() == (nested/'libleaf.so.1').read_bytes()
    assert c.main() == 0
    # Missing dependency must stop packaging, not silently use another ABI.
    (pkg/'lib/libleaf.so.1').unlink()
    (nested/'libleaf.so.1').unlink()
    assert c.main() == 1
print('PASS: real ELF private dependency chain, idempotence, missing dependency')
