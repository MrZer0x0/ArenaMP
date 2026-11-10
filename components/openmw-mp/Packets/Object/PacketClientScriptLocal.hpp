#ifndef OPENMW_PACKETCLIENTSCRIPTLOCAL_HPP
#define OPENMW_PACKETCLIENTSCRIPTLOCAL_HPP

#include <components/openmw-mp/Packets/Object/ObjectPacket.hpp>

namespace mwmp
{
    class PacketClientScriptLocal : public ObjectPacket
    {
    public:
        PacketClientScriptLocal(RakNet::RakPeerInterface *peer);

        /*
            Start of AMP change

            This packet now handles player-attached scripts as well, so it needs to
            serialize the isPlayer flag before the cell reference, the same way
            PacketObjectActivate does. That requires overriding Packet(), not Object()
        */
        virtual void Packet(RakNet::BitStream *newBitstream, bool send);
        /*
            End of AMP change
        */
    };
}

#endif //OPENMW_PACKETCLIENTSCRIPTLOCAL_HPP
