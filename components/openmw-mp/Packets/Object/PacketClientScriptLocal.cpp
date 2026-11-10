#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketClientScriptLocal.hpp"

using namespace mwmp;

PacketClientScriptLocal::PacketClientScriptLocal(RakNet::RakPeerInterface *peer) : ObjectPacket(peer)
{
    packetID = ID_CLIENT_SCRIPT_LOCAL;
    hasCellData = true;
}

/*
    Start of AMP change

    Rewritten from an Object() override into a Packet() override so that:

    * scripts attached to a player are identified by GUID instead of by a cell reference,
      letting the server file them under that player's profile rather than under whichever
      cell they happened to be standing in
    * the ID of the originating script travels with the variables, so a receiver can tell
      whether the indices it was sent still mean anything for the script it is running
    * the number of variables is bounded, so a malformed count can no longer be used to
      make the receiver allocate an arbitrary amount of memory
*/
void PacketClientScriptLocal::Packet(RakNet::BitStream *newBitstream, bool send)
{
    if (!PacketHeader(newBitstream, send))
        return;

    BaseObject baseObject;

    for (unsigned int i = 0; i < objectList->baseObjectCount; i++)
    {
        if (send)
            baseObject = objectList->baseObjects.at(i);

        RW(baseObject.isPlayer, send);

        if (baseObject.isPlayer)
            RW(baseObject.guid, send);
        else
            Object(baseObject, send);

        RW(baseObject.clientScriptId, send, true);

        uint32_t clientLocalsCount = 0;

        if (send)
            clientLocalsCount = static_cast<uint32_t>(baseObject.clientLocals.size());

        RW(clientLocalsCount, send);

        if (!send)
        {
            if (clientLocalsCount > maxClientLocals)
            {
                objectList->isValid = false;
                return;
            }

            baseObject.clientLocals.clear();
            baseObject.clientLocals.resize(clientLocalsCount);
        }

        for (auto&& clientLocal : baseObject.clientLocals)
        {
            RW(clientLocal.internalIndex, send);
            RW(clientLocal.variableType, send);

            if (clientLocal.variableType == mwmp::VARIABLE_TYPE::SHORT || clientLocal.variableType == mwmp::VARIABLE_TYPE::LONG)
                RW(clientLocal.intValue, send);
            else if (clientLocal.variableType == mwmp::VARIABLE_TYPE::FLOAT)
                RW(clientLocal.floatValue, send);
            else
            {
                // An unknown variable type means we can no longer tell how many bytes
                // follow, so the rest of this packet is unreadable
                objectList->isValid = false;
                return;
            }
        }

        if (!send)
            objectList->baseObjects.push_back(baseObject);
    }
}
/*
    End of AMP change
*/
