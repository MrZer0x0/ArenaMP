#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/Base/QuestIndexData.hpp>

#include <apps/openmw-mp/Networking.hpp>
#include <apps/openmw-mp/Player.hpp>
#include <apps/openmw-mp/Script/ScriptFunctions.hpp>

#include "QuestIndex.hpp"

using namespace mwmp;

unsigned int QuestIndexFunctions::GetQuestIndexStage(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    return player->questIndex.stage;
}

unsigned int QuestIndexFunctions::GetQuestIndexMode(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    return player->questIndex.mode;
}

const char *QuestIndexFunctions::GetQuestIndexContentKey(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, "");

    return player->questIndex.contentKey.c_str();
}

const char *QuestIndexFunctions::GetQuestIndexHash(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, "");

    return player->questIndex.indexHash.c_str();
}

unsigned int QuestIndexFunctions::GetQuestIndexEntryCount(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    return player->questIndex.entryCount;
}

unsigned int QuestIndexFunctions::GetQuestIndexChunkIndex(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    return player->questIndex.chunkIndex;
}

unsigned int QuestIndexFunctions::GetQuestIndexChunkCount(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    return player->questIndex.chunkCount;
}

unsigned int QuestIndexFunctions::GetQuestIndexChunkSize(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    return static_cast<unsigned int>(player->questIndex.entries.size());
}

const char *QuestIndexFunctions::GetQuestIndexChunkEntry(unsigned short pid, unsigned int index) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, "");

    if (index >= player->questIndex.entries.size())
        return "";

    return player->questIndex.entries.at(index).c_str();
}

void QuestIndexFunctions::SendQuestIndexRequest(unsigned short pid, unsigned int mode) noexcept
{
    Player *player;
    GET_PLAYER(pid, player,);

    if (mode > QuestIndexData::MODE_UPLOAD)
        return;

    // A request carries no payload, so clear whatever the player last sent us.
    // Reusing that buffer would echo a client's own entries back at it.
    player->questIndex = QuestIndexData();
    player->questIndex.stage = QuestIndexData::STAGE_REQUEST;
    player->questIndex.mode = static_cast<unsigned char>(mode);

    mwmp::Networking::get().getPlayerPacketController()->GetPacket(ID_PLAYER_QUEST_INDEX)->setPlayer(player);
    mwmp::Networking::get().getPlayerPacketController()->GetPacket(ID_PLAYER_QUEST_INDEX)->Send(false);
}
