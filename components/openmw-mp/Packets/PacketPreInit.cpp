#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include "PacketPreInit.hpp"

mwmp::PacketPreInit::PacketPreInit(RakNet::RakPeerInterface *peer)
    : BasePacket(peer), startLocation("default")
{
    packetID = ID_GAME_PREINIT;
}

void mwmp::PacketPreInit::Packet(RakNet::BitStream *newBitstream, bool send)
{
    BasePacket::Packet(newBitstream, send);

    const RakNet::BitSize_t packetSize = bs->GetNumberOfBytesUsed();
    uint32_t expectedPacketSize = BasePacket::headerSize() + sizeof(uint32_t);
    if (!send && expectedPacketSize > packetSize)
    {
        LOG_MESSAGE(TimedLog::LOG_ERROR, "Wrong packet size %d when expected %d", packetSize, expectedPacketSize);
        packetValid = false;
        return;
    }

    uint32_t numberOfChecksums = checksums->size();
    RW(numberOfChecksums, send);

    if (numberOfChecksums > maxPlugins)
    {
        LOG_MESSAGE(TimedLog::LOG_ERROR, "Wrong number of checksums %d when maximum is %d", numberOfChecksums, maxPlugins);
        packetValid = false;
        return;
    }

    struct NAS
    {
        uint32_t hashN;
        uint32_t strSize;
    };

    std::vector<NAS> NumberOfHashesAndStrSizes(numberOfChecksums);

    PluginContainer::const_iterator checksumIt = checksums->begin();

    for (auto &&nas : NumberOfHashesAndStrSizes)
    {
        if (send)
        {
            nas.strSize = checksumIt->first.size();
            nas.hashN = checksumIt++->second.size();
        }
        RW(nas, send);

        expectedPacketSize += nas.strSize + nas.hashN;

        if (nas.strSize > pluginNameMaxLength)
            LOG_MESSAGE(TimedLog::LOG_ERROR, "Wrong string length %d when maximum length is %d",
                        nas.strSize,
                        pluginNameMaxLength);
        else if (nas.hashN > maxHashes)
            LOG_MESSAGE(TimedLog::LOG_ERROR, "Wrong  number of hashes %d when maximum is %d", nas.hashN, maxHashes);
        else
            continue;
        packetValid = false;
        return;
    }

    // Older servers accepted the plugin list by sending a completely empty payload.
    // Keep that response compatible and leave startLocation at its default value.
    if (!send && expectedPacketSize == packetSize)
        return;

    if (!send && expectedPacketSize > packetSize)
    {
        LOG_MESSAGE(TimedLog::LOG_ERROR, "Wrong packet size %d when expected %d", packetSize, expectedPacketSize);
        packetValid = false;
        return;
    }

    checksums->resize(numberOfChecksums);

    auto numberOfHashesIt = NumberOfHashesAndStrSizes.cbegin();

    for (auto &&checksum : *checksums)
    {
        RW(checksum.first, send, false, numberOfHashesIt->strSize);

        checksum.second.resize(numberOfHashesIt->hashN);
        for (auto &&hash : checksum.second)
            RW(hash, send);
        ++numberOfHashesIt;
    }

    // A client or server built before this extension has no trailing location.
    // In that case, keep the hardcoded default without rejecting the connection.
    if (!send && bs->GetNumberOfUnreadBits() == 0)
        return;

    if (!RW(startLocation, send, false, startLocationMaxLength))
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Failed to read the pre-init start location");
        packetValid = false;
    }
}

void mwmp::PacketPreInit::setChecksums(mwmp::PacketPreInit::PluginContainer *newChecksums)
{
    checksums = newChecksums;
}

void mwmp::PacketPreInit::setStartLocation(const std::string& location)
{
    startLocation = location.empty() ? "default" : location.substr(0, startLocationMaxLength);
}

const std::string& mwmp::PacketPreInit::getStartLocation() const
{
    return startLocation;
}
