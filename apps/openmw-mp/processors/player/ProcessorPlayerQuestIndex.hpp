#ifndef OPENMW_PROCESSORPLAYERQUESTINDEX_HPP
#define OPENMW_PROCESSORPLAYERQUESTINDEX_HPP

#include <components/openmw-mp/TimedLog.hpp>
#include <components/openmw-mp/Base/QuestIndexData.hpp>

#include "apps/openmw-mp/Script/ScriptFunctions.hpp"

#include "../PlayerProcessor.hpp"

namespace mwmp
{
    /// ArenaMP X013. There is deliberately no default handling here: an upload
    /// only ever becomes authoritative through server/scripts/questIndexStore.lua,
    /// which rehashes the payload and requires independent confirmations.
    class ProcessorPlayerQuestIndex final : public PlayerProcessor
    {
    public:
        ProcessorPlayerQuestIndex()
        {
            BPP_INIT(ID_PLAYER_QUEST_INDEX)
        }

        void Do(PlayerPacket &packet, Player &player) override
        {
            if (player.questIndex.stage == mwmp::QuestIndexData::STAGE_INVALID)
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                    "Player %s sent a malformed ID_PLAYER_QUEST_INDEX", player.npc.mName.c_str());
                return;
            }

            // A REQUEST is the server's own direction; a client must never send one.
            if (player.questIndex.stage == mwmp::QuestIndexData::STAGE_REQUEST)
                return;

            Script::Call<Script::CallbackIdentity("OnPlayerQuestIndex")>(player.getId());
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERQUESTINDEX_HPP
