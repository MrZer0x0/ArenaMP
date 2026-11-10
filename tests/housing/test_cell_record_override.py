#!/usr/bin/env python3
"""Exercise production Cell store methods with lightweight ESM dependencies.
Run with Python 3 and a C++17 compiler; UBSan checks the repeated override path.
"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'apps/openmw/mwworld/store.cpp').read_text()

def function(signature):
    begin = source.index(signature)
    brace = source.index('{', begin)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[begin:end]

methods = '\n'.join(function(signature) for signature in [
    'const ESM::Cell *Store<ESM::Cell>::search(const ESM::Cell &cell)',
    'const ESM::Cell *Store<ESM::Cell>::search(const std::string &id)',
    'const ESM::Cell *Store<ESM::Cell>::search(int x, int y)',
    'ESM::Cell *Store<ESM::Cell>::insert(const ESM::Cell &cell)',
    'ESM::Cell *Store<ESM::Cell>::override(const ESM::Cell &cell)',
])
# Optional original implementation for reproducing the pre-fix failure.
old = os.environ.get('CELL_OVERRIDE_OLD')
if old:
    methods = methods.replace(function('ESM::Cell *Store<ESM::Cell>::override(const ESM::Cell &cell)'), Path(old).read_text())

harness = r'''
#include <algorithm>
#include <cassert>
#include <cctype>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
namespace Misc { namespace StringUtils {
std::string lowerCase(std::string s) { for(char& c:s)c=std::tolower(static_cast<unsigned char>(c));return s; }
bool ciEqual(const std::string& a,const std::string& b){return lowerCase(a)==lowerCase(b);}
}}
namespace ESM { struct Cell {
 std::string mName; struct {int mX=0,mY=0;} mData;
 bool exterior=false; int payload=0;
 bool isExterior() const{return exterior;}
 int getGridX() const{return mData.mX;} int getGridY() const{return mData.mY;}
}; }
template<class T> class Store;
template<> class Store<ESM::Cell> {
public:
 using DynamicInt=std::map<std::string,ESM::Cell>;
 using DynamicExt=std::map<std::pair<int,int>,ESM::Cell>;
 DynamicInt mInt,mDynamicInt; DynamicExt mExt,mDynamicExt;
 std::vector<ESM::Cell*> mSharedInt,mSharedExt;
 const ESM::Cell* search(const ESM::Cell&) const;
 const ESM::Cell* search(const std::string&) const;
 const ESM::Cell* search(int,int) const;
 ESM::Cell* insert(const ESM::Cell&);
 ESM::Cell* override(const ESM::Cell&);
};
METHODS
int main(){
 for(bool exterior:{false,true}) for(bool content:{false,true}) {
   Store<ESM::Cell> store; ESM::Cell base;
   base.mName="Ebonheart, Manor Jorrvaskr - home"; base.exterior=exterior;
   base.mData.mX=2; base.mData.mY=-13;
   if(content) {
     if(exterior){store.mExt[{2,-13}]=base;store.mSharedExt.push_back(&store.mExt.at({2,-13}));}
     else {store.mInt[Misc::StringUtils::lowerCase(base.mName)]=base;store.mSharedInt.push_back(&store.mInt.begin()->second);}
   } else store.override(base);
   const auto* stable=store.search(base);
   auto& shared=exterior?store.mSharedExt:store.mSharedInt;
   for(int n=1;n<=1000;++n) {
     { ESM::Cell local=base;local.payload=n; if(!exterior && n%2) local.mName=Misc::StringUtils::lowerCase(local.mName);
       assert(store.override(local)==stable); }
     assert(store.search(base)==stable && stable->payload==n);
     assert(shared.size()==1 && shared.front()==stable && shared.front()->payload==n);
   }
   assert(store.override(*stable)==stable); // self-alias is valid
 }
 std::cout<<"PASS: 4000 repeated interior/exterior overrides, static/dynamic records, stable pointers, case and self-alias\n";
}
'''.replace('METHODS', methods)
with tempfile.TemporaryDirectory() as temp:
    cpp=Path(temp)/'test.cpp';binary=Path(temp)/'test';cpp.write_text(harness)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-O2','-Wall','-Wextra',
        '-fsanitize=undefined','-fno-sanitize-recover=all',str(cpp),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
