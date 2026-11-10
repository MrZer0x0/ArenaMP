#include "combatsoundmanager.hpp"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <components/esm/loadcrea.hpp>
#include <components/esm/loadnpc.hpp>
#include <components/esm/loadweap.hpp>
#include <components/misc/rng.hpp>
#include <components/misc/stringops.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/vfs/manager.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwworld/class.hpp"

namespace ArenaCombatSoundNative
{
    const char* const sSwingDir = "sound/fx/item/csoswing/";
    const char* const sHitDir = "sound/fx/item/csohit/";
    const char* const sCreatureDir = "sound/fx/item/csoocreature/";

    enum class WeaponFamily
    {
        Other,
        ShortBlade,
        LongBlade,
        Blunt,
        Staff,
        Spear,
        Axe,
        Thrown,
        Bow,
        Crossbow,
        HandToHand,
        Natural
    };

    enum class BodyKind
    {
        Flesh,
        Bone,
        Ghost,
        Metal
    };

    struct NumberRange
    {
        int first;
        int last;
    };

    typedef std::vector<std::string> FileList;
    typedef std::map<std::string, FileList> CategoryMap;

    struct SoundCache
    {
        bool initialized = false;
        CategoryMap swing;
        CategoryMap hit;
        std::map<std::string, FileList> creature;
    };

    SoundCache& cache()
    {
        static SoundCache result;
        return result;
    }

    bool supportedExtension(const std::string& name)
    {
        const std::size_t dot = name.find_last_of('.');
        if (dot == std::string::npos)
            return false;
        const std::string ext = Misc::StringUtils::lowerCase(name.substr(dot + 1));
        return ext == "wav" || ext == "mp3" || ext == "ogg" || ext == "flac";
    }

    int trailingNumber(const std::string& name)
    {
        const std::size_t dot = name.find_last_of('.');
        if (dot == std::string::npos || dot == 0)
            return -1;
        std::size_t end = dot;
        std::size_t begin = end;
        while (begin > 0 && name[begin - 1] >= '0' && name[begin - 1] <= '9')
            --begin;
        if (begin == end)
            return -1;
        return std::atoi(name.substr(begin, end - begin).c_str());
    }

    bool inRanges(int number, const std::vector<NumberRange>& ranges)
    {
        for (const NumberRange& range : ranges)
            if (number >= range.first && number <= range.last)
                return true;
        return false;
    }

    void buildCategory(CategoryMap& out, const std::string& name, const FileList& files,
        const std::vector<NumberRange>& ranges)
    {
        FileList& result = out[name];
        for (const std::string& file : files)
        {
            const int number = trailingNumber(file);
            if (number >= 0 && inRanges(number, ranges))
                result.push_back(file);
        }
    }

    FileList filesWithPrefix(const std::string& rawPrefix)
    {
        FileList result;
        Resource::ResourceSystem* resources = MWBase::Environment::get().getResourceSystem();
        if (!resources)
            return result;
        const VFS::Manager* vfs = resources->getVFS();
        if (!vfs)
            return result;

        std::string prefix = rawPrefix;
        vfs->normalizeFilename(prefix);
        const std::map<std::string, VFS::File*>& index = vfs->getIndex();
        std::map<std::string, VFS::File*>::const_iterator it = index.lower_bound(prefix);
        for (; it != index.end(); ++it)
        {
            const std::string& name = it->first;
            if (name.size() < prefix.size() || name.compare(0, prefix.size(), prefix) != 0)
                break;
            if (supportedExtension(name))
                result.push_back(name);
        }
        return result;
    }

