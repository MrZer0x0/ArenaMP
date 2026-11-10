#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include <algorithm>
#include "PacketActorAI.hpp"

using namespace mwmp;

PacketActorAI::PacketActorAI(RakNet::RakPeerInterface *peer) : ActorPacket(peer)
{
    packetID = ID_ACTOR_AI;
}

void PacketActorAI::Actor(BaseActor &actor, bool send)
{
    auto rwTarget = [this, send](Target& target)
    {
        RW(target.isPlayer, send);
        if (target.isPlayer)
            RW(target.guid, send);
        else
        {
            RW(target.refId, send, true);
            RW(target.refNum, send);
            RW(target.mpNum, send);
        }
    };

    RW(actor.aiAction, send);

    if (actor.aiAction != mwmp::BaseActorList::CANCEL)
    {
        if (actor.aiAction == mwmp::BaseActorList::WANDER)
        {
            RW(actor.aiDistance, send);
            RW(actor.aiShouldRepeat, send);
        }

        if (actor.aiAction == mwmp::BaseActorList::ESCORT || actor.aiAction == mwmp::BaseActorList::WANDER)
            RW(actor.aiDuration, send);

        if (actor.aiAction == mwmp::BaseActorList::ESCORT || actor.aiAction == mwmp::BaseActorList::TRAVEL)
            RW(actor.aiCoordinates, send);

        if (actor.aiAction == mwmp::BaseActorList::ACTIVATE || actor.aiAction == mwmp::BaseActorList::COMBAT ||
            actor.aiAction == mwmp::BaseActorList::ESCORT || actor.aiAction == mwmp::BaseActorList::FOLLOW)
        {
            RW(actor.hasAiTarget, send);

            if (actor.hasAiTarget)
            {
                rwTarget(actor.aiTarget);
            }
        }
    }

    // X034: synchronize all combat targets, not only whichever target happened
    // to be active at packet creation time. This is what lets a new authority
    // keep a stable aggro set when two or more players are present.
    unsigned char combatTargetCount = send
        ? static_cast<unsigned char>(std::min<std::size_t>(actor.aiCombatTargets.size(), 8)) : 0;
    RW(combatTargetCount, send);
    if (!send) actor.aiCombatTargets.clear();
    for (unsigned char i = 0; i < combatTargetCount; ++i)
    {
        Target target;
        if (send) target = actor.aiCombatTargets[i];
        rwTarget(target);
        if (!send) actor.aiCombatTargets.push_back(target);
    }

    RW(actor.aiHasReturnHome, send);
    if (actor.aiHasReturnHome)
    {
        RW(actor.aiHomeCell.mData, send, true);
        RW(actor.aiHomeCell.mName, send, true);
        RW(actor.aiHomePosition, send, true);

        unsigned char breadcrumbCount = send
            ? static_cast<unsigned char>(std::min<std::size_t>(actor.aiDoorBreadcrumbs.size(), 12)) : 0;
        RW(breadcrumbCount, send);
        if (!send) actor.aiDoorBreadcrumbs.clear();
        for (unsigned char i = 0; i < breadcrumbCount; ++i)
        {
            ActorAiDoorBreadcrumb breadcrumb;
            if (send) breadcrumb = actor.aiDoorBreadcrumbs[i];
            RW(breadcrumb.fromCell.mData, send, true);
            RW(breadcrumb.fromCell.mName, send, true);
            RW(breadcrumb.fromPosition, send, true);
            RW(breadcrumb.toCell.mData, send, true);
            RW(breadcrumb.toCell.mName, send, true);
            RW(breadcrumb.toPosition, send, true);
            if (!send) actor.aiDoorBreadcrumbs.push_back(breadcrumb);
        }
    }
}
