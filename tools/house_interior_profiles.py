"""Build trusted housing counts from TES3 plugins in their actual load order.
Usage: python house_interior_profiles.py --encoding cp1251 --output server/data/custom/houseInteriorProfiles.json Morrowind.esm Mod.esp
Run while the server is stopped, then restart it. Does not modify plugins/saves.
"""
import argparse,json,struct
from pathlib import Path

def records(path):
    with path.open('rb') as f:
        while header:=f.read(16):
            if len(header)!=16:raise ValueError(f'Truncated record: {path}')
            tag,size,_,flags=struct.unpack('<4sIII',header)
            data=f.read(size)
            if len(data)!=size:raise ValueError(f'Truncated payload: {path}')
            yield tag,flags,list(subrecords(data))

def subrecords(data):
    pos=0
    while pos<len(data):
        if pos+8>len(data):raise ValueError('Truncated subrecord header')
        tag,size=struct.unpack_from('<4sI',data,pos);pos+=8
        if pos+size>len(data):raise ValueError('Truncated subrecord')
        yield tag,data[pos:pos+size];pos+=size

def build(paths,encoding):
    definitions={};cells={};loaded=set()
    def text(raw):return raw.split(b'\0')[0].decode(encoding).strip()
    for path in paths:
        origin=path.name.lower();masters=[]
        for tag,flags,subs in records(path):
            if tag==b'TES3':
                masters=[text(v).lower() for k,v in subs if k==b'MAST']
                if any(m not in loaded for m in masters):raise ValueError(f'{path}: provide all masters first, in load order')
                continue
            if tag!=b'CELL':
                name=next((text(v).lower() for k,v in subs if k==b'NAME'),None)
                if name:definitions[name]=None if flags&0x20 or any(k==b'DELE' for k,v in subs) else tag
                continue
            name=None;interior=False;ref=None;refid=None;deleted=False
            refs=None
            def flush():
                if ref is not None and refs is not None:
                    if deleted:refs.pop(ref,None)
                    elif refid:refs[ref]=refid
            for k,v in subs:
                if k==b'MVRF':raise ValueError(f'{path}: moved CELL references require a merged plugin; refusing inaccurate counts')
                if k==b'FRMR':
                    flush();num=struct.unpack('<I',v)[0];slot=num>>24
                    if slot>len(masters):raise ValueError('Reference master index out of range')
                    ref=(masters[slot-1] if slot else origin,num&0xffffff);refid=None;deleted=False
                elif k==b'NAME':
                    if ref is None:name=text(v)
                    else:refid=text(v).lower()
                elif k==b'DATA' and ref is None:
                    interior=bool(struct.unpack_from('<I',v)[0]&1)
                    if interior and name:refs=cells.setdefault(name,{})
                elif k==b'DELE' and ref is not None:deleted=True
            flush()
        loaded.add(origin)
    profiles={}
    furnishing=('bed','chair','table','shelf','desk','bench','stool','cupboard','wardrobe','bookcase')
    items={b'CONT',b'ACTI',b'MISC',b'BOOK',b'WEAP',b'ARMO',b'CLOT',b'ALCH',b'APPA',b'INGR',b'LIGH',b'LOCK',b'PROB',b'REPA'}
    for name,refs in cells.items():
        p=dict(objects=0,containers=0,beds=0,doors=0,source='plugins')
        for refid in refs.values():
            kind=definitions.get(refid)
            if kind==b'DOOR':p['doors']+=1
            elif kind in items or (kind==b'STAT' and any(w in refid for w in furnishing)):
                p['objects']+=1
                if kind==b'CONT':p['containers']+=1
                if 'bed' in refid:p['beds']+=1
        profiles[name]=p
    return {'version':1,'plugins':[p.name for p in paths],'profiles':profiles}

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--encoding',default='cp1251')
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('plugins',type=Path,nargs='+')
    args=parser.parse_args()
    result=build(args.plugins,args.encoding)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(f'Wrote {len(result["profiles"])} interior profiles to {args.output}')
