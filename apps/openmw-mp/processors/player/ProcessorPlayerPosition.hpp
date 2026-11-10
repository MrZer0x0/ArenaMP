#ifndef OPENMW_PROCESSORPLAYERPOSITION_HPP
#define OPENMW_PROCESSORPLAYERPOSITION_HPP

#include "../PlayerProcessor.hpp"

namespace mwmp
{
    class ProcessorPlayerPosition : public PlayerProcessor
    {
    public:
        ProcessorPlayerPosition()
        {
            BPP_INIT(ID_PLAYER_POSITION)
        }

        void Do(PlayerPacket &packet, Player &player) override
        {
            // ArenaMP X051: expose movement packets to CoreScripts before they
            // are forwarded. This lets the server persist the last known-good
            // in-cell transform instead of sampling a half-closed peer during
            // OnPlayerDisconnect after a server restart/shutdown.
            Script::Call<Script::CallbackIdentity("OnPlayerPosition")>(player.getId());
            player.sendToLoaded(&packet);
        }
    };
}

#endif // OPENMW_PROCESSORPLAYERPOSITION_HPP
