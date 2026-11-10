#ifndef OPENMW_MWMECHANICS_ACTIVEGUARD_HPP
#define OPENMW_MWMECHANICS_ACTIVEGUARD_HPP
#include <string>
#include "../mwworld/ptr.hpp"
namespace mwmp { class Attack; }
namespace MWMechanics::ActiveGuard
{
    // Equipment eligibility does not depend on transient fatigue/recovery.
    bool canGuardWithEquipment(const MWWorld::Ptr& actor);
    bool available(const MWWorld::Ptr& actor);
    bool raised(const MWWorld::Ptr& actor);
    void request(const MWWorld::Ptr& actor, bool held);
    void update(const MWWorld::Ptr& actor, float dt);
    std::string animation(const MWWorld::Ptr& actor);
    // Called once by the existing attack-owning client, before enchantments.
    bool resolve(const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim,
        const MWWorld::Ptr& weapon, float& damage, bool ranged, mwmp::Attack* result);
    // Replays only the published costs/reactions. Never recalculates damage.
    void replay(const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim, const mwmp::Attack& result);
}
#endif
