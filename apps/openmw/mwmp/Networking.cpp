#include <stdexcept>
#include <algorithm>
#include <iostream>
#include <string>
#include <random>

#include <components/openmw-mp/TimedLog.hpp>
#include <components/openmw-mp/Utils.hpp>
#include <components/openmw-mp/Version.hpp>
#include <components/openmw-mp/Packets/PacketPreInit.hpp>

#include <components/esm/cellid.hpp>
#include <components/files/configurationmanager.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"

#include "../mwclass/npc.hpp"

#include "../mwmechanics/combat.hpp"
#include "../mwmechanics/npcstats.hpp"

#include "../mwstate/statemanagerimp.hpp"

#include "../mwworld/cellstore.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/inventorystore.hpp"

#include <SDL_messagebox.h>
#include <RakSleep.h>
#include <iomanip>
#include <components/version/version.hpp>

#include "Networking.hpp"
#include "Main.hpp"
#include "LocalPlayer.hpp"
#include "processors/ProcessorInitializer.hpp"
#include "processors/SystemProcessor.hpp"
#include "processors/PlayerProcessor.hpp"
#include "processors/ObjectProcessor.hpp"
#include "processors/ActorProcessor.hpp"
#include "processors/WorldstateProcessor.hpp"
#include "GUIController.hpp"
#include "CellController.hpp"

using namespace mwmp;

std::string listDiscrepancies(PacketPreInit::PluginContainer checksums, PacketPreInit::PluginContainer checksumsResponse)
{
    std::ostringstream sstr;
    sstr << "Your plugins or their load order don't match the server's. A full comparison is included in your debug window and latest log file. In short, the following discrepancies have been found:\n\n";

    int discrepancyCount = 0;

    for (size_t fileIndex = 0; fileIndex < checksums.size() || fileIndex < checksumsResponse.size(); fileIndex++)
    {
        if (fileIndex >= checksumsResponse.size())
        {
            discrepancyCount++;

            if (discrepancyCount > 1)
                sstr << "\n";

            std::string clientFilename = checksums.at(fileIndex).first;

            sstr << fileIndex << ": ";
            sstr << clientFilename << " is past the number of plugins used by the server";
        }
        else if (fileIndex >= checksums.size())
        {
            discrepancyCount++;

            if (discrepancyCount > 1)
                sstr << "\n";

            std::string serverFilename = checksumsResponse.at(fileIndex).first;

            sstr << fileIndex << ": ";
            sstr << serverFilename << " is completely missing from the client but required by the server";
        }
        else
        {
            std::string clientFilename = checksums.at(fileIndex).first;
            std::string serverFilename = checksumsResponse.at(fileIndex).first;

            std::string clientChecksum = Utils::intToHexStr(checksums.at(fileIndex).second.at(0));

            bool filenameMatches = false;
            bool checksumMatches = false;
            std::string eligibleChecksums = "";

            if (Misc::StringUtils::ciEqual(clientFilename, serverFilename))
                filenameMatches = true;

            if (checksumsResponse.at(fileIndex).second.size() > 0)
            {
                for (size_t checksumIndex = 0; checksumIndex < checksumsResponse.at(fileIndex).second.size(); checksumIndex++)
                {
                    std::string serverChecksum = Utils::intToHexStr(checksumsResponse.at(fileIndex).second.at(checksumIndex));

                    if (checksumIndex != 0)
                        eligibleChecksums = eligibleChecksums + " or ";

                    eligibleChecksums = eligibleChecksums + serverChecksum;

                    if (Misc::StringUtils::ciEqual(clientChecksum, serverChecksum))
                    {
                        checksumMatches = true;
                        break;
                    }
                }
            }
            else
                checksumMatches = true;

            if (!filenameMatches || !checksumMatches)
            {
                discrepancyCount++;

                if (discrepancyCount > 1)
                    sstr << "\n";

                sstr << fileIndex << ": ";

                if (!filenameMatches)
                    sstr << clientFilename << " doesn't match " << serverFilename;

                if (!filenameMatches && !checksumMatches)
                    sstr << ", ";

                if (!checksumMatches)
                    sstr << "checksum " << clientChecksum << " doesn't match " << eligibleChecksums;
            }
        }
    }

    return sstr.str();
}

