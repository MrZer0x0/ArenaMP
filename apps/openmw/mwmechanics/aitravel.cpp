#include "aitravel.hpp"

#include <algorithm>
#include <utility>

#include <components/esm/aisequence.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/actionteleport.hpp"

#include "movement.hpp"
#include "creaturestats.hpp"

namespace
{

bool isWithinMaxRange(const osg::Vec3f& pos1, const osg::Vec3f& pos2)
{
    // Maximum travel distance for vanilla compatibility.
    // Was likely meant to prevent NPCs walking into non-loaded exterior cells, but for some reason is used in interior cells as well.
    // We can make this configurable at some point, but the default *must* be the below value. Anything else will break shoddily-written content (*cough* MW *cough*) in bizarre ways.
    return (pos1 - pos2).length2() <= 7168*7168;
}

}

namespace MWMechanics
{
    AiTravel::AiTravel(float x, float y, float z, AiTravel*)
        : mX(x), mY(y), mZ(z), mHidden(false)
    {
    }

    AiTravel::AiTravel(float x, float y, float z, AiInternalTravel* derived)
        : TypedAiPackage<AiTravel>(derived), mX(x), mY(y), mZ(z), mHidden(true)
    {
    }

    AiTravel::AiTravel(float x, float y, float z)
        : AiTravel(x, y, z, this)
    {
    }

    AiTravel::AiTravel(const ESM::AiSequence::AiTravel *travel)
        : mX(travel->mData.mX), mY(travel->mData.mY), mZ(travel->mData.mZ), mHidden(false)
    {
        // Hidden ESM::AiSequence::AiTravel package should be converted into MWMechanics::AiInternalTravel type
        assert(!travel->mHidden);
    }

    bool AiTravel::execute (const MWWorld::Ptr& actor, CharacterController& characterController, AiState& state, float duration)
    {
        MWBase::MechanicsManager* mechMgr = MWBase::Environment::get().getMechanicsManager();
        auto& stats = actor.getClass().getCreatureStats(actor);

        if (!stats.getMovementFlag(CreatureStats::Flag_ForceJump) && !stats.getMovementFlag(CreatureStats::Flag_ForceSneak)
                && (mechMgr->isTurningToPlayer(actor) || mechMgr->getGreetingState(actor) == Greet_InProgress))
            return false;

        const osg::Vec3f actorPos(actor.getRefData().getPosition().asVec3());
        const osg::Vec3f targetPos(mX, mY, mZ);

        stats.setMovementFlag(CreatureStats::Flag_Run, false);
        stats.setDrawState(DrawState_Nothing);

        // Note: we should cancel internal "return after combat" package, if original location is too far away
        if (!isWithinMaxRange(targetPos, actorPos))
            return mHidden;

        // Unfortunately, with vanilla assets destination is sometimes blocked by other actor.
        // If we got close to target, check for actors nearby. If they are, finish AI package.
        int destinationTolerance = 64;
        if (distance(actorPos, targetPos) <= destinationTolerance)
        {
            std::vector<MWWorld::Ptr> targetActors;
            std::pair<MWWorld::Ptr, osg::Vec3f> result = MWBase::Environment::get().getWorld()->getHitContact(actor, destinationTolerance, targetActors);

            if (!result.first.isEmpty())
            {
                actor.getClass().getMovementSettings(actor).mPosition[1] = 0;
                return true;
            }
        }

        if (pathTo(actor, targetPos, duration))
        {
            actor.getClass().getMovementSettings(actor).mPosition[1] = 0;
            return true;
        }
        return false;
    }

    void AiTravel::fastForward(const MWWorld::Ptr& actor, AiState& state)
    {
        if (!isWithinMaxRange(osg::Vec3f(mX, mY, mZ), actor.getRefData().getPosition().asVec3()))
            return;
        // does not do any validation on the travel target (whether it's in air, inside collision geometry, etc),
        // that is the user's responsibility
        MWBase::Environment::get().getWorld()->moveObject(actor, mX, mY, mZ);
        actor.getClass().adjustPosition(actor, false);
        reset();
    }

    void AiTravel::writeState(ESM::AiSequence::AiSequence &sequence) const
    {
        std::unique_ptr<ESM::AiSequence::AiTravel> travel(new ESM::AiSequence::AiTravel());
        travel->mData.mX = mX;
        travel->mData.mY = mY;
        travel->mData.mZ = mZ;
        travel->mHidden = mHidden;

        ESM::AiSequence::AiPackageContainer package;
        package.mType = ESM::AiSequence::Ai_Travel;
        package.mPackage = travel.release();
        sequence.mPackages.push_back(package);
    }

    AiInternalTravel::AiInternalTravel(const ESM::Position& homePosition, const ESM::CellId& homeCellId, std::string homeCellName)
        : AiTravel(homePosition.pos[0], homePosition.pos[1], homePosition.pos[2], this)
        , mHomeCellId(homeCellId)
        , mHomeCellName(std::move(homeCellName))
        , mHomePosition(homePosition)
        , mHasHomeCell(true)
    {
    }