    void initialize()
    {
        SoundCache& c = cache();
        if (c.initialized)
            return;
        c.initialized = true;

        const FileList swingFiles = filesWithPrefix(sSwingDir);
        const FileList hitFiles = filesWithPrefix(sHitDir);

        // Same numeric sorting used by the supplied SoundManager.lua.
        buildCategory(c.swing, "plain", swingFiles, {{21, 23}});
        buildCategory(c.swing, "sharpMetal", swingFiles, {{1, 3}});
        buildCategory(c.swing, "bladeMetal", swingFiles, {{11, 16}});
        buildCategory(c.swing, "ringingBladeMetal", swingFiles, {{24, 26}});
        buildCategory(c.swing, "heavyMetal", swingFiles, {{7, 10}, {17, 20}});
        buildCategory(c.swing, "sharpMetalRinging", swingFiles, {{4, 5}});

        buildCategory(c.hit, "FleshHit", hitFiles, {{1, 10}});
        buildCategory(c.hit, "metallicPunchyFleshHit", hitFiles, {{25, 29}});
        buildCategory(c.hit, "FleshSlice", hitFiles, {{61, 64}});
        buildCategory(c.hit, "FleshStab", hitFiles, {{66, 68}, {70, 70}, {73, 76}});
        buildCategory(c.hit, "stabPure", hitFiles, {{65, 65}, {77, 78}});
        buildCategory(c.hit, "cutPure", hitFiles, {{11, 17}});
        buildCategory(c.hit, "BoneHit", hitFiles, {{72, 72}, {79, 79}});
        buildCategory(c.hit, "armorMediumPureHit", hitFiles, {{41, 56}, {71, 71}});
        buildCategory(c.hit, "BluntBoneHit", hitFiles, {{80, 81}});
        buildCategory(c.hit, "BluntRingPure", hitFiles, {{69, 69}});
        buildCategory(c.hit, "RingingBladePure", hitFiles, {{18, 21}, {30, 30}});
        buildCategory(c.hit, "armorHeavyBladePureHit", hitFiles,
            {{22, 24}, {31, 32}, {57, 60}, {85, 85}, {96, 96}});
        buildCategory(c.hit, "armorHeavyBluntPureHit", hitFiles,
            {{33, 40}, {84, 84}, {87, 95}, {97, 102}});
        buildCategory(c.hit, "BluntHitPure", hitFiles, {{82, 83}, {86, 86}});
    }

    void appendUniqueCategory(FileList& out, std::set<std::string>& added,
        const CategoryMap& categories, const std::string& category)
    {
        CategoryMap::const_iterator found = categories.find(category);
        if (found == categories.end())
            return;
        for (const std::string& file : found->second)
            if (added.insert(file).second)
                out.push_back(file);
    }

    std::string randomFile(const FileList& files)
    {
        if (files.empty())
            return std::string();
        return files[Misc::Rng::rollDice(static_cast<int>(files.size()))];
    }

    std::string pickFrom(const CategoryMap& categories, const std::vector<std::string>& names)
    {
        FileList pool;
        std::set<std::string> added;
        for (const std::string& name : names)
            appendUniqueCategory(pool, added, categories, name);
        return randomFile(pool);
    }

    std::string pickSwingWithPlainShare(const std::vector<std::string>& nonPlain, float plainShare)
    {
        initialize();
        const CategoryMap& categories = cache().swing;
        if (Misc::Rng::rollProbability() < plainShare)
        {
            const std::string plain = pickFrom(categories, {"plain"});
            if (!plain.empty())
                return plain;
        }
        std::string selected = pickFrom(categories, nonPlain);
        if (!selected.empty())
            return selected;
        return pickFrom(categories, {"plain"});
    }

    WeaponFamily weaponFamily(const MWWorld::Ptr& actor, const MWWorld::Ptr& weapon)
    {
        if (weapon.isEmpty())
            return !actor.isEmpty() && actor.getClass().isNpc() ? WeaponFamily::HandToHand : WeaponFamily::Natural;
        if (weapon.getTypeName() != typeid(ESM::Weapon).name())
            return WeaponFamily::Other;

        switch (weapon.get<ESM::Weapon>()->mBase->mData.mType)
        {
            case ESM::Weapon::ShortBladeOneHand: return WeaponFamily::ShortBlade;
            case ESM::Weapon::LongBladeOneHand:
            case ESM::Weapon::LongBladeTwoHand: return WeaponFamily::LongBlade;
            case ESM::Weapon::BluntOneHand:
            case ESM::Weapon::BluntTwoClose: return WeaponFamily::Blunt;
            case ESM::Weapon::BluntTwoWide: return WeaponFamily::Staff;
            case ESM::Weapon::SpearTwoWide: return WeaponFamily::Spear;
            case ESM::Weapon::AxeOneHand:
            case ESM::Weapon::AxeTwoHand: return WeaponFamily::Axe;
            case ESM::Weapon::MarksmanThrown: return WeaponFamily::Thrown;
            case ESM::Weapon::MarksmanBow:
            case ESM::Weapon::Arrow: return WeaponFamily::Bow;
            case ESM::Weapon::MarksmanCrossbow:
            case ESM::Weapon::Bolt: return WeaponFamily::Crossbow;
            default: return WeaponFamily::Other;
        }
    }