std::string listComparison(PacketPreInit::PluginContainer checksums, PacketPreInit::PluginContainer checksumsResponse,
                      bool full = false)
{
    std::ostringstream sstr;
    size_t pluginNameLen1 = 0;
    size_t pluginNameLen2 = 0;
    for (const auto &checksum : checksums)
        if (pluginNameLen1 < checksum.first.size())
            pluginNameLen1 = checksum.first.size();

    for (const auto &checksum : checksums)
        if (pluginNameLen2 < checksum.first.size())
            pluginNameLen2 = checksum.first.size();

    Utils::printWithWidth(sstr, "Your current plugins are:", pluginNameLen1 + 16);
    sstr << "To join this server, use:\n";

    Utils::printWithWidth(sstr, "name", pluginNameLen1 + 2);
    Utils::printWithWidth(sstr, "hash", 14);
    Utils::printWithWidth(sstr, "name", pluginNameLen2 + 2);
    sstr << "hash\n";

    for (size_t i = 0; i < checksums.size() || i < checksumsResponse.size(); i++)
    {
        std::string plugin;
        unsigned val;

        if (i < checksums.size())
        {
            plugin = checksums.at(i).first;
            val = checksums.at(i).second[0];

            Utils::printWithWidth(sstr, plugin, pluginNameLen1 + 2);
            Utils::printWithWidth(sstr, Utils::intToHexStr(val), 14);
        }
        else
            Utils::printWithWidth(sstr, "", pluginNameLen1 + 16);

        if (i < checksumsResponse.size())
        {
            Utils::printWithWidth(sstr, checksumsResponse[i].first, pluginNameLen2 + 2);
            if (checksumsResponse[i].second.size() > 0)
            {
                if (full)
                    for (size_t j = 0; j < checksumsResponse[i].second.size(); j++)
                        Utils::printWithWidth(sstr, Utils::intToHexStr(checksumsResponse[i].second[j]), 14);
                else
                    sstr << Utils::intToHexStr(checksumsResponse[i].second[0]);
            }
            else
                sstr << "any";
        }

        sstr << "\n";
    }

    return sstr.str();
}

Networking::Networking(): startLocation("default"), peer(RakNet::RakPeerInterface::GetInstance()), systemPacketController(peer),
    playerPacketController(peer), actorPacketController(peer), objectPacketController(peer),
    worldstatePacketController(peer)
{

    RakNet::SocketDescriptor sd;
    sd.port=0;
    auto b = peer->Startup(1, &sd, 1);
    RakAssert(b==RakNet::CRABNET_STARTED);

    systemPacketController.SetStream(0, &bsOut);
    playerPacketController.SetStream(0, &bsOut);
    actorPacketController.SetStream(0, &bsOut);
    objectPacketController.SetStream(0, &bsOut);
    worldstatePacketController.SetStream(0, &bsOut);

    connected = 0;
    ProcessorInitializer();
}

Networking::~Networking()
{
    peer->Shutdown(100);
    peer->CloseConnection(peer->GetSystemAddressFromIndex(0), true, 0);
    RakNet::RakPeerInterface::DestroyInstance(peer);
}

