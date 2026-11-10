#ifndef OPENMW_BASEEVENT_HPP
#define OPENMW_BASEEVENT_HPP

#include <components/esm/loadcell.hpp>
#include <components/openmw-mp/Base/BaseStructs.hpp>
#include <RakNetTypes.h>

namespace mwmp
{
    struct ContainerItem
    {
        std::string refId;
        int count = 0;
        int charge = -1;
        double enchantmentCharge = -1;
        std::string soul;
        std::string poisonId;
        int poisonCharges = 0;

        // ArenaMP X012: client-derived quest phasing metadata. The server never
        // trusts this as inventory data; it is only a hint that this source should
        // use per-player claim semantics instead of shared removal.
        bool questItem = false;
        std::string questSourceId;

        int actionCount = 0;

        inline bool operator==(const ContainerItem& rhs)
        {
            return refId == rhs.refId && count == rhs.count && charge == rhs.charge &&
                enchantmentCharge == rhs.enchantmentCharge && soul == rhs.soul &&
                poisonId == rhs.poisonId && poisonCharges == rhs.poisonCharges;
        }
    };

    struct BaseObject
    {
        /*
            Start of AMP change

            Default-initialize every field

            Scripts attached to the player never filled refNum/mpNum through
            getBaseObjectFromPtr(), so ID_CLIENT_SCRIPT_LOCAL packets used to carry
            whatever happened to be on the stack as their uniqueIndex, corrupting
            serverside cell data
        */
        std::string refId = "";
        unsigned int refNum = 0;
        unsigned int mpNum = 0;
        int count = 1;
        int charge = -1;
        double enchantmentCharge = -1;
        std::string soul = "";
        std::string poisonId = "";
        int poisonCharges = 0;
        int goldValue = 1;

        // ArenaMP X012: only serialized by packets that can consume world quest
        // sources (currently ObjectDelete).
        bool questItem = false;
        std::string questSourceId = "";
        /*
            End of AMP change
        */

        ESM::Position position;

        bool objectState;
        int lockLevel;
        float scale;

        unsigned char dialogueChoiceType;
        std::string topicId;
        int guiId;

        std::string soundId;
        float volume;
        float pitch;

        unsigned int goldPool;
        float lastGoldRestockHour;
        int lastGoldRestockDay;


        int doorState;
        bool teleportState;
        ESM::Cell destinationCell;
        ESM::Position destinationPosition;

        std::string musicFilename;

        std::string videoFilename;
        bool allowSkipping;

        std::string animGroup;
        int animMode;

        bool isDisarmed;
        bool droppedByPlayer;

        Target activatingActor;
        Target hittingActor;
        Attack hitAttack;

        bool isSummon;
        int summonEffectId;
        std::string summonSpellId;
        float summonDuration;
        Target master;

        bool hasContainer;

        /*
            Start of AMP addition

            The ID of the script whose locals are carried by this object, so a receiver can
            refuse to apply values that belong to a different script - which is what happens
            when the content files change and local variable indices shift
        */
        std::string clientScriptId = "";
        /*
            End of AMP addition
        */

        std::vector<ClientVariable> clientLocals;
        std::vector<ContainerItem> containerItems;
        unsigned int containerItemCount = 0;

        RakNet::RakNetGUID guid; // only for object lists that can also include players
        bool isPlayer;
    };

    class BaseObjectList
    {
    public:

        BaseObjectList(RakNet::RakNetGUID guid) : guid(guid)
        {

        }

        BaseObjectList()
        {

        }

        enum WORLD_ACTION
        {
            SET = 0,
            ADD = 1,
            REMOVE = 2,
            REQUEST = 3
        };

        enum CONTAINER_SUBACTION
        {
            NONE = 0,
            DRAG = 1,
            DROP = 2,
            TAKE_ALL = 3,
            REPLY_TO_REQUEST = 4,
            RESTOCK_RESULT = 5
        };

        RakNet::RakNetGUID guid;
        
        std::vector<BaseObject> baseObjects;
        unsigned int baseObjectCount;

        ESM::Cell cell;
        std::string consoleCommand;

        unsigned char packetOrigin; // 0 - Gameplay, 1 - Console, 2 - Client script, 3 - Server script
        std::string originClientScript;

        unsigned char action; // 0 - Clear and set in entirety, 1 - Add item, 2 - Remove item, 3 - Request items
        unsigned char containerSubAction; // 0 - None, 1 - Drag, 2 - Drop, 3 - Take all, 4 - Reply to request

        bool isValid;
    };
}

#endif //OPENMW_BASEEVENT_HPP