    bool isThrust(const std::string& attackType)
    {
        return Misc::StringUtils::ciEqual(attackType, "thrust");
    }

    BodyKind bodyKind(const MWWorld::Ptr& victim)
    {
        int blood = 0;
        std::string words;
        if (!victim.isEmpty() && victim.getClass().isNpc())
        {
            const ESM::NPC* record = victim.get<ESM::NPC>()->mBase;
            blood = record->mBloodType;
            words = record->mName + " " + record->mModel + " " + record->mId;
        }
        else if (!victim.isEmpty() && victim.getTypeName() == typeid(ESM::Creature).name())
        {
            const ESM::Creature* record = victim.get<ESM::Creature>()->mBase;
            blood = record->mBloodType;
            words = record->mName + " " + record->mModel + " " + record->mId;
        }

        if (blood == 2)
            return BodyKind::Metal;
        if (blood != 0)
        {
            const std::string lower = Misc::StringUtils::lowerCase(words);
            static const char* ghostWords[] = {
                "ghost", "spectre", "specter", "spirit", "haunt", "wraith", "phantom", "apparition"
            };
            for (const char* word : ghostWords)
                if (lower.find(word) != std::string::npos)
                    return BodyKind::Ghost;
            if (blood == 1)
                return BodyKind::Bone;
        }
        return BodyKind::Flesh;
    }

    std::string pickStaffOrSpearSwing()
    {
        initialize();
        const float roll = Misc::Rng::rollProbability();
        std::string selected;
        if (roll < 0.50f)
            selected = pickFrom(cache().swing, {"plain"});
        else if (roll < 0.90f)
            selected = pickFrom(cache().swing, {"bladeMetal"});
        else
            selected = pickFrom(cache().swing, {"heavyMetal"});
        if (!selected.empty())
            return selected;
        return pickFrom(cache().swing, {"plain", "bladeMetal", "heavyMetal"});
    }

    std::string pickSwing(WeaponFamily family, bool thrust)
    {
        initialize();
        switch (family)
        {
            case WeaponFamily::ShortBlade:
                return pickSwingWithPlainShare({"sharpMetal", "sharpMetalRinging", "ringingBladeMetal"}, 0.30f);
            case WeaponFamily::LongBlade:
                return pickSwingWithPlainShare({"ringingBladeMetal", "bladeMetal"}, 0.30f);
            case WeaponFamily::Blunt:
                return pickSwingWithPlainShare({"heavyMetal"}, 0.50f);
            case WeaponFamily::Axe:
                return pickSwingWithPlainShare({"heavyMetal"}, 0.30f);
            case WeaponFamily::Spear:
                if (thrust)
                    return pickSwingWithPlainShare({"bladeMetal", "ringingBladeMetal"}, 0.30f);
                return pickStaffOrSpearSwing();
            case WeaponFamily::Staff:
                return pickStaffOrSpearSwing();
            case WeaponFamily::Thrown:
                return pickFrom(cache().swing, {"sharpMetal", "sharpMetalRinging", "plain"});
            default:
                // The supplied Lua intentionally keeps H2H, bows, crossbows and natural attacks vanilla.
                return std::string();
        }
    }

