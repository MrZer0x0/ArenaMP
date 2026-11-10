#ifndef GAME_MWMECHANICS_AICOMBAT_H
#define GAME_MWMECHANICS_AICOMBAT_H

#include "typedaipackage.hpp"

#include "../mwworld/cellstore.hpp" // for Doors

#include "../mwbase/world.hpp"

#include "pathfinding.hpp"
#include "movement.hpp"
#include "aitimer.hpp"

namespace ESM
{
    namespace AiSequence
    {
        struct AiCombat;
    }
}

namespace MWMechanics
{
    class Action;

    /// \brief This class holds the variables AiCombat needs which are deleted if the package becomes inactive.
    struct AiCombatStorage : AiTemporaryBase
    {
        float mAttackCooldown;
        AiReactionTimer mReaction;
        float mTimerCombatMove;
        bool mReadyToAttack;
        bool mAttack;
        float mAttackRange;
        bool mCombatMove;
        // Short melee commitment window. While active a close-range fighter
        // keeps pressure on the target instead of repeatedly switching between
        // circle/strafe/dodge/path decisions.
        float mMeleeCommitTimer;
        osg::Vec3f mLastTargetPos;
        const MWWorld::CellStore* mCell;
        std::shared_ptr<Action> mCurrentAction;
        float mActionCooldown;
        float mStrength;
        bool mForceNoShortcut;
        ESM::Position mShortcutFailPos;
        MWMechanics::Movement mMovement;

        enum FleeState
        {
            FleeState_None,
            FleeState_Idle,
            FleeState_RunBlindly,
            FleeState_RunToDestination,
            FleeState_RunToDoor,
            FleeState_RunToGuard
        };
        FleeState mFleeState;
        bool mLOS;
        bool mGeometricLOS;
        float mUpdateLOSTimer;
        float mTargetLostTimer;
        float mCrossCellTimer;
        float mUnreachableTimer;
        const MWWorld::CellStore* mLastActorCell;
        int mDoorTransitions;

        bool mHasLastSeenTarget;
        osg::Vec3f mLastSeenTargetPos;
        osg::Vec3f mLastSeenTargetVelocity;
        float mLostSightTimer;
        float mSearchElapsed;
        bool mSearchingLastKnown;
        osg::Vec3f mSearchDestination;
        int mSearchPointsVisited;
        float mFleeBlindRunTimer;
        ESM::Pathgrid::Point mFleeDest;
        bool mHasFleeDoor;
        osg::Vec3f mFleeDoorPos;

        bool mUseCustomDestination;
        osg::Vec3f mCustomDestination;

        enum TacticalState
        {
            Tactical_None,
            Tactical_StrafeLeft,
            Tactical_StrafeRight,
            Tactical_CircleLeft,
            Tactical_CircleRight,
            Tactical_Retreat,
            Tactical_JumpDodge,
            Tactical_SneakApproach,
            Tactical_Unstuck,
            // U035: level-scaled evasion. HopBack breaks out of an incoming
            // swing; CounterStep is the answer to it - a step into the opening
            // the swing leaves, with the attack cooldown waived once.
            Tactical_HopBack,
            Tactical_CounterStep
        };

        TacticalState mTacticalState;
        float mTacticalTimer;
        float mTacticalCooldown;
        float mTacticalDecisionTimer;
        float mJumpTimer;
        float mSneakTimer;
        // U035 evasion state. mAgilityRating is cached because it reads four
        // stats and is only allowed to change on the same cadence as the rest
        // of the tactical decisions.
        float mReactionDelay;
        float mCounterWindow;
        float mCounterSide;
        float mEvadeX;
        float mEvadeY;
        bool mTargetAttackWasActive;
        float mAgilityRating;
        float mAgilityRefresh;

        float mStuckCheckTimer;
        float mStuckDuration;
        osg::Vec3f mLastActorPos;
        bool mHasLastActorPos;
        osg::Vec3f mCombatOrigin;
        const MWWorld::CellStore* mCombatOriginCell;
        bool mCombatOriginSet;
        float mLeashExceededTimer;

        bool mFormationActive;
        osg::Vec3f mFormationDestination;
        float mFormationUpdateTimer;

        float mThreatUpdateTimer;
        bool mThreatFlee;
        int mFleeGuardActorId;
        bool mFleeAskedGuard;
        float mHealCooldown;

        AiCombatStorage():
        mAttackCooldown(0.0f),
        mTimerCombatMove(0.0f),
        mReadyToAttack(false),
        mAttack(false),
        mAttackRange(0.0f),
        mCombatMove(false),
        mMeleeCommitTimer(0.0f),
        mLastTargetPos(0,0,0),
        mCell(nullptr),
        mCurrentAction(),
        mActionCooldown(0.0f),
        mStrength(),
        mForceNoShortcut(false),
        mShortcutFailPos(),
        mMovement(),
        mFleeState(FleeState_None),
        mLOS(false),
        mGeometricLOS(false),
        mUpdateLOSTimer(0.0f),
        mTargetLostTimer(0.0f),
        mCrossCellTimer(0.0f),
        mUnreachableTimer(0.0f),
        mLastActorCell(nullptr),
        mDoorTransitions(0),
        mHasLastSeenTarget(false),
        mLastSeenTargetPos(),
        mLastSeenTargetVelocity(),
        mLostSightTimer(0.0f),
        mSearchElapsed(0.0f),
        mSearchingLastKnown(false),
        mSearchDestination(),
        mSearchPointsVisited(0),
        mFleeBlindRunTimer(0.0f),
        mHasFleeDoor(false),
        mFleeDoorPos(),
        mUseCustomDestination(false),
        mCustomDestination(),
        mTacticalState(Tactical_None),
        mTacticalTimer(0.0f),
        mTacticalCooldown(0.0f),
        mTacticalDecisionTimer(0.0f),
        mJumpTimer(0.0f),
        mSneakTimer(0.0f),
        mReactionDelay(0.0f),
        mCounterWindow(0.0f),
        mCounterSide(1.0f),
        mEvadeX(0.f),
        mEvadeY(-1.f),
        mTargetAttackWasActive(false),
        mAgilityRating(0.0f),
        mAgilityRefresh(0.0f),
        mStuckCheckTimer(0.0f),
        mStuckDuration(0.0f),
        mLastActorPos(),
        mHasLastActorPos(false),
        mCombatOrigin(),
        mCombatOriginCell(nullptr),
        mCombatOriginSet(false),
        mLeashExceededTimer(0.0f),
        mFormationActive(false),
        mFormationDestination(),
        mFormationUpdateTimer(0.0f),
        mThreatUpdateTimer(0.0f),
        mThreatFlee(false),
        mFleeGuardActorId(-1),
        mFleeAskedGuard(false),
        mHealCooldown(0.0f)
        {}

