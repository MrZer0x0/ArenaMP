from pathlib import Path
import re, subprocess, shutil, tempfile
ROOT=Path(__file__).resolve().parents[2]
source=(ROOT/'apps/openmw/mwworld/worldimp.cpp').read_text()
a=source.index('osg::Vec3f objectQuatToEuler(');b=source.index('\n\n\nstd::string lowerPhysicsString',a)
helper=source[a:b]
a=source.index('void World::rotatePhysicsGrab(');b=source.index('\n    }',a)
rotation=source[a:b]
speed=re.search(r'constexpr float manualAngularSpeed[^;]+;',rotation).group()
angle=re.search(r'deltaRotation.makeRotate\((.*?),\s*requestedAxis\);',rotation,re.S).group(1)
cpp=r'''
#include <algorithm>
#include <cmath>
#include <cassert>
#include <iostream>
#include <random>
namespace osg {
constexpr double PI=3.14159265358979323846, PI_2=PI/2;
struct Vec3f {float a,b,c; Vec3f(float x,float y,float z):a(x),b(y),c(z){} };
struct Quat {double a,b,c,d; double x()const{return a;} double y()const{return b;} double z()const{return c;} double w()const{return d;} };
}
'''+helper+r'''
// Analytic quaternion for the scene's three negative axes in OSG order:
// Z * Y * X (OSG) corresponds to X Hamilton Y Hamilton Z.
osg::Quat fromEuler(double x,double y,double z) {
    const double sx=sin(-x/2),cx=cos(-x/2),sy=sin(-y/2),cy=cos(-y/2),sz=sin(-z/2),cz=cos(-z/2);
    return {sx*cy*cz+cx*sy*sz,cx*sy*cz-sx*cy*sz,cx*cy*sz+sx*sy*cz,cx*cy*cz-sx*sy*sz};
}
void check(double x,double y,double z) {
    auto q=fromEuler(x,y,z); auto e=objectQuatToEuler(q);auto r=fromEuler(e.a,e.b,e.c);
    // q and -q encode the same physical orientation.
    double dot=std::abs(q.a*r.a+q.b*r.b+q.c*r.c+q.d*r.d);
    if (dot<1-1e-6) {std::cerr<<"Orientation mismatch "<<x<<","<<y<<","<<z<<" dot="<<dot<<"\n";std::abort();}
}
int main() {
    for (int sign:{-1,1}) for(double x:{0.,0.3,1.2,-2.}) for(double z:{0.,-0.8,2.}) {
        check(x,sign*osg::PI_2,z);
        check(x,sign*(osg::PI_2-1e-7),z);
        check(x,sign*(osg::PI_2+1e-7),z);
    }
    std::mt19937 gen(94);std::uniform_real_distribution<double>d(-osg::PI,osg::PI);
    for(int i=0;i<100000;++i)check(d(gen),d(gen),d(gen));
    // Integrate the actual input-angle expression at low/high and uneven FPS.
    '''+speed+r'''
    for(int fps:{10,15,20,30,60,144}) {
        double total=0.;const float requestedLength=1.f;const float duration=1.f/fps;
        for(int i=0;i<fps*10;++i)total+='''+angle+r''';
        assert(std::abs(total-2*osg::PI)<1e-5);
    }
    double total=0.,elapsed=0.;const float requestedLength=1.f;
    while(elapsed<10.) {const float duration=std::min(10.-elapsed,(gen()%80+1)/1000.);total+='''+angle+r''';elapsed+=duration;}
    assert(std::abs(total-2*osg::PI)<1e-5);
    std::cout<<"PASS rotation: 100072 quaternion round trips; 360 degrees / 10 s at 10-144 FPS and uneven frames\n";
}
'''
compiler=shutil.which('g++') or shutil.which('g++-13') or shutil.which('x86_64-linux-gnu-g++-13')
if not compiler: raise SystemExit('A C++17 compiler is required for isolated helper tests')
def compile_and_run(code, name):
    with tempfile.TemporaryDirectory(prefix='arenamp-y095-') as directory:
        path=Path(directory)/f'{name}.cpp';binary=Path(directory)/name
        path.write_text(code)
        subprocess.run([compiler,'-std=c++17','-O2','-Wall','-Wextra','-I',str(ROOT),str(path),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
compile_and_run(cpp,'rotation')
# Compile the actual dialogue notifier using real StringUtils formatting and
# GUI enum definitions; only the UI sink and GMST lookup are substituted.
source=(ROOT/'apps/openmw/mwmp/LocalPlayer.cpp').read_text()
a=source.index('    void notifyDialogueInventoryChange(')
b=source.index('    constexpr float sNetworkPoseTransitionSeconds',a)
helper=source[a:b]
cpp=r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <iostream>
#include "components/misc/stringops.hpp"
#include "apps/openmw/mwgui/mode.hpp"
namespace MWGui { enum ShowInDialogueMode { ShowInDialogueMode_Only }; }
struct WindowSink {
    bool dialogue=true,barter=false;
    std::vector<std::string> messages;
    bool containsMode(MWGui::GuiMode m) {return m==MWGui::GM_Dialogue?dialogue:(m==MWGui::GM_Barter?barter:false);}
    void messageBox(const std::string& s,MWGui::ShowInDialogueMode){messages.push_back(s);}
};
WindowSink sink;
namespace MWBase {struct Environment {
    static Environment& get(){static Environment e;return e;}
    WindowSink* getWindowManager(){return &sink;}
};}
namespace MyGUI {struct LanguageManager {
    static LanguageManager& getInstance(){static LanguageManager m;return m;}
    std::string replaceTags(const std::string& s){
        if(s=="#{sNotifyMessage60}")return "%s added";
        if(s=="#{sNotifyMessage61}")return "%d %s added";
        if(s=="#{sNotifyMessage62}")return "%s removed";
        if(s=="#{sNotifyMessage63}")return "%d %s removed";
        throw std::runtime_error("unknown GMST");
    }
};}
'''+helper+r'''
int main(){
    notifyDialogueInventoryChange("robe",1,true);
    notifyDialogueInventoryChange("robe",3,true);
    notifyDialogueInventoryChange("robe",1,false);
    notifyDialogueInventoryChange("robe",2,false);
    assert((sink.messages==std::vector<std::string>{"robe added","3 robe added","robe removed","2 robe removed"}));
    notifyDialogueInventoryChange("robe",0,false);assert(sink.messages.size()==4);
    notifyDialogueInventoryChange("robe",2,false);assert(sink.messages.size()==5);
    sink.barter=true;notifyDialogueInventoryChange("robe",2,true);assert(sink.messages.size()==5);
    sink.barter=false;sink.dialogue=false;notifyDialogueInventoryChange("robe",2,true);assert(sink.messages.size()==5);
    std::cout<<"PASS dialogue notifier: gains, losses, plural quantities, repeats, zero removal and GUI routing\n";
}
'''
compile_and_run(cpp,'dialogue')
