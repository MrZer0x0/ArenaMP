#ifndef OPENMW_MWMP_PLAYERPOSE_H
#define OPENMW_MWMP_PLAYERPOSE_H
#include <string>
#include <components/esm/loadweap.hpp>
#include <components/esm/loadarmo.hpp>
#include "../mwworld/class.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwmechanics/creaturestats.hpp"

namespace mwmp
{
    inline bool isWeaponPose(const std::string& group)
    {
        return group == "arenamp_pose_1h" || group == "arenamp_pose_1hs"
            || group == "arenamp_pose_2c" || group == "arenamp_pose_2w" || group == "arenamp_pose_2b";
    }

    inline bool weaponPoseMatches(const MWWorld::Ptr& ptr, const std::string& group)
    {
        if (ptr.isEmpty() || !ptr.getClass().hasInventoryStore(ptr)) return false;
        const auto& inv = ptr.getClass().getInventoryStore(ptr);
        const auto weapon = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
        if (weapon == inv.end() || weapon->getTypeName() != typeid(ESM::Weapon).name()) return false;
        const int type = weapon->get<ESM::Weapon>()->mBase->mData.mType;
        const auto left = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedLeft);
        const bool shield = left != inv.end() && left->getTypeName() == typeid(ESM::Armor).name();
        if (group == "arenamp_pose_1h" || group == "arenamp_pose_1hs")
            return (type == ESM::Weapon::ShortBladeOneHand || type == ESM::Weapon::LongBladeOneHand
                || type == ESM::Weapon::BluntOneHand || type == ESM::Weapon::AxeOneHand)
                && (group != "arenamp_pose_1hs" || shield);
        if (group == "arenamp_pose_2c") return type == ESM::Weapon::LongBladeTwoHand;
        if (group == "arenamp_pose_2w") return type == ESM::Weapon::SpearTwoWide || type == ESM::Weapon::BluntTwoWide;
        if (group == "arenamp_pose_2b") return type == ESM::Weapon::BluntTwoClose || type == ESM::Weapon::AxeTwoHand;
        return false;
    }
}
#endif
