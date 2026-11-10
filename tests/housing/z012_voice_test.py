"""Compile the production voice routing block with deterministic input state."""
from pathlib import Path
import subprocess, tempfile
s=Path('apps/openmw/mwmp/VoiceChat.cpp').read_text()
block=s[s.index('        const bool keyDown = keys'):s.index('\n        if (!mEnabled)',s.index('        const bool keyDown = keys'))]
code=r'''
#include <cassert>
struct State { bool androidTapLatched=false,pttBlockedUntilRelease=false,pttKeyWasDown=false,touchWasPressed=false,toggleLatched=false;int pushToTalk=0; };
struct Voice { State state;State* mImpl=&state;bool mToggleMode=true,mGroupTalk=true,mInGroup=true,mGroupActive=false;
int tick(bool v=false,bool talkAllowed=true,bool touchPressed=false){bool keys[]={v};bool androidToggleRequested=false;bool toggleAllowed=talkAllowed;
'''+block+r'''
return !wantsToTalk?0:mGroupActive?2:1;
}};
int main(){
Voice v;assert(v.tick()==2);assert(v.tick(true)==1);assert(v.tick(true)==1);assert(v.tick()==1);assert(v.tick(true)==2);assert(v.tick()==2);
v.mToggleMode=false;assert(v.tick(true)==1);assert(v.tick()==2);
assert(v.tick(true,false)==0);assert(v.tick(true,true)==2);assert(v.tick()==2);assert(v.tick(true)==1);
assert(v.tick(false,false)==0);assert(v.mGroupTalk);assert(v.tick()==2);
v.mInGroup=false;assert(v.tick()==0);assert(v.tick(true)==1);assert(v.tick()==0);
v.mInGroup=true;v.mToggleMode=true;assert(v.tick(false,true,true)==1);assert(v.tick()==1);assert(v.tick(false,true,true)==2);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'test.cpp';p.write_text(code)
 subprocess.run(['g++','-std=c++17',str(p),'-o',d+'/test'],check=True)
 subprocess.run([d+'/test'],check=True)
print('PASS Z012: group hands-free, V toggle/hold priority, typing rearm, touch, membership')
