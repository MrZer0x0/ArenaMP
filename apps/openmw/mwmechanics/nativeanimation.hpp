#ifndef MWMECHANICS_NATIVEANIMATION_H
#define MWMECHANICS_NATIVEANIMATION_H

#include <string>

namespace MWMechanics
{
namespace NativeAnimation
{
    // ReAnimation attack variants can be reused by first-person and compatible
    // third-person humanoid skeletons. The selected group is synchronized through
    // Attack::attackAnimation (protocol 809 serializes it for melee and ranged);
    // no separate animation packet is sent.
    inline std::string attackGroup(const std::string& base, const std::string& attack,
                                   const std::string& weaponId, unsigned int sequence)
    {
        const bool alternate = sequence % 2 != 0;
        if (base == "weapononehand") return alternate ? "weapononehand1" : base;
        if (base == "handtohand") return alternate ? "handtohandalt" : base;
        if (base == "throwweaponstar") return alternate ? "throwweaponaltstar" : base;
        if (base == "throwweapon") return alternate ? "throwweaponalt" : base;
        if (base == "weapontwohand")
        {
            const char* katanaTerms[] = {"katana", "scythe", "gravedigger", "bloodrust", "lightsaber"};
            bool katana = false;
            for (const char* term : katanaTerms)
                katana = katana || weaponId.find(term) != std::string::npos;
            if (katana && attack == "chop")
                return alternate ? "weapontwohandktnalt" : "weapontwohandktn";
            if (katana && attack == "slash" && !alternate) return "weapontwohandktn";
            if (alternate) return "weapontwohandalt";
            if (attack == "chop" && sequence % 4 == 2) return "weapontwohandsub";
        }
        if (base == "weapontwowide")
        {
            if (attack == "thrust")
            {
                const char* groups[] = {"weapontwowide", "weapontwowidesub", "weapontwowidesub2",
                    "weapontwowidesub3", "weapontwowidesub4"};
                return groups[sequence % 5];
            }
            if (alternate) return "weapontwowidealt";
            if (attack == "slash" && sequence % 4 == 2) return "weapontwowidesub";
        }
        return base;
    }

    // The dedicated third-person pack names some alternate groups with a trailing
    // "1" while ReAnimation v3 uses explicit "alt" names. Keep the network
    // representation camera-independent and translate only at render time.
    inline std::string canonicalAttackGroup(const std::string& group)
    {
        if (group == "weapontwohand1") return "weapontwohandalt";
        if (group == "weapontwowide1") return "weapontwowidealt";
        if (group == "throwweapon1") return "throwweaponalt";
        return group;
    }

    inline std::string thirdPersonAttackGroup(const std::string& group)
    {
        if (group == "weapontwohandalt") return "weapontwohand1";
        if (group == "weapontwowidealt") return "weapontwowide1";
        if (group == "throwweaponalt") return "throwweapon1";
        return group;
    }

    // Keep the vanilla attack type at the front so logs/debugging remain readable.
    // New clients decode the optional selected ReAnimation group after the separator.
    inline std::string encodeAttackSelection(const std::string& attack, const std::string& group)
    {
        if (group.empty())
            return attack;
        return attack + "|arenamp_anim|" + canonicalAttackGroup(group);
    }

    inline void decodeAttackSelection(const std::string& encoded, std::string& attack, std::string& group)
    {
        group.clear();
        if (encoded.empty())
            return; // Preserve the controller's already known attack type.

        const std::string separator = "|arenamp_anim|";
        const std::size_t pos = encoded.find(separator);
        if (pos == std::string::npos)
        {
            attack = encoded;
            return;
        }

        // Malformed network/script input must not erase the safe local attack type.
        if (pos == 0)
            return;

        attack = encoded.substr(0, pos);
        group = encoded.substr(pos + separator.size());
    }

    inline bool isQuickCastGroup(const std::string& group)
    {
        return group == "quickcast" || group == "quickbuff" || group == "qcconj"
            || group == "qctouch" || group == "qcalt" || group == "qcill"
            || group == "qcsnap" || group == "qcdrain";
    }
}
}
#endif
