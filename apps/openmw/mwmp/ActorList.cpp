#include "ActorList.hpp"

#include "../mwmechanics/aisequence.hpp"
#include "../mwmechanics/aitravel.hpp"
#include "Main.hpp"
#include "Networking.hpp"
#include "LocalPlayer.hpp"
#include "MechanicsHelper.hpp"

#include "../mwworld/class.hpp"

#include <components/openmw-mp/TimedLog.hpp>
#include <algorithm>

using namespace mwmp;

ActorList::ActorList()
{

}

ActorList::~ActorList()
{

}

Networking *ActorList::getNetworking()
{
    return mwmp::Main::get().getNetworking();
}

void ActorList::reset()
{
    cell.blank();
    baseActors.clear();
    positionActors.clear();
    animFlagsActors.clear();
    animPlayActors.clear();
    speechActors.clear();
    statsDynamicActors.clear();
    deathActors.clear();
    equipmentActors.clear();
    aiActors.clear();
    attackActors.clear();
    castActors.clear();
    cellChangeActors.clear();
    guid = mwmp::Main::get().getNetworking()->getLocalPlayer()->guid;
}

void ActorList::addActor(BaseActor baseActor)
{
    baseActors.push_back(baseActor);
}

void ActorList::addPositionActor(BaseActor baseActor)
{
    positionActors.push_back(baseActor);
}

void ActorList::addAnimFlagsActor(BaseActor baseActor)
{
    animFlagsActors.push_back(baseActor);
}

void ActorList::addAnimPlayActor(BaseActor baseActor)
{
    animPlayActors.push_back(baseActor);
}

void ActorList::addSpeechActor(BaseActor baseActor)
{
    speechActors.push_back(baseActor);
}

void ActorList::addStatsDynamicActor(BaseActor baseActor)
{
    statsDynamicActors.push_back(baseActor);
}

void ActorList::addDeathActor(BaseActor baseActor)
{
    deathActors.push_back(baseActor);
}

void ActorList::addEquipmentActor(BaseActor baseActor)
{
    equipmentActors.push_back(baseActor);
}

namespace
{
    void appendUniqueTarget(std::vector<mwmp::Target>& targets, const MWWorld::Ptr& ptr)
    {
        if (ptr.isEmpty()) return;
        const mwmp::Target target = MechanicsHelper::getTarget(ptr);
        for (const mwmp::Target& existing : targets)
        {
            if (existing.isPlayer == target.isPlayer
                && ((target.isPlayer && existing.guid == target.guid)
                    || (!target.isPlayer && existing.refNum == target.refNum && existing.mpNum == target.mpNum)))
                return;
        }
        targets.push_back(target);
    }

    void fillRecoveryState(const MWWorld::Ptr& actorPtr, mwmp::BaseActor& baseActor)
    {
        if (actorPtr.isEmpty()) return;
        const MWMechanics::AiSequence& sequence
            = actorPtr.getClass().getCreatureStats(actorPtr).getAiSequence();

        std::vector<MWWorld::Ptr> combatTargets;
        sequence.getCombatTargets(combatTargets);
        for (const MWWorld::Ptr& target : combatTargets)
            appendUniqueTarget(baseActor.aiCombatTargets, target);

        MWMechanics::AiReturnHomeState state;
        if (sequence.getReturnHomeState(state))
        {
            baseActor.aiHasReturnHome = true;
            baseActor.aiHomeCell = state.mHomeCell;
            baseActor.aiHomePosition = state.mHomePosition;
            for (const MWMechanics::AiReturnHomeState::DoorBreadcrumb& src : state.mDoorBreadcrumbs)
            {
                mwmp::ActorAiDoorBreadcrumb dst;
                dst.fromCell = src.mFromCell;
                dst.fromPosition = src.mFromPosition;
                dst.toCell = src.mToCell;
                dst.toPosition = src.mToPosition;
                baseActor.aiDoorBreadcrumbs.push_back(dst);
            }
        }
    }
}

void ActorList::addAiActor(BaseActor baseActor)
{
    aiActors.push_back(baseActor);
}

