#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include "apps/openmw/mwmp/CraftEconomic.hpp"
constexpr int ID_CHAT_MESSAGE=42;
namespace MWWorld
{
    struct RefNum {unsigned mIndex=7;};
    struct Ref
    {
        std::string getRefId()const{return "robe";} int getCharge()const{return -1;}
        float getEnchantmentCharge()const{return -1;} std::string getSoul()const{return "";}
        RefNum getRefNum()const{return {};} unsigned getMpNum()const{return 0;}
    };
    struct Data {std::string getPoisonId()const{return "";} int getPoisonCharges()const{return 0;}};
    class Ptr;
    inline int skillRewards=0;
    struct Class {void skillUsageSucceeded(const Ptr&,int,int,float)const{++skillRewards;}};
    class Ptr {public: Class getClass()const{return {};}bool isEmpty()const{return false;} Ref getCellRef()const{return {};} Data getRefData()const{return {};}};
}
namespace MWBase
{
    struct Window {std::vector<std::string> messages;void messageBox(const std::string& s){messages.push_back(s);}};
    struct Environment {Window window;static Environment& get(){static Environment e;return e;}Window* getWindowManager(){return &window;}};
}
namespace mwmp
{
    struct LocalPlayer {std::string chatMessage="previous chat";bool logged=true;bool isLoggedIn(){return logged;}};
    struct Packet
    {
        LocalPlayer* player=nullptr;std::vector<std::string> sent;
        void setPlayer(LocalPlayer* p){player=p;}void Send(){sent.push_back(player->chatMessage);}
    };
    struct Networking {bool connected=true;Packet packet;bool isConnected(){return connected;}Packet* getPlayerPacket(int){return &packet;}};
    struct Main
    {
        Networking net;LocalPlayer player;static Main& get(){static Main m;return m;}
        Networking* getNetworking(){return &net;}LocalPlayer* getLocalPlayer(){return &player;}
    };
}

namespace ESM {struct Skill {enum {Enchant=9,Mercantile=24};};}
namespace MWMechanics {inline MWWorld::Ptr getPlayer(){return {};}
namespace XPLeveling {inline int trades=0;inline void awardSuccessfulTrade(const MWWorld::Ptr&){++trades;}}}
