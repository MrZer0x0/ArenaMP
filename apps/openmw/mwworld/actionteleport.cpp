#include "actionteleport.hpp"

#include <algorithm>
#include <list>

#include <components/misc/rng.hpp>

/*
    Start of tes3mp addition

    Include additional headers for multiplayer purposes
*/
#include <components/openmw-mp/TimedLog.hpp>
#include <components/settings/settings.hpp>
#include "../mwbase/windowmanager.hpp"
#include "../mwmp/Main.hpp"
#include "../mwmp/Networking.hpp"
#include "../mwmp/ActorList.hpp"
#include "../mwmp/CellController.hpp"
#include "../mwmp/MechanicsHelper.hpp"
/*
    End of tes3mp addition
*/

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwbase/mechanicsmanager.hpp"

#include "../mwmechanics/creaturestats.hpp"

#include "../mwworld/class.hpp"

#include "player.hpp"

namespace MWWorld
{
    ActionTeleport::ActionTeleport (const std::string& cellName,
        const ESM::Position& position, bool teleportFollowers)
    : Action (true), mCellName (cellName), mPosition (position), mTeleportFollowers(teleportFollowers)
    {
    }

    void ActionTeleport::executeImp (const Ptr& actor)
    {
        if (mTeleportFollowers)
        {
            // Find any NPCs that are following the actor and teleport them with him
            std::set<MWWorld::Ptr> followers;
            const bool includeHostilePursuers = Settings::Manager::getBool("combat pursuit through doors", "Game");
            getFollowers(actor, followers, includeHostilePursuers);

            for (std::set<MWWorld::Ptr>::iterator it = followers.begin(); it != followers.end(); ++it)
                teleport(*it, actor);
        }

        teleport(actor);
    }

    void ActionTeleport::teleport(const Ptr& actor, const Ptr& teleportTarget)
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        actor.getClass().getCreatureStats(actor).land(actor == world->getPlayerPtr());
        if(actor == world->getPlayerPtr())
        {
            world->getPlayer().setTeleported(true);
            if (mCellName.empty())
                world->changeToExteriorCell (mPosition, true);
            else
                world->changeToInteriorCell (mCellName, mPosition, true);
        }
        else
        {
            /*
                Start of tes3mp addition

                Track the original cell of this actor so we can use it when sending a packet
            */
            ESM::Cell originalCell = *actor.getCell()->getCell();
            /*
                End of tes3mp addition
            */

            /*
                Start of tes3mp change (minor)

                If this is a DedicatedActor, get their new cell and override their stored cell with it
                so their cell change is approved in World::moveObject()
            */
            MWWorld::CellStore* newCellStore = nullptr;
            mwmp::CellController* cellController = mwmp::Main::get().getCellController();
            const bool isCombatPursuer = !teleportTarget.isEmpty()
                && actor.getClass().getCreatureStats(actor).getAiSequence().isInCombat(teleportTarget);

            if (mCellName.empty())
            {
                int cellX;
                int cellY;
                world->positionToIndex(mPosition.pos[0], mPosition.pos[1], cellX, cellY);

                newCellStore = world->getExterior(cellX, cellY);
                if (cellController->isDedicatedActor(actor))
                    cellController->getDedicatedActor(actor)->cell = *newCellStore->getCell();

                world->moveObject(actor, newCellStore,
                    mPosition.pos[0], mPosition.pos[1], mPosition.pos[2]);
            }
            else
            {
                newCellStore = world->getInterior(mCellName);
                if (cellController->isDedicatedActor(actor))
                    cellController->getDedicatedActor(actor)->cell = *newCellStore->getCell();

                world->moveObject(actor, newCellStore, mPosition.pos[0], mPosition.pos[1], mPosition.pos[2]);
            }
            /*
                Start of tes3mp change (minor)
            */

            /*
                Start of tes3mp addition

                Send ActorCellChange packets when an actor follows us across cells, regardless of
                whether we're the cell authority or not; the server can decide if it wants to comply
                with them

                Afterwards, send an ActorAI packet about this actor being our follower, to ensure
                they remain our follower even if the destination cell has another player as its
                cell authority
            */
            mwmp::BaseActor baseActor;
            baseActor.refNum = actor.getCellRef().getRefNum().mIndex;
            baseActor.mpNum = actor.getCellRef().getMpNum();
            baseActor.cell = *newCellStore->getCell();
            baseActor.position = actor.getRefData().getPosition();
            baseActor.isFollowerCellChange = true;

            mwmp::ActorList *actorList = mwmp::Main::get().getNetworking()->getActorList();
            actorList->reset();
            actorList->cell = originalCell;

            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending ID_ACTOR_CELL_CHANGE about %s %i-%i to server",
                actor.getCellRef().getRefId().c_str(), baseActor.refNum, baseActor.mpNum);

