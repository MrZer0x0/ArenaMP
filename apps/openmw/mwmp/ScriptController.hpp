#ifndef OPENMW_SCRIPTCONTROLLER_HPP
#define OPENMW_SCRIPTCONTROLLER_HPP

#include <string>

#include <components/openmw-mp/Base/BaseStructs.hpp>

#include "../mwworld/ptr.hpp"

namespace ScriptController
{
    unsigned short getPacketOriginFromContextType(unsigned short contextType);

    /*
        Start of AMP addition

        Local scripts run on every client that has the cell loaded, and there is nothing in
        the engine that stops two clients from both advancing the same quest timer and then
        telling each other about it. That is why scripts built around GetSecondsPassed,
        day counters and similar waits used to run at N times normal speed with N players
        present, and why their variables oscillated instead of settling.

        The helpers below fix that in two ways:

        * hasAuthorityOverPtr() reports whether this client is the one entitled to speak for
          a given reference. Scripts on our own player are always ours; scripts on world
          objects belong to whoever holds authority over that cell. When no client has
          claimed the cell yet we return true, so behaviour degrades to the old
          everyone-reports model rather than to silence.

        * queueLocalChange() and flushQueuedLocalChanges() replace the old one-packet-per-
          assignment model. Changes are coalesced per object and per variable and sent at a
          fixed interval, so a script that writes a timer every frame produces a couple of
          packets per second instead of sixty.
    */
    extern float sSyncInterval;

    bool hasAuthorityOverPtr(const MWWorld::Ptr& ptr);

    void queueLocalChange(const MWWorld::Ptr& ptr, unsigned short packetOrigin,
        const std::string& originScriptName, int internalIndex, mwmp::VARIABLE_TYPE variableType,
        int intValue, float floatValue);

    /// Forget anything still queued for this reference, used after the server has told us
    /// what the values should be so that our stale ones do not immediately overwrite them
    void dropQueuedLocalChanges(const MWWorld::Ptr& ptr);

    void flushQueuedLocalChanges(float dt, bool force = false);

    void clearQueuedLocalChanges();
    /*
        End of AMP addition
    */
}


#endif //OPENMW_SCRIPTCONTROLLER_HPP
