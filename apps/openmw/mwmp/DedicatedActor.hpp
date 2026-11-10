#ifndef OPENMW_DEDICATEDACTOR_HPP
#define OPENMW_DEDICATEDACTOR_HPP

#include <components/openmw-mp/Base/BaseActor.hpp>
#include "../mwmechanics/aisequence.hpp"
#include "../mwworld/manualref.hpp"
#include "InteractionAnimationSync.hpp"

namespace mwmp
{
    class DedicatedActor : public BaseActor
    {
    public:

        DedicatedActor();
        virtual ~DedicatedActor();

        void update(float dt);
        void move(float dt);
        void setCell(MWWorld::CellStore *cellStore);
        void setMovementSettings();
        void setPosition();
        void setAnimFlags();
        void setStatsDynamic();
        void setEquipment();
        void setAi();
        void playAnimation();
        void cancelInteractionAnimation();
        void updateInteractionAnimation();
        void playSound();

        bool hasItem(std::string itemId, int charge);
        void equipItem(std::string itemId, int charge, bool noSound = false);

        void addSpellsActive();
        void removeSpellsActive();
        void setSpellsActive();

        MWWorld::Ptr getPtr();
        void setPtr(const MWWorld::Ptr& newPtr);
        void reloadPtr();

        /// X044: true only once a real ID_ACTOR_AI packet has been applied to
        /// this actor. BaseActor default-constructs aiAction to CANCEL(0), so
        /// calling setAi() on an actor that never received an AI packet used to
        /// wipe its entire AiSequence - including the Wander package every
        /// ordinary NPC relies on to move around.
        bool hasReceivedAi = false;

    private:
        MWWorld::Ptr ptr;

        bool hasReceivedInitialEquipment;
        bool hasChangedCell;

        bool mInteractionAnimationActive;
        InteractionAnimationData mInteractionAnimation;
        float mAiResolveRetry = 0.f;
    };
}

#endif //OPENMW_DEDICATEDACTOR_HPP
