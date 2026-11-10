#ifndef OPENMW_PROCESSORACTORCELLCHANGE_HPP
#define OPENMW_PROCESSORACTORCELLCHANGE_HPP

#include <map>
#include <set>

#include "../ActorProcessor.hpp"

namespace mwmp
{
    class ProcessorActorCellChange : public ActorProcessor
    {
    public:
        ProcessorActorCellChange()
        {
            BPP_INIT(ID_ACTOR_CELL_CHANGE)
        }

        void Do(ActorPacket &packet, Player &player, BaseActorList &actorList) override
        {
            bool isAccepted = false;
            Cell *serverCell = CellController::get()->getCell(&actorList.cell);

            if (serverCell != nullptr)
            {
                bool isFollowerCellChange = false;

                // TODO: Move this check on the Lua side
                for (unsigned int i = 0; i < actorList.count; i++)
                {
                    if (actorList.baseActors.at(i).isFollowerCellChange)
                    {
                        isFollowerCellChange = true;
                        break;
                    }
                }

                // If the cell is loaded, only accept regular cell changes from a cell's authority, but accept follower
                // cell changes from other players
                if (*serverCell->getAuthority() == actorList.guid || isFollowerCellChange)
                    isAccepted = true;
            }
            // If the cell isn't loaded, the packet must be from dialogue or a script, so accept it
            else
            {
                isAccepted = true;
            }

            if (!isAccepted)
                return;

            // Remember who already observes the source cell. Those peers receive the
            // original packet once and must not receive a second destination copy.
            std::set<Player*> sourceObservers;
            if (serverCell != nullptr)
            {
                for (Player *observer : serverCell->getPlayers())
                {
                    if (observer != nullptr && !observer->npc.mName.empty())
                        sourceObservers.insert(observer);
                }
            }

            // Group actors by their destination cell. This is our server-side
            // interest set: a player only receives arrival data for a destination
            // cell that their client actually has loaded.
            std::map<Cell*, BaseActorList> destinationLists;

            for (const BaseActor& baseActor : actorList.baseActors)
            {
                ESM::Cell destinationDescription = baseActor.cell;
                Cell *destinationCell = CellController::get()->getCell(&destinationDescription);
                if (destinationCell == nullptr)
                    continue;

                BaseActorList& destinationList = destinationLists[destinationCell];
                if (destinationList.baseActors.empty())
                {
                    destinationList.guid = actorList.guid;
                    destinationList.cell = actorList.cell; // ActorCellChange is always relative to the source cell
                    destinationList.action = actorList.action;
                    destinationList.isValid = true;
                }
                destinationList.baseActors.push_back(baseActor);

                // Carry the last known AI package into the destination cell before
                // removing the actor from the source cache. It will be replayed
                // after the cell-change packet, preserving combat/follow state.
                if (serverCell != nullptr)
                {
                    BaseActor cachedAI;
                    if (serverCell->getActorAI(baseActor.refNum, baseActor.mpNum, cachedAI))
                        destinationCell->setActorAI(cachedAI);
                }
            }

            if (serverCell != nullptr)
                serverCell->removeActors(&actorList);

            // Persist the move in the Lua cell data before notifying clients.
            Script::Call<Script::CallbackIdentity("OnActorCellChange")>(player.getId(), actorList.cell.getShortDescription().c_str());

            // 1) Everyone who currently loads the source cell sees the actor leave.
            if (serverCell != nullptr)
            {
                packet.setActorList(&actorList);
                serverCell->sendToLoaded(&packet, &actorList);
            }

            // 2) Destination-only observers receive only the actors entering their
            // loaded cell, not the whole mixed packet and not unrelated cells.
            for (auto& destinationEntry : destinationLists)
            {
                Cell *destinationCell = destinationEntry.first;
                BaseActorList& destinationList = destinationEntry.second;
                destinationList.count = static_cast<unsigned int>(destinationList.baseActors.size());

                packet.setActorList(&destinationList);
                for (Player *observer : destinationCell->getPlayers())
                {
                    if (observer == nullptr || observer->npc.mName.empty())
                        continue;
                    if (observer->guid == actorList.guid)
                        continue;
                    if (sourceObservers.count(observer) != 0)
                        continue;

                    packet.Send(observer->guid);
                }
            }

            // ActorCellChange and ActorAI are on the same reliable ordered channel.
            // Replaying destination AI after the move lets a different cell
            // authority continue pursuit/combat without waiting for a new AI event.
            for (auto& destinationEntry : destinationLists)
            {
                Cell *destinationCell = destinationEntry.first;
                if (destinationCell != nullptr && !destinationCell->getPlayers().empty())
                    destinationCell->sendCachedActorAI(*destinationCell->getAuthority());
            }
        }
    };
}

#endif //OPENMW_PROCESSORACTORCELLCHANGE_HPP
