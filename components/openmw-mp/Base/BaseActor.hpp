#ifndef OPENMW_BASEACTOR_HPP
#define OPENMW_BASEACTOR_HPP

#include <components/esm/loadcell.hpp>

#include <vector>

#include <components/openmw-mp/Base/BaseStructs.hpp>

#include <RakNetTypes.h>

namespace mwmp
{
    struct ActorAiDoorBreadcrumb
    {
        ESM::Cell fromCell;
        ESM::Position fromPosition;
        ESM::Cell toCell;
        ESM::Position toPosition;
    };

    class BaseActor
    {
    public:

        // Y002: every scalar member is initialized here.
        //
        // refNum, mpNum, movementFlags, drawState, isFlying, deathState,
        // isFollowerCellChange and the two SimpleCreatureStats booleans were left
        // indeterminate. A default-constructed BaseActor is used as the receive
        // scratch buffer for every actor packet, and packet types that do not
        // serialise a given field simply left the garbage in place - which then
        // propagated into DedicatedActor/LocalActor state. In particular a garbage
        // mDeathAnimationFinished makes LocalActor::update() stop sending position
        // and animFlags for a live NPC.
        BaseActor()
        {
            refNum = 0;
            mpNum = 0;

            // ESM::Position is a packed POD with no blank() of its own.
            blankPosition(position);
            blankPosition(direction);
            blankPosition(aiCoordinates);
            blankPosition(aiHomePosition);
            cell.blank();
            aiHomeCell.blank();

            movementFlags = 0;
            drawState = 0;
            isFlying = false;

            // ESM::StatState<float> zero-initializes itself, so only the two
            // trailing booleans of SimpleCreatureStats need help here.
            creatureStats.mDead = false;
            creatureStats.mDeathAnimationFinished = false;

            deathState = 0;
            isFollowerCellChange = false;

            hasPositionData = false;
            hasStatsDynamicData = false;
            hasAiTarget = false;
            aiAction = 0;
            aiDistance = 0;
            aiDuration = 0;
            aiShouldRepeat = false;
            aiHasReturnHome = false;
        }

    private:

        static void blankPosition(ESM::Position& value)
        {
            for (int i = 0; i < 3; ++i)
            {
                value.pos[i] = 0.f;
                value.rot[i] = 0.f;
            }
        }

    public:

        std::string refId = "";
        unsigned int refNum;
        unsigned int mpNum;

        ESM::Position position;
        ESM::Position direction;

        ESM::Cell cell;

        unsigned int movementFlags;
        char drawState;
        bool isFlying;

        std::string sound;

        SimpleCreatureStats creatureStats;

        Animation animation;
        char deathState;
        bool isInstantDeath = false;
        Attack attack;
        Cast cast;

        Target killer;

        bool isFollowerCellChange;

        bool hasAiTarget;
        Target aiTarget;
        unsigned int aiAction;
        unsigned int aiDistance;
        unsigned int aiDuration;
        bool aiShouldRepeat;
        ESM::Position aiCoordinates;

        // X034: a combat snapshot carries the full target set and the suspended
        // return-home route. This survives authority hand-offs and door changes.
        std::vector<Target> aiCombatTargets;
        bool aiHasReturnHome;
        ESM::Cell aiHomeCell;
        ESM::Position aiHomePosition;
        std::vector<ActorAiDoorBreadcrumb> aiDoorBreadcrumbs;

        bool hasPositionData;
        bool hasStatsDynamicData;

        Item equipmentItems[19];
        SpellsActiveChanges spellsActiveChanges;
    };

    class BaseActorList
    {
    public:

        BaseActorList()
        {

        }

        enum ACTOR_ACTION
        {
            SET = 0,
            ADD = 1,
            REMOVE = 2,
            REQUEST = 3
        };

        enum AI_ACTION
        {
            CANCEL = 0,
            ACTIVATE = 1,
            COMBAT = 2,
            ESCORT = 3,
            FOLLOW = 4,
            TRAVEL = 5,
            WANDER = 6,
            // X034: finish only combat/pursuit state without deleting authored
            // Wander/Travel packages. Used by authority heartbeat disengage.
            COMBAT_END = 7
        };

        RakNet::RakNetGUID guid;

        std::vector<BaseActor> baseActors;

        unsigned int count;

        ESM::Cell cell;

        unsigned char action; // 0 - Clear and set in entirety, 1 - Add item, 2 - Remove item, 3 - Request items

        bool isValid;
    };
}

#endif //OPENMW_BASEACTOR_HPP
