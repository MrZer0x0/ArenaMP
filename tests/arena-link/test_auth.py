#!/usr/bin/env python3
"""Verify native HMAC against Python hashlib and actual TES3MP derivation.
Run from full repo after overlay, or pass --picosha /path/to/picosha2.h.
All passwords in this test are synthetic.
"""
from pathlib import Path
import argparse, hashlib, hmac, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(); parser.add_argument('--picosha',type=Path,default=ROOT/'extern/PicoSHA2/picosha2.h');args=parser.parse_args()
def hx(value): return hashlib.sha256(value.encode()).hexdigest()
def verifier(password,salt):
    first=hx(password)
    return hx(hx(first+hx(hx(first)))+salt)
with tempfile.TemporaryDirectory() as tmp:
    d=Path(tmp); include=d/'extern/PicoSHA2';include.mkdir(parents=True)
    (include/'picosha2.h').write_bytes(args.picosha.read_bytes())
    cpp=d/'auth.cpp'
    cpp.write_text('''#include <components/openmw-mp/arenalinkauth.hpp>
#include <iostream>
int main() {
    std::string key, nonce, proof;
    std::getline(std::cin,key); std::getline(std::cin,nonce); std::getline(std::cin,proof);
    std::string bytes;
    for (size_t i=0;i<proof.size();i+=2) bytes.push_back(static_cast<char>(std::stoi(proof.substr(i,2),nullptr,16)));
    std::cout << ArenaLink::verifyStoredProof(key,nonce,bytes);
}''')
    exe=d/'auth';subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-I',str(ROOT),'-I',str(d),str(cpp),'-o',str(exe)],check=True)
    cases=0
    for password,salt in [('test-pass','a'*64),('Сложный пароль=42','b'*64),('','c'*64)]:
        key=verifier(password,salt); nonce='0123456789ABCDEF'
        proof=hmac.new(bytes.fromhex(key),nonce.encode(),hashlib.sha256).hexdigest()
        for checked,expected in [(proof,'1'),('00'*32,'0'),(proof[:-2],'0')]:
            result=subprocess.run([str(exe)],input=key+'\n'+nonce+'\n'+checked+'\n',text=True,capture_output=True,check=True)
            assert result.stdout==expected, (expected,result.stdout)
            cases+=1
        wrong=verifier(password+'wrong',salt)
        bad=hmac.new(bytes.fromhex(wrong),nonce.encode(),hashlib.sha256).hexdigest()
        result=subprocess.run([str(exe)],input=key+'\n'+nonce+'\n'+bad+'\n',text=True,capture_output=True,check=True)
        assert result.stdout=='0';cases+=1
    print(f'Native HMAC / TES3MP derivation: {cases} checks PASS')
