#include "Player.hpp"
#include "processors/ProcessorInitializer.hpp"
#include <RakPeer.h>
#ifndef __ANDROID__
#include <Kbhit.h>
#else
#define ARENAMP_ANDROID_NO_STDIN 1
#endif

#include <components/misc/stringops.hpp>
#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include <components/openmw-mp/Version.hpp>
#include <components/openmw-mp/Packets/PacketPreInit.hpp>

#include <iostream>
#include <algorithm>
#include <Script/Script.hpp>
#include <Script/API/TimerAPI.hpp>
#include <chrono>
#include <thread>
#include <random>
#include <sstream>
#include <iomanip>
#include <components/openmw-mp/serverstatus.hpp>
#include <csignal>

#include "Networking.hpp"
#include "MasterClient.hpp"
#include "LinkServer.hpp"
#include "Cell.hpp"
#include "CellController.hpp"
#include "processors/PlayerProcessor.hpp"
#include "processors/ActorProcessor.hpp"
#include "processors/ObjectProcessor.hpp"
#include "processors/WorldstateProcessor.hpp"

using namespace mwmp;

Networking *Networking::sThis = 0;

static int currentMpNum = 0;
static bool dataFileEnforcementState = true;
static bool scriptErrorIgnoringState = false;
bool killLoop = false;

Networking::Networking(RakNet::RakPeerInterface *peer)
    : startLocation("default"), mclient(nullptr), mLinkServer(nullptr)
{
    sThis = this;
    this->peer = peer;
    players = Players::getPlayers();

    CellController::create();

    systemPacketController = new SystemPacketController(peer);
    playerPacketController = new PlayerPacketController(peer);
    actorPacketController = new ActorPacketController(peer);
    objectPacketController = new ObjectPacketController(peer);
    worldstatePacketController = new WorldstatePacketController(peer);

    // Set send stream
    systemPacketController->SetStream(0, &bsOut);
    playerPacketController->SetStream(0, &bsOut);
    actorPacketController->SetStream(0, &bsOut);
    objectPacketController->SetStream(0, &bsOut);
    worldstatePacketController->SetStream(0, &bsOut);

    running = true;
    exitCode = 0;

    Script::Call<Script::CallbackIdentity("OnServerInit")>();

    serverPassword = TES3MP_DEFAULT_PASSW;

    ProcessorInitializer();
}

Networking::~Networking()
{
    Script::Call<Script::CallbackIdentity("OnServerExit")>(false);

    CellController::destroy();

    sThis = 0;
    delete systemPacketController;
    delete playerPacketController;
    delete actorPacketController;
    delete objectPacketController;
    delete worldstatePacketController;
}

void Networking::setServerPassword(std::string password) noexcept
{
    serverPassword = password.empty() ? TES3MP_DEFAULT_PASSW : password;
}

bool Networking::isPassworded() const
{
    return serverPassword != TES3MP_DEFAULT_PASSW;
}

void Networking::setStartLocation(const std::string& location)
{
    startLocation = location.empty() ? "default" : location;
}

const std::string& Networking::getStartLocation() const
{
    return startLocation;
}

void Networking::setVoiceEnabled(bool enabled)
{
    voiceEnabled = enabled;
}

bool Networking::isVoiceEnabled() const
{
    return voiceEnabled; // Respect the server setting for the native in-game relay.
}

void Networking::setVoiceRangeMeters(float meters)
{
    voiceRangeMeters = std::max(3.f, std::min(100.f, meters));
}

float Networking::getVoiceRangeMeters() const
{
    return voiceRangeMeters;
}

void Networking::processSystemPacket(RakNet::Packet *packet)
{
    Player *player = Players::getPlayer(packet->guid);

    SystemPacket *myPacket = systemPacketController->GetPacket(packet->data[0]);

    if (packet->data[0] == ID_SYSTEM_HANDSHAKE)
    {
        myPacket->setSystem(&baseSystem);
        myPacket->Read();

        if (!myPacket->isPacketValid())
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Invalid handshake packet from client at %s", packet->systemAddress.ToString());
            kickPlayer(player->guid);
            return;
        }

        if (player->isHandshaked())
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Wrong handshake with client at %s", packet->systemAddress.ToString());
            kickPlayer(player->guid);
            return;
        }

        if (baseSystem.serverPassword != serverPassword)
        {
            if (isPassworded())
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Wrong server password used by client at %s", packet->systemAddress.ToString());
                kickPlayer(player->guid);
                return;
            }
            else
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Client at %s tried to join using password, despite the server not being passworded",
                    packet->systemAddress.ToString());
            }
        }
        player->setHandshake();
        return;
    }
}

