#ifndef OPENMW_PROCESSORPLAYERVOICE_HPP
#define OPENMW_PROCESSORPLAYERVOICE_HPP

#include "../PlayerProcessor.hpp"
#include "../../Main.hpp"
#include "../../VoiceChat.hpp"

namespace mwmp
{
    class ProcessorPlayerVoice final : public PlayerProcessor
    {
    public:
        ProcessorPlayerVoice() { BPP_INIT(ID_PLAYER_VOICE) }

        void Do(PlayerPacket& packet, BasePlayer* player) override
        {
            if (player == nullptr || !packet.isPacketValid() || isLocal())
                return;
            Main::get().getVoiceChat()->receive(player->guid, player->voiceFrame);
        }
    };
}

#endif
