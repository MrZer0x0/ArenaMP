#!/usr/bin/env python3
"""Remove only unchanged release fragments already preserved in CHANGELOG.md."""
import argparse
import hashlib
import json
from pathlib import Path

def clean(root, apply=False):
    root=Path(root).resolve()
    manifest=json.loads((root/'docs/maintenance/v24_merged_notes.json').read_text(encoding='utf-8'))
    result={'removed':[], 'would_remove':[], 'modified_kept':[]}
    if not (root/'CHANGELOG.md').is_file():raise ValueError('CHANGELOG.md is required before cleanup')
    history=(root/'CHANGELOG.md').read_text(encoding='utf-8')
    for name,expected in manifest.items():
        candidate=root/name
        p=candidate.resolve()
        if not p.is_relative_to(root) or p.suffix!='.md' or name=='CHANGELOG.md':
            raise ValueError('Unsafe history path: '+name)
        if not p.exists():continue
        if candidate.is_symlink() or hashlib.sha256(p.read_bytes()).hexdigest()!=expected:
            result['modified_kept'].append(name);continue
        if name not in history:raise ValueError('History is missing source: '+name)
        if apply:p.unlink();result['removed'].append(name)
        else:result['would_remove'].append(name)
    return result

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply',action='store_true',help='Delete unchanged merged notes; otherwise preview only')
    args=parser.parse_args()
    print(json.dumps(clean(Path(__file__).resolve().parents[1],args.apply),ensure_ascii=False,indent=2))