void Networking::update(float dt)
{
    RakNet::Packet *packet;
    std::string errmsg = "";

    for (packet=peer->Receive(); packet; peer->DeallocatePacket(packet), packet=peer->Receive())
    {
        if (!packet->data || packet->length == 0)
            continue;
        switch (packet->data[0])
        {
            case ID_REMOTE_DISCONNECTION_NOTIFICATION:
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Another client has disconnected.");
                break;
            case ID_REMOTE_CONNECTION_LOST:
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Another client has lost connection.");
                break;
            case ID_REMOTE_NEW_INCOMING_CONNECTION:
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Another client has connected.");
                break;
            case ID_CONNECTION_REQUEST_ACCEPTED:
                if (mRestoringConnection)
                    completeConnectionRestore(packet);
                else
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Our connection request has been accepted.");
                break;
            case ID_NEW_INCOMING_CONNECTION:
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "A connection is incoming.");
                break;
            case ID_NO_FREE_INCOMING_CONNECTIONS:
                errmsg = "The server is full.";
                break;
            case ID_DISCONNECTION_NOTIFICATION:
                if (peer->GetConnectionState(serverAddr) != RakNet::IS_CONNECTED)
                    errmsg = "We have been disconnected.";
                break;
            case ID_CONNECTION_LOST:
                if (peer->GetConnectionState(serverAddr) != RakNet::IS_CONNECTED)
                    beginConnectionRestore();
                break;
            case ID_CONNECTION_ATTEMPT_FAILED:
                if (!mRestoringConnection)
                    errmsg = "Connection attempt failed.";
                break;
            case ID_LOADED:
                // Y106: ID_LOADED is a resume acknowledgement only here. Initial
                // login keeps its existing client -> server direction.
                if (mAwaitingRestoreAck && packet->systemAddress == serverAddr
                    && packet->length == PlayerPacket::headerSize())
                {
                    RakNet::BitStream ack(&packet->data[1], packet->length - 1, false);
                    RakNet::RakNetGUID guid;
                    if (ack.Read(guid) && guid == peer->GetMyGUID())
                        mRestoreAckReceived = true;
                }
                break;
            default:
                receiveMessage(packet);
                //LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Message with identifier %i has arrived.", packet->data[0]);
                break;
        }
    }

    if (mRestoringConnection)
    {
        connected = false;
        const float step = std::max(0.f, dt);
        mRestoreRemaining = std::max(0.f, std::chrono::duration<float>(
            mRestoreDeadline - std::chrono::steady_clock::now()).count());
        mReconnectAttemptCooldown -= step;
        if (mAwaitingRestoreAck && mRestoreAckReceived && getLocalPlayer()
            && !getLocalPlayer()->isLoginSyncPending())
        {
            mRestoringConnection = false;
            mAwaitingRestoreAck = false;
            mRestoreRemaining = 0.f;
            connected = true;
            setGameplaySendEnabled(true);
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
                "ArenaMP Y106: session restored; server acknowledgement and full profile received");
        }
        else if (mRestoreRemaining <= 0.f)
            failConnectionRestore("Connection could not be restored within 30 seconds. Please reconnect to the server.");
        else if (!mAwaitingRestoreAck && mReconnectAttemptCooldown <= 0.f && !mConnectPassword.empty())
        {
            mReconnectAttemptCooldown = 2.f;
            peer->Connect(serverAddr.ToString(false), serverAddr.GetPort(),
                mConnectPassword.c_str(), static_cast<int>(mConnectPassword.size()), 0, 0, 3, 500, 0);
        }
    }

    if (!mRestoreError.empty())
    {
        errmsg = mRestoreError;
        mRestoreError.clear();
    }

    if (!errmsg.empty())
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, errmsg.c_str());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "tes3mp", errmsg.c_str(), 0);
        MWBase::Environment::get().getStateManager()->requestQuit();
    }
}

void Networking::beginConnectionRestore()
{
    connected = false;
    mAwaitingRestoreAck = false;
    mRestoreAckReceived = false;
    setGameplaySendEnabled(false);
    if (mRestoringConnection)
        return;
    if (mSessionIdentity.empty() || mResumeToken.empty())
    {
        failConnectionRestore("This server did not provide a resumable session. Please reconnect to the server.");
        return;
    }
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "ArenaMP: connection lost; starting 30 second restore window");
    mRestoringConnection = true;
    mRestoreRemaining = 30.f;
    mRestoreDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    mReconnectAttemptCooldown = 0.f;
}