void Networking::processPlayerPacket(RakNet::Packet *packet)
{
    Player *player = Players::getPlayer(packet->guid);
    if (!player) return;
    if (packet->data[0] == ID_LOADED && awaitingResumeAcks.count(packet->guid))
    {
        // Y106: a resume ACK must never run OnPlayerConnect over the preserved
        // Lua Player. Only now is the client outside blocking preInit().
        if (packet->length == PlayerPacket::headerSize() && player->isHandshaked()
            && player->getLoadState() == Player::POSTLOADED)
            completeSessionResume(player);
        return;
    }
    // Permit chat/UI transport and cell subscriptions during the sentence, but
    // reject gameplay before Read() mutates the authoritative Player object.
    if (player && player->respawnLocked && packet->data[0] != ID_CHAT_MESSAGE
        && packet->data[0] != ID_GUI_MESSAGEBOX && packet->data[0] != ID_PLAYER_CELL_STATE)
        return;


    PlayerPacket *myPacket = playerPacketController->GetPacket(packet->data[0]);

    if (!player->isHandshaked())
    {
        player->incrementHandshakeAttempts();
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Have not completed handshake with client at %s", packet->systemAddress.ToString());
        LOG_APPEND(TimedLog::LOG_WARN, "- Attempts so far: %i", player->getHandshakeAttempts());

        if (player->getHandshakeAttempts() > 20)
            kickPlayer(player->guid, false);
        else if (player->getHandshakeAttempts() > 5)
            kickPlayer(player->guid, true);

        return;
    }

    if (packet->data[0] == ID_LOADED)
    {
        // Duplicate/late startup packets must not reset an authenticated session.
        if (player->getLoadState() != Player::NOTLOADED)
            return;
        player->setLoadState(Player::LOADED);

        unsigned short pid = Players::getPlayer(packet->guid)->getId();
        Script::Call<Script::CallbackIdentity("OnPlayerConnect")>(pid);

        if (player->getLoadState() == Player::KICKED) // kicked inside in OnPlayerConnect
        {
            playerPacketController->GetPacket(ID_USER_DISCONNECTED)->setPlayer(Players::getPlayer(packet->guid));
            playerPacketController->GetPacket(ID_USER_DISCONNECTED)->Send(false);
            Players::deletePlayer(packet->guid);
            return;
        }
    }
    else if (packet->data[0] == ID_PLAYER_BASEINFO)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received ID_PLAYER_BASEINFO about %s", player->npc.mName.c_str());

        // The server owns appearance after login/CharGen has been finalized.
        // Reject any late reply containing the client's temporary local ESM
        // `player` record (the stock Dark Elf template).
        if (player->isAppearanceAuthoritative())
        {
            LOG_APPEND(TimedLog::LOG_INFO,
                "- Ignoring late client BaseInfo; authoritative appearance remains race=%s head=%s hair=%s model=%s flags=%u",
                player->npc.mRace.c_str(), player->npc.mHead.c_str(), player->npc.mHair.c_str(),
                player->npc.mModel.c_str(), static_cast<unsigned int>(player->npc.mFlags));
            return;
        }

        myPacket->setPlayer(player);
        myPacket->Read();
        player->language = player->language == "RU" ? "RU" : "EN";
        LOG_APPEND(TimedLog::LOG_INFO, "- Client language: %s", player->language.c_str());
        LOG_APPEND(TimedLog::LOG_INFO,
            "- BaseInfo accepted privately during login/CharGen: race=%s head=%s hair=%s model=%s flags=%u",
            player->npc.mRace.c_str(), player->npc.mHead.c_str(), player->npc.mHair.c_str(),
            player->npc.mModel.c_str(), static_cast<unsigned int>(player->npc.mFlags));

        // A connecting player is deliberately hidden. Publishing this packet now
        // would let other clients instantiate the temporary Dark Elf before the
        // account/CharGen data is restored. revealPlayer() publishes one final,
        // ordered snapshot once the appearance becomes authoritative.
        if (player->isVisibleToOthers())
            player->sendToLoaded(myPacket);

        // X042: ID_PLAYER_BASEINFO is handled inline here; there is no
        // ProcessorPlayerBaseInfo registered on the server side. Falling
        // through to PlayerProcessor::Process() below therefore logged
        // "Unhandled PlayerPacket with identifier 142" on every login, which
        // is pure noise. The packet is fully processed at this point.
        if (player->getLoadState() == Player::POSTLOADED)
            return;
    }

    if (player->getLoadState() == Player::NOTLOADED)
        return;
    else if (player->getLoadState() == Player::LOADED)
    {
        player->setLoadState(Player::POSTLOADED);
        newPlayer(packet->guid);
        return;
    }


    if (!PlayerProcessor::Process(*packet))
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Unhandled PlayerPacket with identifier %i has arrived", packet->data[0]);

}

void Networking::processActorPacket(RakNet::Packet *packet)
{
    Player *player = Players::getPlayer(packet->guid);
    if (player && player->respawnLocked) return;


    if (!player->isHandshaked() || player->getLoadState() != Player::POSTLOADED)
        return;

    if (!ActorProcessor::Process(*packet, baseActorList))
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Unhandled ActorPacket with identifier %i has arrived", packet->data[0]);

}

