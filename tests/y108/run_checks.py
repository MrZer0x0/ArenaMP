#!/usr/bin/env python3
"""Compile actual picker and ItemView handlers against UI/inventory doubles."""
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]

def method(source, signature):
    start=source.index(signature)
    opening=source.index('{',start)
    depth=1
    end=opening+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}')
        end+=1
    return source[start:end]

picker=(ROOT/'apps/openmw/mwgui/itemselection.cpp').read_text()
view=(ROOT/'apps/openmw/mwgui/itemview.cpp').read_text()
view_header=(ROOT/'apps/openmw/mwgui/itemview.hpp').read_text()
enchanting=(ROOT/'apps/openmw/mwgui/enchantingdialog.cpp').read_text()
# Keep defaults in the doubles grounded in the real constructor.
for value in ['mExtendedMode(false)','mSingleClickActionEnabled(false)','mViewMode(View_List)']:
    assert value in view
parts=['namespace MWGui {']
setter=method(view_header,'void setSingleClickActionEnabled(')
parts.append(setter.replace('void setSingleClickActionEnabled(', 'void ItemView::setSingleClickActionEnabled(',1))
for signature in ['void ItemView::setExtendedMode(', 'void ItemView::setModel(',
    'bool ItemView::handleNavigationKey(', 'void ItemView::onSelectedItem(',
    'void ItemView::onListItemPressed(', 'void ItemView::onListItemDragged(',
    'void ItemView::onListItemReleased(', 'void ItemView::onListItemDoubleClicked(']:
    parts.append(method(view,signature))
for name in ['onSelectItem','onSelectSoul','onItemSelected','onSoulSelected','onItemCancel','onSoulCancel']:
    parts.append(method(enchanting,'void EnchantingDialog::'+name+'('))
parts.append('}')
parts.append(picker[picker.index('namespace MWGui'):])
body='\n'.join(parts)
scenarios=(ROOT/'tests/y108/selection_scenarios.cpp').read_text()
with tempfile.TemporaryDirectory(prefix='arenamp-y108-') as folder:
    folder=Path(folder)
    for legacy in [False,True]:
        variant=body
        if legacy:
            original='mItemView->setSingleClickActionEnabled(true);'
            assert variant.count(original)==1
            variant=variant.replace(original,'mItemView->setSingleClickActionEnabled(false);')
        cpp=folder/('legacy.cpp' if legacy else 'fixed.cpp')
        cpp.write_text('#include "tests/y108/selection_harness.hpp"\n'+variant+'\n'+scenarios)
        binary=cpp.with_suffix('')
        subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
            '-O2','-I',str(ROOT),str(cpp),'-o',str(binary)],check=True)
        subprocess.run([str(binary)]+(['--expect-blocked'] if legacy else []),check=True)
patcher=(ROOT/'android/buildscripts/patches/openmw/31-arenamp-y092-ui-ai-progression-enchant.py').read_text()
assert "MARKERS['apps/openmw/mwgui/itemselection.cpp'] = ['setSingleClickActionEnabled(true)']" in patcher
assert 'enchant-v22' in (ROOT/'android/buildscripts/patches/openmw/apply-arenamp-patches.sh').read_text()
print('PASS Android authoritative source marker and cache patchset version')