    AiInternalTravel::AiInternalTravel(float x, float y, float z)
        : AiTravel(x, y, z, this)
    {
    }

    AiInternalTravel::AiInternalTravel(const ESM::AiSequence::AiTravel* travel)
        : AiTravel(travel->mData.mX, travel->mData.mY, travel->mData.mZ, this)
    {
        // The legacy save format contains only XYZ for hidden travel packages.
        // Do not infer a home cell from the cell where the save was loaded: that
        // could be the pursuit destination and would corrupt return-home behavior.
    }

    void AiInternalTravel::recordDoorTransition(const ESM::Cell& fromCell, const ESM::Position& fromPosition,
        const ESM::Cell& toCell, const ESM::Position& toPosition)
    {
        if (!mHasHomeCell)
            return;

        DoorBreadcrumb breadcrumb;
        breadcrumb.mFromCell = fromCell;
        breadcrumb.mFromPosition = fromPosition;
        breadcrumb.mToCell = toCell;
        breadcrumb.mToPosition = toPosition;

        // A reverse traversal cancels the last breadcrumb instead of growing a
        // A->B->A loop. This also makes the state compact when an NPC retreats.
        if (!mDoorBreadcrumbs.empty())
        {
            const DoorBreadcrumb& last = mDoorBreadcrumbs.back();
            if (last.mFromCell.getCellId() == toCell.getCellId()
                && last.mToCell.getCellId() == fromCell.getCellId())
            {
                mDoorBreadcrumbs.pop_back();
                return;
            }
        }

        if (!mDoorBreadcrumbs.empty()
            && mDoorBreadcrumbs.back().mFromCell.getCellId() == breadcrumb.mFromCell.getCellId()
            && mDoorBreadcrumbs.back().mToCell.getCellId() == breadcrumb.mToCell.getCellId())
        {
            mDoorBreadcrumbs.back() = std::move(breadcrumb);
            return;
        }

        constexpr std::size_t maxBreadcrumbs = 12;
        if (mDoorBreadcrumbs.size() >= maxBreadcrumbs)
            mDoorBreadcrumbs.erase(mDoorBreadcrumbs.begin());
        mDoorBreadcrumbs.push_back(std::move(breadcrumb));
    }

    bool AiInternalTravel::exportState(AiReturnHomeState& state) const
    {
        if (!mHasHomeCell)
            return false;

        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::CellStore* homeStore = nullptr;
        if (mHomeCellId.mPaged)
            homeStore = world->getExterior(mHomeCellId.mIndex.mX, mHomeCellId.mIndex.mY);
        else if (!mHomeCellName.empty())
            homeStore = world->getInterior(mHomeCellName);
        if (homeStore == nullptr || homeStore->getCell() == nullptr)
            return false;

        state.mHomeCell = *homeStore->getCell();
        state.mHomePosition = mHomePosition;
        state.mDoorBreadcrumbs = mDoorBreadcrumbs;
        return true;
    }

    void AiInternalTravel::restoreState(const AiReturnHomeState& state)
    {
        mHomeCellId = state.mHomeCell.getCellId();
        mHomeCellName = state.mHomeCell.isExterior() ? std::string() : state.mHomeCell.mName;
        mHomePosition = state.mHomePosition;
        mHasHomeCell = true;
        mHomeTeleportQueued = false;
        mHomeRecoveryTimer = 0.f;
        mDoorBreadcrumbs = state.mDoorBreadcrumbs;
        if (mDoorBreadcrumbs.size() > 12)
            mDoorBreadcrumbs.erase(mDoorBreadcrumbs.begin(), mDoorBreadcrumbs.end() - 12);
    }