void Networking::processObjectPacket(RakNet::Packet *packet)
{
    Player *player = Players::getPlayer(packet->guid);
    if (player && player->respawnLocked) return;


    if (!player->isHandshaked() || player->getLoadState() != Player::POSTLOADED)
        return;

    if (!ObjectProcessor::Process(*packet, baseObjectList))
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Unhandled ObjectPacket with identifier %i has arrived", packet->data[0]);

}

void Networking::processWorldstatePacket(RakNet::Packet *packet)
{
    Player *player = Players::getPlayer(packet->guid);
    if (player && player->respawnLocked) return;


    if (!player->isHandshaked() || player->getLoadState() != Player::POSTLOADED)
        return;

    if (!WorldstateProcessor::Process(*packet, baseWorldstate))
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Unhandled WorldstatePacket with identifier %i has arrived", packet->data[0]);

}

std::string Networking::generateResumeToken() const
{
    std::random_device rd;
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (int i = 0; i < 8; ++i)
        out << std::setw(8) << static_cast<uint32_t>(rd());
    return out.str();
}

void Networking::expireSuspendedPlayers()
{
    const auto now = std::chrono::steady_clock::now();
    std::vector<RakNet::RakNetGUID> expired;
    for (const auto& entry : suspendedSessions)
        if (entry.second.expiresAt <= now)
            expired.push_back(entry.first);

    for (const RakNet::RakNetGUID& guid : expired)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
            "Reconnect grace expired for guid %lu; completing disconnect", guid.g);
        suspendedSessions.erase(guid);
        disconnectPlayer(guid);
    }

    expired.clear();
    for (const auto& entry : awaitingResumeAcks)
        if (entry.second <= now)
            expired.push_back(entry.first);
    for (const auto& guid : expired)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "ArenaMP Y106: resume acknowledgement timed out");
        kickPlayer(guid);
        disconnectPlayer(guid);
    }
}

