#include "../../apps/openmw/mwgui/quickbarrules.hpp"
#include <array>
#include <cassert>
#include <iostream>
#include <limits>
using namespace MWGui::QuickBarRules;
int main() {
    for (int level=1;level<=150;++level) assert(unlocked(level)==1+level/30);
    assert(unlocked(-100)==1 && unlocked(10000)==6);
    for (int i=-10;i<=70;++i) assert(valid(i)==(i>=1 && i<=60));
    std::array<bool,60> filled{}; filled[0]=filled[20]=filled[50]=true;
    auto assigned=[&](int i){assert(i>=0 && i<60);return filled[i];};
    assert(select(0,0,1,true,assigned)==0);
    assert(select(0,0,3,true,assigned)==20);
    assert(select(0,2,3,true,assigned)==0);
    assert(select(0,0,6,true,assigned)==20);
    assert(select(0,2,6,true,assigned)==50);
    assert(select(0,5,6,true,assigned)==0);
    assert(select(0,5,6,false,assigned)==50);
    assert(select(0,5,1,false,assigned)==0); // level rollback preserves data, locks use
    assert(select(1,0,6,true,assigned)==-1);
    for(int column=0;column<10;++column) for(int rows=1;rows<=6;++rows) {
        auto all=[](int){return true;};
        const int slot=select(column,rows-1,rows,true,all);assert(slot==column);
    }
    Gesture g;g.press();assert(g.update(.016f,false)==Gesture::Tap);assert(g.update(.016f,false)==Gesture::None);
    for(int fps:{20,30,60,144}) {
        g.cancel();g.press();int holds=0;
        for(int i=0;i<fps;++i){g.press();auto result=g.update(1.f/fps,true);assert(result!=Gesture::Tap);holds+=(result==Gesture::Hold);}
        assert(holds==1);assert(g.update(.01f,false)==Gesture::None);
    }
    g.press();g.cancel();assert(g.update(.02f,false)==Gesture::None); // menu or chat takes focus
    g.press();assert(g.update(std::numeric_limits<float>::quiet_NaN(),true)==Gesture::None);g.cancel();
    std::cout<<"PASS: unlock levels, 60-slot bounds, filled-only cycling, rollback, tap/hold/repeat/focus and 20..144 FPS\n";
}
