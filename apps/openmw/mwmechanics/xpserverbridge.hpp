#ifndef OPENMW_MWMECHANICS_XPSERVERBRIDGE_H
#define OPENMW_MWMECHANICS_XPSERVERBRIDGE_H

#include <string>

namespace MWMechanics
{
    namespace XPLeveling
    {
        // ArenaMP X050: award XP after server-side group validation/splitting.
        // scaled=true applies the normal server-enforced XP/difficulty multiplier;
        // scaled=false treats amount as the exact final XP value.
        void awardServer(float amount, bool scaled, const std::string& reason);

        // Server has already mutated the authoritative XP value. This only
        // displays a signed adjustment in the local right-side HUD feed.
        void notifyServerAdjustment(float amount, const std::string& reason);
    }
}

#endif
