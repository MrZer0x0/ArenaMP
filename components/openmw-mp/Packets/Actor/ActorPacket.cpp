#include <components/openmw-mp/NetworkMessages.hpp>
#include <PacketPriority.h>
#include <RakPeer.h>
#include <utility>
#include "ActorPacket.hpp"

using namespace mwmp;

ActorPacket::ActorPacket(RakNet::RakPeerInterface *peer) : BasePacket(peer)
{
    packetID = 0;
    priority = HIGH_PRIORITY;
    reliability = RELIABLE_ORDERED;
    orderChannel = CHANNEL_ACTOR;
    this->peer = peer;
}

ActorPacket::~ActorPacket()
{

}

void ActorPacket::setActorList(BaseActorList *newActorList)
{
    actorList = newActorList;
    guid = actorList->guid;
}

void ActorPacket::Packet(RakNet::BitStream *newBitstream, bool send)
{
    if (!PacketHeader(newBitstream, send))
        return;

    // Y002: the scratch BaseActor is created *inside* the loop.
    //
    // It used to be declared once, above the loop, and reused for every entry of
    // a batched packet. Almost every Actor() override serialises only a subset of
    // BaseActor and picks that subset conditionally - PacketActorAI writes
    // aiTarget/aiDistance/aiCoordinates only for certain aiAction values,
    // PacketActorAttack writes the damage block only when isHit, and
    // PacketActorStatsDynamic never touches mDead/mDeathAnimationFinished at all.
    // On the receiving side every field the current entry does not deserialise
    // therefore kept the value left behind by the *previous actor in the same
    // packet*, and entry 0 kept whatever the constructor left.
    //
    // The consequences scale directly with how many actors are batched together,
    // which is exactly the busy-cell/high-population case:
    //   - an NPC inherited another NPC's combat target through hasAiTarget/aiTarget;
    //   - Wander radius and Travel coordinates bled between actors;
    //   - mDead / mDeathAnimationFinished bled between actors, which makes
    //     LocalActor::update() skip position and animFlags updates for a perfectly
    //     alive NPC, so remote clients saw it frozen in place.
    //
    // A fresh instance per entry costs one cheap construction and removes the
    // whole class of bug. The wire format is untouched.
    for (unsigned int i = 0; i < actorList->count; i++)
    {
        BaseActor actor;

        if (send)
            actor = actorList->baseActors.at(i);

        RW(actor.refNum, send);
        RW(actor.mpNum, send);

        Actor(actor, send);

        if (!send)
            actorList->baseActors.push_back(std::move(actor));
    }
}

bool ActorPacket::PacketHeader(RakNet::BitStream *newBitstream, bool send)
{
    BasePacket::Packet(newBitstream, send);

    RW(actorList->cell.mData, send, true);
    RW(actorList->cell.mName, send, true);

    if (send)
        actorList->count = (unsigned int)(actorList->baseActors.size());
    else
        actorList->baseActors.clear();

    RW(actorList->count, send);

    if (actorList->count > maxActors)
    {
        actorList->isValid = false;
        return false;
    }

    return true;
}


void ActorPacket::Actor(BaseActor &actor, bool send)
{

}
