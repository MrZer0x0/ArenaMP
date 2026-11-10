#ifndef OPENMW_PACKETPLAYERQUESTINDEX_HPP
#define OPENMW_PACKETPLAYERQUESTINDEX_HPP

#include <components/openmw-mp/Packets/Player/PlayerPacket.hpp>

namespace mwmp
{
    /// ArenaMP X013: transport for the client-oracle quest item index.
    ///
    /// Direction is encoded in QuestIndexData::stage. The server sends a single
    /// STAGE_REQUEST telling the client what it wants; the client answers with
    /// STAGE_HANDSHAKE, optionally followed by STAGE_CHUNK packets and a final
    /// STAGE_END. The server never trusts the declared hash on its own: it
    /// recomputes it over the received payload before accepting anything.
    class PacketPlayerQuestIndex : public PlayerPacket
    {
    public:
        PacketPlayerQuestIndex(RakNet::RakPeerInterface *peer);
        void Packet(RakNet::BitStream *newBitstream, bool send) override;
    };
}

#endif //OPENMW_PACKETPLAYERQUESTINDEX_HPP