bool Networking::completeConnectionRestore(RakNet::Packet* packet)
{
    if (!packet || !mRestoringConnection || mAwaitingRestoreAck)
        return false;
    serverAddr = packet->systemAddress;
    BaseClientPacketProcessor::SetServerAddr(serverAddr);
    connected = true;
    mResumeAccepted = false;
    if (mReconnectCollections)
        preInit(mReconnectContent, mReconnectGroundcover, *mReconnectCollections);
    if (!connected || !mResumeAccepted)
    {
        failConnectionRestore("The server could not restore the previous session. Please reconnect and log in again.");
        return false;
    }
    LocalPlayer* local = getLocalPlayer();
    if (local)
    {
        local->guid = getLocalSystem()->guid = peer->GetMyGUID();
        local->beginLoginSync(true);
        mAwaitingRestoreAck = true;
        mRestoreAckReceived = false;
        connected = false;
        getPlayerPacket(ID_LOADED)->setPlayer(local);
        getPlayerPacket(ID_LOADED)->Send();
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
            "ArenaMP Y106: resume accepted; waiting for server acknowledgement and profile");
        return true;
    }
    failConnectionRestore("Local player is unavailable during session restore.");
    return false;
}

void Networking::failConnectionRestore(const std::string& message)
{
    connected = false;
    mRestoringConnection = false;
    mAwaitingRestoreAck = false;
    mRestoreAckReceived = false;
    mRestoreRemaining = 0.f;
    mRestoreError = message;
    setGameplaySendEnabled(false);
    peer->CloseConnection(serverAddr, true);
}

void Networking::setGameplaySendEnabled(bool enabled)
{
    // This also covers packets sent directly by input/UI handlers, outside
    // Main::frame. Never queue stale activations or commands across the outage.
    for (unsigned id = 0; id < 256; ++id)
    {
        if (systemPacketController.ContainsPacket(id))
            systemPacketController.GetPacket(id)->setSendEnabled(enabled);
        if (playerPacketController.ContainsPacket(id))
            playerPacketController.GetPacket(id)->setSendEnabled(enabled || id == ID_LOADED);
        if (actorPacketController.ContainsPacket(id))
            actorPacketController.GetPacket(id)->setSendEnabled(enabled);
        if (objectPacketController.ContainsPacket(id))
            objectPacketController.GetPacket(id)->setSendEnabled(enabled);
        if (worldstatePacketController.ContainsPacket(id))
            worldstatePacketController.GetPacket(id)->setSendEnabled(enabled);
    }
}