bool Networking::preInit(RakNet::Packet *packet, RakNet::BitStream &bsIn)
{
    if (packet->data[0] != ID_GAME_PREINIT)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "%s sent wrong first packet (ID_GAME_PREINIT was expected)",
                           packet->systemAddress.ToString());
        peer->CloseConnection(packet->systemAddress, true);
        return false;
    }

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received ID_GAME_PREINIT from %s", packet->systemAddress.ToString());
    PacketPreInit::PluginContainer dataFiles;
    PacketPreInit::PluginContainer clientGroundcover;

    PacketPreInit packetPreInit(peer);
    packetPreInit.SetReadStream(&bsIn);
    packetPreInit.setChecksums(&dataFiles);
    packetPreInit.setGroundcoverChecksums(&clientGroundcover);
    packetPreInit.Read();

    if (!packetPreInit.isPacketValid())
    {
        LOG_APPEND(TimedLog::LOG_ERROR, "- Packet was invalid");
        peer->CloseConnection(packet->systemAddress, false);
        return false;
    }

    const bool syncCapable = packetPreInit.supportsManifestSync();
    const uint8_t phase = packetPreInit.getManifestPhase();
    const bool resumeCapable = packetPreInit.supportsSessionResume();
    const std::string deviceHash = packetPreInit.getDeviceHash();
    const std::string requestedResumeToken = packetPreInit.getResumeToken();

    // ArenaMP FIX12: when enforcement is enabled, a new client first receives
    // the authoritative content order and optional groundcover list. No player
    // object is created yet, so this remains entirely before world/login state.
    if (dataFileEnforcementState && syncCapable && phase == PacketPreInit::PHASE_REQUEST)
    {
        LOG_APPEND(TimedLog::LOG_INFO,
            "- Sending launch manifest before hash verification (%d content, %d optional groundcover)",
            static_cast<int>(samples.size()), static_cast<int>(groundcoverSamples.size()));
        RakNet::BitStream manifestStream;
        packetPreInit.SetSendStream(&manifestStream);
        packetPreInit.setChecksums(&samples);
        packetPreInit.setGroundcoverChecksums(&groundcoverSamples);
        packetPreInit.setStartLocation(startLocation);
        packetPreInit.setManifestPhase(PacketPreInit::PHASE_MANIFEST);
        packetPreInit.Send(packet->systemAddress);
        return false;
    }

    bool matches = samples.size() == dataFiles.size();
    if (matches)
    {
        for (std::size_t i = 0; i < samples.size(); ++i)
        {
            const PacketPreInit::PluginPair& required = samples[i];
            const PacketPreInit::PluginPair& supplied = dataFiles[i];
            const unsigned suppliedHash = supplied.second.empty() ? 0 : supplied.second.front();
            LOG_APPEND(TimedLog::LOG_INFO, "- idx: %i\tchecksum: %X\tfile: %s",
                static_cast<int>(i), suppliedHash, supplied.first.c_str());

            if (!Misc::StringUtils::ciEqual(required.first, supplied.first) || supplied.second.empty())
            {
                matches = false;
                break;
            }

            // An empty required hash list means that any checksum is accepted.
            if (!required.second.empty()
                && std::find(required.second.begin(), required.second.end(), suppliedHash) == required.second.end())
            {
                matches = false;
                break;
            }
        }
    }

    RakNet::BitStream responseStream;
    packetPreInit.SetSendStream(&responseStream);
    packetPreInit.setStartLocation(startLocation);

    if (dataFileEnforcementState && !matches)
    {
        LOG_APPEND(TimedLog::LOG_INFO, "- Client was not allowed to connect due to incompatible data files");
        packetPreInit.setChecksums(&samples);
        packetPreInit.setGroundcoverChecksums(&groundcoverSamples);
        if (syncCapable)
            packetPreInit.setManifestPhase(PacketPreInit::PHASE_REJECTED);
        packetPreInit.Send(packet->systemAddress);
        peer->CloseConnection(packet->systemAddress, true);
        return false;
    }

    LOG_APPEND(TimedLog::LOG_INFO, "- Client was allowed to connect");

    // ArenaMP reconnect: a suspended logical player can be rebound to a new
    // RakNet GUID (and therefore a new IP/port) for 30 seconds. Both the
    // private process identity and one-time token must match; the token is rotated
    // immediately so captured/replayed pre-init data cannot be reused.
    RakNet::RakNetGUID suspendedGuid = RakNet::UNASSIGNED_CRABNET_GUID;
    if (resumeCapable && !deviceHash.empty() && !requestedResumeToken.empty())
    {
        const auto now = std::chrono::steady_clock::now();
        for (const auto& entry : suspendedSessions)
        {
            if (entry.second.expiresAt > now
                && entry.second.deviceHash == deviceHash
                && entry.second.token == requestedResumeToken)
            {
                suspendedGuid = entry.first;
                break;
            }
        }
        // The old transport may already be gone while its loss event is still
        // queued. Never replace a different live connection with a resume.
        if (suspendedGuid == RakNet::UNASSIGNED_CRABNET_GUID)
        {
            for (const auto& entry : resumeSessions)
            {
                if (entry.second.deviceHash == deviceHash && entry.second.token == requestedResumeToken
                    && peer->GetConnectionState(entry.first) != RakNet::IS_CONNECTED)
                {
                    suspendedGuid = entry.first;
                    break;
                }
            }
        }
    }

    Player* suspendedPlayer = suspendedGuid != RakNet::UNASSIGNED_CRABNET_GUID
        ? Players::getPlayer(suspendedGuid) : nullptr;
    const bool resumed = suspendedPlayer != nullptr
        && suspendedPlayer->isHandshaked() && suspendedPlayer->getLoadState() == Player::POSTLOADED
        && suspendedPlayer->isAppearanceAuthoritative()
        && Players::rebindPlayerGuid(suspendedGuid, packet->guid);

    // Y106: a failed resume is not a fresh login. Do not allocate a new slot or
    // reset Lua authentication while the client is still running the old world.
    const bool noFreeSlot = !resumed && Players::getPlayers()->size() >= maxConnections();
    if ((!requestedResumeToken.empty() && !resumed)
        || (!resumed && Players::doesPlayerExist(packet->guid)) || noFreeSlot)
    {
        PacketPreInit::PluginContainer empty;
        packetPreInit.setChecksums(&empty);
        packetPreInit.setGroundcoverChecksums(&empty);
        packetPreInit.setDeviceHash("");
        packetPreInit.setResumeToken("");
        packetPreInit.setIssuedResumeToken("");
        packetPreInit.setResumeAccepted(false);
        packetPreInit.setManifestPhase(PacketPreInit::PHASE_REJECTED);
        packetPreInit.Send(packet->systemAddress);
        peer->CloseConnection(packet->systemAddress, true);
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, noFreeSlot
            ? "ArenaMP Y106: login refused; logical player slots are reserved"
            : "ArenaMP Y106: resume refused; no matching resumable session");
        return false;
    }
    const std::string nextResumeToken = resumeCapable && !deviceHash.empty()
        ? generateResumeToken() : std::string();

    PacketPreInit::PluginContainer empty;
    packetPreInit.setChecksums(&empty);
    packetPreInit.setGroundcoverChecksums(&empty);
    packetPreInit.setDeviceHash("");
    packetPreInit.setResumeToken("");
    packetPreInit.setIssuedResumeToken(nextResumeToken);
    packetPreInit.setResumeAccepted(resumed);
    if (syncCapable)
        packetPreInit.setManifestPhase(PacketPreInit::PHASE_ACCEPTED);
    packetPreInit.Send(packet->systemAddress);

    ResumeSession liveSession;
    liveSession.deviceHash = deviceHash;
    liveSession.token = nextResumeToken;
    liveSession.expiresAt = std::chrono::steady_clock::time_point::max();

    if (resumed)
    {
        suspendedSessions.erase(suspendedGuid);
        resumeSessions.erase(suspendedGuid);
        resumeSessions[packet->guid] = liveSession;
        awaitingResumeAcks.erase(suspendedGuid);
        awaitingResumeAcks[packet->guid] = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        if (suspendedGuid != packet->guid && suspendedPlayer->isVisibleToOthers())
        {
            PlayerPacket* gone = playerPacketController->GetPacket(ID_USER_DISCONNECTED);
            gone->setPlayer(suspendedPlayer);
            gone->setGUID(suspendedGuid);
            gone->Send(true);
        }
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
            "ArenaMP Y106: session rebound; waiting for client resume acknowledgement");
        return true;
    }

    Players::newPlayer(packet->guid);
    if (resumeCapable && !deviceHash.empty())
        resumeSessions[packet->guid] = liveSession;
    systemPacketController->SetStream(&bsIn, nullptr);
    systemPacketController->GetPacket(ID_SYSTEM_HANDSHAKE)->RequestData(packet->guid);
    return true;
}

