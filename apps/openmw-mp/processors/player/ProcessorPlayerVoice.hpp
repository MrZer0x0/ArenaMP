#ifndef OPENMW_PROCESSORPLAYERVOICE_HPP
#define OPENMW_PROCESSORPLAYERVOICE_HPP

#include <chrono>
#include <cstdint>
#include <cmath>
#include <set>
#include <unordered_map>
#include <components/misc/constants.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerVoice.hpp>
#include "../PlayerProcessor.hpp"
#include "../../Networking.hpp"
#include "../../Player.hpp"

namespace mwmp
{
    class ProcessorPlayerVoice : public PlayerProcessor
    {
    public:
        ProcessorPlayerVoice() { BPP_INIT(ID_PLAYER_VOICE) }

        void Do(PlayerPacket& packet, Player& player) override
        {
            if (!packet.isPacketValid() || !Networking::get().isVoiceEnabled() || !player.isVisibleToOthers())
                return;

            const VoiceFrame& frame = player.voiceFrame;
            const bool groupFrame = frame.mode == VoiceFrame::ModeGroup;
            if (frame.codec != VoiceFrame::CodecImaAdpcm16k
                || (frame.mode != VoiceFrame::ModeProximity && !groupFrame)
                || frame.payload.size() != VoiceFrame::ImaAdpcmPayloadBytes
                || frame.payload.size() > PacketPlayerVoice::MaxPayloadBytes)
                return;

            // 50 packets/s is the expected 20 ms frame rate. Allow short jitter/bursts
            // but refuse a client that tries to turn voice into an arbitrary UDP flood.
            struct RateState
            {
                std::chrono::steady_clock::time_point start;
                unsigned int count = 0;
            };
            static std::unordered_map<std::uint64_t, RateState> rate;
            const auto now = std::chrono::steady_clock::now();
            RateState& state = rate[player.guid.g];
            if (state.start.time_since_epoch().count() == 0
                || now - state.start >= std::chrono::seconds(1))
            {
                state.start = now;
                state.count = 0;
            }
            if (++state.count > 75)
                return;

            // Z005: party voice. The group is a script concept, so the relay
            // only compares the opaque identifier the script set on each player.
            // Distance, cell and the interior/exterior split are all irrelevant
            // here - that is the entire point of talking inside a group - and
            // so is the AOI recipient list, so the frame goes out through the
            // full player table. A listener who has not loaded the speaker has
            // no Ptr to position a 3D stream on, which is exactly why the
            // client plays a group frame flat instead of in the world.
            if (groupFrame)
            {
                const unsigned int group = player.getVoiceGroup();
                if (group == 0)
                    return;
                for (const auto& entry : *Players::getPlayers())
                {
                    Player* other = entry.second;
                    if (other == nullptr || other == &player || !other->isVisibleToOthers())
                        continue;
                    if (other->getVoiceGroup() != group)
                        continue;
                    packet.setPlayer(&player);
                    packet.Send(other->guid);
                }
                return;
            }

            const float maxUnits = Networking::get().getVoiceRangeMeters() * Constants::UnitsPerMeter;
            const float maxDistance2 = maxUnits * maxUnits;
            const std::set<RakNet::RakNetGUID> recipients = player.getLoadedPlayerGuids();
            for (const RakNet::RakNetGUID& guid : recipients)
            {
                Player* other = Players::getPlayer(guid);
                if (other == nullptr || !other->isVisibleToOthers())
                    continue;

                // Interiors are isolated acoustic spaces. Exteriors may cross cell
                // borders naturally as long as their world-space distance is close.
                if (player.cell.isExterior() != other->cell.isExterior())
                    continue;
                if (!player.cell.isExterior() && player.cell.mName != other->cell.mName)
                    continue;

                const float dx = player.position.pos[0] - other->position.pos[0];
                const float dy = player.position.pos[1] - other->position.pos[1];
                const float dz = player.position.pos[2] - other->position.pos[2];
                if (dx * dx + dy * dy + dz * dz > maxDistance2)
                    continue;

                packet.setPlayer(&player);
                packet.Send(other->guid);
            }
        }
    };
}

#endif
