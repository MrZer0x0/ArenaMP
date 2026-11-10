#!/usr/bin/env python3
"""Compile the actual H019b TES3 hold-E classifying branches with minimal mocks.

The full game client still needs an engine build and hands-on regression tests.
Run from any directory: python3 tests/housing/h019b_hold_e_test.py
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
input_cpp = (root / 'apps/openmw/mwinput/actionmanager.cpp').read_text(encoding='utf8')
world_cpp = (root / 'apps/openmw/mwworld/worldimp.cpp').read_text(encoding='utf8')


def extract(source, signature):
    begin = source.index(signature)
    brace = source.index('{', begin)
    depth = 0
    for pos in range(brace, len(source)):
        if source[pos] == '{':
            depth += 1
        elif source[pos] == '}':
            depth -= 1
            if depth == 0:
                return source[begin:pos + 1]
    raise ValueError('Function body not complete: ' + signature)


menu = extract(input_cpp, 'bool opensFurnitureActionMenu(const MWWorld::ConstPtr& object)')
creative = extract(world_cpp, 'bool isCreativePlacementRecord(const MWWorld::ConstPtr& object)')
assert 'showsInInventory' not in menu, 'hold-E menu must not use inventory visibility'
assert 'opensFurnitureActionMenu(target)' in input_cpp, 'hold-E dispatch must use the classifier'
assert 'isCreativePlacementRecord(object)' in world_cpp, 'world-grab permission must use the fixture classifier'

mock_header = r'''
#include <cassert>
#include <string>
#include <typeinfo>
namespace ESM {
struct Static {};
struct Container {};
struct Activator {};
struct Weapon {};
struct Potion {};
struct Armor {};
struct Clothing {};
struct Miscellaneous {};
struct Light {
    enum Flags { Carry = 0x002 };
    struct Data { int mFlags; } mData;
};
}
struct LightRef { const ESM::Light* mBase; };
namespace MWWorld {
class ConstPtr {
public:
    ConstPtr(): empty(true), type(), light(nullptr), ref{nullptr} {}
    explicit ConstPtr(const std::string& name, const ESM::Light* base=nullptr)
        : empty(false), type(name), light(base), ref{base} {}
    bool isEmpty() const { return empty; }
    const std::string& getTypeName() const { return type; }
    template<class T> const LightRef* get() const { return &ref; }
private:
    bool empty;
    std::string type;
    const ESM::Light* light;
    LightRef ref;
};
}
'''

main = r'''
int main() {
    auto verify = [](const MWWorld::ConstPtr& o, bool menuExpected) {
        assert(opensFurnitureActionMenu(o) == menuExpected);
        assert(isCreativePlacementRecord(o) == menuExpected);
    };
    verify(MWWorld::ConstPtr(), false);
    verify(MWWorld::ConstPtr(typeid(ESM::Static).name()), true);
    verify(MWWorld::ConstPtr(typeid(ESM::Container).name()), true);
    verify(MWWorld::ConstPtr(typeid(ESM::Activator).name()), true);
    const ESM::Light fixture{{0}};
    const ESM::Light carryable{{ESM::Light::Carry}};
    verify(MWWorld::ConstPtr(typeid(ESM::Light).name(), &fixture), true);
    verify(MWWorld::ConstPtr(typeid(ESM::Light).name(), &carryable), false);
    verify(MWWorld::ConstPtr(typeid(ESM::Light).name()), false);
    verify(MWWorld::ConstPtr(typeid(ESM::Weapon).name()), false);
    verify(MWWorld::ConstPtr(typeid(ESM::Potion).name()), false);
    verify(MWWorld::ConstPtr(typeid(ESM::Armor).name()), false);
    verify(MWWorld::ConstPtr(typeid(ESM::Clothing).name()), false);
    verify(MWWorld::ConstPtr(typeid(ESM::Miscellaneous).name()), false);
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='arenamp_h019b_') as directory:
    src = Path(directory) / 'test.cpp'
    binary = Path(directory) / 'test'
    src.write_text(mock_header + '\n' + menu + '\n' + creative + '\n' + main, encoding='utf8')
    subprocess.run(['g++', '-std=c++11', '-Wall', '-Wextra', '-Werror',
                    str(src), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('PASS: H019b actual client/world classifiers agree for furniture, stationary/carryable lights and portable items')
