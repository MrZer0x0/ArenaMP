#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/Base/BasePlayer.hpp>

#include "PacketPlayerQuestIndex.hpp"

using namespace mwmp;

PacketPlayerQuestIndex::PacketPlayerQuestIndex(RakNet::RakPeerInterface *peer) : PlayerPacket(peer)
{
    packetID = ID_PLAYER_QUEST_INDEX;
}

void PacketPlayerQuestIndex::Packet(RakNet::BitStream *newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream, send);

    QuestIndexData& data = player->questIndex;

    if (send)
        data.chunkSize = static_cast<unsigned int>(data.entries.size());
    else
        data.entries.clear();

    RW(data.stage, send);
    RW(data.mode, send);
    RW(data.contentKey, send, true);
    RW(data.indexHash, send, true);
    RW(data.entryCount, send);
    RW(data.chunkIndex, send);
    RW(data.chunkCount, send);
    RW(data.chunkSize, send);

    // Everything below this line is attacker controlled on the server side, so
    // every length is clamped before a single allocation happens. A malformed
    // packet is dropped whole instead of being partially applied.
    if (!send)
    {
        if (data.contentKey.size() > QuestIndexData::sMaxKeyLength
            || data.indexHash.size() > QuestIndexData::sMaxKeyLength
            || data.entryCount > QuestIndexData::sMaxEntries
            || data.chunkCount > QuestIndexData::sMaxChunks
            || data.chunkIndex >= QuestIndexData::sMaxChunks
            || data.chunkSize > QuestIndexData::sMaxChunkEntries)
        {
            data = QuestIndexData();
            data.stage = QuestIndexData::STAGE_INVALID;
            return;
        }
        data.entries.reserve(data.chunkSize);
    }

    std::string entry;
    for (unsigned int i = 0; i < data.chunkSize; ++i)
    {
        if (send)
            entry = data.entries.at(i);

        RW(entry, send, true);

        if (!send)
        {
            if (entry.empty() || entry.size() > QuestIndexData::sMaxEntryLength)
            {
                data = QuestIndexData();
                data.stage = QuestIndexData::STAGE_INVALID;
                return;
            }
            data.entries.push_back(entry);
        }
    }
}
