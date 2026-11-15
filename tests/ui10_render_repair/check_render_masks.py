#!/usr/bin/env python3
"""Compile the production mask transition method with a minimal viewer double.
Covers the startup camera-reset regression, nesting, and later cell transitions.
No OSG/GL context is mocked as a successful visual render.
"""
from pathlib import Path
import subprocess, tempfile, xml.etree.ElementTree as ET
r=Path(__file__).resolve().parents[2]
s=(r/'apps/openmw/mwgui/windowmanagerimp.cpp').read_text()
a=s.index('    void WindowManager::enableScene(bool enable)');b=s.index('    void WindowManager::refreshSceneVisibility()',a)
method=s[a:b]
code=r'''
#include <cassert>
#include <iostream>
#include "apps/openmw/mwrender/vismask.hpp"
struct Camera { unsigned mask=~0u; unsigned getCullMask(){return mask;} void setCullMask(unsigned m){mask=m;} };
struct Visitor { unsigned mask=~0u; unsigned getTraversalMask(){return mask;} void setTraversalMask(unsigned m){mask=m;} };
struct Viewer { Camera camera; Visitor update; Camera* getCamera(){return &camera;} Visitor* getUpdateVisitor(){return &update;} };
class WindowManager {
public:
 Viewer* mViewer;
 unsigned mOldCullMask=0, mOldUpdateMask=0;
 bool mSceneDisabled=false;
 void enableScene(bool enable);
};
'''+method+r'''
int main(){
 const unsigned off=MWRender::Mask_GUI|MWRender::Mask_PreCompile;
 const unsigned normal=~(MWRender::Mask_UpdateVisitor|MWRender::Mask_SimpleWater);
 Viewer v; WindowManager w;w.mViewer=&v;
 // Startup begins before RenderingManager configures the camera.
 w.enableScene(false);
 assert(v.camera.mask==off && v.update.mask==off);
 v.camera.mask=normal;
 // Reapply during loading must retain the original FULL update visitor mask.
 w.enableScene(false);
 assert(w.mOldUpdateMask==~0u && w.mOldCullMask==normal);
 for(int i=0;i<100;++i)w.enableScene(false);
 w.enableScene(true);
 assert(v.camera.mask==normal && v.update.mask==~0u);
 assert((v.update.mask & MWRender::Mask_Scene)!=0);
 assert((v.update.mask & MWRender::Mask_UpdateVisitor)!=0);
 // Repeated transitions and restoring while the camera changed since last tick.
 for(int i=0;i<10;++i){
  w.enableScene(false);w.enableScene(false);
  v.camera.mask=normal & ~MWRender::Mask_Debug;
  w.enableScene(true);
  assert(v.camera.mask==(normal & ~MWRender::Mask_Debug));
  assert(v.update.mask==~0u);
 }
 // An explicit gameplay visibility override is preserved, not forced to all bits.
 v.update.mask=~MWRender::Mask_Debug;v.camera.mask=normal & ~MWRender::Mask_Water;
 w.enableScene(false);w.enableScene(true);
 assert(v.update.mask==~MWRender::Mask_Debug);
 assert(v.camera.mask==(normal & ~MWRender::Mask_Water));
 w.enableScene(true);assert(!w.mSceneDisabled);
 std::cout<<"Production paired render/update masks: PASS\n";
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-I'+str(r),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
loading=(r/'apps/openmw/mwgui/loadingscreen.cpp').read_text()
assert 'holdLoadingCullMask' not in loading and 'setCullMask' not in loading
assert 'getSubWidgetText()->setWordWrap(true)' in loading
assert 'mTipText->setFontHeight(18)' in loading
assert 'mTipText->setFontHeight(13)' not in loading
start=loading.index('void LoadingScreen::loadingOn(')
end=loading.index('void LoadingScreen::loadingOff(')
assert loading.index('stopThreading()',start)<loading.index('setupCopyFramebufferToTextureCallback();',start)<end
start=end;end=loading.index('void LoadingScreen::updateLayout()',start)
assert loading.index('removeGuiMode(GM_LoadingWallpaper)',start)<loading.index('restoreViewerThreading();',start)<end
start=loading.index('void LoadingScreen::draw()')
assert loading.index('refreshSceneVisibility();',start)<loading.index('updateTraversal();',start)<loading.index('renderingTraversals();',start)
assert 'getBool("arenamp modern skin"' not in s
settings=ET.parse(r/'files/mygui/openmw_settings_window.layout')
assert settings.find('.//UserString[@value="arenamp modern skin"]') is None
for name in ['openmw_tooltips.layout','openmw_stats_window.layout','openmw_chargen_class.layout','openmw_chargen_create_class.layout']:
 tree=ET.parse(r/'files/mygui'/name)
 for w in tree.iter('Widget'):
  if w.get('name') in ('ArchetypePowerText','SkillProgressText','LevelProgressText'):
   assert w.find('./Property[@key="TextShadow"]').get('value')=='true'
   assert w.find('./Property[@key="TextShadowColour"]').get('value')=='0 0 0'
layout=ET.parse(r/'files/mygui/openmw_loading_screen.layout')
assert layout.find('.//Widget[@name="LoadingTipTitle"]') is not None
assert layout.find('.//Widget[@name="LoadingTip"]/Property[@key="FontHeight"]').get('value')=='18'
print('Loading traversal order, text wrapping, shadows and mandatory skin: PASS')
