#include "PacketPlayerLevel.hpp"

#include <cstdint>
#include <string>
#include <components/openmw-mp/NetworkMessages.hpp>

using namespace mwmp;

PacketPlayerLevel::PacketPlayerLevel(RakNet::RakPeerInterface *peer) : PlayerPacket(peer)
{
    packetID = ID_PLAYER_LEVEL;
}

void PacketPlayerLevel::Packet(RakNet::BitStream *newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream, send);

    RW(player->creatureStats.mLevel, send);

    RW(player->npcStats.mLevelProgress, send);

    RW(player->npcStats.mXpVersion, send);
    RW(player->npcStats.mExperience, send);
    RW(player->npcStats.mSkillPoints, send);
    RW(player->npcStats.mXpAttributeProgress, send);

    RW(player->xpRewardKeysChanged, send);
    if (player->xpRewardKeysChanged)
    {
        uint32_t rewardKeyCount = 0;
        if (send)
            rewardKeyCount = static_cast<uint32_t>(player->npcStats.mXpRewardKeys.size());

        RW(rewardKeyCount, send);

        if (!send)
        {
            if (rewardKeyCount > 16384)
            {
                packetValid = false;
                return;
            }
            player->npcStats.mXpRewardKeys.clear();
            player->npcStats.mXpRewardKeys.resize(rewardKeyCount);
        }

        for (std::string& key : player->npcStats.mXpRewardKeys)
            RW(key, send);
    }
}
