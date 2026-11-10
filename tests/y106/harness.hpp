#pragma once
#include "transport.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/Packets/PacketPreInit.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include <cassert>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <cctype>

namespace Script {
constexpr uint64_t CallbackIdentity(const char* s) {
    uint64_t h=0;while(*s)h=h*31+static_cast<unsigned char>(*s++);return h;
}
inline unsigned connects=0,disconnects=0;
template<uint64_t callback,class... Args> void Call(Args&&...) {
    if constexpr(callback==CallbackIdentity("OnPlayerConnect"))++connects;
    if constexpr(callback==CallbackIdentity("OnPlayerDisconnect"))++disconnects;
}
}
namespace mwmp {
struct Target {bool isPlayer=false;RakNet::RakNetGUID guid;};
struct Attack {Target target;};
struct Cast {Target target;};
struct Spell {Target caster;};
struct SpellChanges {std::vector<Spell> activeSpells;};
struct BaseActor {
    Target aiTarget,killer;std::vector<Target> aiCombatTargets;
    Attack attack;Cast cast;SpellChanges spellsActiveChanges;
};
struct BaseActorList {
    RakNet::RakNetGUID guid;
    int cell=1;
    unsigned count=0;
    std::vector<BaseActor> baseActors;
};
struct BaseSystem {RakNet::RakNetGUID guid;std::string serverPassword;};
}
struct BasePlayer {
    RakNet::RakNetGUID guid;
    struct Npc {std::string mName="player",mRace="race",mHead="head",mHair="hair",mModel="model";unsigned mFlags=0;} npc;
    struct {int endStage=0;} charGenState;
    bool respawnLocked=false,exchangeFullInfo=false;
    std::string language="RU";
    std::vector<RakNet::RakNetGUID> alliedPlayers;
    mwmp::Attack attack;mwmp::Cast cast;mwmp::Target killer;mwmp::SpellChanges spellsActiveChanges;
};
class Player;
class Cell {
public:
    RakNet::RakNetGUID authorityGuid;
    mwmp::BaseActorList cellActorList,cellActorAIList;
    std::vector<Player*> players;
    void rebindPlayerGuid(RakNet::RakNetGUID,RakNet::RakNetGUID);
    RakNet::RakNetGUID* getAuthority(){return &authorityGuid;}
    mwmp::BaseActorList* getActorList(){return &cellActorList;}
    void sendCachedActorAI(RakNet::RakNetGUID){}
    template<class P> void sendToLoaded(P* packet,mwmp::BaseActorList* list);
};
class CellController {
public:
    using TContainer=std::vector<Cell*>;
    TContainer cells;
    static CellController* get(){static CellController c;return &c;}
    void rebindPlayerGuid(RakNet::RakNetGUID,RakNet::RakNetGUID);
};
class Player:public BasePlayer {
public:
    enum{NOTLOADED,LOADED,POSTLOADED,KICKED};
    unsigned short id=0;
    int loadState=NOTLOADED,handshake=0;
    bool visible=false,authoritative=false,loggedIn=true;
    CellController::TContainer cells;
    std::set<RakNet::RakNetGUID> pendingCellChangeRecipients;
    explicit Player(RakNet::RakNetGUID g){guid=g;}
    unsigned short getId(){return id;}
    bool isHandshaked(){return handshake==100;}
    void setHandshake(){handshake=100;}
    void incrementHandshakeAttempts(){++handshake;}
    int getHandshakeAttempts(){return handshake;}
    int getLoadState(){return loadState;}
    void setLoadState(int v){loadState=v;}
    bool isVisibleToOthers()const{return visible;}
    bool isAppearanceAuthoritative()const{return authoritative;}
    CellController::TContainer* getCells(){return &cells;}
    template<class P> void sendToLoaded(P*){}
    template<class F> void forEachLoaded(F f){
        std::set<Player*> seen;
        for(auto* c:cells)for(auto* p:c->players)if(p!=this&&seen.insert(p).second)f(this,p);
    }
};
template<class P> void Cell::sendToLoaded(P* packet,mwmp::BaseActorList* list){
    for(auto* p:players)if(p->guid!=list->guid)packet->Send(p->guid);
}
class Players {
public:
    inline static std::map<RakNet::RakNetGUID,Player*> players;
    inline static unsigned created=0,deleted=0;
    static bool rebindPlayerGuid(RakNet::RakNetGUID,RakNet::RakNetGUID);
    static std::map<RakNet::RakNetGUID,Player*>* getPlayers(){return &players;}
    static bool doesPlayerExist(RakNet::RakNetGUID g){return players.count(g);}
    static Player* getPlayer(RakNet::RakNetGUID g){auto i=players.find(g);return i==players.end()?nullptr:i->second;}
    static void newPlayer(RakNet::RakNetGUID g){assert(!players.count(g));players[g]=new Player(g);++created;}
    static void deletePlayer(RakNet::RakNetGUID g){auto i=players.find(g);if(i!=players.end()){delete i->second;players.erase(i);++deleted;}}
    static void clear(){for(auto& e:players)delete e.second;players.clear();created=deleted=0;}
};
class LocalPlayer:public BasePlayer {
public:
    enum{LoginSync_StatsDynamic=1,LoginSync_Attributes=2,LoginSync_Skills=4,
        LoginSync_Level=8,LoginSync_Equipment=16,LoginSync_All=31};
    bool mLoginSyncPending=false,mDynamicStatsBaselineValid=false;
    int mLoginSyncReceived=0,baselines=0;
    float mLoginSyncElapsed=0;
    void beginLoginSync(bool force=false);
    void markLoginSyncReceived(int section);
    bool isLoginSyncPending()const{return mLoginSyncPending;}
    void adoptServerStateAsBaseline(){++baselines;}
};
namespace mwmp {
class TestPacket:public BasePacket {
    BasePlayer* player=nullptr;
public:
    TestPacket(RakNet::RakPeerInterface* p,unsigned id):BasePacket(p){packetID=id;}
    void setPlayer(BasePlayer* p){player=p;setGUID(p->guid);}
    void setSystem(BaseSystem*){}
    void setActorList(BaseActorList* a){setGUID(a->guid);}
    void Packet(RakNet::BitStream* stream,bool send)override{
        BasePacket::Packet(stream,send);
        if(send&&packetID!=ID_LOADED&&packetID!=ID_USER_DISCONNECTED){uint8_t payload=1;stream->Write(payload);}
    }
};
using PlayerPacket=TestPacket;
using ActorPacket=TestPacket;
using SystemPacket=TestPacket;
class Controller {
    RakNet::BitStream out;
    std::map<unsigned,std::unique_ptr<TestPacket>> packets;
public:
    Controller(RakNet::RakPeerInterface* p,std::initializer_list<unsigned> ids){
        for(auto id:ids){auto q=std::make_unique<TestPacket>(p,id);q->SetSendStream(&out);packets[id]=std::move(q);}
    }
    bool ContainsPacket(unsigned id){return packets.count(id);}
    TestPacket* GetPacket(unsigned id){return packets.at(id).get();}
    void SetStream(RakNet::BitStream* in,RakNet::BitStream*){for(auto& e:packets)e.second->SetReadStream(in);}
};
struct PlayerProcessor {inline static unsigned chats=0;static bool Process(RakNet::Packet& p){if(p.data[0]==ID_CHAT_MESSAGE)++chats;return true;}};
struct ObjectProcessor {inline static unsigned activations=0;template<class T>static bool Process(RakNet::Packet& p,T&){if(p.data[0]==ID_OBJECT_ACTIVATE)++activations;return true;}};
struct ActorProcessor {template<class T>static bool Process(RakNet::Packet&,T&){return true;}};
struct WorldstateProcessor {template<class T>static bool Process(RakNet::Packet&,T&){return true;}};
struct BaseClientPacketProcessor {static void SetServerAddr(RakNet::SystemAddress){}};
}
namespace Misc {struct StringUtils {static bool ciEqual(std::string a,std::string b){
    std::transform(a.begin(),a.end(),a.begin(),[](unsigned char c){return std::tolower(c);});
    std::transform(b.begin(),b.end(),b.begin(),[](unsigned char c){return std::tolower(c);});return a==b;
}};}
namespace boost {namespace filesystem {struct path {
    std::string value;path(std::string s):value(std::move(s)){}
    path extension()const {auto i=value.rfind('.');return path(i==std::string::npos?"":value.substr(i));}
    std::string string()const{return value;}
};}}
namespace Files {
struct MultiDirCollection {
    bool doesExist(const std::string& s)const{return s!="missing.esm";}
    boost::filesystem::path getPath(const std::string& s)const{return boost::filesystem::path(s);}
};
struct Collections {MultiDirCollection c;const MultiDirCollection& getCollection(const std::string&)const{return c;}};
}
namespace Utils {inline unsigned hashes=0;inline unsigned crc32Checksum(const std::string&){++hashes;return 31337;}}
inline std::string listDiscrepancies(const mwmp::PacketPreInit::PluginContainer&,const mwmp::PacketPreInit::PluginContainer&){return "mismatch";}
inline std::string listComparison(const mwmp::PacketPreInit::PluginContainer&,const mwmp::PacketPreInit::PluginContainer&,bool){return "mismatch";}
inline unsigned dialogs=0;
constexpr int SDL_MESSAGEBOX_ERROR=1;
inline void SDL_ShowSimpleMessageBox(int,const char*,const char*,int){++dialogs;}
inline void RakSleep(int){static unsigned calls=0;if(++calls>10000)throw std::runtime_error("unexpected empty transport wait");}
namespace MWBase {
struct StateManager {unsigned quits=0;void requestQuit(){++quits;}};
struct Environment {static Environment& get(){static Environment e;return e;}StateManager s;StateManager* getStateManager(){return &s;}};
}
namespace server {
using namespace mwmp;
inline bool dataFileEnforcementState=true;
class Networking {
public:
    struct ResumeSession {std::string deviceHash,token;std::chrono::steady_clock::time_point expiresAt;};
    RakNet::RakPeerInterface* peer;
    Controller sys,pl,act,obj,world;
    Controller* systemPacketController=&sys;Controller* playerPacketController=&pl;
    Controller* actorPacketController=&act;Controller* objectPacketController=&obj;Controller* worldstatePacketController=&world;
    BaseSystem baseSystem;BaseActorList baseActorList;int baseObjectList=0,baseWorldstate=0;
    std::map<RakNet::RakNetGUID,ResumeSession> resumeSessions,suspendedSessions;
    std::map<RakNet::RakNetGUID,std::chrono::steady_clock::time_point> awaitingResumeAcks;
    std::string startLocation="default",serverPassword;
    unsigned capacity=16;
    unsigned maxConnections()const{return capacity;}
    PacketPreInit::PluginContainer samples{{"Morrowind.esm",{31337}}},groundcoverSamples;
    explicit Networking(RakNet::RakPeerInterface* p):peer(p),sys(p,{ID_SYSTEM_HANDSHAKE}),
        pl(p,{ID_LOADED,ID_PLAYER_BASEINFO,ID_PLAYER_STATS_DYNAMIC,ID_PLAYER_ATTRIBUTE,ID_PLAYER_SKILL,
            ID_PLAYER_LEVEL,ID_PLAYER_EQUIPMENT,ID_CHAT_MESSAGE,ID_USER_DISCONNECTED,ID_PLAYER_SHAPESHIFT,
            ID_PLAYER_POSITION,ID_PLAYER_CELL_CHANGE,ID_PLAYER_ANIM_FLAGS}),
        act(p,{ID_ACTOR_AUTHORITY,ID_ACTOR_AI}),obj(p,{ID_OBJECT_ACTIVATE}),world(p,{ID_WORLD_TIME}){}
    bool isPassworded(){return !serverPassword.empty();}
    void kickPlayer(RakNet::RakNetGUID g,bool notify=true){peer->CloseConnection(g,notify);}
    void newPlayer(RakNet::RakNetGUID){}
    std::string generateResumeToken()const;
    void expireSuspendedPlayers();void processSystemPacket(RakNet::Packet*);
    void processPlayerPacket(RakNet::Packet*);void processActorPacket(RakNet::Packet*);
    void processObjectPacket(RakNet::Packet*);void processWorldstatePacket(RakNet::Packet*);
    bool preInit(RakNet::Packet*,RakNet::BitStream&);void update(RakNet::Packet*,RakNet::BitStream&);
    void disconnectPlayer(RakNet::RakNetGUID);void suspendPlayer(RakNet::RakNetGUID);
    void sendPlayerSnapshot(Player*,RakNet::RakNetGUID);void exchangePlayerSnapshots(Player*);
    void completeSessionResume(Player*);void deliver(RakNet::Packet*);
};
}
namespace client {
using namespace mwmp;
class Networking {
public:
    RakNet::RakPeerInterface* peer;
    Controller systemPacketController,playerPacketController,actorPacketController,objectPacketController,worldstatePacketController;
    LocalPlayer local;BaseSystem system;
    bool connected=true,mRestoringConnection=false,mAwaitingRestoreAck=false,mRestoreAckReceived=false,mResumeAccepted=false;
    float mRestoreRemaining=0,mReconnectAttemptCooldown=0;
    std::chrono::steady_clock::time_point mRestoreDeadline;
    std::string mSessionIdentity="private-session-nonce",mResumeToken,mRestoreError,mConnectPassword="compatibility",startLocation;
    std::map<std::string,unsigned> mContentChecksums;
    std::vector<std::string> mReconnectContent{"Morrowind.esm"},mReconnectGroundcover;
    Files::Collections collections;Files::Collections* mReconnectCollections=&collections;
    RakNet::SystemAddress serverAddr{200};
    explicit Networking(RakNet::RakPeerInterface* p):peer(p),systemPacketController(p,{ID_SYSTEM_HANDSHAKE}),
        playerPacketController(p,{ID_LOADED,ID_PLAYER_BASEINFO,ID_CHAT_MESSAGE,ID_PLAYER_STATS_DYNAMIC,
            ID_PLAYER_ATTRIBUTE,ID_PLAYER_SKILL,ID_PLAYER_LEVEL,ID_PLAYER_EQUIPMENT}),
        actorPacketController(p,{ID_ACTOR_AUTHORITY}),objectPacketController(p,{ID_OBJECT_ACTIVATE}),worldstatePacketController(p,{ID_WORLD_TIME}){
        local.guid=system.guid=peer->GetMyGUID();
    }
    LocalPlayer* getLocalPlayer(){return &local;}BaseSystem* getLocalSystem(){return &system;}
    TestPacket* getPlayerPacket(unsigned id){return playerPacketController.GetPacket(id);}
    void update(float);void beginConnectionRestore();bool completeConnectionRestore(RakNet::Packet*);
    void failConnectionRestore(const std::string&);void setGameplaySendEnabled(bool);
    void preInit(std::vector<std::string>&,std::vector<std::string>&,Files::Collections&);
    void receiveMessage(RakNet::Packet* p){
        if(p->systemAddress!=serverAddr)return;
        int section=0;
        switch(p->data[0]){
            case ID_PLAYER_STATS_DYNAMIC:section=1;break;case ID_PLAYER_ATTRIBUTE:section=2;break;
            case ID_PLAYER_SKILL:section=4;break;case ID_PLAYER_LEVEL:section=8;break;
            case ID_PLAYER_EQUIPMENT:section=16;break;
        }
        if(section)local.markLoginSyncReceived(section);
    }
};
}