void Networking::connect(const std::string &ip, unsigned short port, std::vector<std::string> &content, std::vector<std::string> &groundcover, Files::Collections &collections)
{
    RakNet::SystemAddress master;
    master.SetBinaryAddress(ip.c_str());
    master.SetPortHostOrder(port);
    std::string errmsg = "";

    std::stringstream sstr;
    sstr << TES3MP_VERSION;

    // X031: ArenaMP no longer impersonates the vanilla TES3MP build identity.
    // Every client advertises the ArenaMP protocol and stable compatibility hash.
    const int advertisedProtocol = TES3MP_PROTO_VERSION;
    sstr << advertisedProtocol;

    std::string commitHashString = TES3MP_COMPAT_COMMIT_HASH;

    // Remove carriage returns added to version files on Windows.
    commitHashString.erase(std::remove(commitHashString.begin(), commitHashString.end(), '\r'), commitHashString.end());
    sstr << commitHashString;

    mConnectPassword = sstr.str();
    // Y106: private, in-memory session identity; no hardware/profile data.
    std::random_device random;
    std::ostringstream identity;
    identity << std::hex << std::setfill('0');
    for (int i = 0; i < 8; ++i)
        identity << std::setw(8) << static_cast<uint32_t>(random());
    mSessionIdentity = identity.str();
    mResumeToken.clear();
    mContentChecksums.clear();
    mResumeAccepted = false;
    setGameplaySendEnabled(true);
    mReconnectContent = content;
    mReconnectGroundcover = groundcover;
    mReconnectCollections = &collections;

    if (peer->Connect(master.ToString(false), master.GetPort(), mConnectPassword.c_str(),
            static_cast<int>(mConnectPassword.size()), 0, 0, 3, 500, 0) != RakNet::CONNECTION_ATTEMPT_STARTED)
        errmsg = "Connection attempt failed.\n";

    bool queue = true;
    while (queue)
    {
        for (RakNet::Packet *packet = peer->Receive(); packet; peer->DeallocatePacket(packet), packet = peer->Receive())
        {
            switch (packet->data[0])
            {
                case ID_CONNECTION_ATTEMPT_FAILED:
                {
                    errmsg = "Connection failed.\n"
                            "Either the IP address is wrong or a firewall on either system is blocking\n"
                            "UDP packets on the port you have chosen.";
                    queue = false;
                    break;
                }
                case ID_INVALID_PASSWORD:
                {
                    errmsg = "Version mismatch!\nYour client is on version " TES3MP_VERSION "\n"
                        "Please make sure the server is on the same version.";
                    queue = false;
                    break;
                }
                case ID_INCOMPATIBLE_PROTOCOL_VERSION:
                {
                    errmsg = "Network protocol mismatch!\nMake sure your client is really on the same version\n"
                        "as the server you are trying to connect to.";
                    queue = false;
                    break;
                }
                case ID_CONNECTION_REQUEST_ACCEPTED:
                {
                    serverAddr = packet->systemAddress;
                    BaseClientPacketProcessor::SetServerAddr(packet->systemAddress);

                    connected = true;
                    queue = false;

                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Received ID_CONNECTION_REQUESTED_ACCEPTED from %s",
                                       serverAddr.ToString());

                    break;
                }
                case ID_DISCONNECTION_NOTIFICATION:
                    throw std::runtime_error("ID_DISCONNECTION_NOTIFICATION.\n");
                case ID_CONNECTION_BANNED:
                    throw std::runtime_error("You have been banned from this server.\n");
                case ID_CONNECTION_LOST:
                    throw std::runtime_error("ID_CONNECTION_LOST.\n");
                default:
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Connection message with identifier %i has arrived in initialization.",
                                       packet->data[0]);
            }
        }
    }

    if (!errmsg.empty())
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, errmsg.c_str());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "tes3mp", errmsg.c_str(), 0);
    }
    else
        preInit(content, groundcover, collections);

    getLocalPlayer()->guid = getLocalSystem()->guid = peer->GetMyGUID();
}