void Networking::update(RakNet::Packet *packet, RakNet::BitStream &bsIn)
{
    if (suspendedSessions.count(packet->guid)
        || (awaitingResumeAcks.count(packet->guid) && packet->data[0] != ID_LOADED))
        return;
    if (systemPacketController->ContainsPacket(packet->data[0]))
    {
        systemPacketController->SetStream(&bsIn, nullptr);
        processSystemPacket(packet);
    }
    else if (playerPacketController->ContainsPacket(packet->data[0]))
    {
        playerPacketController->SetStream(&bsIn, nullptr);
        processPlayerPacket(packet);
    }
    else if (actorPacketController->ContainsPacket(packet->data[0]))
    {
        actorPacketController->SetStream(&bsIn, 0);
        processActorPacket(packet);
    }
    else if (objectPacketController->ContainsPacket(packet->data[0]))
    {
        objectPacketController->SetStream(&bsIn, 0);
        processObjectPacket(packet);
    }
    else if (worldstatePacketController->ContainsPacket(packet->data[0]))
    {
        worldstatePacketController->SetStream(&bsIn, 0);
        processWorldstatePacket(packet);
    }
    else
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Unhandled RakNet packet with identifier %i has arrived", packet->data[0]);
}

void Networking::newPlayer(RakNet::RakNetGUID guid)
{
    // Do NOT request ID_PLAYER_BASEINFO here. ArenaMP already receives BaseInfo
    // before OnPlayerConnect. A second legacy request can return later with the
    // client's default ESM `player` (Dark Elf), racing and overwriting the final
    // authoritative appearance. Other runtime state is still requested normally.
    playerPacketController->GetPacket(ID_PLAYER_STATS_DYNAMIC)->RequestData(guid);
    playerPacketController->GetPacket(ID_PLAYER_POSITION)->RequestData(guid);
    playerPacketController->GetPacket(ID_PLAYER_CELL_CHANGE)->RequestData(guid);
    playerPacketController->GetPacket(ID_PLAYER_EQUIPMENT)->RequestData(guid);

    // FIX24: the login/registration presence gate is disabled. Players are visible
    // immediately and normal cell-overlap/interest synchronization decides when a
    // remote DedicatedPlayer is exchanged. We intentionally still do not issue the
    // legacy second BaseInfo RequestData here, which was the source of the Dunmer
    // appearance overwrite race fixed in FIX21.
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
        "Deferring remote player snapshots for %lu until authentication/CharGen is complete", guid.g);
}

void Networking::sendPlayerSnapshot(Player* player, RakNet::RakNetGUID destination)
{
    if (player == nullptr || destination == RakNet::UNASSIGNED_CRABNET_GUID)
        return;

    // BaseInfo must be first: it creates the DedicatedPlayer. Position and Cell
    // then place that already-valid race/head/hair model in the destination cell.
    static const unsigned char packetIds[] = {
        ID_PLAYER_BASEINFO,
        ID_PLAYER_SHAPESHIFT,
        ID_PLAYER_STATS_DYNAMIC,
        ID_PLAYER_ATTRIBUTE,
        ID_PLAYER_SKILL,
        ID_PLAYER_POSITION,
        ID_PLAYER_CELL_CHANGE,
        ID_PLAYER_EQUIPMENT,
        ID_PLAYER_ANIM_FLAGS
    };

    const bool previousExchangeState = player->exchangeFullInfo;
    player->exchangeFullInfo = true;

    for (unsigned char packetId : packetIds)
    {
        PlayerPacket* packet = playerPacketController->GetPacket(packetId);
        packet->setPlayer(player);
        packet->Send(destination);
    }

    player->exchangeFullInfo = previousExchangeState;
}