void ActorList::addAiActor(const MWWorld::Ptr& actorPtr, const MWWorld::Ptr& targetPtr, unsigned int aiAction)
{
    mwmp::BaseActor baseActor;
    baseActor.refNum = actorPtr.getCellRef().getRefNum().mIndex;
    baseActor.mpNum = actorPtr.getCellRef().getMpNum();
    baseActor.aiAction = aiAction;
    baseActor.hasAiTarget = !targetPtr.isEmpty();
    if (baseActor.hasAiTarget)
        baseActor.aiTarget = MechanicsHelper::getTarget(targetPtr);
    if (aiAction == mwmp::BaseActorList::COMBAT)
        fillRecoveryState(actorPtr, baseActor);

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Preparing to send ID_ACTOR_AI about %s %i-%i\n- action: %i",
        actorPtr.getCellRef().getRefId().c_str(), baseActor.refNum, baseActor.mpNum, aiAction);

    if (baseActor.aiTarget.isPlayer)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "- Has player target %s",
            targetPtr.getClass().getName(targetPtr).c_str());
    }
    else
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "- Has actor target %s %i-%i",
            targetPtr.getCellRef().getRefId().c_str(), baseActor.aiTarget.refNum, baseActor.aiTarget.mpNum);
    }

    addAiActor(baseActor);
}

void ActorList::addAiEscortActor(const MWWorld::Ptr& actorPtr, const MWWorld::Ptr& targetPtr,
    unsigned int duration, float x, float y, float z)
{
    BaseActor baseActor;
    baseActor.refNum = actorPtr.getCellRef().getRefNum().mIndex;
    baseActor.mpNum = actorPtr.getCellRef().getMpNum();
    baseActor.aiAction = BaseActorList::ESCORT;
    baseActor.hasAiTarget = !targetPtr.isEmpty();
    if (baseActor.hasAiTarget)
        baseActor.aiTarget = MechanicsHelper::getTarget(targetPtr);
    baseActor.aiDuration = duration;
    baseActor.aiCoordinates.pos[0] = x;
    baseActor.aiCoordinates.pos[1] = y;
    baseActor.aiCoordinates.pos[2] = z;

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
        "Preparing to send ID_ACTOR_AI escort about %s %i-%i\n- duration: %u, destination: %.2f %.2f %.2f",
        actorPtr.getCellRef().getRefId().c_str(), baseActor.refNum, baseActor.mpNum, duration, x, y, z);

    addAiActor(baseActor);
}

void ActorList::addAiStateActor(const MWWorld::Ptr& actorPtr, unsigned int aiAction)
{
    if (actorPtr.isEmpty()) return;

    BaseActor baseActor;
    baseActor.refNum = actorPtr.getCellRef().getRefNum().mIndex;
    baseActor.mpNum = actorPtr.getCellRef().getMpNum();
    baseActor.aiAction = aiAction;
    fillRecoveryState(actorPtr, baseActor);

    if (aiAction == BaseActorList::COMBAT)
    {
        MWWorld::Ptr activeTarget;
        if (actorPtr.getClass().getCreatureStats(actorPtr).getAiSequence().getCombatTarget(activeTarget)
            && !activeTarget.isEmpty())
        {
            baseActor.hasAiTarget = true;
            baseActor.aiTarget = MechanicsHelper::getTarget(activeTarget);
            // Active target is first in the snapshot so observers share the same
            // immediate focus after an authority hand-off.
            baseActor.aiCombatTargets.erase(
                std::remove_if(baseActor.aiCombatTargets.begin(), baseActor.aiCombatTargets.end(),
                    [&baseActor](const Target& target)
                    {
                        return target.isPlayer == baseActor.aiTarget.isPlayer
                            && ((target.isPlayer && target.guid == baseActor.aiTarget.guid)
                                || (!target.isPlayer && target.refNum == baseActor.aiTarget.refNum
                                    && target.mpNum == baseActor.aiTarget.mpNum));
                    }),
                baseActor.aiCombatTargets.end());
            baseActor.aiCombatTargets.insert(baseActor.aiCombatTargets.begin(), baseActor.aiTarget);
        }
    }

    addAiActor(baseActor);
}

void ActorList::addAttackActor(BaseActor baseActor)
{
    attackActors.push_back(baseActor);
}

void ActorList::addAttackActor(const MWWorld::Ptr& actorPtr, const mwmp::Attack &attack)
{
    mwmp::BaseActor baseActor;
    baseActor.refNum = actorPtr.getCellRef().getRefNum().mIndex;
    baseActor.mpNum = actorPtr.getCellRef().getMpNum();
    baseActor.attack = attack;
    attackActors.push_back(baseActor);
}

