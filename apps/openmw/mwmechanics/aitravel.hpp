#ifndef GAME_MWMECHANICS_AITRAVEL_H
#define GAME_MWMECHANICS_AITRAVEL_H

#include "typedaipackage.hpp"

#include <cstddef>
#include <string>
#include <vector>

#include <components/esm/cellid.hpp>
#include <components/esm/defs.hpp>
#include <components/esm/loadcell.hpp>

namespace ESM
{
namespace AiSequence
{
    struct AiTravel;
}
}

namespace MWMechanics
{
    struct AiInternalTravel;

    /// \brief Causes the AI to travel to the specified point
    class AiTravel : public TypedAiPackage<AiTravel>
    {
        public:
            AiTravel(float x, float y, float z, AiTravel* derived);

            AiTravel(float x, float y, float z, AiInternalTravel* derived);

            AiTravel(float x, float y, float z);

            explicit AiTravel(const ESM::AiSequence::AiTravel* travel);

            /// Simulates the passing of time
            void fastForward(const MWWorld::Ptr& actor, AiState& state) override;

            void writeState(ESM::AiSequence::AiSequence &sequence) const override;

            bool execute (const MWWorld::Ptr& actor, CharacterController& characterController, AiState& state, float duration) override;

            static constexpr AiPackageTypeId getTypeId() { return AiPackageTypeId::Travel; }

            static constexpr Options makeDefaultOptions()
            {
                AiPackage::Options options;
                options.mUseVariableSpeed = true;
                options.mAlwaysActive = true;
                return options;
            }

            osg::Vec3f getDestination() const override { return osg::Vec3f(mX, mY, mZ); }

        private:
            const float mX;
            const float mY;
            const float mZ;

            const bool mHidden;
    };

    struct AiReturnHomeState
    {
        struct DoorBreadcrumb
        {
            ESM::Cell mFromCell;
            ESM::Position mFromPosition;
            ESM::Cell mToCell;
            ESM::Position mToPosition;
        };

        ESM::Cell mHomeCell;
        ESM::Position mHomePosition;
        std::vector<DoorBreadcrumb> mDoorBreadcrumbs;
    };

    struct AiInternalTravel final : public AiTravel
    {
        using DoorBreadcrumb = AiReturnHomeState::DoorBreadcrumb;

        AiInternalTravel(const ESM::Position& homePosition, const ESM::CellId& homeCellId, std::string homeCellName);
        AiInternalTravel(float x, float y, float z);

        explicit AiInternalTravel(const ESM::AiSequence::AiTravel* travel);

        static constexpr AiPackageTypeId getTypeId() { return AiPackageTypeId::InternalTravel; }

        bool execute(const MWWorld::Ptr& actor, CharacterController& characterController, AiState& state, float duration) override;
        void recordDoorTransition(const ESM::Cell& fromCell, const ESM::Position& fromPosition,
            const ESM::Cell& toCell, const ESM::Position& toPosition);
        std::size_t getDoorTransitionCount() const { return mDoorBreadcrumbs.size(); }
        bool exportState(AiReturnHomeState& state) const;
        void restoreState(const AiReturnHomeState& state);

        std::unique_ptr<AiPackage> clone() const override;

    private:
        ESM::CellId mHomeCellId;
        std::string mHomeCellName;
        ESM::Position mHomePosition;
        bool mHasHomeCell = false;
        bool mHomeTeleportQueued = false;
        float mHomeRecoveryTimer = 0.f;
        std::vector<DoorBreadcrumb> mDoorBreadcrumbs;
    };
}

#endif