void Networking::exchangePlayerSnapshots(Player* player)
{
    if (player == nullptr || !player->isVisibleToOthers()
        || !player->isAppearanceAuthoritative()
        || player->getLoadState() != Player::POSTLOADED)
        return;

    player->forEachLoaded([this](Player* subject, Player* other)
    {
        if (other == nullptr || !other->isAppearanceAuthoritative()
            || other->getLoadState() != Player::POSTLOADED)
            return;

        sendPlayerSnapshot(subject, other->guid);
        sendPlayerSnapshot(other, subject->guid);

        LOG_APPEND(TimedLog::LOG_INFO,
            "- Exchanged authoritative cell-local snapshots: %s (%s/%s/%s) <-> %s (%s/%s/%s)",
            subject->npc.mName.c_str(), subject->npc.mRace.c_str(), subject->npc.mHead.c_str(),
            subject->npc.mHair.c_str(), other->npc.mName.c_str(), other->npc.mRace.c_str(),
            other->npc.mHead.c_str(), other->npc.mHair.c_str());
    });
}

void Networking::completeSessionResume(Player* player)
{
    awaitingResumeAcks.erase(player->guid);
    // Y106: send the current authoritative native profile, without a fresh Lua
    // login, disk reload, timestamp reset or account migration.
    const unsigned char profileIds[] = { ID_PLAYER_STATS_DYNAMIC, ID_PLAYER_ATTRIBUTE,
        ID_PLAYER_SKILL, ID_PLAYER_LEVEL, ID_PLAYER_EQUIPMENT };
    const bool previousExchangeState = player->exchangeFullInfo;
    player->exchangeFullInfo = true;
    for (unsigned char id : profileIds)
    {
        PlayerPacket* profile = playerPacketController->GetPacket(id);
        profile->setPlayer(player);
        profile->Send(player->guid);
    }
    player->exchangeFullInfo = previousExchangeState;

    exchangePlayerSnapshots(player);
    // The client kept its cells during the outage. Reassert authority (including
    // the resumed peer itself) after remapping any old GUID references.
    for (Cell* cell : *player->getCells())
    {
        if (!cell) continue;
        BaseActorList authority;
        authority.cell = cell->getActorList()->cell;
        authority.count = 0;
        authority.guid = *cell->getAuthority();
        if (authority.guid == RakNet::UNASSIGNED_CRABNET_GUID) continue;
        cell->sendCachedActorAI(authority.guid);
        ActorPacket* packet = actorPacketController->GetPacket(ID_ACTOR_AUTHORITY);
        packet->setActorList(&authority);
        packet->Send(authority.guid);
        cell->sendToLoaded(packet, &authority);
    }

    PlayerPacket* acknowledgement = playerPacketController->GetPacket(ID_LOADED);
    acknowledgement->setPlayer(player);
    acknowledgement->Send(player->guid);
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
        "ArenaMP Y106: resumed player %i; commands and activation enabled", player->getId());
}

void Networking::revealPlayer(Player* player)
{
    if (player == nullptr)
        return;

    player->setAppearanceAuthoritative(true);
    player->setVisibleToOthers(true);

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
        "Publishing authoritative appearance for %s (%i): race=%s head=%s hair=%s model=%s flags=%u",
        player->npc.mName.c_str(), player->getId(), player->npc.mRace.c_str(),
        player->npc.mHead.c_str(), player->npc.mHair.c_str(), player->npc.mModel.c_str(),
        static_cast<unsigned int>(player->npc.mFlags));

    // If loaded-cell membership is already known, publish immediately. If the
    // login teleport is still being processed, ProcessorPlayerCellState and
    // ProcessorPlayerCellChange call this exchange again once AOI overlap exists.
    exchangePlayerSnapshots(player);
}

void Networking::disconnectPlayer(RakNet::RakNetGUID guid)
{
    Player *player = Players::getPlayer(guid);
    if (!player)
        return;
    resumeSessions.erase(guid);
    suspendedSessions.erase(guid);
    awaitingResumeAcks.erase(guid);
    Script::Call<Script::CallbackIdentity("OnPlayerDisconnect")>(player->getId());

    if (player->isVisibleToOthers())
    {
        playerPacketController->GetPacket(ID_USER_DISCONNECTED)->setPlayer(player);
        playerPacketController->GetPacket(ID_USER_DISCONNECTED)->Send(true);
    }
    Players::deletePlayer(guid);
}

void Networking::suspendPlayer(RakNet::RakNetGUID guid)
{
    // Repeated loss/close events must not extend the original grace window.
    if (suspendedSessions.count(guid)) return;
    Player* player = Players::getPlayer(guid);
    auto sessionIt = resumeSessions.find(guid);
    if (player == nullptr || !player->isHandshaked() || player->getLoadState() != Player::POSTLOADED
        || !player->isAppearanceAuthoritative()
        || sessionIt == resumeSessions.end()
        || sessionIt->second.deviceHash.empty() || sessionIt->second.token.empty())
    {
        disconnectPlayer(guid);
        return;
    }

    ResumeSession suspended = sessionIt->second;
    suspended.expiresAt = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    suspendedSessions[guid] = suspended;
    resumeSessions.erase(sessionIt);
    awaitingResumeAcks.erase(guid);
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Holding player %i for 30 seconds for reconnect (guid %lu)", player->getId(), guid.g);
}

PlayerPacketController *Networking::getPlayerPacketController() const
{
    return playerPacketController;
}

