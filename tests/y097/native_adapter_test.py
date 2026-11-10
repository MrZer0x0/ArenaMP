#!/usr/bin/env python3
"""Compile actual Content.cpp + catalog; substitute engine/crypto/encoding APIs.
This checks adapter wiring and errors, not real RakNet, PicoSHA2 or code pages.
"""
from pathlib import Path
import json, shutil, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[2]
stub=r'''
#pragma once
#include <algorithm>
#include <deque>
#include <stdexcept>
#include <string>
#include <vector>
namespace ESM {struct Cell {std::string name;};}
struct Player {int guid=0;};
inline Player firstPlayer{1},secondPlayer{2};
struct Players {static Player* getPlayer(unsigned short id){return id==1?&firstPlayer:(id==2?&secondPlayer:nullptr);}};
namespace mwmp {
struct Animation {std::string groupname;int mode=0,count=0;bool persist=false;};
struct BaseActor {std::string refId;unsigned int refNum=0,mpNum=0;Animation animation;};
struct BaseActorList {enum {SET=0};ESM::Cell cell;int guid=0;unsigned int count=0;int action=0;bool isValid=false;std::vector<BaseActor> baseActors;};
struct ActorPacket {BaseActorList* list=nullptr;int sent=0,recipient=0;
 void setActorList(BaseActorList* p){list=p;}void Send(int id){++sent;recipient=id;}};
inline ActorPacket testPacket;
struct ActorPacketController {ActorPacket* GetPacket(int){return &testPacket;}};
struct Networking {ActorPacketController controller;static Networking& get(){static Networking n;return n;}
 ActorPacketController* getActorPacketController(){return &controller;}};
}
struct Cell {mwmp::BaseActor actor;Cell(){actor.refId="arrille";actor.refNum=331473;}
 bool containsActor(int n,int m){return n==331473&&m==0;}
 mwmp::BaseActor* getActor(int,int){return &actor;}
 std::deque<Player*> getPlayers(){return {&firstPlayer,&secondPlayer};}};
struct CellController {Cell cell;static CellController* get(){static CellController c;return &c;}
 Cell* getCell(ESM::Cell* c){return c->name=="Home"?&cell:nullptr;}};
namespace Utils {inline ESM::Cell getCellFromDescription(const char* s){return {s};}}
enum {ID_ACTOR_ANIM_PLAY=1};
namespace ToUTF8 {enum FromType{WINDOWS_1251};inline FromType calculateEncoding(const std::string& s){
 if(s!="win1251"){throw std::runtime_error("invalid test encoding");}return WINDOWS_1251;}
struct Utf8Encoder {explicit Utf8Encoder(FromType){}std::string getUtf8(const std::string& s){return s;}};}
namespace picosha2 {inline std::string hash256_hex_string(const std::string& s){
 unsigned long long n=14695981039346656037ull;for(unsigned char c:s){n^=c;n*=1099511628211ull;}
 auto h=std::to_string(n);return std::string(64-h.size(),'0')+h;}}
'''
test=r'''
#include "Stub.hpp"
#include "apps/openmw-mp/Script/Functions/Content.hpp"
#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <locale>
std::string le(unsigned int v){std::string s;for(int i=0;i<4;++i)s+=char(v>>(8*i));return s;}
std::string sub(std::string t,std::string s){return t+le(s.size())+s;}
std::string rec(std::string t,std::string s){return t+le(s.size())+le(0)+le(0)+s;}
struct Comma:std::numpunct<char>{char do_decimal_point()const override{return ',';}};
int main(){
 namespace fs=std::filesystem;
 auto directory=fs::temp_directory_path()/("arenamp-adapter-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 fs::create_directories(directory);auto file=directory/"Base.esm";
 std::string h(300,'\0');float version=1.3f;std::memcpy(&h[0],&version,4);
 auto bytes=rec("TES3",sub("HEDR",h))+rec("NPC_",sub("NAME",std::string("arrille\0",8)));
 {std::ofstream out(file,std::ios::binary);out.write(bytes.data(),bytes.size());}
 assert(ContentFunctions::GetVersion()==1);assert(!ContentFunctions::AddFile("a.esm"));
 assert(ContentFunctions::Begin("utf8",""));assert(ContentFunctions::AddFile(file.u8string().c_str()));assert(ContentFunctions::Commit());
 assert(ContentFunctions::GetRecordCount("NPC_")==1);
 assert(std::string(ContentFunctions::NormalizeId("АРИЛЛ"))=="арилл");
 assert(std::string(ContentFunctions::GetRecord("NPC_","ARRILLE")).find("arrille")!=std::string::npos);
 ContentFunctions::GetSubrecord("NPC_","arrille","MISS",0,"hex");assert(std::string(ContentFunctions::GetError())=="subrecord_not_found");
 assert(std::string(ContentFunctions::GetSubrecord("NPC_","arrille","NAME",0,"text"))=="arrille");assert(std::string(ContentFunctions::GetError()).empty());
 assert(ContentFunctions::SendActorAnimation(1,"Home",331473,0,"arrille","idle2",0,1,false));
 assert(mwmp::testPacket.sent==1&&mwmp::testPacket.recipient==1&&mwmp::testPacket.list->isValid);
 assert(mwmp::testPacket.list->baseActors[0].animation.groupname=="idle2");
 assert(!ContentFunctions::SendActorAnimation(3,"Home",331473,0,"arrille","idle2",0,1,false));
 assert(!ContentFunctions::SendActorAnimation(1,"Home",331473,0,"wrong","idle2",0,1,false));
 assert(!ContentFunctions::SendActorAnimation(1,"Away",331473,0,"arrille","idle2",0,1,false));
 assert(!ContentFunctions::SendActorAnimation(1,"Home",331473,0,"arrille","../bad",0,1,false));
 assert(ContentFunctions::SendActorAnimation(2,"Home",331473,0,"arrille","idle2",2,2,true));
 assert(mwmp::testPacket.sent==2&&mwmp::testPacket.recipient==2&&mwmp::testPacket.list->baseActors.size()==1);
 assert(ContentFunctions::Begin("utf8",""));assert(ContentFunctions::AddFile("/missing.esm"));assert(!ContentFunctions::Commit());
 assert(ContentFunctions::GetRecordCount("NPC_")==1);
 auto invalid=directory/"Invalid.esm";auto bad=rec("TES3",sub("HEDR",h))+rec("NPC_",sub("NAME",std::string("\xe0\x80\x80\0",4)));
 {std::ofstream out(invalid,std::ios::binary);out.write(bad.data(),bad.size());}
 assert(ContentFunctions::Begin("utf8",""));assert(ContentFunctions::AddFile(invalid.u8string().c_str()));assert(!ContentFunctions::Commit());
 assert(std::string(ContentFunctions::GetError()).find("UTF-8")!=std::string::npos);
 assert(ContentFunctions::GetRecordCount("NPC_")==1);
 std::locale::global(std::locale(std::locale::classic(),new Comma));
 std::cout<<ContentFunctions::GetInfo()<<'\n';
 fs::remove_all(directory);
 std::cout<<"PASS actual native Content adapter: registration signatures, transactional errors, UTF-8, locale and animation sends (engine APIs mocked)\n";
}
'''
compiler=shutil.which('g++') or shutil.which('g++-13') or shutil.which('x86_64-linux-gnu-g++-13')
if not compiler:raise SystemExit('C++17 compiler required')
with tempfile.TemporaryDirectory(prefix='arenamp-native-adapter-') as directory:
    d=Path(directory);(d/'Stub.hpp').write_text(stub);(d/'test.cpp').write_text(test)
    for header in ['extern/PicoSHA2/picosha2.h','components/to_utf8/to_utf8.hpp','components/openmw-mp/NetworkMessages.hpp',
        'apps/openmw-mp/CellController.hpp','apps/openmw-mp/Cell.hpp','apps/openmw-mp/Networking.hpp','apps/openmw-mp/Player.hpp','apps/openmw-mp/Utils.hpp']:
        path=d/header;path.parent.mkdir(parents=True,exist_ok=True);path.write_text('#include "Stub.hpp"\n')
    exe=d/'adapter'
    subprocess.run([compiler,'-std=c++17','-Wall','-Wextra','-Werror','-I',str(d),'-I',str(ROOT),
        str(ROOT/'apps/openmw-mp/content/ContentCatalog.cpp'),str(ROOT/'apps/openmw-mp/Script/Functions/Content.cpp'),str(d/'test.cpp'),'-o',str(exe)],check=True)
    output=subprocess.check_output([str(exe)],text=True).splitlines()
    assert json.loads(output[0])['plugins'][0]['version']>1
    print(output[1])