    std::vector<std::string> fleshCategories(WeaponFamily family, bool thrust)
    {
        switch (family)
        {
            case WeaponFamily::ShortBlade:
                return thrust
                    ? std::vector<std::string>{"FleshStab", "FleshHit", "metallicPunchyFleshHit"}
                    : std::vector<std::string>{"FleshSlice", "FleshHit", "metallicPunchyFleshHit"};
            case WeaponFamily::LongBlade:
                return {"FleshHit", "FleshStab", "metallicPunchyFleshHit"};
            case WeaponFamily::Spear:
                if (thrust)
                    return {"FleshStab", "metallicPunchyFleshHit"};
                break;
            case WeaponFamily::Blunt:
                return {"metallicPunchyFleshHit"};
            case WeaponFamily::Thrown:
                return {"FleshStab", "FleshHit", "metallicPunchyFleshHit"};
            case WeaponFamily::Bow:
            case WeaponFamily::Crossbow:
                return {"FleshStab", "FleshHit"};
            case WeaponFamily::HandToHand:
                return {"FleshHit"};
            default:
                break;
        }
        return {"FleshHit", "metallicPunchyFleshHit"};
    }

    std::vector<std::string> armorCategories(MWMechanics::CombatSoundManager::Surface surface,
        WeaponFamily family, bool thrust)
    {
        using Surface = MWMechanics::CombatSoundManager::Surface;
        if (surface == Surface::LightArmor)
            return fleshCategories(family, thrust);

        if (surface == Surface::MediumArmor)
        {
            if (thrust)
                return {"armorMediumPureHit", "stabPure", "metallicPunchyFleshHit"};
            if (family == WeaponFamily::HandToHand || family == WeaponFamily::Bow || family == WeaponFamily::Crossbow)
                return {"armorMediumPureHit"};
            return {"armorMediumPureHit", "metallicPunchyFleshHit"};
        }

        if (surface == Surface::HeavyArmor)
        {
            if (thrust)
            {
                if (family == WeaponFamily::Blunt || family == WeaponFamily::Staff)
                    return {"armorHeavyBluntPureHit", "stabPure", "metallicPunchyFleshHit"};
                return {"armorHeavyBladePureHit", "stabPure", "metallicPunchyFleshHit"};
            }
            switch (family)
            {
                case WeaponFamily::ShortBlade:
                case WeaponFamily::LongBlade:
                case WeaponFamily::Axe:
                    return {"armorHeavyBladePureHit", "metallicPunchyFleshHit"};
                case WeaponFamily::Blunt:
                case WeaponFamily::Staff:
                    return {"armorHeavyBluntPureHit", "metallicPunchyFleshHit"};
                case WeaponFamily::HandToHand:
                    return {"armorHeavyBluntPureHit"};
                case WeaponFamily::Bow:
                case WeaponFamily::Crossbow:
                    return {"armorHeavyBladePureHit", "armorHeavyBluntPureHit"};
                default:
                    return {"armorHeavyBladePureHit", "armorHeavyBluntPureHit", "metallicPunchyFleshHit"};
            }
        }
        return std::vector<std::string>();
    }

    std::vector<std::string> bodyCategories(BodyKind body, WeaponFamily family, bool thrust)
    {
        if (body == BodyKind::Flesh)
            return fleshCategories(family, thrust);
        if (body == BodyKind::Metal)
        {
            if (family == WeaponFamily::Blunt || family == WeaponFamily::Staff || family == WeaponFamily::HandToHand)
                return {"armorHeavyBluntPureHit"};
            if (family == WeaponFamily::ShortBlade || family == WeaponFamily::LongBlade || family == WeaponFamily::Axe || thrust)
                return {"armorHeavyBladePureHit"};
            return {"armorHeavyBladePureHit", "armorHeavyBluntPureHit"};
        }

        // Ghosts intentionally use the same bone impact families as the supplied Lua.
        if (family == WeaponFamily::Blunt || family == WeaponFamily::Staff)
            return {"BluntHitPure", "BluntBoneHit"};
        if (thrust)
            return {"stabPure"};
        return {"BoneHit"};
    }

    float randomPitch()
    {
        return 0.9f + Misc::Rng::rollClosedProbability() * 0.2f;
    }

    bool playFile(const MWWorld::Ptr& actor, const std::string& file, float volume = 1.f)
    {
        if (file.empty() || actor.isEmpty())
            return false;
        return MWBase::Environment::get().getSoundManager()->playSoundFile3D(actor, file, volume, randomPitch()) != nullptr;
    }

