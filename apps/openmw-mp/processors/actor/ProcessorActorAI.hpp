#ifndef OPENMW_PROCESSORACTORAI_HPP
#define OPENMW_PROCESSORACTORAI_HPP

#include "../ActorProcessor.hpp"

namespace mwmp
{
    class ProcessorActorAI : public ActorProcessor
    {
    public:
        ProcessorActorAI()
        {
            BPP_INIT(ID_ACTOR_AI)
        }

        void Do(ActorPacket &packet, Player &player, BaseActorList &actorList) override
        {
            Cell *serverCell = CellController::get()->getCell(&actorList.cell);

            if (serverCell != nullptr)
            {
                // X034: only the current cell authority is allowed to mutate NPC
                // AI. A late packet from the previous authority must never replace
                // the new owner's combat target or disengage snapshot.
                if (*serverCell->getAuthority() != actorList.guid)
                {
                    LOG_APPEND(TimedLog::LOG_WARN,
                        "Rejected ActorAI from non-authority %s for cell %s",
                        player.npc.mName.c_str(), actorList.cell.getShortDescription().c_str());
                    return;
                }
                Script::Call<Script::CallbackIdentity("OnActorAI")>(player.getId(), actorList.cell.getShortDescription().c_str());
            }
        }
    };
}

#endif //OPENMW_PROCESSORACTORAI_HPP
