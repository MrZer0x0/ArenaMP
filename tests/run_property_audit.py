#!/usr/bin/env python3
"""Run property/shop/server regressions in isolated processes (Python, Lua 5.4, g++)."""
from pathlib import Path
import subprocess, sys
root=Path(__file__).resolve().parents[1]
failed=[]
tests=[]
for folder in ('housing','server','server_features'):
 for p in sorted((root/'tests'/folder).iterdir()):
  if p.suffix in ('.lua','.py') and p.name!='run_lua.py':tests.append(p)
for p in tests:
 rel=p.relative_to(root)
 command=[sys.executable,str(rel)]
 if p.suffix=='.lua':command=[sys.executable,'tests/housing/run_lua.py',str(rel)]
 r=subprocess.run(command,cwd=root,capture_output=True,text=True)
 print(('PASS ' if r.returncode==0 else 'FAIL ')+str(rel))
 if r.returncode:
  failed.append(str(rel));print(r.stdout+r.stderr)
print(f'{len(tests)-len(failed)}/{len(tests)} regression files passed')
sys.exit(bool(failed))
