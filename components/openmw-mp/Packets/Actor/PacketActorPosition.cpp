#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include "PacketActorPosition.hpp"

using namespace mwmp;

PacketActorPosition::PacketActorPosition(RakNet::RakPeerInterface *peer) : ActorPacket(peer)
{
    packetID = ID_ACTOR_POSITION;
    priority = HIGH_PRIORITY;
    reliability = UNRELIABLE_SEQUENCED;
    orderChannel = CHANNEL_ACTOR_MOVEMENT;
}

void PacketActorPosition::Actor(BaseActor &actor, bool send)
{
    RW(actor.position, send, true);
    RW(actor.direction, send, true);

    actor.hasPositionData = true;
}