    bool AiInternalTravel::execute(const MWWorld::Ptr& actor, CharacterController& characterController, AiState& state, float duration)
    {
        if (actor.getCell() == nullptr || actor.getCell()->getCell() == nullptr)
            return AiTravel::execute(actor, characterController, state, duration);

        // X024: a hidden return-home package with no recorded home cell (an old
        // save, or a package rebuilt after an authority hand-off) used to fall
        // straight through to AiTravel. AiTravel treats an out-of-range hidden
        // destination as *completed*, so the package was silently dropped and the
        // NPC simply stayed wherever the fight had ended. Treat the stored XYZ as
        // an exterior home point instead and recover the same way as below.
        if (!mHasHomeCell)
        {
            if (!actor.getCell()->isExterior())
                return AiTravel::execute(actor, characterController, state, duration);

            if (mHomeTeleportQueued)
                return false;

            const osg::Vec3f actorPosition = actor.getRefData().getPosition().asVec3();
            const osg::Vec3f fallbackHome = getDestination();

            if (isWithinMaxRange(fallbackHome, actorPosition))
            {
                if (AiTravel::execute(actor, characterController, state, duration))
                    return true;

                mHomeRecoveryTimer += std::max(0.f, duration);
                if (mHomeRecoveryTimer < 8.f)
                    return false;
            }

            // Out of walking range, or walking has made no progress for a while.
            ESM::Position fallbackPosition = actor.getRefData().getPosition();
            fallbackPosition.pos[0] = fallbackHome.x();
            fallbackPosition.pos[1] = fallbackHome.y();
            fallbackPosition.pos[2] = fallbackHome.z();

            actor.getClass().getMovementSettings(actor).mPosition[1] = 0.f;
            MWWorld::ActionTeleport::queueDelayedTeleport(
                actor, std::string(), fallbackPosition, 0.f, -1, true);
            mHomeTeleportQueued = true;
            return false;
        }

        const ESM::Cell* currentCell = actor.getCell()->getCell();
        if (currentCell->getCellId() == mHomeCellId)
        {
            mHomeRecoveryTimer = 0.f;
            return AiTravel::execute(actor, characterController, state, duration);
        }

        // We deliberately do not run a full AI simulation through unloaded
        // interiors. Exterior cells, however, share one coordinate space and an
        // NPC that crossed an ordinary grid boundary during combat can simply
        // walk back towards its real home point.
        if (mHomeTeleportQueued)
            return false;

        const bool homeIsExterior = mHomeCellName.empty();
        if (currentCell->isExterior() && homeIsExterior)
        {
            const osg::Vec3f actorPosition = actor.getRefData().getPosition().asVec3();
            const osg::Vec3f homePosition = mHomePosition.asVec3();
            const bool canWalkHome = isWithinMaxRange(homePosition, actorPosition);

            // AiTravel intentionally treats an out-of-range hidden package as
            // "complete". Do not call it in that case: completing here would
            // strand the NPC in the wrong exterior cell.
            if (canWalkHome)
            {
                const bool reached = AiTravel::execute(actor, characterController, state, duration);
                if (reached)
                    return true;
            }
            else
                actor.getClass().getMovementSettings(actor).mPosition[1] = 0.f;

            mHomeRecoveryTimer += std::max(0.f, duration);

            // Authority hand-offs or partially unloaded navmeshes can leave the
            // hidden return package with no useful path forever. Give a nearby
            // actor a short visible chance to walk home; if it is already beyond
            // the safe vanilla travel radius, recover sooner.
            const float recoveryDelay = canWalkHome ? 8.f : 1.5f;
            if (mHomeRecoveryTimer >= recoveryDelay)
            {
                actor.getClass().getMovementSettings(actor).mPosition[1] = 0.f;
                MWWorld::ActionTeleport::queueDelayedTeleport(
                    actor, mHomeCellName, mHomePosition, 0.f, -1, true);
                mHomeTeleportQueued = true;
            }
            return false;
        }

        const ESM::CellId currentCellId = currentCell->getCellId();
        const DoorBreadcrumb* breadcrumb = nullptr;
        for (auto it = mDoorBreadcrumbs.rbegin(); it != mDoorBreadcrumbs.rend(); ++it)
        {
            if (it->mToCell.getCellId() == currentCellId)
            {
                breadcrumb = &*it;
                break;
            }
        }

        if (!breadcrumb)
        {
            // X022: missing breadcrumbs can happen after authority changes or a
            // legacy mid-combat state. Waiting forever is worse than a controlled
            // recovery. Keep the actor still for a brief grace period, then move
            // it back to its persistent home cell.
            actor.getClass().getMovementSettings(actor).mPosition[1] = 0.f;
            mHomeRecoveryTimer += std::max(0.f, duration);
            if (mHomeRecoveryTimer >= 1.5f)
            {
                MWWorld::ActionTeleport::queueDelayedTeleport(
                    actor, mHomeCellName, mHomePosition, 0.f, -1, true);
                mHomeTeleportQueued = true;
            }
            return false;
        }

        mHomeRecoveryTimer = 0.f;

        auto& stats = actor.getClass().getCreatureStats(actor);
        stats.setMovementFlag(CreatureStats::Flag_Run, false);
        stats.setDrawState(DrawState_Nothing);

        const osg::Vec3f doorPoint = breadcrumb->mToPosition.asVec3();
        const float distToDoor = distance(actor.getRefData().getPosition().asVec3(), doorPoint);
        if (distToDoor > 96.f && !pathTo(actor, doorPoint, duration, 72.f))
            return false;

        actor.getClass().getMovementSettings(actor).mPosition[1] = 0.f;
        const std::string previousCellName = breadcrumb->mFromCell.isExterior()
            ? std::string() : breadcrumb->mFromCell.mName;
        const ESM::Position previousPosition = breadcrumb->mFromPosition;

        // Consume exactly one door at a time. The remaining breadcrumb stack is
        // carried in the next AI heartbeat, so another authority can resume the
        // return route in the following cell.
        for (auto it = mDoorBreadcrumbs.end(); it != mDoorBreadcrumbs.begin();)
        {
            --it;
            if (&*it == breadcrumb)
            {
                mDoorBreadcrumbs.erase(it);
                break;
            }
        }
        MWWorld::ActionTeleport::queueDelayedTeleport(
            actor, previousCellName, previousPosition, 0.f, -1, true);
        return false;
    }

    std::unique_ptr<AiPackage> AiInternalTravel::clone() const
    {
        return std::make_unique<AiInternalTravel>(*this);
    }
}

