#ifndef OPENMW_PACKETPLAYERVOICE_HPP
#define OPENMW_PACKETPLAYERVOICE_HPP

#include <cstdint>

#include <components/openmw-mp/Packets/Player/PlayerPacket.hpp>

namespace mwmp
{
    class PacketPlayerVoice : public PlayerPacket
    {
    public:
        explicit PacketPlayerVoice(RakNet::RakPeerInterface* peer);
        void Packet(RakNet::BitStream* newBitstream, bool send) override;

        static constexpr std::uint16_t MaxPayloadBytes = 512;
    };
}

#endif
