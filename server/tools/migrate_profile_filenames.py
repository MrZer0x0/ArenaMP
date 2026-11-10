#!/usr/bin/env python3
"""Rename legacy _utf8_ JSON accounts. Run with the game server STOPPED.
Default: inspect only. --apply: back up sources, then rename without overwrite.
Only filenames change; profile bytes, passwords and character data stay intact.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import tempfile

RESERVED={"CON","PRN","AUX","NUL"}|{f"{p}{i}" for p in ("COM","LPT") for i in range(1,10)}

def filename(name, legacy=False):
    name=name.strip().replace(":",";")
    name=re.sub(r'[<>"/\\|*?\r\n]',"_",name).replace(".",",")
    if not legacy: name=re.sub(r'[\x00-\x1f]',"_",name)
    if (name if legacy else name.upper()) in RESERVED: name="_"+name
    if not name or len(name.encode("utf-8"))>240: raise ValueError("invalid or oversized account name")
    return name

def plan(directory):
    moves=[];errors=[]
    occupied={p.name.casefold() for p in directory.iterdir()}
    for path in sorted(directory.glob("_utf8_*.json")):
        try:
            if path.is_symlink() or not path.is_file(): raise ValueError("not a regular account file")
            if path.stat().st_size>64*1024*1024: raise ValueError("oversized profile")
            data=json.loads(path.read_text(encoding="utf-8-sig"))
            name=data.get("login",{}).get("name")
            if not isinstance(name,str): raise ValueError("login.name missing")
            legacy=filename(name,True)
            expected={"_utf8_"+hashlib.sha256(legacy.encode()).hexdigest(),"_utf8_"+legacy.encode().hex()}
            if path.stem.casefold() not in {v.casefold() for v in expected}:
                raise ValueError("filename hash differs from login.name; inspect manually")
            target=directory/(filename(name)+".json")
            if target.name.casefold() in occupied: raise ValueError("named profile already exists or another source has the same name")
            occupied.add(target.name.casefold());moves.append((path,target))
            backup=Path(str(path)+".bak")
            if backup.exists():
                destination=Path(str(target)+".bak")
                if backup.is_symlink() or destination.name.casefold() in occupied: raise ValueError("backup conflict")
                occupied.add(destination.name.casefold());moves.append((backup,destination))
        except (OSError,ValueError,TypeError,AttributeError) as exc:
            errors.append(f"{path.name}: {exc}")
    return moves,errors

def rename(source,target):
    if os.name=="nt": os.rename(source,target) # Windows refuses an existing target.
    else:
        os.link(source,target) # atomic, refuses an existing target
        source.unlink()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("player_directory",type=Path)
    parser.add_argument("--apply",action="store_true",help="apply after stopping the server")
    args=parser.parse_args();directory=args.player_directory.resolve()
    if not directory.is_dir(): parser.error("player directory does not exist")
    moves,errors=plan(directory)
    for source,target in moves: print(f"{source.name} -> {target.name}")
    for error in errors: print("ERROR: "+error)
    if errors: print("No files changed; resolve all conflicts first.");return 2
    if not args.apply: print(f"Dry run: {len(moves)} file(s). Use --apply with the server stopped.");return 0
    if not moves: print("No legacy profiles to migrate.");return 0
    saved=Path(tempfile.mkdtemp(prefix="profile-filename-backup-",dir=directory.parent))
    # Complete the entire backup before the first mutation; never rewrite JSON.
    for source,_ in moves: shutil.copy2(source,saved/source.name)
    print("Backup: "+str(saved))
    for source,target in moves: rename(source,target)
    print(f"Migrated {len(moves)} file(s).");return 0

if __name__=="__main__": raise SystemExit(main())
