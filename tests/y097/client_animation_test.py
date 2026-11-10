#!/usr/bin/env python3
"""Compile the actual Cell::readAnimPlay body against small engine UI stubs."""
from pathlib import Path
import shutil, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[2]
source=(ROOT/'apps/openmw/mwmp/Cell.cpp').read_text()
a=source.index('void Cell::readAnimPlay(');b=source.index('\nvoid Cell::readStatsDynamic',a)
function=source[a:b]
code=r'''
#include <cassert>
#include <iostream>
#include <map>
#include <string>
#include <vector>
struct Animation {std::string groupname;int mode=0,count=1;bool persist=false;};
struct Actor {int refNum=10,mpNum=0;Animation animation;};
struct ActorList {int guid=7;std::vector<Actor> baseActors;};
struct LocalActor {Animation animation;int getPtr(){return 42;}};
struct DedicatedActor {Animation animation;int played=0;void playAnimation(){++played;}};
struct LocalPlayer {int guid=7;};
struct CellController {std::string generateMapIndex(const Actor& a){return std::to_string(a.refNum)+"-"+std::to_string(a.mpNum);}};
struct Main {LocalPlayer player;CellController controller;static Main& get(){static Main m;return m;}
 LocalPlayer* getLocalPlayer(){return &player;}CellController* getCellController(){return &controller;}};
struct Mechanics {int played=0;bool playAnimationGroup(int,const std::string&,int,int,bool){++played;return true;}};
namespace MWBase {struct Environment {Mechanics mechanics;static Environment& get(){static Environment e;return e;}
 Mechanics* getMechanicsManager(){return &mechanics;}};}
struct Cell {std::map<std::string,LocalActor*> localActors;std::map<std::string,DedicatedActor*> dedicatedActors;
 void readAnimPlay(ActorList&);};
'''+function+r'''
int main(){
 Cell cell;LocalActor local;DedicatedActor dedicated;cell.localActors["10-0"]=&local;cell.dedicatedActors["20-0"]=&dedicated;
 Actor a;a.animation.groupname="idle2";ActorList list;list.baseActors={a};cell.readAnimPlay(list);
 assert(MWBase::Environment::get().mechanics.played==1);assert(local.animation.groupname.empty());
 list.guid=8;cell.readAnimPlay(list);assert(MWBase::Environment::get().mechanics.played==1);
 list.guid=7;list.baseActors[0].animation.groupname="../bad";cell.readAnimPlay(list);assert(MWBase::Environment::get().mechanics.played==1);
 list.baseActors[0].animation.groupname="idle2";list.baseActors[0].animation.count=0;cell.readAnimPlay(list);assert(MWBase::Environment::get().mechanics.played==1);
 list.baseActors[0].animation.count=1;list.baseActors[0].animation.mode=3;cell.readAnimPlay(list);assert(MWBase::Environment::get().mechanics.played==1);
 list.baseActors[0].refNum=20;list.baseActors[0].animation.mode=0;cell.readAnimPlay(list);assert(dedicated.played==1);
 list.baseActors[0].refNum=99;cell.readAnimPlay(list);assert(dedicated.played==1 && MWBase::Environment::get().mechanics.played==1);
 list.baseActors={a};Actor remote=a;remote.refNum=20;list.baseActors.push_back(remote);cell.readAnimPlay(list);
 assert(dedicated.played==2 && MWBase::Environment::get().mechanics.played==2);
 std::cout<<"PASS actual client animation router: authority, observer, no echo, bounds and unknown actor\n";
}
'''
compiler=shutil.which('g++') or shutil.which('g++-13') or shutil.which('x86_64-linux-gnu-g++-13')
if not compiler:raise SystemExit('C++17 compiler required')
with tempfile.TemporaryDirectory(prefix='arenamp-animation-api-') as directory:
    source=Path(directory)/'client.cpp';exe=Path(directory)/'client';source.write_text(code)
    subprocess.run([compiler,'-std=c++17','-Wall','-Wextra','-Werror',str(source),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
