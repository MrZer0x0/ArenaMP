#ifndef OPENMW_OBJECTPACKET_HPP
#define OPENMW_OBJECTPACKET_HPP

#include <string>
#include <RakNetTypes.h>
#include <BitStream.h>
#include <PacketPriority.h>
#include <components/openmw-mp/Base/BaseObject.hpp>

#include <components/openmw-mp/Packets/BasePacket.hpp>


namespace mwmp
{
    class ObjectPacket : public BasePacket
    {
    public:
        ObjectPacket(RakNet::RakPeerInterface *peer);

        ~ObjectPacket();

        void setObjectList(BaseObjectList *newObjectList);

        virtual void Packet(RakNet::BitStream *newBitstream, bool send);

    protected:
        virtual void Object(BaseObject &baseObject, bool send);
        bool PacketHeader(RakNet::BitStream *newBitstream, bool send);
        BaseObjectList *objectList;
        static const int maxObjects = 3000;
        /*
            Start of AMP addition

            Upper bound on the number of local variables a single object may carry in one
            packet. Without it, a hostile or corrupt packet could ask us to resize a vector
            to 4 billion entries
        */
        static const unsigned int maxClientLocals = 1024;
        /*
            End of AMP addition
        */
        bool hasCellData;
    };
}

#endif //OPENMW_OBJECTPACKET_HPP