void ActorList::addCastActor(BaseActor baseActor)
{
    castActors.push_back(baseActor);
}

void ActorList::addCellChangeActor(BaseActor baseActor)
{
    cellChangeActors.push_back(baseActor);
}

void ActorList::sendPositionActors()
{
    if (positionActors.size() > 0)
    {
        baseActors = positionActors;
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_POSITION)->setActorList(this);
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_POSITION)->Send();
    }
}

void ActorList::sendAnimFlagsActors()
{
    if (animFlagsActors.size() > 0)
    {
        baseActors = animFlagsActors;
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_ANIM_FLAGS)->setActorList(this);
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_ANIM_FLAGS)->Send();
    }
}

void ActorList::sendAnimPlayActors()
{
    if (animPlayActors.size() > 0)
    {
        baseActors = animPlayActors;
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_ANIM_PLAY)->setActorList(this);
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_ANIM_PLAY)->Send();
    }
}

void ActorList::sendSpeechActors()
{
    if (speechActors.size() > 0)
    {
        baseActors = speechActors;
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_SPEECH)->setActorList(this);
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_SPEECH)->Send();
    }
}

void ActorList::sendStatsDynamicActors()
{
    if (statsDynamicActors.size() > 0)
    {
        baseActors = statsDynamicActors;
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_STATS_DYNAMIC)->setActorList(this);
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_STATS_DYNAMIC)->Send();
    }
}

void ActorList::sendDeathActors()
{
    if (deathActors.size() > 0)
    {
        baseActors = deathActors;
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_DEATH)->setActorList(this);
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_DEATH)->Send();
    }
}

void ActorList::sendEquipmentActors()
{
    if (equipmentActors.size() > 0)
    {
        baseActors = equipmentActors;
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_EQUIPMENT)->setActorList(this);
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_EQUIPMENT)->Send();
    }
}

void ActorList::sendAiActors()
{
    if (aiActors.size() > 0)
    {
        baseActors = aiActors;
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_AI)->setActorList(this);
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_AI)->Send();
    }
}

void ActorList::sendAttackActors()
{
    if (attackActors.size() > 0)
    {
        baseActors = attackActors;
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_ATTACK)->setActorList(this);
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_ATTACK)->Send();
    }
}

void ActorList::sendCastActors()
{
    if (castActors.size() > 0)
    {
        baseActors = castActors;
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_CAST)->setActorList(this);
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_CAST)->Send();
    }
}

void ActorList::sendCellChangeActors()
{
    if (cellChangeActors.size() > 0)
    {
        baseActors = cellChangeActors;
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_CELL_CHANGE)->setActorList(this);
        Main::get().getNetworking()->getActorPacket(ID_ACTOR_CELL_CHANGE)->Send();
    }
}

void ActorList::sendActorsInCell(MWWorld::CellStore* cellStore)
{
    reset();
    cell = *cellStore->getCell();
    action = BaseActorList::SET;

    for (auto &ref : cellStore->getNpcs()->mList)
    {
        MWWorld::Ptr ptr(&ref, 0);

        // If this Ptr is lacking a unique index, ignore it
        if (ptr.getCellRef().getRefNum().mIndex == 0 && ptr.getCellRef().getMpNum() == 0) continue;

        BaseActor actor;
        actor.refId = ptr.getCellRef().getRefId();
        actor.refNum = ptr.getCellRef().getRefNum().mIndex;
        actor.mpNum = ptr.getCellRef().getMpNum();

        addActor(actor);
    }

    for (auto &ref : cellStore->getCreatures()->mList)
    {
        MWWorld::Ptr ptr(&ref, 0);

        // If this Ptr is lacking a unique index, ignore it
        if (ptr.getCellRef().getRefNum().mIndex == 0 && ptr.getCellRef().getMpNum() == 0) continue;

        BaseActor actor;
        actor.refId = ptr.getCellRef().getRefId();
        actor.refNum = ptr.getCellRef().getRefNum().mIndex;
        actor.mpNum = ptr.getCellRef().getMpNum();

        addActor(actor);
    }

    mwmp::Main::get().getNetworking()->getActorPacket(ID_ACTOR_LIST)->setActorList(this);
    mwmp::Main::get().getNetworking()->getActorPacket(ID_ACTOR_LIST)->Send();
}