        void startCombatMove(bool isDistantCombat, float distToTarget, float rangeAttack, const MWWorld::Ptr& actor, const MWWorld::Ptr& target);
        void updateCombatMove(float duration);
        void stopCombatMove();
        void startAttackIfReady(const MWWorld::Ptr& actor, CharacterController& characterController,
            const ESM::Weapon* weapon, bool distantCombat);
        void updateAttack(CharacterController& characterController);
        void stopAttack();

        void startFleeing();
        void stopFleeing();
        bool isFleeing();
        bool hasTacticalMovement() const;
        bool isCombatMoving() const;
        bool suppressesAttack() const;
    };

    /// \brief Causes the actor to fight another actor
    class AiCombat final : public TypedAiPackage<AiCombat>
    {
        public:
            ///Constructor
            /** \param actor Actor to fight **/
            explicit AiCombat(const MWWorld::Ptr& actor);

            explicit AiCombat (const ESM::AiSequence::AiCombat* combat);

            void init();

            bool execute (const MWWorld::Ptr& actor, CharacterController& characterController, AiState& state, float duration) override;

            static constexpr AiPackageTypeId getTypeId() { return AiPackageTypeId::Combat; }

            static constexpr Options makeDefaultOptions()
            {
                AiPackage::Options options;
                options.mPriority = 1;
                options.mCanCancel = false;
                options.mShouldCancelPreviousAi = false;
                return options;
            }

            ///Returns target ID
            MWWorld::Ptr getTarget() const override;

            void writeState(ESM::AiSequence::AiSequence &sequence) const override;

        private:
            // X024: how long this package has been unable to reach its target
            // because the target lives in another coordinate space, and how long it
            // has been running without ever resolving the target at all.
            //
            // These used to live in AiCombatStorage, which is part of AiState and is
            // rebuilt from scratch whenever a client takes over an actor. With many
            // players in one area the actor authority changes often, every hand-off
            // reset the counters, and the NPC could stay frozen in a combat stance
            // indefinitely. Keeping them in the package means they survive both the
            // hand-off and the save/load round trip.
            float mForeignSpaceElapsed = 0.f;
            float mUnresolvedTargetElapsed = 0.f;

            /// Returns true if combat should end
            bool attack(const MWWorld::Ptr& actor, const MWWorld::Ptr& target, AiCombatStorage& storage, CharacterController& characterController);

            void updateLOS(const MWWorld::Ptr& actor, const MWWorld::Ptr& target, float duration, AiCombatStorage& storage);
            bool updateStealthSearch(const MWWorld::Ptr& actor, const MWWorld::Ptr& target, float duration,
                AiCombatStorage& storage);
            void updateFormationDestination(const MWWorld::Ptr& actor, const MWWorld::Ptr& target, float duration,
                AiCombatStorage& storage, bool ranged);
            bool updateThreatFlee(const MWWorld::Ptr& actor, const MWWorld::Ptr& target, AiCombatStorage& storage);

            void updateFleeing(const MWWorld::Ptr& actor, const MWWorld::Ptr& target, float duration, AiCombatStorage& storage);
            void updateTacticalMovement(const MWWorld::Ptr& actor, const MWWorld::Ptr& target, float duration,
                AiCombatStorage& storage, CharacterController& characterController);
            void clearTacticalMovement(const MWWorld::Ptr& actor, AiCombatStorage& storage);
            void handleCellTransition(const MWWorld::Ptr& actor, AiCombatStorage& storage);
            void queueDoorTransition(const MWWorld::Ptr& actor, const MWWorld::Ptr& door, AiCombatStorage& storage);
            bool updateCrossCellPursuit(const MWWorld::Ptr& actor, const MWWorld::Ptr& target, float duration,
                AiCombatStorage& storage, CharacterController& characterController);
            bool tryDrinkHealingPotion(const MWWorld::Ptr& actor, AiCombatStorage& storage);
            bool updatePursuitLeash(const MWWorld::Ptr& actor, float duration, AiCombatStorage& storage);

            /// Transfer desired movement (from AiCombatStorage) to Actor
            /// If applyTranslation is false, only the facing is applied and the
            /// translation produced by pathTo() in this same frame is preserved.
            void updateActorsMovement(const MWWorld::Ptr& actor, float duration, AiCombatStorage& storage,
                bool applyTranslation = true);
            void rotateActorOnAxis(const MWWorld::Ptr& actor, int axis, 
                MWMechanics::Movement& actorMovementSettings, AiCombatStorage& storage);
    };
    
    
}

#endif