void Networking::preInit(std::vector<std::string> &content, std::vector<std::string> &groundcover, Files::Collections &collections)
{
    // The running world still uses the files verified at launch. Avoid rereading
    // gigabytes of ESM data twice per reconnect while the grace timer is ticking.
    auto checksumFor = [&](const std::string& path) -> unsigned
    {
        const auto cached = mContentChecksums.find(path);
        if (cached != mContentChecksums.end()) return cached->second;
        const unsigned checksum = Utils::crc32Checksum(path);
        mContentChecksums.emplace(path, checksum);
        return checksum;
    };
    auto makeChecksums = [&](const std::vector<std::string>& files, PacketPreInit::PluginContainer& output)
    {
        output.clear();
        int idx = 0;
        for (const std::string& name : files)
        {
            boost::filesystem::path filename(name);
            const Files::MultiDirCollection& col = collections.getCollection(filename.extension().string());
            if (!col.doesExist(name))
                throw std::runtime_error("Plugin doesn't exist: " + name);

            PacketPreInit::HashList hashList;
            const unsigned crc32 = checksumFor(col.getPath(name).string());
            hashList.push_back(crc32);
            output.push_back(std::make_pair(name, hashList));
            LOG_APPEND(TimedLog::LOG_WARN, "idx: %d\tchecksum: %X\tfile: %s\n",
                idx++, crc32, col.getPath(name).string().c_str());
        }
    };

    auto receiveResponse = [&](PacketPreInit::PluginContainer& required,
                               PacketPreInit::PluginContainer& optionalGroundcover,
                               uint8_t& phase, bool& supportsSync) -> bool
    {
        bool done = false;
        const auto deadline = mRestoringConnection ? mRestoreDeadline
            : std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (!done)
        {
            if (std::chrono::steady_clock::now() >= deadline)
            {
                connected = false;
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Timed out waiting for server pre-init response");
                return false;
            }
            RakNet::Packet *packet = peer->Receive();
            if (!packet)
            {
                RakSleep(100);
                continue;
            }

            if (!packet->data || packet->length == 0 || packet->systemAddress != serverAddr)
            {
                peer->DeallocatePacket(packet);
                continue;
            }

            RakNet::BitStream bsIn(&packet->data[0], packet->length, false);
            unsigned char packetId = 0;
            if (!bsIn.Read(packetId))
            {
                peer->DeallocatePacket(packet);
                continue;
            }
            switch (packetId)
            {
                case ID_DISCONNECTION_NOTIFICATION:
                case ID_CONNECTION_LOST:
                    connected = false;
                    done = true;
                    break;
                case ID_GAME_PREINIT:
                {
                    if (packet->length < BasePacket::headerSize())
                    {
                        connected = false;
                        done = true;
                        break;
                    }
                    bsIn.IgnoreBytes((unsigned) RakNet::RakNetGUID::size());
                    PacketPreInit response(peer);
                    response.setChecksums(&required);
                    response.setGroundcoverChecksums(&optionalGroundcover);
                    response.Packet(&bsIn, false);
                    if (!response.isPacketValid())
                    {
                        connected = false;
                        peer->DeallocatePacket(packet);
                        return false;
                    }
                    startLocation = response.getStartLocation();
                    phase = response.getManifestPhase();
                    supportsSync = response.supportsManifestSync();
                    if (supportsSync && phase == PacketPreInit::PHASE_REJECTED && required.empty())
                    {
                        connected = false;
                        if (!mRestoringConnection)
                            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "ArenaMP",
                                "The server rejected the connection. Please try connecting again shortly.", 0);
                    }
                    // Manifest responses precede token verification and must
                    // never replace the credential used by PHASE_VERIFY.
                    if (phase == PacketPreInit::PHASE_ACCEPTED
                        || (!supportsSync && required.empty()))
                    {
                        mResumeAccepted = response.supportsSessionResume() && response.getResumeAccepted();
                        if (response.supportsSessionResume() && !response.getIssuedResumeToken().empty())
                            mResumeToken = response.getIssuedResumeToken();
                    }
                    done = true;
                    break;
                }
            }
            peer->DeallocatePacket(packet);
        }
        return connected;
    };

    PacketPreInit::PluginContainer currentChecksums;
    makeChecksums(content, currentChecksums);

    // New ArenaMP clients announce manifest-sync support in the trailing phase
    // byte. Old servers simply ignore the extension and keep the legacy flow.
    PacketPreInit::PluginContainer emptyGroundcover;
    PacketPreInit request(peer);
    RakNet::BitStream requestStream;
    const RakNet::RakNetGUID guid = peer->GetMyGUID();
    request.setChecksums(&currentChecksums);
    request.setGroundcoverChecksums(&emptyGroundcover);
    request.setManifestPhase(PacketPreInit::PHASE_REQUEST);
    request.setDeviceHash(mSessionIdentity);
    request.setResumeToken(mResumeToken);
    request.setGUID(guid);
    request.SetSendStream(&requestStream);
    request.Send(serverAddr);

    PacketPreInit::PluginContainer responseChecksums;
    PacketPreInit::PluginContainer responseGroundcover;
    uint8_t responsePhase = PacketPreInit::PHASE_REQUEST;
    bool serverSupportsSync = false;
    if (!receiveResponse(responseChecksums, responseGroundcover, responsePhase, serverSupportsSync))
        return;

    if (serverSupportsSync && responsePhase == PacketPreInit::PHASE_MANIFEST)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
            "Server data-file manifest received: %d required content file(s), %d optional groundcover file(s)",
            static_cast<int>(responseChecksums.size()), static_cast<int>(responseGroundcover.size()));

        std::vector<std::string> orderedContent;
        PacketPreInit::PluginContainer verificationChecksums;
        int index = 0;
        for (const PacketPreInit::PluginPair& requirement : responseChecksums)
        {
            boost::filesystem::path filename(requirement.first);
            const Files::MultiDirCollection& col = collections.getCollection(filename.extension().string());
            if (!col.doesExist(requirement.first))
            {
                LOG_APPEND(TimedLog::LOG_ERROR, "Required server content is missing: %s", requirement.first.c_str());
                continue;
            }

            const unsigned crc32 = checksumFor(col.getPath(requirement.first).string());
            PacketPreInit::HashList hashList(1, crc32);
            verificationChecksums.push_back(std::make_pair(requirement.first, hashList));
            orderedContent.push_back(requirement.first);
            LOG_APPEND(TimedLog::LOG_INFO, "Server order %d: %s [%X]", index++, requirement.first.c_str(), crc32);
        }

        // The Engine has not loaded ESM/ESP records yet, so changing this vector
        // here safely changes the order for this launch only without rewriting
        // the user's persistent OpenMW profile.
        content = orderedContent;

        std::vector<std::string> orderedGroundcover;
        for (const PacketPreInit::PluginPair& recommendation : responseGroundcover)
        {
            boost::filesystem::path filename(recommendation.first);
            const Files::MultiDirCollection& col = collections.getCollection(filename.extension().string());
            if (!col.doesExist(recommendation.first))
            {
                LOG_APPEND(TimedLog::LOG_INFO, "Optional groundcover not installed, skipping: %s",
                    recommendation.first.c_str());
                continue;
            }

            const unsigned crc32 = checksumFor(col.getPath(recommendation.first).string());
            const bool hashAllowed = recommendation.second.empty()
                || std::find(recommendation.second.begin(), recommendation.second.end(), crc32) != recommendation.second.end();
            if (!hashAllowed)
            {
                LOG_APPEND(TimedLog::LOG_WARN,
                    "Optional groundcover has a different hash and will be skipped: %s [%X]",
                    recommendation.first.c_str(), crc32);
                continue;
            }
            orderedGroundcover.push_back(recommendation.first);
            LOG_APPEND(TimedLog::LOG_INFO, "Optional groundcover enabled: %s [%X]",
                recommendation.first.c_str(), crc32);
        }
        groundcover = orderedGroundcover;

        // Second pre-init: hashes are now calculated in the exact order supplied
        // by the server. Missing required files result in a shorter container and
        // are rejected cleanly by the server before the world is loaded.
        PacketPreInit verify(peer);
        RakNet::BitStream verifyStream;
        verify.setChecksums(&verificationChecksums);
        verify.setGroundcoverChecksums(&emptyGroundcover);
        verify.setManifestPhase(PacketPreInit::PHASE_VERIFY);
        verify.setDeviceHash(mSessionIdentity);
        verify.setResumeToken(mResumeToken);
        verify.setGUID(guid);
        verify.SetSendStream(&verifyStream);
        verify.Send(serverAddr);

        PacketPreInit::PluginContainer verifyResponse;
        PacketPreInit::PluginContainer ignoredGroundcover;
        uint8_t verifyPhase = PacketPreInit::PHASE_REQUEST;
        bool verifySupportsSync = false;
        if (!receiveResponse(verifyResponse, ignoredGroundcover, verifyPhase, verifySupportsSync))
            return;

        if (verifySupportsSync && verifyPhase == PacketPreInit::PHASE_ACCEPTED)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Server content order and hashes accepted");
            return;
        }

        const PacketPreInit::PluginContainer& expected = verifyResponse.empty()
            ? responseChecksums : verifyResponse;
        const std::string errmsg = listDiscrepancies(verificationChecksums, expected);
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, errmsg.c_str());
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, listComparison(verificationChecksums, expected, true).c_str());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "ArenaMP", errmsg.c_str(), 0);
        connected = false;
        return;
    }

    // Legacy server path: an empty response means accepted; a returned plugin
    // list is the traditional mismatch response.
    if (!responseChecksums.empty())
    {
        const std::string errmsg = listDiscrepancies(currentChecksums, responseChecksums);
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, errmsg.c_str());
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, listComparison(currentChecksums, responseChecksums, true).c_str());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "tes3mp", errmsg.c_str(), 0);
        connected = false;
    }
}

