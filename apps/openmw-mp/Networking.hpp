#ifndef OPENMW_NETWORKING_HPP
#define OPENMW_NETWORKING_HPP

#include <components/openmw-mp/Controllers/SystemPacketController.hpp>
#include <components/openmw-mp/Controllers/PlayerPacketController.hpp>
#include <components/openmw-mp/Controllers/ActorPacketController.hpp>
#include <components/openmw-mp/Controllers/ObjectPacketController.hpp>
#include <components/openmw-mp/Controllers/WorldstatePacketController.hpp>
#include <components/openmw-mp/Packets/PacketPreInit.hpp>
#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <unordered_map>
#include "Player.hpp"

class MasterClient;
namespace  mwmp
{
    class LinkServer;
    class Networking
    {
    public:
        Networking(RakNet::RakPeerInterface *peer);
        ~Networking();

        void newPlayer(RakNet::RakNetGUID guid);
        void revealPlayer(Player* player);
        void exchangePlayerSnapshots(Player* player);
        void disconnectPlayer(RakNet::RakNetGUID guid);
        void suspendPlayer(RakNet::RakNetGUID guid);
        void kickPlayer(RakNet::RakNetGUID guid, bool sendNotification = true);
        
        void banAddress(const char *ipAddress);
        void unbanAddress(const char *ipAddress);
        RakNet::SystemAddress getSystemAddress(RakNet::RakNetGUID guid);

        void processSystemPacket(RakNet::Packet *packet);
        void processPlayerPacket(RakNet::Packet *packet);
        void processActorPacket(RakNet::Packet *packet);
        void processObjectPacket(RakNet::Packet *packet);
        void processWorldstatePacket(RakNet::Packet *packet);
        void update(RakNet::Packet *packet, RakNet::BitStream &bsIn);

        unsigned short numberOfConnections() const;
        unsigned int maxConnections() const;
        int getAvgPing(RakNet::AddressOrGUID) const;
        unsigned short getPort() const;

        int mainLoop();

        void setLinkServer(LinkServer* server);
        void publishPlayerGlobalToLauncher(Player& player, const std::string& text);

        void stopServer(int code);

        SystemPacketController *getSystemPacketController() const;
        PlayerPacketController *getPlayerPacketController() const;
        ActorPacketController *getActorPacketController() const;
        ObjectPacketController *getObjectPacketController() const;
        WorldstatePacketController *getWorldstatePacketController() const;

        BaseActorList *getReceivedActorList();
        BaseObjectList *getReceivedObjectList();
        BaseWorldstate *getReceivedWorldstate();

        int getCurrentMpNum();
        void setCurrentMpNum(int value);
        int incrementMpNum();

        bool getDataFileEnforcementState();
        void setDataFileEnforcementState(bool state);

        bool getScriptErrorIgnoringState();
        void setScriptErrorIgnoringState(bool state);

        MasterClient *getMasterClient();
        void InitQuery(std::string queryAddr, unsigned short queryPort);
        void setServerPassword(std::string passw) noexcept;
        bool isPassworded() const;
        void setStartLocation(const std::string& location);
        const std::string& getStartLocation() const;
        void setVoiceEnabled(bool enabled);
        bool isVoiceEnabled() const;
        void setVoiceRangeMeters(float meters);
        float getVoiceRangeMeters() const;

        static const Networking &get();
        static Networking *getPtr();

        void postInit();

        PacketPreInit::PluginContainer &getSamples();
        PacketPreInit::PluginContainer &getGroundcoverSamples();
    private:
        struct ResumeSession
        {
            std::string deviceHash;
            std::string token;
            std::chrono::steady_clock::time_point expiresAt;
        };

        void sendPlayerSnapshot(Player* player, RakNet::RakNetGUID destination);
        void completeSessionResume(Player* player);
        bool preInit(RakNet::Packet *packet, RakNet::BitStream &bsIn);
        std::string generateResumeToken() const;
        void expireSuspendedPlayers();
        // ArenaMP Y111: fair per-connection packet scheduling (see mainLoop).
        void processIncomingPacket(RakNet::Packet *packet);
        void reportSlowPacket(unsigned char packetId, RakNet::RakNetGUID guid,
            std::chrono::steady_clock::duration elapsed);
        std::string serverPassword;
        std::string startLocation;
        bool voiceEnabled = true;
        float voiceRangeMeters = 30.f;
        static Networking *sThis;

        RakNet::RakPeerInterface *peer;
        RakNet::BitStream bsOut;
        TPlayers *players;
        MasterClient *mclient;
        LinkServer* mLinkServer;

        BaseSystem baseSystem;
        BaseActorList baseActorList;
        BaseObjectList baseObjectList;
        BaseWorldstate baseWorldstate;

        SystemPacketController *systemPacketController;
        PlayerPacketController *playerPacketController;
        ActorPacketController *actorPacketController;
        ObjectPacketController *objectPacketController;
        WorldstatePacketController *worldstatePacketController;

        bool running;
        int exitCode;
        PacketPreInit::PluginContainer samples;
        PacketPreInit::PluginContainer groundcoverSamples;
        std::map<RakNet::RakNetGUID, ResumeSession> resumeSessions;
        std::map<RakNet::RakNetGUID, ResumeSession> suspendedSessions;
        std::map<RakNet::RakNetGUID, std::chrono::steady_clock::time_point> awaitingResumeAcks;

        // ArenaMP Y111: packets fetched from RakNet, one FIFO per transport GUID,
        // served round-robin so one client's burst cannot starve the others.
        static constexpr std::size_t MAX_LOCAL_PACKET_BACKLOG = 8192;
        static constexpr long long SLOW_PACKET_WARN_MS = 50;
        std::unordered_map<std::uint64_t, std::deque<RakNet::Packet*>> packetQueues;
        std::deque<std::uint64_t> packetSchedule;
        std::size_t queuedPackets = 0;
        unsigned slowPacketsSinceReport = 0;
        long long slowPacketMsSinceReport = 0;
        std::chrono::steady_clock::time_point nextSlowPacketReport{};
    };
}


#endif //OPENMW_NETWORKING_HPP