ActorPacketController *Networking::getActorPacketController() const
{
    return actorPacketController;
}

ObjectPacketController *Networking::getObjectPacketController() const
{
    return objectPacketController;
}

WorldstatePacketController *Networking::getWorldstatePacketController() const
{
    return worldstatePacketController;
}

BaseActorList *Networking::getReceivedActorList()
{
    return &baseActorList;
}

BaseObjectList *Networking::getReceivedObjectList()
{
    return &baseObjectList;
}

BaseWorldstate *Networking::getReceivedWorldstate()
{
    return &baseWorldstate;
}

int Networking::getCurrentMpNum()
{
    return currentMpNum;
}

void Networking::setCurrentMpNum(int value)
{
    currentMpNum = value;
}

int Networking::incrementMpNum()
{
    currentMpNum++;
    Script::Call<Script::CallbackIdentity("OnMpNumIncrement")>(currentMpNum);
    return currentMpNum;
}

bool Networking::getDataFileEnforcementState()
{
    return dataFileEnforcementState;
}

void Networking::setDataFileEnforcementState(bool state)
{
    dataFileEnforcementState = state;
}

bool Networking::getScriptErrorIgnoringState()
{
    return scriptErrorIgnoringState;
}

void Networking::setScriptErrorIgnoringState(bool state)
{
    scriptErrorIgnoringState = state;
}

const Networking &Networking::get()
{
    return *sThis;
}


Networking *Networking::getPtr()
{
    return sThis;
}

RakNet::SystemAddress Networking::getSystemAddress(RakNet::RakNetGUID guid)
{
    return peer->GetSystemAddressFromGuid(guid);
}

void Networking::stopServer(int code)
{
    running = false;
    exitCode = code;
}

void signalHandler(int signum) 
{
    std::cout << "Interrupt signal (" << signum << ") received.\n";
    //15 is SIGTERM(Normal OS stop call), 2 is SIGINT(Ctrl+C)
    if(signum == 15 || signum == 2)
    {
        killLoop = true;
    }
}

void Networking::setLinkServer(LinkServer* server)
{
    mLinkServer = server;
}

void Networking::publishPlayerGlobalToLauncher(Player& player, const std::string& text)
{
    if (mLinkServer == nullptr || !mLinkServer->running() || text.empty() || player.npc.mName.empty())
        return;

    // Called by coreChat only after /// has passed its existing RP/command checks.
    // LinkServer distributes it to launcher clients without routing it back to Lua.
    mLinkServer->publishFromGame(player.npc.mName, text);
}

