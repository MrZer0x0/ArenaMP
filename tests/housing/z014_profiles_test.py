from pathlib import Path
import importlib.util,struct,tempfile
spec=importlib.util.spec_from_file_location('profiles','tools/house_interior_profiles.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
def sub(k,v):return k+struct.pack('<I',len(v))+v
def rec(k,*parts):
 data=b''.join(parts);return k+struct.pack('<III',len(data),0,0)+data
def name(s):return sub(b'NAME',s.encode()+b'\0')
def ref(n,s,deleted=False):return sub(b'FRMR',struct.pack('<I',n))+name(s)+(sub(b'DELE',b'\0'*4) if deleted else b'')
with tempfile.TemporaryDirectory() as d:
 root=Path(d);a=root/'Base.esm';b=root/'Extra.esp'
 a.write_bytes(rec(b'TES3')+rec(b'DOOR',name('door1'))+rec(b'CONT',name('chest'))+rec(b'STAT',name('bed_01'))+rec(b'NPC_',name('actor'))+rec(b'CELL',name('Test House'),sub(b'DATA',struct.pack('<III',1,0,0)),ref(1,'door1'),ref(2,'chest'),ref(3,'bed_01'),ref(4,'actor')))
 b.write_bytes(rec(b'TES3',sub(b'MAST',b'Base.esm\0'))+rec(b'CELL',name('Test House'),sub(b'DATA',struct.pack('<III',1,0,0)),ref(0x01000002,'chest',True),ref(1,'door1')))
 p=m.build([a],'cp1251')['profiles']['Test House'];assert (p['objects'],p['containers'],p['beds'],p['doors'])==(2,1,1,1)
 p=m.build([a,b],'cp1251')['profiles']['Test House'];assert (p['objects'],p['containers'],p['beds'],p['doors'])==(1,0,1,2)
 try:m.build([b],'cp1251');raise AssertionError('missing master accepted')
 except ValueError:pass
print('PASS Z014: plugin content and doors, master reference overrides/deletions, actors excluded, load order validation')
