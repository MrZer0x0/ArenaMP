#include "obstacle.hpp"

#include <cmath>

#include <osg/Math>

#include <components/sceneutil/positionattitudetransform.hpp>

#include "../mwworld/class.hpp"
#include "../mwworld/cellstore.hpp"

#include "movement.hpp"

namespace MWMechanics
{
    // Short detection and a turn-in-place response avoid rapidly switching
    // between walk/strafe/backpedal animation groups when an actor is blocked.
    static const float DIST_SAME_SPOT = 0.35f;
    static const float DURATION_SAME_SPOT = 0.65f;
    static const float DURATION_TO_TURN_AWAY = 0.55f;
    static const float DURATION_RETREAT = 0.8f;

    bool proximityToDoor(const MWWorld::Ptr& actor, float minDist)
    {
        if(getNearbyDoor(actor, minDist).isEmpty())
            return false;
        else
            return true;
    }

    const MWWorld::Ptr getNearbyDoor(const MWWorld::Ptr& actor, float minDist)
    {
        MWWorld::CellStore *cell = actor.getCell();

        // Check all the doors in this cell
        const MWWorld::CellRefList<ESM::Door>& doors = cell->getReadOnlyDoors();
        osg::Vec3f pos(actor.getRefData().getPosition().asVec3());
        pos.z() = 0;

        osg::Vec3f actorDir = (actor.getRefData().getBaseNode()->getAttitude() * osg::Vec3f(0,1,0));

        for (const auto& ref : doors.mList)
        {
            osg::Vec3f doorPos(ref.mData.getPosition().asVec3());

            // FIXME: cast
            const MWWorld::Ptr doorPtr = MWWorld::Ptr(&const_cast<MWWorld::LiveCellRef<ESM::Door> &>(ref), actor.getCell());

            const auto doorState = doorPtr.getClass().getDoorState(doorPtr);
            float doorRot = ref.mData.getPosition().rot[2] - doorPtr.getCellRef().getPosition().rot[2];

            if (doorState != MWWorld::DoorState::Idle || doorRot != 0)
                continue; // the door is already opened/opening

            doorPos.z() = 0;

            float angle = std::acos(actorDir * (doorPos - pos) / (actorDir.length() * (doorPos - pos).length()));

            // Allow 60 degrees angle between actor and door
            if (angle < -osg::PI / 3 || angle > osg::PI / 3)
                continue;

            // Door is not close enough
            if ((pos - doorPos).length2() > minDist*minDist)
                continue;

            return doorPtr; // found, stop searching
        }

        return MWWorld::Ptr(); // none found
    }

    ObstacleCheck::ObstacleCheck()
      : mWalkState(WalkState::Initial)
      , mStateDuration(0.f)
      , mEvasionAngle(0.f)
      , mPathRebuildPending(false)
    {
    }

    void ObstacleCheck::clear()
    {
        mWalkState = WalkState::Initial;
        mStateDuration = 0.f;
        mPathRebuildPending = false;
    }

    bool ObstacleCheck::isEvading() const
    {
        return mWalkState == WalkState::TurnAway || mWalkState == WalkState::Retreat;
    }

    /*
     * input   - actor, duration (time since last check)
     * output  - true if evasive action needs to be taken
     *
     * Walking state transitions (player greeting check not shown):
     *
     * Initial -> Norm <-> CheckStuck -> TurnAway -> Retreat -> Norm
     *
     * The actor first stops, turns exactly away from the obstacle, walks a
     * short distance in that opposite direction, and only then rebuilds path.
     *
     */
    void ObstacleCheck::update(const MWWorld::Ptr& actor, const osg::Vec3f& destination, float duration)
    {
        const osg::Vec3f position = actor.getRefData().getPosition().asVec3();

        if (mWalkState == WalkState::Initial)
        {
            mWalkState = WalkState::Norm;
            mStateDuration = 0.f;
            mPrev = position;
            mInitialDistance = (destination - position).length();
            return;
        }

        if (mWalkState == WalkState::TurnAway)
        {
            mStateDuration += duration;
            if (mStateDuration >= DURATION_TO_TURN_AWAY)
            {
                mWalkState = WalkState::Retreat;
                mStateDuration = 0.f;
                mPrev = position;
            }
            return;
        }

        if (mWalkState == WalkState::Retreat)
        {
            mStateDuration += duration;
            if (mStateDuration >= DURATION_RETREAT)
            {
                mWalkState = WalkState::Norm;
                mStateDuration = 0.f;
                mPrev = position;
                mInitialDistance = (destination - position).length();
                mPathRebuildPending = true;
            }
            return;
        }

        const float distSameSpot = DIST_SAME_SPOT * actor.getClass().getCurrentSpeed(actor) * duration;
        const float prevDistance = (destination - mPrev).length();
        const float currentDistance = (destination - position).length();
        const float movedDistance = prevDistance - currentDistance;
        const float movedFromInitialDistance = mInitialDistance - currentDistance;
        mPrev = position;

        if (movedDistance >= distSameSpot && movedFromInitialDistance >= distSameSpot)
        {
            mWalkState = WalkState::Norm;
            mStateDuration = 0.f;
            mInitialDistance = currentDistance;
            return;
        }

        if (mWalkState == WalkState::Norm)
        {
            mWalkState = WalkState::CheckStuck;
            mStateDuration = duration;
            mInitialDistance = currentDistance;
            return;
        }

        mStateDuration += duration;
        if (mStateDuration < DURATION_SAME_SPOT)
            return;

        // Stop first, then use one stable 180-degree turn. No random left/right
        // oscillation is allowed here: it was the main source of visible NPC
        // twitching when several colliders met in a narrow space.
        mWalkState = WalkState::TurnAway;
        mStateDuration = 0.f;
        mPrev = position;
        mPathRebuildPending = false;
        chooseEvasionAngle(actor);
    }

    void ObstacleCheck::takeEvasiveAction(MWMechanics::Movement& actorMovement) const
    {
        actorMovement.mPosition[0] = 0.f;
        actorMovement.mPosition[1] = mWalkState == WalkState::Retreat ? 1.f : 0.f;
    }

    float ObstacleCheck::getEvasionAngle() const
    {
        return mEvasionAngle;
    }

    bool ObstacleCheck::consumePathRebuildRequest()
    {
        const bool result = mPathRebuildPending;
        mPathRebuildPending = false;
        return result;
    }

    void ObstacleCheck::chooseEvasionAngle(const MWWorld::Ptr& actor)
    {
        mEvasionAngle = actor.getRefData().getPosition().rot[2] + osg::PI;
    }

}