int Networking::mainLoop()
{
#ifdef __ANDROID__
    // ARENAMP_ANDROID_LOOP_RESET: a Service restart may follow a prior signal.
    killLoop = false;
#endif
    RakNet::Packet *packet;
    const auto statusStarted = std::chrono::steady_clock::now();
    std::int64_t lastStatusSecond = -1;

#ifndef _WIN32
    struct sigaction sigIntHandler;
    
    sigIntHandler.sa_handler = signalHandler;
    sigemptyset(&sigIntHandler.sa_mask);
    sigIntHandler.sa_flags = 0;
    sigaction(SIGTERM, &sigIntHandler, nullptr);
    sigaction(SIGINT, &sigIntHandler, nullptr);
#endif
    
    while (running && !killLoop)
    {
        // U034: the ArenaLink TCP worker only queues launcher -> game messages.
        // Drain them here, on the normal server/Lua thread. This is the crucial
        // thread boundary that keeps Lua and CoreScripts out of the TCP worker.
        if (mLinkServer != nullptr)
        {
            LinkServer::PendingGameChat pending;
            unsigned drained = 0;
            while (drained < 64 && mLinkServer->popPendingGameChat(pending))
            {
                Script::Call<Script::CallbackIdentity("OnLauncherGlobalChat")>(
                    pending.author.c_str(), pending.userId, pending.level, pending.color, pending.text.c_str());
                ++drained;
            }
        }

        // The same response is served by RakNet to offline pings on the game port.
        // Publish on the server thread; RakNet copies it under its own mutex.
        const auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - statusStarted).count();
        if (uptime != lastStatusSecond)
        {
            lastStatusSecond = uptime;
            unsigned online = 0;
            for (const auto& entry : *players)
                if (entry.second && entry.second->isHandshaked() && entry.second->getLoadState() != Player::KICKED)
                    ++online;
            const std::string status = ArenaStatus::payload(online, maxConnections(), static_cast<std::uint64_t>(uptime));
            peer->SetOfflinePingResponse(status.data(), static_cast<unsigned>(status.size()));
        }
#ifndef __ANDROID__
        if (kbhit() && getch() == '\n')
            break;
#endif
        // Y098: sustained traffic must yield to timers and suspended sessions.
        // Check the budget BEFORE Receive: never fetch a packet we cannot release.
        const auto packetDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(4);
        for (unsigned processed=0; processed<256 && (processed==0 || std::chrono::steady_clock::now()<packetDeadline)
            && (packet=peer->Receive())!=nullptr; ++processed, peer->DeallocatePacket(packet))
        {
            if (!packet->data || packet->length==0) continue;
            // ArenaMP Y052: mclient only exists when [MasterServer] enabled is
            // true. The dedicated-server default ships with it disabled, so this
            // used to dereference a null pointer for every incoming packet.
            if (getMasterClient() != nullptr && getMasterClient()->Process(packet))
                continue;

            switch (packet->data[0])
            {
                case ID_REMOTE_DISCONNECTION_NOTIFICATION:
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Client at %s has disconnected", packet->systemAddress.ToString());
                    break;
                case ID_REMOTE_CONNECTION_LOST:
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Client at %s has lost connection", packet->systemAddress.ToString());
                    break;
                case ID_REMOTE_NEW_INCOMING_CONNECTION:
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Client at %s has connected", packet->systemAddress.ToString());
                    break;
                case ID_CONNECTION_REQUEST_ACCEPTED:    // client to server
                {
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Our connection request has been accepted");
                    break;
                }
                case ID_NEW_INCOMING_CONNECTION:
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "A connection is incoming from %s", packet->systemAddress.ToString());
                    // RakPeer usually preserves its GUID across reconnects. A
                    // new transport for an existing player still needs token
                    // verification, even if the loss event has not arrived yet.
                    if (Players::doesPlayerExist(packet->guid))
                        suspendPlayer(packet->guid);
                    break;
                case ID_NO_FREE_INCOMING_CONNECTIONS:
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "The server is full");
                    break;
                case ID_DISCONNECTION_NOTIFICATION:
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,  "Client at %s has disconnected", packet->systemAddress.ToString());
                    // A stale close after ID_CONNECTION_LOST must not destroy
                    // the logical session held in the reconnect grace window.
                    if (!suspendedSessions.count(packet->guid)
                        && peer->GetConnectionState(packet->guid) != RakNet::IS_CONNECTED)
                        disconnectPlayer(packet->guid);
                    break;
                case ID_CONNECTION_LOST:
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Client at %s has lost connection", packet->systemAddress.ToString());
                    // A queued event can refer to the old transport of the same
                    // GUID. Do not suspend its already connected replacement.
                    if (peer->GetConnectionState(packet->guid) != RakNet::IS_CONNECTED)
                        suspendPlayer(packet->guid);
                    break;
                case ID_SND_RECEIPT_ACKED:
                case ID_CONNECTED_PING:
                case ID_UNCONNECTED_PING:
                case ID_UNCONNECTED_PING_OPEN_CONNECTIONS:
                    // Offline status queries are already answered by RakNet.
                    break;
                default:
                {
                    if (packet->length < 1u + static_cast<unsigned>(RakNet::RakNetGUID::size())) break;
                    RakNet::BitStream bsIn(&packet->data[1], packet->length - 1, false);
                    bsIn.IgnoreBytes((unsigned int) RakNet::RakNetGUID::size()); // Ignore GUID from received packet


                    // Y106: PREINIT is meaningful even when a suspended player
                    // still occupies the same GUID in Players.
                    if (packet->data[0] == ID_GAME_PREINIT)
                        preInit(packet, bsIn);
                    else if (Players::doesPlayerExist(packet->guid))
                        update(packet, bsIn);
                    else
                        preInit(packet, bsIn);
                    break;
                }
            }
        }
        expireSuspendedPlayers();
        TimerAPI::Tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    TimerAPI::Terminate();
    return exitCode;
}

void Networking::kickPlayer(RakNet::RakNetGUID guid, bool sendNotification)
{
    peer->CloseConnection(guid, sendNotification);
}

void Networking::banAddress(const char *ipAddress)
{
    peer->AddToBanList(ipAddress);
}

void Networking::unbanAddress(const char *ipAddress)
{
    peer->RemoveFromBanList(ipAddress);
}

unsigned short Networking::numberOfConnections() const
{
    return peer->NumberOfConnections();
}

unsigned int Networking::maxConnections() const
{
    return peer->GetMaximumIncomingConnections();
}

int Networking::getAvgPing(RakNet::AddressOrGUID addr) const
{
    return peer->GetAveragePing(addr);
}

unsigned short Networking::getPort() const
{
    return peer->GetMyBoundAddress().GetPort();
}

MasterClient *Networking::getMasterClient()
{
    return mclient;
}

void Networking::InitQuery(std::string queryAddr, unsigned short queryPort)
{
    mclient = new MasterClient(peer, queryAddr, queryPort);
}

void Networking::postInit()
{
    Script::Call<Script::CallbackIdentity("OnRequestDataFileList")>();
    Script::Call<Script::CallbackIdentity("OnServerPostInit")>();
}

PacketPreInit::PluginContainer &Networking::getSamples()
{
    return samples;
}

PacketPreInit::PluginContainer &Networking::getGroundcoverSamples()
{
    return groundcoverSamples;
}