    void playRingLayer(const MWWorld::Ptr& victim, WeaponFamily family, bool thrust)
    {
        if (thrust || Misc::Rng::rollProbability() >= 0.25f)
            return;
        std::string file;
        if (family == WeaponFamily::Blunt)
            file = pickFrom(cache().hit, {"BluntRingPure"});
        else if (family == WeaponFamily::ShortBlade || family == WeaponFamily::LongBlade || family == WeaponFamily::Axe)
            file = pickFrom(cache().hit, {"RingingBladePure"});
        if (!file.empty())
            playFile(victim, file);
    }

    std::string creatureClass(const MWWorld::Ptr& victim, BodyKind body)
    {
        if (body == BodyKind::Ghost) return "ghost";
        if (body == BodyKind::Bone) return "skeleton";
        if (body == BodyKind::Metal) return "metal";
        if (victim.isEmpty() || victim.getTypeName() != typeid(ESM::Creature).name())
            return std::string();
        switch (victim.get<ESM::Creature>()->mBase->mData.mType)
        {
            case ESM::Creature::Creatures: return "creatures";
            case ESM::Creature::Daedra: return "daedra";
            case ESM::Creature::Undead: return "undead";
            case ESM::Creature::Humanoid: return "humanoid";
            default: return std::string();
        }
    }

    void playCreatureLayer(const MWWorld::Ptr& victim, BodyKind body)
    {
        const std::string cls = creatureClass(victim, body);
        if (cls.empty())
            return;
        SoundCache& c = cache();
        std::map<std::string, FileList>::iterator found = c.creature.find(cls);
        if (found == c.creature.end())
            found = c.creature.insert(std::make_pair(cls, filesWithPrefix(std::string(sCreatureDir) + cls + "/"))).first;
        const std::string file = randomFile(found->second);
        if (!file.empty())
            playFile(victim, file);
    }
}

namespace MWMechanics
{
    bool CombatSoundManager::playSwing(const MWWorld::Ptr& actor, const MWWorld::Ptr& weapon,
        const std::string& attackType, float /*attackStrength*/)
    {
        ArenaCombatSoundNative::initialize();
        const ArenaCombatSoundNative::WeaponFamily family = ArenaCombatSoundNative::weaponFamily(actor, weapon);
        const std::string file = ArenaCombatSoundNative::pickSwing(
            family, ArenaCombatSoundNative::isThrust(attackType));
        return ArenaCombatSoundNative::playFile(actor, file);
    }

    bool CombatSoundManager::playHit(const MWWorld::Ptr& victim, const MWWorld::Ptr& attacker,
        const MWWorld::Ptr& weapon, Surface surface, const std::string& attackType)
    {
        ArenaCombatSoundNative::initialize();
        const ArenaCombatSoundNative::WeaponFamily family = ArenaCombatSoundNative::weaponFamily(attacker, weapon);
        const bool thrust = ArenaCombatSoundNative::isThrust(attackType);
        const ArenaCombatSoundNative::BodyKind body = ArenaCombatSoundNative::bodyKind(victim);

        std::vector<std::string> categories;
        if (surface == Surface::Body)
            categories = ArenaCombatSoundNative::bodyCategories(body, family, thrust);
        else
            categories = ArenaCombatSoundNative::armorCategories(surface, family, thrust);

        const std::string mainFile = ArenaCombatSoundNative::pickFrom(ArenaCombatSoundNative::cache().hit, categories);
        if (mainFile.empty())
            return false;

        // Medium/heavy pure armor impacts in the Lua version have a body layer underneath.
        // ArenaMP still plays Health Damage for health hits, which naturally provides that layer,
        // so only the selected CSO impact and optional ring/creature layers are added here.
        if (!ArenaCombatSoundNative::playFile(victim, mainFile))
            return false;

        ArenaCombatSoundNative::playRingLayer(victim, family, thrust);
        ArenaCombatSoundNative::playCreatureLayer(victim, body);
        return true;
    }
}
