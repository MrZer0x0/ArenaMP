#ifndef OPENMW_NETWORKING_HPP
#define OPENMW_NETWORKING_HPP

#include <RakPeerInterface.h>
#include <BitStream.h>
#include <string>
#include <vector>
#include <chrono>
#include <map>

#include <components/openmw-mp/NetworkMessages.hpp>

#include <components/openmw-mp/Controllers/SystemPacketController.hpp>
#include <components/openmw-mp/Controllers/PlayerPacketController.hpp>
#include <components/openmw-mp/Controllers/ActorPacketController.hpp>
#include <components/openmw-mp/Controllers/ObjectPacketController.hpp>
#include <components/openmw-mp/Controllers/WorldstatePacketController.hpp>

#include <components/files/collections.hpp>

#include "LocalSystem.hpp"
#include "ActorList.hpp"
#include "ObjectList.hpp"
#include "Worldstate.hpp"

namespace mwmp
{
    class LocalPlayer;

    class Networking
    {
    public:
        Networking();
        ~Networking();
        void connect(const std::string& ip, unsigned short port, std::vector<std::string> &content, std::vector<std::string> &groundcover, Files::Collections &collections);
        void update(float dt);

        SystemPacket *getSystemPacket(RakNet::MessageID id);
        PlayerPacket *getPlayerPacket(RakNet::MessageID id);
        ActorPacket *getActorPacket(RakNet::MessageID id);
        ObjectPacket *getObjectPacket(RakNet::MessageID id);
        WorldstatePacket *getWorldstatePacket(RakNet::MessageID id);

        RakNet::SystemAddress serverAddress()
        {
            return serverAddr;
        }

        bool isConnected();
        bool isRestoringConnection() const { return mRestoringConnection; }
        float getRestoreRemaining() const { return mRestoreRemaining; }
        float getRestoreDuration() const { return 30.f; }

        LocalSystem *getLocalSystem();
        LocalPlayer *getLocalPlayer();
        ActorList *getActorList();
        ObjectList *getObjectList();
        Worldstate *getWorldstate();
        const std::string& getStartLocation() const;

    private:
        bool connected;
        bool mRestoringConnection = false;
        bool mAwaitingRestoreAck = false;
        bool mRestoreAckReceived = false;
        bool mResumeAccepted = false;
        float mRestoreRemaining = 0.f;
        float mReconnectAttemptCooldown = 0.f;
        std::chrono::steady_clock::time_point mRestoreDeadline;
        std::string mSessionIdentity;
        std::string mResumeToken;
        std::string mRestoreError;
        std::map<std::string, unsigned> mContentChecksums;
        std::string mConnectPassword;
        std::vector<std::string> mReconnectContent;
        std::vector<std::string> mReconnectGroundcover;
        Files::Collections* mReconnectCollections = nullptr;
        std::string startLocation;
        RakNet::RakPeerInterface *peer;
        RakNet::SystemAddress serverAddr;
        RakNet::BitStream bsOut;

        SystemPacketController systemPacketController;
        PlayerPacketController playerPacketController;
        ActorPacketController actorPacketController;
        ObjectPacketController objectPacketController;
        WorldstatePacketController worldstatePacketController;

        ActorList actorList;
        ObjectList objectList;
        Worldstate worldstate;

        void receiveMessage(RakNet::Packet *packet);
        void beginConnectionRestore();
        bool completeConnectionRestore(RakNet::Packet* packet);
        void failConnectionRestore(const std::string& message);
        void setGameplaySendEnabled(bool enabled);

        void preInit(std::vector<std::string> &content, std::vector<std::string> &groundcover, Files::Collections &collections);
    };
}


#endif //OPENMW_NETWORKING_HPP