void Networking::receiveMessage(RakNet::Packet *packet)
{
    if (packet->length < 2)
        return;

    if (systemPacketController.ContainsPacket(packet->data[0]))
    {
        if (!SystemProcessor::Process(*packet))
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Unhandled SystemPacket with identifier %i has arrived", packet->data[0]);
    }
    else if (playerPacketController.ContainsPacket(packet->data[0]))
    {
        if (!PlayerProcessor::Process(*packet))
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Unhandled PlayerPacket with identifier %i has arrived", packet->data[0]);
    }
    else if (actorPacketController.ContainsPacket(packet->data[0]))
    {
        if (!ActorProcessor::Process(*packet, actorList))
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Unhandled ActorPacket with identifier %i has arrived", packet->data[0]);
    }
    else if (objectPacketController.ContainsPacket(packet->data[0]))
    {
        if (!ObjectProcessor::Process(*packet, objectList))
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Unhandled ObjectPacket with identifier %i has arrived", packet->data[0]);
    }
    else if (worldstatePacketController.ContainsPacket(packet->data[0]))
    {
        if (!WorldstateProcessor::Process(*packet, worldstate))
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Unhandled WorldstatePacket with identifier %i has arrived", packet->data[0]);
    }
}

SystemPacket *Networking::getSystemPacket(RakNet::MessageID id)
{
    return systemPacketController.GetPacket(id);
}

PlayerPacket *Networking::getPlayerPacket(RakNet::MessageID id)
{
    return playerPacketController.GetPacket(id);
}

ActorPacket *Networking::getActorPacket(RakNet::MessageID id)
{
    return actorPacketController.GetPacket(id);
}

ObjectPacket *Networking::getObjectPacket(RakNet::MessageID id)
{
    return objectPacketController.GetPacket(id);
}

WorldstatePacket *Networking::getWorldstatePacket(RakNet::MessageID id)
{
    return worldstatePacketController.GetPacket(id);
}

LocalSystem *Networking::getLocalSystem()
{
    return mwmp::Main::get().getLocalSystem();
}

LocalPlayer *Networking::getLocalPlayer()
{
    return mwmp::Main::get().getLocalPlayer();
}

ActorList *Networking::getActorList()
{
    return &actorList;
}

ObjectList *Networking::getObjectList()
{
    return &objectList;
}

Worldstate *Networking::getWorldstate()
{
    return &worldstate;
}

bool Networking::isConnected()
{
    return connected;
}

const std::string& Networking::getStartLocation() const
{
    return startLocation;
}
