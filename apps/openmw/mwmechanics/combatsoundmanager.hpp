#ifndef OPENMW_MWMECHANICS_COMBATSOUNDMANAGER_H
#define OPENMW_MWMECHANICS_COMBATSOUNDMANAGER_H

#include <string>

#include "../mwworld/ptr.hpp"

namespace MWMechanics
{
    /// Native ArenaMP port of the useful runtime part of Combat Sounds Overhaul Overhauled.
    ///
    /// It deliberately does not depend on OpenMW Lua.  If the CSO sound folders are not
    /// installed in the VFS every function returns false and callers keep the vanilla sound.
    class CombatSoundManager
    {
    public:
        enum class Surface
        {
            Body,
            LightArmor,
            MediumArmor,
            HeavyArmor
        };

        /// Try to replace a vanilla melee Weapon Swish. Returns true when a custom file played.
        static bool playSwing(const MWWorld::Ptr& actor, const MWWorld::Ptr& weapon,
            const std::string& attackType, float attackStrength);

        /// Try to play the CSO impact for a successful hit. For Surface::Body this is a layer
        /// over Health Damage. For armor surfaces the caller should skip the vanilla armor hit
        /// only when this function returns true.
        static bool playHit(const MWWorld::Ptr& victim, const MWWorld::Ptr& attacker,
            const MWWorld::Ptr& weapon, Surface surface, const std::string& attackType);
    };
}

#endif
