#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketPlayerVoice.hpp"

mwmp::PacketPlayerVoice::PacketPlayerVoice(RakNet::RakPeerInterface* peer)
    : PlayerPacket(peer)
{
    packetID = ID_PLAYER_VOICE;
    priority = HIGH_PRIORITY;
    // Do not use UNRELIABLE_SEQUENCED here: server->client voice from different
    // speakers shares one RakNet connection. A connection-wide sequenced lane
    // could make speaker B obsolete speaker A. VoiceFrame::sequence is per speaker.
    reliability = UNRELIABLE;
    orderChannel = CHANNEL_VOICE;
}

void mwmp::PacketPlayerVoice::Packet(RakNet::BitStream* newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream, send);
    if (player == nullptr)
    {
        packetValid = false;
        return;
    }

    VoiceFrame& frame = player->voiceFrame;
    RW(frame.sequence, send);
    RW(frame.codec, send);
    RW(frame.mode, send);

    std::uint16_t payloadSize = send
        ? static_cast<std::uint16_t>(std::min<std::size_t>(frame.payload.size(), MaxPayloadBytes))
        : 0;
    if (!RW(payloadSize, send))
    {
        packetValid = false;
        return;
    }

    if (payloadSize > MaxPayloadBytes)
    {
        packetValid = false;
        frame.payload.clear();
        return;
    }

    if (send)
    {
        if (payloadSize != 0)
            bs->WriteAlignedBytes(frame.payload.data(), payloadSize);
    }
    else
    {
        frame.payload.resize(payloadSize);
        if (payloadSize != 0 && !bs->ReadAlignedBytes(frame.payload.data(), payloadSize))
        {
            packetValid = false;
            frame.payload.clear();
        }
    }
}
