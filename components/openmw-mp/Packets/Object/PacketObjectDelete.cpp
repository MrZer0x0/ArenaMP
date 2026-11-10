#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketObjectDelete.hpp"

using namespace mwmp;

PacketObjectDelete::PacketObjectDelete(RakNet::RakPeerInterface *peer) : ObjectPacket(peer)
{
    packetID = ID_OBJECT_DELETE;
    hasCellData = true;
}


void PacketObjectDelete::Packet(RakNet::BitStream *newBitstream, bool send)
{
    if (!PacketHeader(newBitstream, send))
        return;

    BaseObject baseObject;
    for (unsigned int i = 0; i < objectList->baseObjectCount; ++i)
    {
        if (send)
            baseObject = objectList->baseObjects.at(i);

        Object(baseObject, send);

        // X012 protocol extension. Count matters for authored stacks; questItem and
        // questSourceId tell the server to claim this source only for the attached
        // player instead of persisting a shared ObjectDelete.
        RW(baseObject.count, send);
        RW(baseObject.questItem, send);
        RW(baseObject.questSourceId, send, true);

        if (!send)
            objectList->baseObjects.push_back(baseObject);
    }
}
