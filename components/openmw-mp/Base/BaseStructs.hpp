#ifndef OPENMW_BASESTRUCTS_HPP
#define OPENMW_BASESTRUCTS_HPP

#include <string>

#include <components/esm/activespells.hpp>
#include <components/esm/loadcell.hpp>
#include <components/esm/statstate.hpp>

#include <RakNetTypes.h>

namespace mwmp
{
    namespace DialogueChoiceType
    {
        enum DIALOGUE_CHOICE
        {
            TOPIC,
            PERSUASION,
            COMPANION_SHARE,
            BARTER,
            SPELLS,
            TRAVEL,
            SPELLMAKING,
            ENCHANTING,
            TRAINING,
            REPAIR,
            MOVE_ASIDE,
            MOVE_ASIDE_PAID
        };
    }

    enum PACKET_ORIGIN
    {
        CLIENT_GAMEPLAY = 0,
        CLIENT_CONSOLE = 1,
        CLIENT_DIALOGUE = 2,
        CLIENT_SCRIPT_LOCAL = 3,
        CLIENT_SCRIPT_GLOBAL = 4,
        SERVER_SCRIPT = 5
    };

    enum VARIABLE_TYPE
    {
        SHORT,
        LONG,
        FLOAT,
        INT,
        STRING
    };

    struct ClientVariable
    {
        /*
            Start of AMP change

            Give every field a default so a partially filled or malformed packet can never
            leave uninitialized memory behind, which previously produced garbage indices
            and values when applied to a script's locals
        */
        std::string id = "";
        int internalIndex = 0;

        char variableType = 0;

        int intValue = 0;
        float floatValue = 0.0f;
        std::string stringValue = "";
        /*
            End of AMP change
        */
    };

    struct Time
    {
        float hour;
        int day;
        int month;
        int year;

        int daysPassed;
        float timeScale;
    };

    struct Item
    {
        std::string refId;
        int count = 0;
        int charge = -1;
        float enchantmentCharge = -1;
        std::string soul;
        std::string poisonId;
        int poisonCharges = 0;

        inline bool operator==(const Item& rhs)
        {
            return refId == rhs.refId && count == rhs.count && charge == rhs.charge &&
                enchantmentCharge == rhs.enchantmentCharge && soul == rhs.soul &&
                poisonId == rhs.poisonId && poisonCharges == rhs.poisonCharges;
        }
    };

    struct ProjectileOrigin
    {
        // Y002: default member initializers throughout this header.
        //
        // These structs are the receive-side scratch storage for network packets.
        // Serialization is conditional almost everywhere (an Attack only carries
        // its damage block when isHit, a Cast only carries a projectile origin
        // when hasProjectile, and so on), so any field a given packet does not
        // write was previously read back as indeterminate memory.
        float origin[3] = { 0.f, 0.f, 0.f };
        float orientation[4] = { 0.f, 0.f, 0.f, 0.f };
    };
    
    struct Target
    {
        bool isPlayer = false;

        std::string refId;
        unsigned int refNum = 0;
        unsigned int mpNum = 0;

        std::string name; // Remove this once the server can get names corresponding to different refIds

        RakNet::RakNetGUID guid;
    };

    class Attack
    {
    public:

        Target target;

        enum TYPE
        {
            MELEE = 0,
            RANGED
        };

        char type = MELEE;
        std::string attackAnimation;

        std::string rangedWeaponId;
        std::string rangedAmmoId;

        ESM::Position hitPosition = {};
        ProjectileOrigin projectileOrigin;

        // Y086 local-only HUD metadata. Not serialized; protocol 809 unchanged.
        bool comboFinisher = false;
        float comboAppliedDamage = 0.f;
        float damage = 0;
        float attackStrength = 0;

        bool isHit = false;
        bool success = false;
        bool block = false;
        // Protocol 808: 0 none, 1 partial guard, 2 perfect parry, 3 guard broken.
        unsigned char guardResult = 0;
        float guardFatigue = 0.f;
        float guardWear = 0.f;
        std::string guardItem;
        
        bool pressed = false;
        bool instant = false;
        bool knockdown = false;
        bool applyWeaponEnchantment = false;
        bool applyAmmoEnchantment = false;

        bool shouldSend = false;
    };

    class Cast
    {
    public:

        Target target;

        char type = 0; // 0 - regular magic, 1 - item magic
        enum TYPE
        {
            REGULAR = 0,
            ITEM
        };

        std::string spellId; // id of spell (e.g. "fireball")
        std::string itemId;

        bool hasProjectile = false;
        ProjectileOrigin projectileOrigin;

        bool isHit = false;
        bool success = false;
        bool pressed = false;
        bool instant = false;

        bool shouldSend = false;
    };

    struct SpellCooldown
    {
        std::string id;
        int startTimestampDay = 0;
        double startTimestampHour = 0.0;
    };

    struct ActiveSpell
    {
        std::string id;
        bool isStackingSpell = false;
        int timestampDay = 0;
        double timestampHour = 0.0;
        Target caster;
        ESM::ActiveSpells::ActiveSpellParams params;
    };

    struct SpellsActiveChanges
    {
        std::vector<ActiveSpell> activeSpells;
        enum ACTION_TYPE
        {
            SET = 0,
            ADD,
            REMOVE
        };
        int action = SET; // 0 - Clear and set in entirety, 1 - Add spell, 2 - Remove spell
    };

    struct Animation
    {
        std::string groupname;
        int mode = 0;
        int count = 0;
        bool persist = false;
    };

    struct SimpleCreatureStats
    {
        ESM::StatState<float> mDynamic[3];
        bool mDead = false;
        bool mDeathAnimationFinished = false;
    };
}

#endif //OPENMW_BASESTRUCTS_HPP