            LOG_APPEND(TimedLog::LOG_INFO, "- Moved from %s to %s", actorList->cell.getDescription().c_str(),
                baseActor.cell.getDescription().c_str());

            actorList->addCellChangeActor(baseActor);
            actorList->sendCellChangeActors();

            // Send ActorAI to bring all players in the new cell up to speed. Hostile
            // pursuers keep AiCombat; ordinary followers keep AiFollow.
            actorList->cell = baseActor.cell;
            baseActor.aiAction = isCombatPursuer
                ? mwmp::BaseActorList::COMBAT : mwmp::BaseActorList::FOLLOW;
            const MWWorld::Ptr aiTarget = !teleportTarget.isEmpty()
                ? teleportTarget : world->getPlayerPtr();
            baseActor.aiTarget = MechanicsHelper::getTarget(aiTarget);
            actorList->addAiActor(baseActor);
            actorList->sendAiActors();
            /*
                End of tes3mp addition
            */
        }
    }

    void ActionTeleport::getFollowers(const MWWorld::Ptr& actor, std::set<MWWorld::Ptr>& out, bool includeHostiles) {
        std::set<MWWorld::Ptr> followers;
        MWBase::MechanicsManager* mechanics = MWBase::Environment::get().getMechanicsManager();
        mechanics->getActorsFollowing(actor, followers);

        // Combat pursuers are deliberately collected separately instead of pretending that
        // AiCombat is an AiFollow package. This keeps ally/follower semantics unchanged.
        if (includeHostiles)
        {
            const std::list<MWWorld::Ptr> pursuers = mechanics->getActorsFighting(actor);
            followers.insert(pursuers.begin(), pursuers.end());
        }

        std::size_t hostilePursuerCount = 0;
        const int maxHostilePursuers = std::max(0,
            Settings::Manager::getInt("combat pursuit max actors", "Game"));
        const float guaranteedDistance = std::max(0.f,
            Settings::Manager::getFloat("combat pursuit guaranteed distance", "Game"));
        const float maximumDoorDistance = std::max(guaranteedDistance,
            Settings::Manager::getFloat("combat pursuit door max distance", "Game"));
        const float minimumChance = std::max(0.f, std::min(1.f,
            Settings::Manager::getFloat("combat pursuit minimum chance", "Game")));

        for(std::set<MWWorld::Ptr>::iterator it = followers.begin();it != followers.end();++it)
        {
            MWWorld::Ptr follower = *it;

            std::string script = follower.getClass().getScript(follower);

            const bool isHostilePursuer = follower.getClass().getCreatureStats(follower).getAiSequence().isInCombat(actor);
            if (!includeHostiles && isHostilePursuer)
                continue;
            if (isHostilePursuer && mwmp::Main::isInitialized()
                && !mwmp::Main::get().getCellController()->isLocalActor(follower))
                continue;

            if (!script.empty() && follower.getRefData().getLocals().getIntVar(script, "stayoutside") == 1)
                continue;

            const float distance = (follower.getRefData().getPosition().asVec3()
                - actor.getRefData().getPosition().asVec3()).length();

            if (isHostilePursuer)
            {
                // Only NPCs and humanoid/bipedal creatures may chase through a door.
                // Very close attackers always follow; farther attackers roll a decreasing chance.
                const bool humanoid = follower.getClass().isNpc() || follower.getClass().isBipedal(follower);
                if (!humanoid || maxHostilePursuers == 0 || hostilePursuerCount >= static_cast<std::size_t>(maxHostilePursuers))
                    continue;
                if (distance > maximumDoorDistance)
                    continue;

                float pursuitChance = 1.f;
                if (distance > guaranteedDistance && maximumDoorDistance > guaranteedDistance)
                {
                    const float normalizedDistance = std::min(1.f,
                        (distance - guaranteedDistance) / (maximumDoorDistance - guaranteedDistance));
                    pursuitChance = 1.f - normalizedDistance * (1.f - minimumChance);
                }

                if (Misc::Rng::rollClosedProbability() > pursuitChance)
                    continue;

                ++hostilePursuerCount;
            }
            else if (distance > 800.f)
            {
                // Keep the original follower teleport radius.
                continue;
            }

            out.emplace(follower);
        }
    }
}
