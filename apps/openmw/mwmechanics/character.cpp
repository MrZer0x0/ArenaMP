/*
 * OpenMW - The completely unofficial reimplementation of Morrowind
 *
 * This file (character.cpp) is part of the OpenMW package.
 *
 * OpenMW is distributed as free software: you can redistribute it
 * and/or modify it under the terms of the GNU General Public License
 * version 3, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 3 along with this program. If not, see
 * https://www.gnu.org/licenses/ .
 */

#include "character.hpp"
#include "nativeanimation.hpp"
#include "../mwrender/npcanimation.hpp"
#include "activeguard.hpp"
#include "arenacombatbalance.hpp"

#include <iostream>

#include <components/misc/mathutil.hpp>
#include <components/misc/stringops.hpp>
#include <components/misc/rng.hpp>

#include <components/esm/loadclas.hpp>
#include <components/esm/loadench.hpp>

#include <components/settings/settings.hpp>

#include <components/sceneutil/positionattitudetransform.hpp>

/*
    Start of tes3mp addition

    Include additional headers for multiplayer purposes
*/
#include <components/openmw-mp/TimedLog.hpp>
#include "../mwmp/Main.hpp"
#include "../mwmp/LocalPlayer.hpp"
#include "../mwmp/LocalActor.hpp"
#include "../mwmp/PlayerList.hpp"
#include "../mwmp/DedicatedPlayer.hpp"
#include "../mwmp/CellController.hpp"
#include "../mwmp/MechanicsHelper.hpp"
#include "../mwmp/InteractionAnimationSync.hpp"
/*
    End of tes3mp addition
*/

#include "../mwrender/animation.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwbase/windowmanager.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/player.hpp"

#include "aicombataction.hpp"
#include "movement.hpp"
#include "npcstats.hpp"
#include "creaturestats.hpp"
#include "arenacombatlimits.hpp"
#include "security.hpp"
#include "actorutil.hpp"
#include "spellcasting.hpp"
#include "spellutil.hpp"
#include "combatsoundmanager.hpp"

namespace
{

constexpr float sDynamicMovementTransitionSeconds = 0.12f;

// ArenaMP: Stealth-specialized classes (localized as "Бродяга" in the
// Arena class UI) do not pay the passive sneak fatigue upkeep.  Compare the
// specialization enum rather than any localized class/specialization text.
bool hasStealthSpecialization(const MWWorld::Ptr& actor, MWBase::World* world)
{
    if (!actor.getClass().isNpc())
        return false;

    const ESM::NPC* npc = actor.get<ESM::NPC>()->mBase;
    const ESM::Class* klass = world->getStore().get<ESM::Class>().search(npc->mClass);
    return klass && klass->mData.mSpecialization == ESM::Class::Stealth;
}

std::string getBestAttack (const ESM::Weapon* weapon)
{
    int slash = (weapon->mData.mSlash[0] + weapon->mData.mSlash[1])/2;
    int chop = (weapon->mData.mChop[0] + weapon->mData.mChop[1])/2;
    int thrust = (weapon->mData.mThrust[0] + weapon->mData.mThrust[1])/2;
    if (slash == chop && slash == thrust)
        return "slash";
    else if (thrust >= chop && thrust >= slash)
        return "thrust";
    else if (slash >= chop && slash >= thrust)
        return "slash";
    else
        return "chop";
}

// Converts a movement Run state to its equivalent Walk state.
MWMechanics::CharacterState runStateToWalkState (MWMechanics::CharacterState state)
{
    using namespace MWMechanics;
    CharacterState ret = state;
    switch (state)
    {
        case CharState_RunForward:
            ret = CharState_WalkForward;
            break;
        case CharState_RunBack:
            ret = CharState_WalkBack;
            break;
        case CharState_RunLeft:
            ret = CharState_WalkLeft;
            break;
        case CharState_RunRight:
            ret = CharState_WalkRight;
            break;
        case CharState_SwimRunForward:
            ret = CharState_SwimWalkForward;
            break;
        case CharState_SwimRunBack:
            ret = CharState_SwimWalkBack;
            break;
        case CharState_SwimRunLeft:
            ret = CharState_SwimWalkLeft;
            break;
        case CharState_SwimRunRight:
            ret = CharState_SwimWalkRight;
            break;
        default:
            break;
    }
    return ret;
}

float getFallDamage(const MWWorld::Ptr& ptr, float fallHeight)
{
    MWBase::World *world = MWBase::Environment::get().getWorld();
    const MWWorld::Store<ESM::GameSetting> &store = world->getStore().get<ESM::GameSetting>();

    const float fallDistanceMin = store.find("fFallDamageDistanceMin")->mValue.getFloat();

    if (fallHeight >= fallDistanceMin)
    {
        const float acrobaticsSkill = static_cast<float>(ptr.getClass().getSkill(ptr, ESM::Skill::Acrobatics));
        const float jumpSpellBonus = ptr.getClass().getCreatureStats(ptr).getMagicEffects().get(ESM::MagicEffect::Jump).getMagnitude();
        const float fallAcroBase = store.find("fFallAcroBase")->mValue.getFloat();
        const float fallAcroMult = store.find("fFallAcroMult")->mValue.getFloat();
        const float fallDistanceBase = store.find("fFallDistanceBase")->mValue.getFloat();
        const float fallDistanceMult = store.find("fFallDistanceMult")->mValue.getFloat();

        float x = fallHeight - fallDistanceMin;
        x -= (1.5f * acrobaticsSkill) + jumpSpellBonus;
        x = std::max(0.0f, x);

        float a = fallAcroBase + fallAcroMult * (100 - acrobaticsSkill);
        x = fallDistanceBase + fallDistanceMult * x;
        x *= a;

        return x;
    }
    return 0.f;
}

}

namespace MWMechanics
{

struct StateInfo {
    CharacterState state;
    const char groupname[32];
};

static const StateInfo sMovementList[] = {
    { CharState_WalkForward, "walkforward" },
    { CharState_WalkBack, "walkback" },
    { CharState_WalkLeft, "walkleft" },
    { CharState_WalkRight, "walkright" },

    { CharState_SwimWalkForward, "swimwalkforward" },
    { CharState_SwimWalkBack, "swimwalkback" },
    { CharState_SwimWalkLeft, "swimwalkleft" },
    { CharState_SwimWalkRight, "swimwalkright" },

    { CharState_RunForward, "runforward" },
    { CharState_RunBack, "runback" },
    { CharState_RunLeft, "runleft" },
    { CharState_RunRight, "runright" },

    { CharState_SwimRunForward, "swimrunforward" },
    { CharState_SwimRunBack, "swimrunback" },
    { CharState_SwimRunLeft, "swimrunleft" },
    { CharState_SwimRunRight, "swimrunright" },

    { CharState_SneakForward, "sneakforward" },
    { CharState_SneakBack, "sneakback" },
    { CharState_SneakLeft, "sneakleft" },
    { CharState_SneakRight, "sneakright" },

    { CharState_Jump, "jump" },

    { CharState_TurnLeft, "turnleft" },
    { CharState_TurnRight, "turnright" },
    { CharState_SwimTurnLeft, "swimturnleft" },
    { CharState_SwimTurnRight, "swimturnright" },
};
static const StateInfo *sMovementListEnd = &sMovementList[sizeof(sMovementList)/sizeof(sMovementList[0])];


class FindCharState {
    CharacterState state;

public:
    FindCharState(CharacterState _state) : state(_state) { }

    bool operator()(const StateInfo &info) const
    { return info.state == state; }
};


std::string CharacterController::chooseRandomGroup (const std::string& prefix, int* num) const
{
    int numAnims=0;
    while (mAnimation->hasAnimation(prefix + std::to_string(numAnims+1)))
        ++numAnims;

    int roll = Misc::Rng::rollDice(numAnims) + 1; // [1, numAnims]
    if (num)
        *num = roll;
    return prefix + std::to_string(roll);
}

void CharacterController::refreshHitRecoilAnims(CharacterState& idle)
{
    bool recovery = mPtr.getClass().getCreatureStats(mPtr).getHitRecovery();
    bool knockdown = mPtr.getClass().getCreatureStats(mPtr).getKnockedDown();
    bool block = mPtr.getClass().getCreatureStats(mPtr).getBlock();
    bool isSwimming = MWBase::Environment::get().getWorld()->isSwimming(mPtr);
    if(mHitState == CharState_None)
    {
        if ((mPtr.getClass().getCreatureStats(mPtr).getFatigue().getCurrent() < 0
                || mPtr.getClass().getCreatureStats(mPtr).getFatigue().getBase() == 0))
        {
            mTimeUntilWake = Misc::Rng::rollClosedProbability() * 2 + 1; // Wake up after 1 to 3 seconds
            if (isSwimming && mAnimation->hasAnimation("swimknockout"))
            {
                mHitState = CharState_SwimKnockOut;
                mCurrentHit = "swimknockout";
                mAnimation->play(mCurrentHit, Priority_Knockdown, MWRender::Animation::BlendMask_All, false, 1, "start", "stop", 0.0f, ~0ul);
            }
            else if (!isSwimming && mAnimation->hasAnimation("knockout"))
            {
                mHitState = CharState_KnockOut;
                mCurrentHit = "knockout";
                mAnimation->play(mCurrentHit, Priority_Knockdown, MWRender::Animation::BlendMask_All, false, 1, "start", "stop", 0.0f, ~0ul);
            }
            else
            {
                // Knockout animations are missing. Fall back to idle animation, so target actor still can be killed via HtH.
                mCurrentHit.erase();
            }

            mPtr.getClass().getCreatureStats(mPtr).setKnockedDown(true);
        }
        else if (knockdown)
        {
            if (isSwimming && mAnimation->hasAnimation("swimknockdown"))
            {
                mHitState = CharState_SwimKnockDown;
                mCurrentHit = "swimknockdown";
                mAnimation->play(mCurrentHit, Priority_Knockdown, MWRender::Animation::BlendMask_All, true, 1, "start", "stop", 0.0f, 0);
            }
            else if (!isSwimming && mAnimation->hasAnimation("knockdown"))
            {
                mHitState = CharState_KnockDown;
                mCurrentHit = "knockdown";
                mAnimation->play(mCurrentHit, Priority_Knockdown, MWRender::Animation::BlendMask_All, true, 1, "start", "stop", 0.0f, 0);
            }
            else
            {
                // Knockdown animation is missing. Cancel knockdown state.
                mPtr.getClass().getCreatureStats(mPtr).setKnockedDown(false);
            }
        }
        else if (recovery)
        {
            std::string anim = chooseRandomGroup("swimhit");
            if (isSwimming && mAnimation->hasAnimation(anim))
            {
                mHitState = CharState_SwimHit;
                mCurrentHit = anim;
                mAnimation->play(mCurrentHit, Priority_Hit, MWRender::Animation::BlendMask_All, true, 1, "start", "stop", 0.0f, 0);
            }
            else
            {
                anim = chooseRandomGroup("hit");
                if (mAnimation->hasAnimation(anim))
                {
                    mHitState = CharState_Hit;
                    mCurrentHit = anim;
                    mAnimation->play(mCurrentHit, Priority_Hit, MWRender::Animation::BlendMask_All, true, 1, "start", "stop", 0.0f, 0);
                }
            }
        }
        else if (block && mAnimation->hasAnimation("shield"))
        {
            mHitState = CharState_Block;
            mCurrentHit = "shield";
            MWRender::Animation::AnimPriority priorityBlock (Priority_Hit);
            priorityBlock[MWRender::Animation::BoneGroup_LeftArm] = Priority_Block;
            priorityBlock[MWRender::Animation::BoneGroup_LowerBody] = Priority_WeaponLowerBody;
            mAnimation->play(mCurrentHit, priorityBlock, MWRender::Animation::BlendMask_All, true, 1, "block start", "block stop", 0.0f, 0);
        }

        // Cancel upper body animations
        if (isKnockedOut() || isKnockedDown())
        {
            if (mUpperBodyState > UpperCharState_WeapEquiped)
            {
                mAnimation->disable(mCurrentWeapon);
                mUpperBodyState = UpperCharState_WeapEquiped;
                if (mWeaponType > ESM::Weapon::None)
                    mAnimation->showWeapons(true);
            }
            else if (mUpperBodyState > UpperCharState_Nothing && mUpperBodyState < UpperCharState_WeapEquiped)
            {
                mAnimation->disable(mCurrentWeapon);
                mUpperBodyState = UpperCharState_Nothing;
            }
        }
        if (mHitState != CharState_None)
            idle = CharState_None;
    }
    else if(!mAnimation->isPlaying(mCurrentHit))
    {
        mCurrentHit.erase();
        if (knockdown)
            mPtr.getClass().getCreatureStats(mPtr).setKnockedDown(false);
        if (recovery)
            mPtr.getClass().getCreatureStats(mPtr).setHitRecovery(false);
        if (block)
            mPtr.getClass().getCreatureStats(mPtr).setBlock(false);
        mHitState = CharState_None;
    }
    else if (isKnockedOut() && mPtr.getClass().getCreatureStats(mPtr).getFatigue().getCurrent() > 0 
            && mTimeUntilWake <= 0)
    {
        mHitState = isSwimming ? CharState_SwimKnockDown : CharState_KnockDown;
        mAnimation->disable(mCurrentHit);
        mAnimation->play(mCurrentHit, Priority_Knockdown, MWRender::Animation::BlendMask_All, true, 1, "loop stop", "stop", 0.0f, 0);
    }
}

void CharacterController::refreshJumpAnims(const std::string& weapShortGroup, JumpingState jump, CharacterState& idle, bool force)
{
    if (!force && jump == mJumpState && idle == CharState_None)
        return;

    std::string jumpAnimName;
    MWRender::Animation::BlendMask jumpmask = MWRender::Animation::BlendMask_All;
    if (jump != JumpState_None)
    {
        jumpAnimName = "jump";
        if(!weapShortGroup.empty())
        {
            jumpAnimName += weapShortGroup;
            jumpAnimName = nativeMovementGroup(jumpAnimName);
            if(!mAnimation->hasAnimation(jumpAnimName))
            {
                jumpAnimName = fallbackShortWeaponGroup("jump", &jumpmask);

                // If we apply jump only for lower body, do not reset idle animations.
                // For upper body there will be idle animation.
                if (jumpmask == MWRender::Animation::BlendMask_LowerBody && idle == CharState_None)
                    idle = CharState_Idle;
            }
        }
    }

    if (!force && jump == mJumpState)
        return;

    bool startAtLoop = (jump == mJumpState);
    mJumpState = jump;

    if (!mCurrentJump.empty())
    {
        mAnimation->disable(mCurrentJump);
        mCurrentJump.clear();
    }

    if(mJumpState == JumpState_InAir)
    {
        if (mAnimation->hasAnimation(jumpAnimName))
        {
            mAnimation->play(jumpAnimName, Priority_Jump, jumpmask, false,
                         1.0f, startAtLoop ? "loop start" : "start", "stop", 0.f, ~0ul);
            mCurrentJump = jumpAnimName;
        }
    }
    else if (mJumpState == JumpState_Landing)
    {
        if (mAnimation->hasAnimation(jumpAnimName))
        {
            mAnimation->play(jumpAnimName, Priority_Jump, jumpmask, true,
                         1.0f, "loop stop", "stop", 0.0f, 0);
            mCurrentJump = jumpAnimName;
        }
    }
}

bool CharacterController::onOpen()
{
    if (mPtr.getTypeName() == typeid(ESM::Container).name())
    {
        if (!mAnimation->hasAnimation("containeropen"))
            return true;

        if (mAnimation->isPlaying("containeropen"))
            return false;

        if (mAnimation->isPlaying("containerclose"))
            return false;

        mAnimation->play("containeropen", Priority_Persistent, MWRender::Animation::BlendMask_All, false, 1.0f, "start", "stop", 0.f, 0);
        if (mAnimation->isPlaying("containeropen"))
            return false;
    }

    return true;
}

void CharacterController::onClose()
{
    if (mPtr.getTypeName() == typeid(ESM::Container).name())
    {
        if (!mAnimation->hasAnimation("containerclose"))
            return;

        float complete, startPoint = 0.f;
        bool animPlaying = mAnimation->getInfo("containeropen", &complete);
        if (animPlaying)
            startPoint = 1.f - complete;

        mAnimation->play("containerclose", Priority_Persistent, MWRender::Animation::BlendMask_All, false, 1.0f, "start", "stop", startPoint, 0);
    }
}

bool CharacterController::usesNativeFirstPerson() const
{
    const auto* npc = dynamic_cast<const MWRender::NpcAnimation*>(mAnimation);
    return npc && npc->usesFirstPersonAnimations();
}

bool CharacterController::usesNativeReAnimation() const
{
    return usesNativeFirstPerson() || (mAnimation && mAnimation->hasAnimation("weapononehand1"));
}

bool CharacterController::usesThrowingStar() const
{
    if (!usesNativeReAnimation() || mWeapon.isEmpty()
        || mWeapon.getTypeName() != typeid(ESM::Weapon).name()
        || mWeapon.get<ESM::Weapon>()->mBase->mData.mType != ESM::Weapon::MarksmanThrown)
        return false;
    return Misc::StringUtils::lowerCase(mWeapon.getCellRef().getRefId()).find("star") != std::string::npos;
}

std::string CharacterController::nativeMovementGroup(const std::string& group) const
{
    if (usesThrowingStar() && mAnimation->hasAnimation(group + "star"))
        return group + "star";
    return group;
}

void CharacterController::selectNativeAttack(const std::string& synchronizedGroup)
{
    if (!mAnimation)
        return;

    const std::string base = getWeaponAnimation(mWeaponType);
    const std::string id = mWeapon.isEmpty() ? std::string()
        : Misc::StringUtils::lowerCase(mWeapon.getCellRef().getRefId());
    std::string selected = !synchronizedGroup.empty()
        ? synchronizedGroup
        : NativeAnimation::attackGroup(base, mAttackType, id, mNativeAttackSequence++);

    // The dedicated third-person pack deliberately keeps its original OpenMW
    // group names (WeaponTwoHand1/WeaponTwoWide1/ThrowWeapon1). Translate the
    // canonical ReAnimation group locally instead of putting camera-specific
    // names on the wire. First person therefore keeps the ReAnimation v3 clip,
    // while third person can use the purpose-built full-body animation.
    if (!usesNativeFirstPerson())
    {
        const std::string thirdPerson = NativeAnimation::thirdPersonAttackGroup(selected);
        if (thirdPerson != selected && mAnimation->hasAnimation(thirdPerson))
            selected = thirdPerson;
    }

    // Require the whole attack pipeline, not only a group name. Partial external
    // replacements must not strand the controller in its wind-up or skip a hit.
    bool complete = mAnimation->hasAnimation(selected);
    const char* keys[] = {" start", " min attack", " max attack", " min hit"};
    for (const char* key : keys)
        complete = complete && mAnimation->getTextKeyTime(selected + ": " + mAttackType + key) >= 0.f;
    complete = complete && mAnimation->getTextKeyTime(selected + ": " + mAttackType
        + (mAttackType == "shoot" ? " release" : " hit")) >= 0.f;
    const char* meleeKeys[] = {" small follow start", " small follow stop", " medium follow start",
        " medium follow stop", " large follow start", " large follow stop"};
    if (mAttackType != "shoot")
        for (const char* key : meleeKeys)
            complete = complete && mAnimation->getTextKeyTime(selected + ": " + mAttackType + key) >= 0.f;
    else
        complete = complete && mAnimation->getTextKeyTime(selected + ": shoot follow stop") >= 0.f;

    mAnimation->disable(mNativeTail);
    mNativeTail.clear();
    mAnimation->disable(mCurrentWeapon);
    mCurrentWeapon = complete ? selected : base;
}

std::string CharacterController::getWeaponAnimation(int weaponType) const
{
    std::string weaponGroup = getWeaponType(weaponType)->mLongGroup;
    bool isRealWeapon = weaponType != ESM::Weapon::HandToHand && weaponType != ESM::Weapon::Spell && weaponType != ESM::Weapon::None;
    if (isRealWeapon && !mAnimation->hasAnimation(weaponGroup))
    {
        static const std::string oneHandFallback = getWeaponType(ESM::Weapon::LongBladeOneHand)->mLongGroup;
        static const std::string twoHandFallback = getWeaponType(ESM::Weapon::LongBladeTwoHand)->mLongGroup;

        const ESM::WeaponType* weapInfo = getWeaponType(weaponType);

        // For real two-handed melee weapons use 2h swords animations as fallback, otherwise use the 1h ones
        if (weapInfo->mFlags & ESM::WeaponType::TwoHanded && weapInfo->mWeaponClass == ESM::WeaponType::Melee)
            weaponGroup = twoHandFallback;
        else if (isRealWeapon)
            weaponGroup = oneHandFallback;
    }

    if (weaponType == ESM::Weapon::MarksmanThrown && usesThrowingStar()
        && mAnimation->hasAnimation("throwweaponstar"))
        return "throwweaponstar";
    return weaponGroup;
}

std::string CharacterController::fallbackShortWeaponGroup(const std::string& baseGroupName, MWRender::Animation::BlendMask* blendMask)
{
    bool isRealWeapon = mWeaponType != ESM::Weapon::HandToHand && mWeaponType != ESM::Weapon::Spell && mWeaponType != ESM::Weapon::None;
    if (!isRealWeapon)
    {
        if (blendMask != nullptr)
            *blendMask = MWRender::Animation::BlendMask_LowerBody;

        return baseGroupName;
    }

    static const std::string oneHandFallback = getWeaponType(ESM::Weapon::LongBladeOneHand)->mShortGroup;
    static const std::string twoHandFallback = getWeaponType(ESM::Weapon::LongBladeTwoHand)->mShortGroup;

    std::string groupName = baseGroupName;
    const ESM::WeaponType* weapInfo = getWeaponType(mWeaponType);

    // For real two-handed melee weapons use 2h swords animations as fallback, otherwise use the 1h ones
    if (weapInfo->mFlags & ESM::WeaponType::TwoHanded && weapInfo->mWeaponClass == ESM::WeaponType::Melee)
        groupName += twoHandFallback;
    else
        groupName += oneHandFallback;

    // Special case for crossbows - we shouls apply 1h animations a fallback only for lower body
    if (mWeaponType == ESM::Weapon::MarksmanCrossbow && blendMask != nullptr)
        *blendMask = MWRender::Animation::BlendMask_LowerBody;

    if (!mAnimation->hasAnimation(groupName))
    {
        groupName = baseGroupName;
        if (blendMask != nullptr)
            *blendMask = MWRender::Animation::BlendMask_LowerBody;
    }

    return groupName;
}

void CharacterController::refreshMovementAnims(const std::string& weapShortGroup, CharacterState movement, CharacterState& idle, bool force)
{
    if (movement == mMovementState && idle == mIdleState && !force)
        return;

    // Reset idle if we actually play movement animations excepts of these cases:
    // 1. When we play turning animations
    // 2. When we use a fallback animation for lower body since movement animation for given weapon is missing (e.g. for crossbows and spellcasting)
    bool resetIdle = (movement != CharState_None && !isTurning());

    std::string movementAnimName;
    bool usesDynamicMovement = false;
    bool usedDynamicMovementBefore = false;
    MWRender::Animation::BlendMask movemask;
    const StateInfo *movestate;

    if (!mCurrentMovement.empty())
    {
        const StateInfo* previousState
            = std::find_if(sMovementList, sMovementListEnd, FindCharState(mMovementState));
        if (previousState != sMovementListEnd)
        {
            const std::string previousDynamic
                = mwmp::getDynamicMovementAnimation(mPtr, previousState->groupname);
            usedDynamicMovementBefore = !previousDynamic.empty()
                && previousDynamic == mCurrentMovement;
        }
    }

    movemask = MWRender::Animation::BlendMask_All;
    movestate = std::find_if(sMovementList, sMovementListEnd, FindCharState(movement));
    if(movestate != sMovementListEnd)
    {
        movementAnimName = movestate->groupname;
        const std::string dynamicMovement
            = mwmp::getDynamicMovementAnimation(mPtr, movementAnimName);
        if (!dynamicMovement.empty() && mAnimation->hasAnimation(dynamicMovement))
        {
            movementAnimName = dynamicMovement;
            usesDynamicMovement = true;

            // The supplied walk cycles are authored as general animation
            // groups. In a weapon or spell stance keep them on the legs so
            // weapon, shield and casting controllers continue to own the
            // upper body.
            if (!weapShortGroup.empty())
            {
                movemask = MWRender::Animation::BlendMask_LowerBody;
                if (idle == CharState_None)
                    idle = CharState_Idle;
                resetIdle = false;
            }
        }
        else if(!weapShortGroup.empty())
        {
            std::string::size_type swimpos = movementAnimName.find("swim");
            if (swimpos == std::string::npos)
            {
                if (mWeaponType == ESM::Weapon::Spell && (movement == CharState_TurnLeft || movement == CharState_TurnRight)) // Spellcasting stance turning is a special case
                    movementAnimName = weapShortGroup + movementAnimName;
                else
                    movementAnimName += weapShortGroup;
            }

            movementAnimName = nativeMovementGroup(movementAnimName);
            if(!mAnimation->hasAnimation(movementAnimName))
            {
                movementAnimName = movestate->groupname;
                if (swimpos == std::string::npos)
                {
                    movementAnimName = fallbackShortWeaponGroup(movementAnimName, &movemask);

                    // If we apply movement only for lower body, do not reset idle animations.
                    // For upper body there will be idle animation.
                    if (movemask == MWRender::Animation::BlendMask_LowerBody && idle == CharState_None)
                        idle = CharState_Idle;

                    if (movemask == MWRender::Animation::BlendMask_LowerBody)
                        resetIdle = false;
                }
            }
        }
    }

    if(force || movement != mMovementState)
    {
        mMovementState = movement;
        if(movestate != sMovementListEnd)
        {
            if(!mAnimation->hasAnimation(movementAnimName))
            {
                std::string::size_type swimpos = movementAnimName.find("swim");
                if (swimpos != std::string::npos)
                {
                    movementAnimName.erase(swimpos, 4);
                    if (!weapShortGroup.empty())
                    {
                        std::string weapMovementAnimName = movementAnimName + weapShortGroup;
                        if(mAnimation->hasAnimation(weapMovementAnimName))
                            movementAnimName = weapMovementAnimName;
                        else
                        {
                            movementAnimName = fallbackShortWeaponGroup(movementAnimName, &movemask);
                            if (movemask == MWRender::Animation::BlendMask_LowerBody)
                                resetIdle = false;
                        }
                    }
                }

                if (swimpos == std::string::npos || !mAnimation->hasAnimation(movementAnimName))
                {
                    std::string::size_type runpos = movementAnimName.find("run");
                    if (runpos != std::string::npos)
                    {
                        movementAnimName.replace(runpos, runpos+3, "walk");
                        if (!mAnimation->hasAnimation(movementAnimName))
                            movementAnimName.clear();
                    }
                    else
                        movementAnimName.clear();
                }
            }
        }

        // If we're playing the same animation, start it from the point it ended
        float startpoint = 0.f;
        if (!mCurrentMovement.empty() && movementAnimName == mCurrentMovement)
            mAnimation->getInfo(mCurrentMovement, &startpoint);

        mMovementAnimationControlled = true;

        mAnimation->disable(mCurrentMovement);

        if ((usesDynamicMovement || usedDynamicMovementBefore)
            && movementAnimName != mCurrentMovement)
        {
            mAnimation->beginBoneTransition(movemask, sDynamicMovementTransitionSeconds);
        }

        if (!mAnimation->hasAnimation(movementAnimName))
            movementAnimName.clear();

        mCurrentMovement = movementAnimName;
        if(!mCurrentMovement.empty())
        {
            if (resetIdle)
            {
                mAnimation->disable(mCurrentIdle);
                mIdleState = CharState_None;
                idle = CharState_None;
            }

            // For non-flying creatures, MW uses the Walk animation to calculate the animation velocity
            // even if we are running. This must be replicated, otherwise the observed speed would differ drastically.
            std::string anim = mCurrentMovement;
            mAdjustMovementAnimSpeed = true;
            if (mPtr.getClass().getTypeName() == typeid(ESM::Creature).name()
                    && !(mPtr.get<ESM::Creature>()->mBase->mFlags & ESM::Creature::Flies))
            {
                CharacterState walkState = runStateToWalkState(mMovementState);
                const StateInfo *stateinfo = std::find_if(sMovementList, sMovementListEnd, FindCharState(walkState));
                anim = stateinfo->groupname;

                mMovementAnimSpeed = mAnimation->getVelocity(anim);
                if (mMovementAnimSpeed <= 1.0f)
                {
                    // Another bug: when using a fallback animation (e.g. RunForward as fallback to SwimRunForward),
                    // then the equivalent Walk animation will not use a fallback, and if that animation doesn't exist
                    // we will play without any scaling.
                    // Makes the speed attribute of most water creatures totally useless.
                    // And again, this can not be fixed without patching game data.
                    mAdjustMovementAnimSpeed = false;
                    mMovementAnimSpeed = 1.f;
                }
            }
            else
            {
                mMovementAnimSpeed = mAnimation->getVelocity(anim);

                if (mMovementAnimSpeed <= 1.0f)
                {
                    // The first person anims don't have any velocity to calculate a speed multiplier from.
                    // We use the third person velocities instead.
                    // FIXME: should be pulled from the actual animation, but it is not presently loaded.
                    bool sneaking = mMovementState == CharState_SneakForward || mMovementState == CharState_SneakBack
                                 || mMovementState == CharState_SneakLeft    || mMovementState == CharState_SneakRight;
                    mMovementAnimSpeed = (sneaking ? 33.5452f : (isRunning() ? 222.857f : 154.064f));
                    mMovementAnimationControlled = false;
                }
            }

            mAnimation->play(mCurrentMovement, Priority_Movement, movemask, false,
                             1.f, "start", "stop", startpoint, ~0ul, true);
        }
        else
            mMovementState = CharState_None;
    }
}

void CharacterController::refreshIdleAnims(const std::string& weapShortGroup, CharacterState idle, bool force)
{
    // FIXME: if one of the below states is close to their last animation frame (i.e. will be disabled in the coming update),
    // the idle animation should be displayed
    if (((mUpperBodyState != UpperCharState_Nothing && mUpperBodyState != UpperCharState_WeapEquiped)
            || (mMovementState != CharState_None && !isTurning())
            || mHitState != CharState_None)
            && !mPtr.getClass().isBipedal(mPtr))
        idle = CharState_None;

    if(force || idle != mIdleState || (!mAnimation->isPlaying(mCurrentIdle) && mAnimQueue.empty()))
    {
        mIdleState = idle;
        size_t numLoops = ~0ul;

        std::string idleGroup;
        MWRender::Animation::AnimPriority idlePriority (Priority_Default);
        // Only play "idleswim" or "idlesneak" if they exist. Otherwise, fallback to
        // "idle"+weapon or "idle".
        if(mIdleState == CharState_IdleSwim && mAnimation->hasAnimation("idleswim"))
        {
            idleGroup = "idleswim";
            idlePriority = Priority_SwimIdle;
        }
        else if(mIdleState == CharState_IdleSneak && mAnimation->hasAnimation("idlesneak"))
        {
            idleGroup = "idlesneak";
            idlePriority[MWRender::Animation::BoneGroup_LowerBody] = Priority_SneakIdleLowerBody;
        }
        else if(mIdleState != CharState_None)
        {
            idleGroup = "idle";
            if(!weapShortGroup.empty())
            {
                idleGroup += weapShortGroup;
                if(!mAnimation->hasAnimation(idleGroup))
                {
                    idleGroup = fallbackShortWeaponGroup("idle");
                }

                // play until the Loop Stop key 2 to 5 times, then play until the Stop key
                // this replicates original engine behavior for the "Idle1h" 1st-person animation
                numLoops = 1 + Misc::Rng::rollDice(4); 
            }
        }

        if (usesNativeReAnimation() && mIdleState != CharState_IdleSwim && mIdleState != CharState_None)
        {
            const std::string typed = nativeMovementGroup("idle" + weapShortGroup);
            const std::string candidate = typed + (mIdleState == CharState_IdleSneak ? "sneak" : "");
            if (mAnimation->hasAnimation(candidate)) idleGroup = candidate;
        }

        // There is no need to restart anim if the new and old anims are the same.
        // Just update a number of loops.
        float startPoint = 0;
        if (!mCurrentIdle.empty() && mCurrentIdle == idleGroup)
        {
            mAnimation->getInfo(mCurrentIdle, &startPoint);
        }

        if(!mCurrentIdle.empty())
            mAnimation->disable(mCurrentIdle);

        mCurrentIdle = idleGroup;
        if(!mCurrentIdle.empty())
            mAnimation->play(mCurrentIdle, idlePriority, MWRender::Animation::BlendMask_All, false,
                             1.0f, "start", "stop", startPoint, numLoops, true);
    }
}

void CharacterController::refreshCurrentAnims(CharacterState idle, CharacterState movement, JumpingState jump, bool force)
{
    // If the current animation is persistent, do not touch it
    if (isPersistentAnimPlaying())
        return;

    if (mPtr.getClass().isActor())
        refreshHitRecoilAnims(idle);

    std::string weap;
    if (mPtr.getClass().hasInventoryStore(mPtr))
        weap = getWeaponType(mWeaponType)->mShortGroup;

    refreshJumpAnims(weap, jump, idle, force);
    refreshMovementAnims(weap, movement, idle, force);

    // idle handled last as it can depend on the other states
    refreshIdleAnims(weap, idle, force);
}

void CharacterController::playDeath(float startpoint, CharacterState death)
{
    // Make sure the character was swimming upon death for forward-compatibility
    const bool wasSwimming = MWBase::Environment::get().getWorld()->isSwimming(mPtr);

    switch (death)
    {
    case CharState_SwimDeath:
        mCurrentDeath = "swimdeath";
        break;
    case CharState_SwimDeathKnockDown:
        mCurrentDeath = (wasSwimming ? "swimdeathknockdown" : "deathknockdown");
        break;
    case CharState_SwimDeathKnockOut:
        mCurrentDeath = (wasSwimming ? "swimdeathknockout" : "deathknockout");
        break;
    case CharState_DeathKnockDown:
        mCurrentDeath = "deathknockdown";
        break;
    case CharState_DeathKnockOut:
        mCurrentDeath = "deathknockout";
        break;
    default:
        mCurrentDeath = "death" + std::to_string(death - CharState_Death1 + 1);
    }
    mDeathState = death;

    mPtr.getClass().getCreatureStats(mPtr).setDeathAnimation(mDeathState - CharState_Death1);

    // For dead actors, refreshCurrentAnims is no longer called, so we need to disable the movement state manually.
    // Note that these animations wouldn't actually be visible (due to the Death animation's priority being higher).
    // However, they could still trigger text keys, such as Hit events, or sounds.
    mMovementState = CharState_None;
    mAnimation->disable(mCurrentMovement);
    mCurrentMovement = "";
    mUpperBodyState = UpperCharState_Nothing;
    mAnimation->disable(mCurrentWeapon);
    mCurrentWeapon = "";
    mHitState = CharState_None;
    mAnimation->disable(mCurrentHit);
    mCurrentHit = "";
    mIdleState = CharState_None;
    mAnimation->disable(mCurrentIdle);
    mCurrentIdle = "";
    mJumpState = JumpState_None;
    mAnimation->disable(mCurrentJump);
    mCurrentJump = "";
    mMovementAnimationControlled = true;

    mAnimation->play(mCurrentDeath, Priority_Death, MWRender::Animation::BlendMask_All,
                    false, 1.0f, "start", "stop", startpoint, 0);
}

CharacterState CharacterController::chooseRandomDeathState() const
{
    int selected=0;
    chooseRandomGroup("death", &selected);
    return static_cast<CharacterState>(CharState_Death1 + (selected-1));
}

void CharacterController::playRandomDeath(float startpoint)
{
    /*
        Start of tes3mp addition

        If this is a LocalActor or DedicatedActor whose death animation is supposed to be finished,
        set the startpoint to the animation's end
    */
    if (mPtr.getClass().getCreatureStats(mPtr).isDeathAnimationFinished() &&
        (mwmp::Main::get().getCellController()->isLocalActor(mPtr) || mwmp::Main::get().getCellController()->isDedicatedActor(mPtr)))
    {
        startpoint = 1.F;
    }
    /*
        End of tes3mp addition
    */

    if (mPtr == getPlayer())
    {
        // The first-person animations do not include death, so we need to
        // force-switch to third person before playing the death animation.
        MWBase::Environment::get().getWorld()->useDeathCamera();
    }

    /*
        Start tes3mp change (major)

        If this is a DedicatedPlayer, use the deathState received from their PlayerDeath packet

        If this is a DedicatedActor, use the deathState from their ActorDeath packet
    */
    if (mwmp::PlayerList::isDedicatedPlayer(mPtr))
    {
        mDeathState = static_cast<CharacterState>(mwmp::PlayerList::getPlayer(mPtr)->deathState);
    }
    else if (mwmp::Main::get().getCellController()->hasQueuedDeathState(mPtr))
    {
        mDeathState = static_cast<CharacterState>(mwmp::Main::get().getCellController()->getQueuedDeathState(mPtr));
        mwmp::Main::get().getCellController()->clearQueuedDeathState(mPtr);
    }
    else if(mHitState == CharState_SwimKnockDown && mAnimation->hasAnimation("swimdeathknockdown"))
    /*
        End of tes3mp change (major)
    */
    {
        mDeathState = CharState_SwimDeathKnockDown;
    }
    else if(mHitState == CharState_SwimKnockOut && mAnimation->hasAnimation("swimdeathknockout"))
    {
        mDeathState = CharState_SwimDeathKnockOut;
    }
    else if(MWBase::Environment::get().getWorld()->isSwimming(mPtr) && mAnimation->hasAnimation("swimdeath"))
    {
        mDeathState = CharState_SwimDeath;
    }
    else if (mHitState == CharState_KnockDown && mAnimation->hasAnimation("deathknockdown"))
    {
        mDeathState = CharState_DeathKnockDown;
    }
    else if (mHitState == CharState_KnockOut && mAnimation->hasAnimation("deathknockout"))
    {
        mDeathState = CharState_DeathKnockOut;
    }
    else
    {
        mDeathState = chooseRandomDeathState();
    }

    /*
        Start of tes3mp addition

        If this is the local player, send a PlayerDeath packet with the decided-upon
        death animation

        If this is a local actor, send an ActorDeath packet with the animation
    */
    if (mPtr == getPlayer())
    {
        mwmp::Main::get().getLocalPlayer()->sendDeath(mDeathState);
    }
    else if (!mPtr.getClass().getCreatureStats(mPtr).isDeathAnimationFinished() && mwmp::Main::get().getCellController()->isLocalActor(mPtr))
    {
        mwmp::Main::get().getCellController()->getLocalActor(mPtr)->sendDeath(mDeathState);
    }
    /*
        End of tes3mp addition
    */

    // Do not interrupt scripted animation by death
    if (isPersistentAnimPlaying())
        return;

    playDeath(startpoint, mDeathState);
}

std::string CharacterController::chooseRandomAttackAnimation() const
{
    std::string result;
    bool isSwimming = MWBase::Environment::get().getWorld()->isSwimming(mPtr);

    if (isSwimming)
        result = chooseRandomGroup("swimattack");

    if (!isSwimming || !mAnimation->hasAnimation(result))
        result = chooseRandomGroup("attack");

    return result;
}

CharacterController::CharacterController(const MWWorld::Ptr &ptr, MWRender::Animation *anim)
    : mPtr(ptr)
    , mWeapon(MWWorld::Ptr())
    , mAnimation(anim)
    , mIdleState(CharState_None)
    , mMovementState(CharState_None)
    , mMovementAnimSpeed(0.f)
    , mAdjustMovementAnimSpeed(false)
    , mHasMovedInXY(false)
    , mMovementAnimationControlled(true)
    , mDeathState(CharState_None)
    , mFloatToSurface(true)
    , mHitState(CharState_None)
    , mUpperBodyState(UpperCharState_Nothing)
    , mJumpState(JumpState_None)
    , mWeaponType(ESM::Weapon::None)
    , mAttackStrength(0.f)
    , mSkipAnim(false)
    , mSecondsOfSwimming(0)
    , mSecondsOfRunning(0)
    , mTurnAnimationThreshold(0)
    , mAttackingOrSpell(false)
    , mCastingManualSpell(false)
    , mQuickCastActive(false)
    , mQuickCastReleased(false)
    , mQuickCastLocalRelease(false)
    , mQuickCastIsItem(false)
    , mTimeUntilWake(0.f)
    , mIsMovingBackward(false)
    , mMpLastObservedPosition()
    , mMpHasLastObservedPosition(false)
    , mMpBlockedMeleeTime(0.f)
{
    if(!mAnimation)
        return;

    mAnimation->setTextKeyListener(this);

    const MWWorld::Class &cls = mPtr.getClass();
    if(cls.isActor())
    {
        /* Accumulate along X/Y only for now, until we can figure out how we should
         * handle knockout and death which moves the character down. */
        mAnimation->setAccumulation(osg::Vec3f(1.0f, 1.0f, 0.0f));

        if (cls.hasInventoryStore(mPtr))
        {
            getActiveWeapon(mPtr, &mWeaponType);
            if (mWeaponType != ESM::Weapon::None)
            {
                mUpperBodyState = UpperCharState_WeapEquiped;
                mCurrentWeapon = getWeaponAnimation(mWeaponType);
            }

            if(mWeaponType != ESM::Weapon::None && mWeaponType != ESM::Weapon::Spell && mWeaponType != ESM::Weapon::HandToHand)
            {
                mAnimation->showWeapons(true);
                // Note: controllers for ranged weapon should use time for beginning of animation to play shooting properly,
                // for other weapons they should use absolute time. Some mods rely on this behaviour (to rotate throwing projectiles, for example)
                ESM::WeaponType::Class weaponClass = getWeaponType(mWeaponType)->mWeaponClass;
                bool useRelativeDuration = weaponClass == ESM::WeaponType::Ranged;
                mAnimation->setWeaponGroup(mCurrentWeapon, useRelativeDuration);
            }

            mAnimation->showCarriedLeft(updateCarriedLeftVisible(mWeaponType));
        }

        if(!cls.getCreatureStats(mPtr).isDead())
        {
            mIdleState = CharState_Idle;
            if (cls.getCreatureStats(mPtr).getFallHeight() > 0)
                mJumpState = JumpState_InAir;
        }
        else
        {
            const MWMechanics::CreatureStats& cStats = mPtr.getClass().getCreatureStats(mPtr);
            if (cStats.isDeathAnimationFinished())
            {
                // Set the death state, but don't play it yet
                // We will play it in the first frame, but only if no script set the skipAnim flag
                signed char deathanim = cStats.getDeathAnimation();
                if (deathanim == -1)
                    mDeathState = chooseRandomDeathState();
                else
                    mDeathState = static_cast<CharacterState>(CharState_Death1 + deathanim);

                mFloatToSurface = false;
            }
            // else: nothing to do, will detect death in the next frame and start playing death animation
        }
    }
    else
    {
        /* Don't accumulate with non-actors. */
        mAnimation->setAccumulation(osg::Vec3f(0.f, 0.f, 0.f));

        mIdleState = CharState_Idle;
    }

    // Do not update animation status for dead actors
    if(mDeathState == CharState_None && (!cls.isActor() || !cls.getCreatureStats(mPtr).isDead()))
        refreshCurrentAnims(mIdleState, mMovementState, mJumpState, true);

    mAnimation->runAnimation(0.f);

    unpersistAnimationState();
}

CharacterController::~CharacterController()
{
    finishSecurityAction();
    if (mAnimation)
    {
        persistAnimationState();
        mAnimation->setTextKeyListener(nullptr);
    }
}

void split(const std::string &s, char delim, std::vector<std::string> &elems) {
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        elems.push_back(item);
    }
}

void CharacterController::handleTextKey(const std::string &groupname, SceneUtil::TextKeyMap::ConstIterator key, const SceneUtil::TextKeyMap& map)
{
    const std::string &evt = key->second;

    if(evt.compare(0, 7, "sound: ") == 0)
    {
        MWBase::SoundManager *sndMgr = MWBase::Environment::get().getSoundManager();
        sndMgr->playSound3D(mPtr, evt.substr(7), 1.0f, 1.0f);
        return;
    }
    if(evt.compare(0, 10, "soundgen: ") == 0)
    {
        std::string soundgen = evt.substr(10);

        // The event can optionally contain volume and pitch modifiers
        float volume=1.f, pitch=1.f;
        if (soundgen.find(' ') != std::string::npos)
        {
            std::vector<std::string> tokens;
            split(soundgen, ' ', tokens);
            soundgen = tokens[0];
            if (tokens.size() >= 2)
            {
                std::stringstream stream;
                stream << tokens[1];
                stream >> volume;
            }
            if (tokens.size() >= 3)
            {
                std::stringstream stream;
                stream << tokens[2];
                stream >> pitch;
            }
        }

        // Visual footsteps do not depend on the sound volume or audio availability.
        if (soundgen == "left" || soundgen == "right" || soundgen == "land")
            MWBase::Environment::get().getWorld()->emitFootstep(mPtr, soundgen != "right", soundgen == "land");

        std::string sound = mPtr.getClass().getSoundIdFromSndGen(mPtr, soundgen);
        if(!sound.empty())
        {
            MWBase::SoundManager *sndMgr = MWBase::Environment::get().getSoundManager();
            // NB: landing sound is not played for NPCs here
            if(soundgen == "left" || soundgen == "right" || soundgen == "land")
            {
                sndMgr->playSound3D(mPtr, sound, volume, pitch, MWSound::Type::Foot,
                                    MWSound::PlayMode::NoPlayerLocal);
            }
            else
            {
                sndMgr->playSound3D(mPtr, sound, volume, pitch);
            }
        }
        return;
    }

    if(evt.compare(0, groupname.size(), groupname) != 0 ||
       evt.compare(groupname.size(), 2, ": ") != 0)
    {
        // Not ours, skip it
        return;
    }
    size_t off = groupname.size()+2;
    size_t len = evt.size() - off;

    if(groupname == "shield" && evt.compare(off, len, "equip attach") == 0)
        mAnimation->showCarriedLeft(true);
    else if(groupname == "shield" && evt.compare(off, len, "unequip detach") == 0)
        mAnimation->showCarriedLeft(false);
    else if(evt.compare(off, len, "equip attach") == 0)
        mAnimation->showWeapons(true);
    else if(evt.compare(off, len, "unequip detach") == 0)
        mAnimation->showWeapons(false);
    else if(evt.compare(off, len, "chop hit") == 0)
        mPtr.getClass().hit(mPtr, mAttackStrength, ESM::Weapon::AT_Chop);
    else if(evt.compare(off, len, "slash hit") == 0)
        mPtr.getClass().hit(mPtr, mAttackStrength, ESM::Weapon::AT_Slash);
    else if(evt.compare(off, len, "thrust hit") == 0)
        mPtr.getClass().hit(mPtr, mAttackStrength, ESM::Weapon::AT_Thrust);
    else if(evt.compare(off, len, "hit") == 0)
    {
        if (groupname == "attack1" || groupname == "swimattack1")
            mPtr.getClass().hit(mPtr, mAttackStrength, ESM::Weapon::AT_Chop);
        else if (groupname == "attack2" || groupname == "swimattack2")
            mPtr.getClass().hit(mPtr, mAttackStrength, ESM::Weapon::AT_Slash);
        else if (groupname == "attack3" || groupname == "swimattack3")
            mPtr.getClass().hit(mPtr, mAttackStrength, ESM::Weapon::AT_Thrust);
        else
            mPtr.getClass().hit(mPtr, mAttackStrength);
    }
    else if (!groupname.empty()
             && (groupname.compare(0, groupname.size()-1, "attack") == 0 || groupname.compare(0, groupname.size()-1, "swimattack") == 0)
             && evt.compare(off, len, "start") == 0)
    {
        std::multimap<float, std::string>::const_iterator hitKey = key;

        // Not all animations have a hit key defined. If there is none, the hit happens with the start key.
        bool hasHitKey = false;
        while (hitKey != map.end())
        {
            if (hitKey->second == groupname + ": hit")
            {
                hasHitKey = true;
                break;
            }
            if (hitKey->second == groupname + ": stop")
                break;
            ++hitKey;
        }
        if (!hasHitKey)
        {
            if (groupname == "attack1" || groupname == "swimattack1")
                mPtr.getClass().hit(mPtr, mAttackStrength, ESM::Weapon::AT_Chop);
            else if (groupname == "attack2" || groupname == "swimattack2")
                mPtr.getClass().hit(mPtr, mAttackStrength, ESM::Weapon::AT_Slash);
            else if (groupname == "attack3" || groupname == "swimattack3")
                mPtr.getClass().hit(mPtr, mAttackStrength, ESM::Weapon::AT_Thrust);
        }
    }
    else if (evt.compare(off, len, "shoot attach") == 0)
        mAnimation->attachArrow();
    else if (evt.compare(off, len, "shoot release") == 0)
        mAnimation->releaseArrow(mAttackStrength);
    else if (evt.compare(off, len, "shoot follow attach") == 0)
        mAnimation->attachArrow();

    else if (mQuickCastActive && groupname == mQuickCastGroup
             && (evt.compare(off, len, "release") == 0
                 || (groupname == "spellcast" && evt.compare(off, len, mAttackType + " release") == 0)))
    {
        // The quick-cast packet is sent once on animation start (pressed=true) and
        // once from the ordinary spell pipeline at this exact release key. Keeping
        // the actual spell application here preserves vanilla success/fatigue/XP
        // and TES3MP projectile/target synchronisation.
        if (!mQuickCastReleased)
        {
            mQuickCastReleased = true;
            if (mQuickCastLocalRelease)
            {
                Spells& spells = mPtr.getClass().getCreatureStats(mPtr).getSpells();
                const std::string selected = spells.getSelectedSpell();
                MWBase::World* world = MWBase::Environment::get().getWorld();
                if (mQuickCastIsItem && mPtr.getClass().hasInventoryStore(mPtr))
                {
                    // Y070: lock the selected enchanted item for the short gesture.
                    // Cast through the ordinary World::castSpell path at release so
                    // charge usage, Cast::ITEM networking and CastOnce removal remain
                    // exactly the same as prepared item magic.
                    MWWorld::InventoryStore& inv = mPtr.getClass().getInventoryStore(mPtr);
                    MWWorld::ContainerStoreIterator item = inv.begin();
                    for (; item != inv.end(); ++item)
                    {
                        if (Misc::StringUtils::ciEqual(item->getCellRef().getRefId(), mQuickCastItemId))
                            break;
                    }
                    if (item != inv.end())
                    {
                        spells.setSelectedSpell("");
                        inv.setSelectedEnchantItem(item);
                        world->castSpell(mPtr, false);
                    }
                }
                else
                {
                    spells.setSelectedSpell(mQuickCastSpellId);
                    world->castSpell(mPtr, false);
                    // Vanilla skips its success-roll block in god mode; it must still
                    // send the release packet for this explicitly started MP gesture.
                    if (mPtr == getPlayer() && world->getGodModeState())
                    {
                        if (mwmp::Cast* cast = MechanicsHelper::getLocalCast(mPtr))
                        {
                            cast->success = true;
                            cast->pressed = false;
                            cast->shouldSend = true;
                        }
                    }
                }
                spells.setSelectedSpell(selected);
            }
        }
    }
    else if (mQuickCastActive && groupname == mQuickCastGroup
             && (evt.compare(off, len, "stop") == 0
                 || (groupname == "spellcast" && evt.compare(off, len, mAttackType + " stop") == 0)))
    {
        // Cleanup is deferred to update(), outside Animation::runAnimation.
        // Auto-disable removes the visual after this callback returns.
        if (!mQuickCastLocalRelease || mQuickCastReleased)
        {
            mQuickCastActive = false;
            mQuickCastReleased = false;
            mQuickCastLocalRelease = false;
            mQuickCastIsItem = false;
            mQuickCastGroup.clear();
            mQuickCastSpellId.clear();
            mQuickCastItemId.clear();
        }
    }

    else if (mUpperBodyState == UpperCharState_CastingSpell
             && ((groupname == "spellcast" && evt.size() >= 7 && evt.substr(evt.size()-7, 7) == "release"
                  // Make sure this key is actually for the RangeType we are casting. The flame atronach has
                  // the same animation for all range types, so there are 3 "release" keys on the same time, one for each range type.
                  && evt.compare(off, len, mAttackType + " release") == 0)
                 || (NativeAnimation::isQuickCastGroup(groupname) && evt.compare(off, len, "release") == 0)))
    {
        /*
            Start of tes3mp change (major)

            Make the completion of the spellcast animation actually cast spells only for the
            local player and local actors, relying on Cast packets to cause spells to be cast
            for dedicated players and actors
        */
        if (mPtr == getPlayer() || mwmp::Main::get().getCellController()->isLocalActor(mPtr))
        {
            MWBase::Environment::get().getWorld()->castSpell(mPtr, mCastingManualSpell);
            mCastingManualSpell = false;
        }
        /*
            End of tes3mp change (major)
        */
    }

    else if (groupname == "shield" && evt.compare(off, len, "block hit") == 0)
        mPtr.getClass().block(mPtr);
    else if (groupname == "containeropen" && evt.compare(off, len, "loot") == 0)
        MWBase::Environment::get().getWindowManager()->pushGuiMode(MWGui::GM_Container, mPtr);
}

void CharacterController::updatePtr(const MWWorld::Ptr &ptr)
{
    finishSecurityAction();
    mPtr = ptr;
    mMpHasLastObservedPosition = false;
    mMpBlockedMeleeTime = 0.f;
}

void CharacterController::updateIdleStormState(bool inwater)
{
    if (!mAnimation->hasAnimation("idlestorm") || mUpperBodyState != UpperCharState_Nothing || inwater)
    {
        mAnimation->disable("idlestorm");
        return;
    }

    if (MWBase::Environment::get().getWorld()->isInStorm())
    {
        osg::Vec3f stormDirection = MWBase::Environment::get().getWorld()->getStormDirection();
        osg::Vec3f characterDirection = mPtr.getRefData().getBaseNode()->getAttitude() * osg::Vec3f(0,1,0);
        stormDirection.normalize();
        characterDirection.normalize();
        if (stormDirection * characterDirection < -0.5f)
        {
            if (!mAnimation->isPlaying("idlestorm"))
            {
                int mask = MWRender::Animation::BlendMask_Torso | MWRender::Animation::BlendMask_RightArm;
                mAnimation->play("idlestorm", Priority_Storm, mask, true, 1.0f, "start", "stop", 0.0f, ~0ul);
            }
            else
            {
                mAnimation->setLoopingEnabled("idlestorm", true);
            }
            return;
        }
    }

    if (mAnimation->isPlaying("idlestorm"))
    {
        mAnimation->setLoopingEnabled("idlestorm", false);
    }
}

bool CharacterController::updateCreatureState()
{
    const MWWorld::Class &cls = mPtr.getClass();
    CreatureStats &stats = cls.getCreatureStats(mPtr);

    int weapType = ESM::Weapon::None;
    if(stats.getDrawState() == DrawState_Weapon)
        weapType = ESM::Weapon::HandToHand;
    else if (stats.getDrawState() == DrawState_Spell)
        weapType = ESM::Weapon::Spell;

    if (weapType != mWeaponType)
    {
        mWeaponType = weapType;
        if (mAnimation->isPlaying(mCurrentWeapon))
            mAnimation->disable(mCurrentWeapon);
    }

    if(mAttackingOrSpell)
    {
        if(mUpperBodyState == UpperCharState_Nothing && mHitState == CharState_None)
        {
            MWBase::Environment::get().getWorld()->breakInvisibility(mPtr);

            std::string startKey = "start";
            std::string stopKey = "stop";
            if (weapType == ESM::Weapon::Spell)
            {
                const std::string spellid = stats.getSpells().getSelectedSpell();
                bool canCast = mCastingManualSpell || MWBase::Environment::get().getWorld()->startSpellCast(mPtr);

                if (!spellid.empty() && canCast)
                {
                    /*
                        Start of tes3mp addition

                        If this mPtr belongs to a LocalPlayer or LocalActor, get their Attack and prepare
                        it for sending
                    */
                    mwmp::Cast *localCast = MechanicsHelper::getLocalCast(mPtr);

                    if (localCast)
                    {
                        MechanicsHelper::resetCast(localCast);
                        localCast->type = mwmp::Cast::REGULAR;
                        localCast->spellId = spellid;
                        localCast->pressed = true;
                        localCast->shouldSend = true;

                        // Mark the attack as instant if there is no spellcast animation
                        if (!mAnimation->hasAnimation("spellcast"))
                            localCast->instant = true;
                    }
                    /*
                        End of tes3mp addition
                    */

                    MWMechanics::CastSpell cast(mPtr, nullptr, false, mCastingManualSpell);
                    cast.playSpellCastingEffects(spellid, false);

                    if (!mAnimation->hasAnimation("spellcast"))
                    {
                        MWBase::Environment::get().getWorld()->castSpell(mPtr, mCastingManualSpell); // No "release" text key to use, so cast immediately
                        mCastingManualSpell = false;
                    }
                    else
                    {
                        const ESM::Spell *spell = MWBase::Environment::get().getWorld()->getStore().get<ESM::Spell>().find(spellid);
                        const ESM::ENAMstruct &effectentry = spell->mEffects.mList.at(0);

                        switch(effectentry.mRange)
                        {
                            case 0: mAttackType = "self"; break;
                            case 1: mAttackType = "touch"; break;
                            case 2: mAttackType = "target"; break;
                        }

                        startKey = mAttackType + " " + startKey;
                        stopKey = mAttackType + " " + stopKey;
                        mCurrentWeapon = "spellcast";
                    }
                }
                else
                    mCurrentWeapon = "";
            }

            if (weapType != ESM::Weapon::Spell || !mAnimation->hasAnimation("spellcast")) // Not all creatures have a dedicated spellcast animation
            {
                mCurrentWeapon = chooseRandomAttackAnimation();
            }

            if (!mCurrentWeapon.empty())
            {
                // Creature spell animations must use the casting state: the release
                // text-key handler rejects ordinary attack states. Set it before
                // play(), which can dispatch text keys at the animation start.
                mUpperBodyState = (weapType == ESM::Weapon::Spell && mCurrentWeapon == "spellcast")
                    ? UpperCharState_CastingSpell : UpperCharState_StartToMinAttack;
                mAnimation->play(mCurrentWeapon, Priority_Weapon,
                                 MWRender::Animation::BlendMask_All, true,
                                 1, startKey, stopKey,
                                 0.0f, 0);

                mAttackStrength = std::min(1.f, 0.1f + Misc::Rng::rollClosedProbability());

                if (weapType == ESM::Weapon::HandToHand)
                    playSwishSound(0.0f);
            }
        }

        mAttackingOrSpell = false;
    }

    bool animPlaying = mAnimation->getInfo(mCurrentWeapon);
    if (!animPlaying)
        mUpperBodyState = UpperCharState_Nothing;
    return false;
}

bool CharacterController::updateCarriedLeftVisible(const int weaptype) const
{
    // Shields/torches shouldn't be visible during any operation involving two hands
    // There seems to be no text keys for this purpose, except maybe for "[un]equip start/stop",
    // but they are also present in weapon drawing animation.
    return mAnimation->updateCarriedLeftVisible(weaptype);
}

// Y092: NPC spell state normalize, shared by authority and remote controllers.
// Called only for a pending prepared cast, never while a cast clip is active.
void CharacterController::normalizePreparedSpellState()
{
    mAnimation->disable(mCurrentWeapon);
    mAnimation->disable("shield");
    mAnimation->disable(mNativeTail);
    mNativeTail.clear();
    mWeaponTransitionElapsed = 0.f;
    mWeaponTransitionTracked = false;
    mWeaponType = ESM::Weapon::Spell;
    mCurrentWeapon = getWeaponAnimation(mWeaponType);
    mAnimation->setWeaponGroup(mCurrentWeapon, false);
    mAnimation->showWeapons(false);
    mAnimation->showCarriedLeft(updateCarriedLeftVisible(mWeaponType));
    mUpperBodyState = UpperCharState_WeapEquiped;
    mAnimation->disable(mCurrentIdle);
    mIdleState = CharState_None;
}

bool CharacterController::updateWeaponState(CharacterState& idle)
{
    const MWWorld::Class &cls = mPtr.getClass();
    CreatureStats &stats = cls.getCreatureStats(mPtr);
    int weaptype = ESM::Weapon::None;
    if(stats.getDrawState() == DrawState_Weapon)
        weaptype = ESM::Weapon::HandToHand;
    else if (stats.getDrawState() == DrawState_Spell)
        weaptype = ESM::Weapon::Spell;

    const bool isWerewolf = cls.isNpc() && cls.getNpcStats(mPtr).isWerewolf();

    std::string upSoundId;
    std::string downSoundId;
    bool weaponChanged = false;
    MWWorld::Ptr desiredWeapon;
    if (mPtr.getClass().hasInventoryStore(mPtr))
    {
        MWWorld::InventoryStore &inv = cls.getInventoryStore(mPtr);
        const auto carriedWeapon = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
        if (carriedWeapon != inv.end()) desiredWeapon = *carriedWeapon;
        MWWorld::ContainerStoreIterator weapon = getActiveWeapon(mPtr, &weaptype);
        if(stats.getDrawState() == DrawState_Spell)
            weapon = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);

        if(weapon != inv.end() && mWeaponType != ESM::Weapon::HandToHand && weaptype != ESM::Weapon::HandToHand && weaptype != ESM::Weapon::Spell && weaptype != ESM::Weapon::None)
            upSoundId = weapon->getClass().getUpSoundId(*weapon);

        if(weapon != inv.end() && mWeaponType != ESM::Weapon::HandToHand && mWeaponType != ESM::Weapon::Spell && mWeaponType != ESM::Weapon::None)
            downSoundId = weapon->getClass().getDownSoundId(*weapon);

        // weapon->HtH switch: weapon is empty already, so we need to take sound from previous weapon
        if(weapon == inv.end() && !mWeapon.isEmpty() && weaptype == ESM::Weapon::HandToHand && mWeaponType != ESM::Weapon::Spell)
            downSoundId = mWeapon.getClass().getDownSoundId(mWeapon);

        MWWorld::Ptr newWeapon = weapon != inv.end() ? *weapon : MWWorld::Ptr();

        if (mWeapon != newWeapon)
        {
            mWeapon = newWeapon;
            weaponChanged = true;
        }
    }

    // For biped actors, blend weapon animations with lower body animations with higher priority
    MWRender::Animation::AnimPriority priorityWeapon(Priority_Weapon);
    if (mPtr.getClass().isBipedal(mPtr))
        priorityWeapon[MWRender::Animation::BoneGroup_LowerBody] = Priority_WeaponLowerBody;

    bool forcestateupdate = false;

    // Y092: first-person transition target. A repeated request preserves its clip;
    // a real reversal cancels exactly once, with matching progress on the reverse.
    float transitionStart = 0.f;
    bool transitionStarted = false;
    const bool inTransition = mUpperBodyState == UpperCharState_EquipingWeap
        || mUpperBodyState == UpperCharState_UnEquipingWeap;
    if (inTransition && mWeaponTransitionTracked
        && (mWeaponTransitionTarget != weaptype || mWeaponTransitionItem != desiredWeapon
            || mWeaponTransitionDrawState != static_cast<int>(stats.getDrawState())))
    {
        float progress = 0.f;
        const bool reversingSheath = mUpperBodyState == UpperCharState_UnEquipingWeap;
        if (mWeaponTransitionItem == desiredWeapon
            && (weaptype == ESM::Weapon::None || weaptype == mWeaponType)
            && mAnimation->getInfo(mCurrentWeapon, &progress))
            transitionStart = std::max(0.f, std::min(1.f, 1.f - progress));
        mAnimation->disable(mCurrentWeapon);
        mAnimation->disable("shield");
        mAnimation->disable(mNativeTail);
        mNativeTail.clear();
        mWeaponTransitionElapsed = 0.f;
        if (reversingSheath)
        {
            mWeaponType = ESM::Weapon::None;
            mCurrentWeapon = getWeaponAnimation(mWeaponType);
            mAnimation->setWeaponGroup(mCurrentWeapon, false);
            mUpperBodyState = UpperCharState_Nothing;
        }
        else
            mUpperBodyState = UpperCharState_WeapEquiped;
        mWeaponTransitionTracked = false;
        forcestateupdate = true;
    }
    if (!inTransition)
        mWeaponTransitionTracked = false;
    auto skippedTransitionKey = [&](const std::string& group, const std::string& phase,
                                    const std::string& event)
    {
        if (transitionStart <= 0.f) return false;
        const float begin = mAnimation->getTextKeyTime(group + ": " + phase + " start");
        const float end = mAnimation->getTextKeyTime(group + ": " + phase + " stop");
        const float key = mAnimation->getTextKeyTime(group + ": " + event);
        return begin >= 0.f && end >= begin && key >= begin
            && key <= begin + (end - begin) * transitionStart;
    };
    auto trackTransition = [&]()
    {
        mWeaponTransitionTracked = true;
        mWeaponTransitionTarget = weaptype;
        mWeaponTransitionDrawState = static_cast<int>(stats.getDrawState());
        mWeaponTransitionItem = desiredWeapon;
        mWeaponTransitionElapsed = 0.f;
        transitionStarted = true;
    };

    if (weaptype == ESM::Weapon::Spell && mAttackingOrSpell && !mQuickCastActive
        && mUpperBodyState != UpperCharState_CastingSpell
        && !isKnockedOut() && !isKnockedDown() && !isRecovery())
    {
        if (mWeaponType != ESM::Weapon::Spell || mUpperBodyState != UpperCharState_WeapEquiped)
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE, "Y092: normalizing prepared spell stance for %s",
                mPtr.getCellRef().getRefId().c_str());
        normalizePreparedSpellState();
        forcestateupdate = true;
    }

    // Y073: a broken/custom KF must not hold the controller in a transition
    // forever. This guard affects draw/sheath only, never a held attack or cast.
    if (mUpperBodyState == UpperCharState_EquipingWeap || mUpperBodyState == UpperCharState_UnEquipingWeap)
    {
        mWeaponTransitionElapsed += std::max(0.f, MWBase::Environment::get().getFrameDuration());
        if (mWeaponTransitionElapsed > 3.f)
        {
            mAnimation->disable(mCurrentWeapon);
            mAnimation->disable("shield");
            mAnimation->disable(mNativeTail);
            mNativeTail.clear();
            mWeaponType = weaptype;
            mCurrentWeapon = getWeaponAnimation(weaptype);
            mAnimation->setWeaponGroup(mCurrentWeapon, getWeaponType(weaptype)->mWeaponClass == ESM::WeaponType::Ranged);
            mUpperBodyState = weaptype == ESM::Weapon::None ? UpperCharState_Nothing : UpperCharState_WeapEquiped;
            mAnimation->showWeapons(weaptype != ESM::Weapon::None && weaptype != ESM::Weapon::Spell);
            mAnimation->showCarriedLeft(updateCarriedLeftVisible(weaptype));
            mWeaponTransitionElapsed = 0.f;
            forcestateupdate = true;
        }
    }
    else mWeaponTransitionElapsed = 0.f;

    // We should not play equipping animation and sound during weapon->weapon transition
    bool isStillWeapon = weaptype != ESM::Weapon::HandToHand && weaptype != ESM::Weapon::Spell && weaptype != ESM::Weapon::None &&
                            mWeaponType != ESM::Weapon::HandToHand && mWeaponType != ESM::Weapon::Spell && mWeaponType != ESM::Weapon::None;

    // If the current weapon type was changed in the middle of attack (e.g. by Equip console command or when bound spell expires),
    // we should force actor to the "weapon equipped" state, interrupt attack and update animations.
    if (isStillWeapon && mWeaponType != weaptype && mUpperBodyState > UpperCharState_WeapEquiped)
    {
        forcestateupdate = true;
        mUpperBodyState = UpperCharState_WeapEquiped;
        mAttackingOrSpell = false;
        mAnimation->disable(mCurrentWeapon);
        mAnimation->showWeapons(true);
        if (mPtr == getPlayer())
            MWBase::Environment::get().getWorld()->getPlayer().setAttackingOrSpell(false);
    }

    if(!isKnockedOut() && !isKnockedDown() && !isRecovery())
    {
        std::string weapgroup;
        if ((!isWerewolf || mWeaponType != ESM::Weapon::Spell)
            && weaptype != mWeaponType
            && mWeaponType != ESM::Weapon::None
            && mUpperBodyState != UpperCharState_UnEquipingWeap
            && !isStillWeapon)
        {
            // We can not play un-equip animation if weapon changed since last update
            if (!weaponChanged)
            {
                // Note: we do not disable unequipping animation automatically to avoid body desync
                weapgroup = getWeaponAnimation(mWeaponType);
                // Y073: attacks can leave mCurrentWeapon on an alternate group.
                // Track the clip we actually play, otherwise completion is read
                // from that old attack and the retained unequip layer never dies.
                mAnimation->disable(mCurrentWeapon);
                mAnimation->disable(mNativeTail);
                mNativeTail.clear();
                mCurrentWeapon = weapgroup;
                int unequipMask = MWRender::Animation::BlendMask_All;
                bool useShieldAnims = mAnimation->useShieldAnimations();
                if (useShieldAnims && mWeaponType != ESM::Weapon::HandToHand && mWeaponType != ESM::Weapon::Spell && !(mWeaponType == ESM::Weapon::None && weaptype == ESM::Weapon::Spell))
                {
                    unequipMask &= ~MWRender::Animation::BlendMask_LeftArm;
                    mAnimation->play("shield", Priority_Block,
                                MWRender::Animation::BlendMask_LeftArm, true,
                                1.0f, "unequip start", "unequip stop", transitionStart, 0);
                    if (skippedTransitionKey("shield", "unequip", "unequip detach"))
                        mAnimation->showCarriedLeft(false);
                }
                else if (mWeaponType == ESM::Weapon::HandToHand)
                    mAnimation->showCarriedLeft(false);

                mAnimation->play(weapgroup, priorityWeapon, unequipMask, false,
                                1.0f, "unequip start", "unequip stop", transitionStart, 0);
                mUpperBodyState = UpperCharState_UnEquipingWeap;
                trackTransition();

                mAnimation->detachArrow();

                // If we do not have the "unequip detach" key, hide weapon manually.
                if (mAnimation->getTextKeyTime(weapgroup+": unequip detach") < 0
                    || skippedTransitionKey(weapgroup, "unequip", "unequip detach"))
                    mAnimation->showWeapons(false);
            }

            if(!downSoundId.empty())
            {
                MWBase::SoundManager *sndMgr = MWBase::Environment::get().getSoundManager();
                sndMgr->playSound3D(mPtr, downSoundId, 1.0f, 1.0f);
            }
        }

        float complete;
        bool animPlaying = mAnimation->getInfo(mCurrentWeapon, &complete);
        if (!transitionStarted && (!animPlaying || complete >= 1.0f || !mAnimation->isPlaying(mCurrentWeapon)))
        {
            if (mUpperBodyState == UpperCharState_UnEquipingWeap)
            {
                // Retained states (autoDisable=false) must be removed BEFORE
                // mCurrentWeapon is replaced by the newly selected weapon group.
                mAnimation->disable(mCurrentWeapon);
                mAnimation->disable("shield");
                mAnimation->disable(mNativeTail);
                mNativeTail.clear();
                mAnimation->showWeapons(false);
                mAnimation->showCarriedLeft(updateCarriedLeftVisible(weaptype));
                mUpperBodyState = UpperCharState_Nothing;
                mWeaponTransitionElapsed = 0.f;
                forcestateupdate = true;
                // Rapid F/F reversal still needs to reattach the requested weapon.
                mWeaponType = ESM::Weapon::None;
                mCurrentWeapon = getWeaponAnimation(mWeaponType);
                mAnimation->setWeaponGroup(mCurrentWeapon, false);
                isStillWeapon = false;
            }
            // Weapon is changed, no current animation (e.g. unequipping or attack).
            // Start equipping animation now.
            if (weaptype != mWeaponType)
            {
                forcestateupdate = true;
                bool useShieldAnims = mAnimation->useShieldAnimations();
                if (!useShieldAnims)
                    mAnimation->showCarriedLeft(updateCarriedLeftVisible(weaptype));

                weapgroup = getWeaponAnimation(weaptype);

                // Note: controllers for ranged weapon should use time for beginning of animation to play shooting properly,
                // for other weapons they should use absolute time. Some mods rely on this behaviour (to rotate throwing projectiles, for example)
                ESM::WeaponType::Class weaponClass = getWeaponType(weaptype)->mWeaponClass;
                bool useRelativeDuration = weaponClass == ESM::WeaponType::Ranged;
                mAnimation->setWeaponGroup(weapgroup, useRelativeDuration);

                if (!isStillWeapon)
                {
                    mAnimation->disable(mCurrentWeapon);
                    if (weaptype != ESM::Weapon::None)
                    {
                        mAnimation->showWeapons(false);
                        int equipMask = MWRender::Animation::BlendMask_All;
                        if (useShieldAnims && weaptype != ESM::Weapon::Spell)
                        {
                            equipMask &= ~MWRender::Animation::BlendMask_LeftArm;
                            mAnimation->play("shield", Priority_Block,
                                        MWRender::Animation::BlendMask_LeftArm, true,
                                        1.0f, "equip start", "equip stop", transitionStart, 0);
                            if (skippedTransitionKey("shield", "equip", "equip attach"))
                                mAnimation->showCarriedLeft(true);
                        }

                        mAnimation->play(weapgroup, priorityWeapon, equipMask, true,
                                        1.0f, "equip start", "equip stop", transitionStart, 0);
                        mUpperBodyState = UpperCharState_EquipingWeap;
                        trackTransition();

                        // If we do not have the "equip attach" key, show weapon manually.
                        if (weaptype != ESM::Weapon::Spell)
                        {
                            if (mAnimation->getTextKeyTime(weapgroup+": equip attach") < 0
                                || skippedTransitionKey(weapgroup, "equip", "equip attach"))
                                mAnimation->showWeapons(true);
                        }
                    }
                }

                if(isWerewolf)
                {
                    const MWWorld::ESMStore &store = MWBase::Environment::get().getWorld()->getStore();
                    const ESM::Sound *sound = store.get<ESM::Sound>().searchRandom("WolfEquip");
                    if(sound)
                    {
                        MWBase::SoundManager *sndMgr = MWBase::Environment::get().getSoundManager();
                        sndMgr->playSound3D(mPtr, sound->mId, 1.0f, 1.0f);
                    }
                }

                mWeaponType = weaptype;
                mCurrentWeapon = getWeaponAnimation(mWeaponType);

                if(!upSoundId.empty() && !isStillWeapon)
                {
                    MWBase::SoundManager *sndMgr = MWBase::Environment::get().getSoundManager();
                    sndMgr->playSound3D(mPtr, upSoundId, 1.0f, 1.0f);
                }
            }

            // Make sure that we disabled unequipping animation
            if (mUpperBodyState == UpperCharState_UnEquipingWeap)
            {
                mUpperBodyState = UpperCharState_Nothing;
                mAnimation->disable(mCurrentWeapon);
                mWeaponType = ESM::Weapon::None;
                mCurrentWeapon = getWeaponAnimation(mWeaponType);
            }
        }
    }

    if(isWerewolf)
    {
        MWBase::SoundManager *sndMgr = MWBase::Environment::get().getSoundManager();
        if(cls.getCreatureStats(mPtr).getStance(MWMechanics::CreatureStats::Stance_Run)
            && mHasMovedInXY
            && !MWBase::Environment::get().getWorld()->isSwimming(mPtr)
            && mWeaponType == ESM::Weapon::None)
        {
            if(!sndMgr->getSoundPlaying(mPtr, "WolfRun"))
                sndMgr->playSound3D(mPtr, "WolfRun", 1.0f, 1.0f, MWSound::Type::Sfx,
                                    MWSound::PlayMode::Loop);
        }
        else
            sndMgr->stopSound3D(mPtr, "WolfRun");
    }

    // Cancel attack if we no longer have ammunition
    bool ammunition = true;
    bool isWeapon = false;
    float weapSpeed = 1.f;
    if (mPtr.getClass().hasInventoryStore(mPtr))
    {
        MWWorld::InventoryStore &inv = cls.getInventoryStore(mPtr);
        MWWorld::ConstContainerStoreIterator weapon = getActiveWeapon(mPtr, &weaptype);
        isWeapon = (weapon != inv.end() && weapon->getTypeName() == typeid(ESM::Weapon).name());
        if (isWeapon)
        {
            weapSpeed = weapon->get<ESM::Weapon>()->mBase->mData.mSpeed;
            MWWorld::ConstContainerStoreIterator ammo = inv.getSlot(MWWorld::InventoryStore::Slot_Ammunition);
            int ammotype = getWeaponType(weapon->get<ESM::Weapon>()->mBase->mData.mType)->mAmmoType;
            if (ammotype != ESM::Weapon::None && (ammo == inv.end() || ammo->get<ESM::Weapon>()->mBase->mData.mType != ammotype))
                ammunition = false;
        }

        if (!ammunition && mUpperBodyState > UpperCharState_WeapEquiped)
        {
            mAnimation->disable(mCurrentWeapon);
            mUpperBodyState = UpperCharState_WeapEquiped;
        }
    }

    // CB1: creatures (not NPCs or players) swing at 0.5x..1.5x speed by level.
    // Every client computes the same factor from the same level, so the
    // animation stays in step with the attack-owning client.
    if (cls.isActor() && !cls.isNpc())
        weapSpeed *= ArenaCombatBalance::creatureAttackSpeed(cls.getCreatureStats(mPtr).getLevel());

    // Combat for actors with persistent animations obviously will be buggy
    if (isPersistentAnimPlaying())
        return forcestateupdate;

    float complete;
    bool animPlaying;
    ESM::WeaponType::Class weapclass = getWeaponType(mWeaponType)->mWeaponClass;
    if(mAttackingOrSpell)
    {
        MWWorld::Ptr player = getPlayer();

        bool resetIdle = ammunition;
        if(mUpperBodyState == UpperCharState_WeapEquiped && (mHitState == CharState_None || mHitState == CharState_Block))
        {
            MWBase::Environment::get().getWorld()->breakInvisibility(mPtr);
            mAttackStrength = 0;

            // Randomize attacks for non-bipedal creatures with Weapon flag
            if (mPtr.getClass().getTypeName() == typeid(ESM::Creature).name() &&
                !mPtr.getClass().isBipedal(mPtr) &&
                (!mAnimation->hasAnimation(mCurrentWeapon) || isRandomAttackAnimation(mCurrentWeapon)))
            {
                mCurrentWeapon = chooseRandomAttackAnimation();
            }

            if(mWeaponType == ESM::Weapon::Spell)
            {
                if (mSecurityPhase == SecurityPhase::SpellAnimation)
                    mSecurityAnimationStarted = true;
                // Unset casting flag, otherwise pressing the mouse button down would
                // continue casting every frame if there is no animation
                mAttackingOrSpell = false;
                if (mPtr == player)
                {
                    MWBase::Environment::get().getWorld()->getPlayer().setAttackingOrSpell(false);

                    // For the player, set the spell we want to cast
                    // This has to be done at the start of the casting animation,
                    // *not* when selecting a spell in the GUI (otherwise you could change the spell mid-animation)
                    std::string selectedSpell = MWBase::Environment::get().getWindowManager()->getSelectedSpell();
                    stats.getSpells().setSelectedSpell(selectedSpell);
                }
                std::string spellid = stats.getSpells().getSelectedSpell();
                bool isMagicItem = false;
                bool canCast = mCastingManualSpell || MWBase::Environment::get().getWorld()->startSpellCast(mPtr);

                if (spellid.empty())
                {
                    if (mPtr.getClass().hasInventoryStore(mPtr))
                    {
                        MWWorld::InventoryStore& inv = mPtr.getClass().getInventoryStore(mPtr);
                        if (inv.getSelectedEnchantItem() != inv.end())
                        {
                            const MWWorld::Ptr& enchantItem = *inv.getSelectedEnchantItem();
                            spellid = enchantItem.getClass().getEnchantment(enchantItem);
                            isMagicItem = true;
                        }
                    }
                }

                static const bool useCastingAnimations = Settings::Manager::getBool("use magic item animations", "Game");
                if (isMagicItem && !useCastingAnimations)
                {
                    // Enchanted items by default do not use casting animations
                    MWBase::Environment::get().getWorld()->castSpell(mPtr);
                    resetIdle = false;
                }
                else if(!spellid.empty() && canCast)
                {
                    /*
                        Start of tes3mp addition

                        If this mPtr belongs to a LocalPlayer or LocalActor, get their Cast and prepare
                        it for sending
                    */
                    mwmp::Cast *localCast = MechanicsHelper::getLocalCast(mPtr);

                    if (localCast)
                    {
                        MechanicsHelper::resetCast(localCast);
                        localCast->type = mwmp::Cast::REGULAR;
                        localCast->spellId = spellid;
                        localCast->pressed = true;
                        localCast->shouldSend = true;
                    }
                    /*
                        End of tes3mp addition
                    */

                    MWMechanics::CastSpell cast(mPtr, nullptr, false, mCastingManualSpell);
                    cast.playSpellCastingEffects(spellid, isMagicItem);

                    std::vector<ESM::ENAMstruct> effects;
                    const MWWorld::ESMStore &store = MWBase::Environment::get().getWorld()->getStore();
                    if (isMagicItem)
                    {
                        const ESM::Enchantment *enchantment = store.get<ESM::Enchantment>().find(spellid);
                        effects = enchantment->mEffects.mList;
                    }
                    else
                    {
                        const ESM::Spell *spell = store.get<ESM::Spell>().find(spellid);
                        effects = spell->mEffects.mList;
                    }

                    const ESM::MagicEffect *effect = store.get<ESM::MagicEffect>().find(effects.back().mEffectID); // use last effect of list for color of VFX_Hands
                    const ESM::ENAMstruct &firstEffect = effects.at(0); // first effect used for casting animation
                    // Y080: prepared magic uses the native two-hand spellcast group.
                    // Quick-cast gestures belong exclusively to startQuickCast().
                    if (!isRandomAttackAnimation(mCurrentWeapon))
                        mCurrentWeapon = getWeaponAnimation(ESM::Weapon::Spell);

                    const ESM::Static* castStatic = MWBase::Environment::get().getWorld()->getStore().get<ESM::Static>().find ("VFX_Hands");

                    for (size_t iter = 0; iter < effects.size(); ++iter) // play hand VFX for each effect
                    {
                        if (mAnimation->getNode("Bip01 L Hand"))
                            mAnimation->addEffect("meshes\\" + castStatic->mModel, -1, false, "Bip01 L Hand", effect->mParticle);

                        if (mAnimation->getNode("Bip01 R Hand"))
                            mAnimation->addEffect("meshes\\" + castStatic->mModel, -1, false, "Bip01 R Hand", effect->mParticle);
                    }

                    switch(firstEffect.mRange)
                    {
                        case 0: mAttackType = "self"; break;
                        case 1: mAttackType = "touch"; break;
                        case 2: mAttackType = "target"; break;
                    }

                    std::string startKey;
                    std::string stopKey;
                    if (isRandomAttackAnimation(mCurrentWeapon))
                    {
                        startKey = "start";
                        stopKey = "stop";
                        MWBase::Environment::get().getWorld()->castSpell(mPtr, mCastingManualSpell); // No "release" text key to use, so cast immediately
                        mCastingManualSpell = false;
                    }
                    else
                    {
                        startKey = mAttackType+" start";
                        stopKey = mAttackType+" stop";
                    }

                    mAnimation->play(mCurrentWeapon, priorityWeapon,
                                     MWRender::Animation::BlendMask_All, true,
                                     1, startKey, stopKey, 0.0f, 0);
                    mUpperBodyState = UpperCharState_CastingSpell;
                }
                else
                {
                    resetIdle = false;
                }
            }
            else if(mWeaponType == ESM::Weapon::PickProbe)
            {
                MWWorld::ContainerStoreIterator weapon = mPtr.getClass().getInventoryStore(mPtr).getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
                if (weapon == mPtr.getClass().getInventoryStore(mPtr).end())
                {
                    finishSecurityAction();
                    mAttackingOrSpell = false;
                    return forcestateupdate;
                }
                MWWorld::Ptr item = *weapon;
                // TODO: this will only work for the player, and needs to be fixed if NPCs should ever use lockpicks/probes.
                MWWorld::Ptr target = mSecurityPhase == SecurityPhase::EquipTool
                    ? mSecurityTarget : MWBase::Environment::get().getWorld()->getFacedObject();
                if (mSecurityPhase == SecurityPhase::EquipTool)
                    mSecurityPhase = SecurityPhase::ToolAnimation;
                // One attempt per activation/attack, including a broken last tool.
                mAttackingOrSpell = false;
                MWBase::Environment::get().getWorld()->getPlayer().setAttackingOrSpell(false);
                std::string resultMessage, resultSound;

                if(!target.isEmpty())
                {
                    if(item.getTypeName() == typeid(ESM::Lockpick).name())
                        Security(mPtr).pickLock(target, item, resultMessage, resultSound);
                    else if(item.getTypeName() == typeid(ESM::Probe).name())
                        Security(mPtr).probeTrap(target, item, resultMessage, resultSound);
                }
                mAnimation->play(mCurrentWeapon, priorityWeapon,
                                 MWRender::Animation::BlendMask_All, true,
                                 1.0f, "start", "stop", 0.0, 0);
                mUpperBodyState = UpperCharState_FollowStartToFollowStop;

                if(!resultMessage.empty())
                    MWBase::Environment::get().getWindowManager()->messageBox(resultMessage);
                if(!resultSound.empty())
                    MWBase::Environment::get().getSoundManager()->playSound3D(target, resultSound,
                                                                              1.0f, 1.0f);
            }
            else if (ammunition)
            {
                std::string startKey;
                std::string stopKey;
                std::string synchronizedAnimationGroup;

                if(weapclass == ESM::WeaponType::Ranged || weapclass == ESM::WeaponType::Thrown)
                {
                    mAttackType = "shoot";
                    startKey = mAttackType+" start";
                    stopKey = mAttackType+" min attack";

                    // Protocol 809 carries the selected bow/thrown ReAnimation group
                    // in the same attackAnimation field as melee. This keeps thrown
                    // alternate variants identical on every observing client.
                    if (mwmp::Attack* dedicatedAttack = MechanicsHelper::getDedicatedAttack(mPtr))
                        NativeAnimation::decodeAttackSelection(
                            dedicatedAttack->attackAnimation, mAttackType, synchronizedAnimationGroup);
                }
                else if (isRandomAttackAnimation(mCurrentWeapon))
                {
                    startKey = "start";
                    stopKey = "stop";
                }
                else
                {
                    // ArenaMP FIX26: NPCs can use their highest-damage attack type instead of
                    // deriving it from the movement direction. Keep the player setting separate.
                    const bool useBestAttack = mPtr == getPlayer()
                        ? Settings::Manager::getBool("best attack", "Game")
                        : Settings::Manager::getBool("npcs use best attack", "Game");

                    mwmp::Attack* dedicatedAttack = MechanicsHelper::getDedicatedAttack(mPtr);

                    if (dedicatedAttack)
                    {
                        // attackAnimation already travels in the normal melee Attack packet.
                        // Decode the optional ReAnimation group so every observer plays the
                        // exact same alternating/katana/wide-weapon variant as the attacker.
                        NativeAnimation::decodeAttackSelection(
                            dedicatedAttack->attackAnimation, mAttackType, synchronizedAnimationGroup);
                    }
                    else if (mPtr == getPlayer() || useBestAttack)
                    {
                        if (useBestAttack)
                        {
                            if (isWeapon)
                            {
                                MWWorld::ConstContainerStoreIterator weapon = mPtr.getClass().getInventoryStore(mPtr).getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
                                mAttackType = getBestAttack(weapon->get<ESM::Weapon>()->mBase);
                            }
                            else
                            {
                                // There is no "best attack" for Hand-to-Hand
                                setAttackTypeRandomly(mAttackType);
                            }
                        }
                        else
                        {
                            setAttackTypeBasedOnMovement();
                        }
                    }

                    // else if this is a local AI actor, use mAttackType set by AiCombat.
                    startKey = mAttackType+" start";
                    stopKey = mAttackType+" min attack";

                }

                selectNativeAttack(synchronizedAnimationGroup);

                // Piggyback the selected visual group on the existing Attack event.
                // Protocol 809 serializes this field for ranged attacks too, so melee,
                // bows and thrown alternates stay exact without a second packet.
                if (mwmp::Attack* localAttack = MechanicsHelper::getLocalAttack(mPtr))
                    localAttack->attackAnimation = NativeAnimation::encodeAttackSelection(mAttackType, mCurrentWeapon);

                mAnimation->play(mCurrentWeapon, priorityWeapon,
                                 MWRender::Animation::BlendMask_All, false,
                                 weapSpeed, startKey, stopKey,
                                 0.0f, 0);
                mUpperBodyState = UpperCharState_StartToMinAttack;
            }
        }

        // We should not break swim and sneak animations
        if (resetIdle &&
            idle != CharState_IdleSneak && idle != CharState_IdleSwim &&
            mIdleState != CharState_IdleSneak && mIdleState != CharState_IdleSwim)
        {
            mAnimation->disable(mCurrentIdle);
            mIdleState = CharState_None;
        }

        animPlaying = mAnimation->getInfo(mCurrentWeapon, &complete);
        if(mUpperBodyState == UpperCharState_MinAttackToMaxAttack && !isKnockedDown())
            mAttackStrength = complete;
    }
    else
    {
        animPlaying = mAnimation->getInfo(mCurrentWeapon, &complete);
        if(mUpperBodyState == UpperCharState_MinAttackToMaxAttack && !isKnockedDown())
        {
            float attackStrength = complete;
            float minAttackTime = mAnimation->getTextKeyTime(mCurrentWeapon+": "+mAttackType+" "+"min attack");
            float maxAttackTime = mAnimation->getTextKeyTime(mCurrentWeapon+": "+mAttackType+" "+"max attack");
            if (minAttackTime == maxAttackTime)
            {
                // most creatures don't actually have an attack wind-up animation, so use a uniform random value
                // (even some creatures that can use weapons don't have a wind-up animation either, e.g. Rieklings)
                // Note: vanilla MW uses a random value for *all* non-player actors, but we probably don't need to go that far.
                attackStrength = std::min(1.f, 0.1f + Misc::Rng::rollClosedProbability());
            }

            if(weapclass != ESM::WeaponType::Ranged && weapclass != ESM::WeaponType::Thrown)
            {
                MWBase::SoundManager *sndMgr = MWBase::Environment::get().getSoundManager();

                if(isWerewolf)
                {
                    const MWWorld::ESMStore &store = MWBase::Environment::get().getWorld()->getStore();
                    const ESM::Sound *sound = store.get<ESM::Sound>().searchRandom("WolfSwing");
                    if(sound)
                        sndMgr->playSound3D(mPtr, sound->mId, 1.0f, 1.0f);
                }
                else
                {
                    playSwishSound(attackStrength);
                }
            }
            mAttackStrength = attackStrength;

            mAnimation->disable(mCurrentWeapon);
            mAnimation->play(mCurrentWeapon, priorityWeapon,
                             MWRender::Animation::BlendMask_All, false,
                             weapSpeed, mAttackType+" max attack", mAttackType+" min hit",
                             1.0f-complete, 0);

            complete = 0.f;
            mUpperBodyState = UpperCharState_MaxAttackToMinHit;
        }
        else if (isKnockedDown())
        {
            if (mUpperBodyState > UpperCharState_WeapEquiped)
            {
                mUpperBodyState = UpperCharState_WeapEquiped;
                if (mWeaponType > ESM::Weapon::None)
                    mAnimation->showWeapons(true);
            }
            mAnimation->disable(mCurrentWeapon);
        }
    }

    mAnimation->setPitchFactor(0.f);
    if (weapclass == ESM::WeaponType::Ranged || weapclass == ESM::WeaponType::Thrown)
    {
        switch (mUpperBodyState)
        {
        case UpperCharState_StartToMinAttack:
            mAnimation->setPitchFactor(complete);
            break;
        case UpperCharState_MinAttackToMaxAttack:
        case UpperCharState_MaxAttackToMinHit:
        case UpperCharState_MinHitToHit:
            mAnimation->setPitchFactor(1.f);
            break;
        case UpperCharState_FollowStartToFollowStop:
            if (animPlaying)
            {
                // technically we do not need a pitch for crossbow reload animation,
                // but we should avoid abrupt repositioning
                if (mWeaponType == ESM::Weapon::MarksmanCrossbow)
                    mAnimation->setPitchFactor(std::max(0.f, 1.f-complete*10.f));
                else
                    mAnimation->setPitchFactor(1.f-complete);
            }
            break;
        default:
            break;
        }
    }

    if(!transitionStarted && (!animPlaying || (mUpperBodyState == UpperCharState_EquipingWeap
        && (!mAnimation->isPlaying(mCurrentWeapon) || complete >= 1.f))))
    {
        if(mUpperBodyState == UpperCharState_EquipingWeap ||
           mUpperBodyState == UpperCharState_FollowStartToFollowStop ||
           mUpperBodyState == UpperCharState_CastingSpell)
        {
            if (ammunition && mWeaponType == ESM::Weapon::MarksmanCrossbow)
                mAnimation->attachArrow();

            if (usesNativeReAnimation() && mUpperBodyState == UpperCharState_FollowStartToFollowStop
                && !mAttackingOrSpell && !mQuickCastActive && !ActiveGuard::raised(mPtr))
            {
                const std::string tail = mCurrentWeapon + "extra";
                if (mAnimation->getTextKeyTime(tail + ": " + mAttackType + " tail start") >= 0.f
                    && mAnimation->getTextKeyTime(tail + ": " + mAttackType + " tail stop") >= 0.f)
                {
                    mAnimation->disable(mNativeTail);
                    mNativeTail = tail;
                    mAnimation->play(tail, priorityWeapon, MWRender::Animation::BlendMask_UpperBody,
                        true, weapSpeed, mAttackType + " tail start", mAttackType + " tail stop", 0.f, 0);
                }
            }
            if (mUpperBodyState == UpperCharState_EquipingWeap)
            {
                mAnimation->disable(mCurrentWeapon);
                mAnimation->disable("shield");
                mAnimation->showWeapons(mWeaponType != ESM::Weapon::None && mWeaponType != ESM::Weapon::Spell);
                mWeaponTransitionElapsed = 0.f;
                forcestateupdate = true;
            }
            mUpperBodyState = UpperCharState_WeapEquiped;
            // Y071: shield-sheathing mods normally rely on a shield equip-attach text key.
            if (mWeaponType != ESM::Weapon::Spell)
                mAnimation->showCarriedLeft(updateCarriedLeftVisible(mWeaponType));
        }
        else if(mUpperBodyState == UpperCharState_UnEquipingWeap)
        {
            // Y072: atomically clear stale third-person upper-body transition
            // layers after sheathing. This prevents the rare arms-down pose that
            // previously recovered only after toggling F several times.
            mAnimation->disable(mCurrentWeapon);
            mAnimation->disable("shield");
            mAnimation->disable(mNativeTail);
            mNativeTail.clear();
            mAnimation->showWeapons(false);
            mAnimation->showCarriedLeft(updateCarriedLeftVisible(weaptype));
            mUpperBodyState = UpperCharState_Nothing;
            mWeaponType = ESM::Weapon::None;
            mCurrentWeapon = getWeaponAnimation(mWeaponType);
            mAnimation->setWeaponGroup(mCurrentWeapon, false);
            mWeaponTransitionElapsed = 0.f;
            forcestateupdate = true;
        }
    }
    else if(complete >= 1.0f && !isRandomAttackAnimation(mCurrentWeapon))
    {
        std::string start, stop;
        switch(mUpperBodyState)
        {
            case UpperCharState_MinAttackToMaxAttack:
                //hack to avoid body pos desync when jumping/sneaking in 'max attack' state
                if(!mAnimation->isPlaying(mCurrentWeapon))
                    mAnimation->play(mCurrentWeapon, priorityWeapon,
                        MWRender::Animation::BlendMask_All, false,
                        0, mAttackType+" min attack", mAttackType+" max attack", 0.999f, 0);
                break;
            case UpperCharState_StartToMinAttack:
            case UpperCharState_MaxAttackToMinHit:
            {
                if (mUpperBodyState == UpperCharState_StartToMinAttack)
                {
                    // If actor is already stopped preparing attack, do not play the "min attack -> max attack" part.
                    // Happens if the player did not hold the attack button.
                    // Note: if the "min attack"->"max attack" is a stub, "play" it anyway. Attack strength will be random.
                    float minAttackTime = mAnimation->getTextKeyTime(mCurrentWeapon+": "+mAttackType+" "+"min attack");
                    float maxAttackTime = mAnimation->getTextKeyTime(mCurrentWeapon+": "+mAttackType+" "+"max attack");
                    if (mAttackingOrSpell || minAttackTime == maxAttackTime)
                    {
                        start = mAttackType+" min attack";
                        stop = mAttackType+" max attack";
                        mUpperBodyState = UpperCharState_MinAttackToMaxAttack;
                        break;
                    }

                    if(weapclass != ESM::WeaponType::Ranged && weapclass != ESM::WeaponType::Thrown)
                        playSwishSound(0.0f);
                }

                if(mAttackType == "shoot")
                {
                    start = mAttackType+" min hit";
                    stop = mAttackType+" release";
                }
                else
                {
                    start = mAttackType+" min hit";
                    stop = mAttackType+" hit";
                }
                mUpperBodyState = UpperCharState_MinHitToHit;
                break;
            }
            case UpperCharState_MinHitToHit:
                if(mAttackType == "shoot")
                {
                    start = mAttackType+" follow start";
                    stop = mAttackType+" follow stop";
                }
                else
                {
                    float str = mAttackStrength;
                    start = mAttackType+((str < 0.5f) ? " small follow start"
                                                                  : (str < 1.0f) ? " medium follow start"
                                                                                 : " large follow start");
                    stop = mAttackType+((str < 0.5f) ? " small follow stop"
                                                                 : (str < 1.0f) ? " medium follow stop"
                                                                                : " large follow stop");
                }
                mUpperBodyState = UpperCharState_FollowStartToFollowStop;
                break;
            default:
                break;
        }

        // Note: apply crossbow reload animation only for upper body
        // since blending with movement animations can give weird result.
        if(!start.empty())
        {
            int mask = MWRender::Animation::BlendMask_All;
            if (mWeaponType == ESM::Weapon::MarksmanCrossbow)
                mask = MWRender::Animation::BlendMask_UpperBody;

            mAnimation->disable(mCurrentWeapon);
            if (mUpperBodyState == UpperCharState_FollowStartToFollowStop)
                mAnimation->play(mCurrentWeapon, priorityWeapon,
                                 mask, true,
                                 weapSpeed, start, stop, 0.0f, 0);
            else
                mAnimation->play(mCurrentWeapon, priorityWeapon,
                                 mask, false,
                                 weapSpeed, start, stop, 0.0f, 0);
        }
    }
    else if(complete >= 1.0f && isRandomAttackAnimation(mCurrentWeapon))
    {
        mAnimation->disable(mCurrentWeapon);
        mUpperBodyState = UpperCharState_WeapEquiped;
    }

    // Repair visibility after interrupted/missing shield text keys and inventory
    // changes. Preserve attachment timing while drawing/sheathing and casting.
    // Only touch the scene graph when visibility actually changes.
    if (mPtr.getClass().hasInventoryStore(mPtr) && !mQuickCastActive
        && mUpperBodyState != UpperCharState_EquipingWeap
        && mUpperBodyState != UpperCharState_UnEquipingWeap
        && mUpperBodyState != UpperCharState_CastingSpell)
    {
        const bool showLeft = updateCarriedLeftVisible(weaptype);
        if (mAnimation->getCarriedLeftShown() != showLeft)
            mAnimation->showCarriedLeft(showLeft);
    }

    if (mPtr.getClass().hasInventoryStore(mPtr))
    {
        const MWWorld::InventoryStore& inv = mPtr.getClass().getInventoryStore(mPtr);
        MWWorld::ConstContainerStoreIterator torch = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedLeft);
        if(!mQuickCastActive && torch != inv.end() && torch->getTypeName() == typeid(ESM::Light).name()
                && updateCarriedLeftVisible(mWeaponType))
        {
            if (mAnimation->isPlaying("shield"))
                mAnimation->disable("shield");

            std::string torchGroup = "torch";
            if (usesNativeReAnimation())
            {
                if (mWeaponType == ESM::Weapon::ShortBladeOneHand) torchGroup = "torch1s";
                else if (mWeaponType == ESM::Weapon::MarksmanThrown) torchGroup = "torch1t";
                if (!mAnimation->hasAnimation(torchGroup)) torchGroup = "torch";
            }
            for (const char* group : {"torch", "torch1s", "torch1t"})
                if (torchGroup != group) mAnimation->disable(group);
            mAnimation->play(torchGroup, Priority_Torch, MWRender::Animation::BlendMask_LeftArm,
                false, 1.0f, "start", "stop", 0.0f, (~(size_t)0), true);
        }
        else
        {
            for (const char* group : {"torch", "torch1s", "torch1t"})
                mAnimation->disable(group);
        }
    }

    mAnimation->setAccurateAiming(mUpperBodyState > UpperCharState_WeapEquiped);

    return forcestateupdate;
}

void CharacterController::updateAnimQueue()
{
    if(mAnimQueue.size() > 1)
    {
        if(mAnimation->isPlaying(mAnimQueue.front().mGroup) == false)
        {
            mAnimation->disable(mAnimQueue.front().mGroup);
            mAnimQueue.pop_front();

            bool loopfallback = (mAnimQueue.front().mGroup.compare(0,4,"idle") == 0);
            mAnimation->play(mAnimQueue.front().mGroup, Priority_Default,
                             MWRender::Animation::BlendMask_All, false,
                             1.0f, "start", "stop", 0.0f, mAnimQueue.front().mLoopCount, loopfallback);
        }
    }

    if(!mAnimQueue.empty())
        mAnimation->setLoopingEnabled(mAnimQueue.front().mGroup, mAnimQueue.size() <= 1);
}

void CharacterController::updateNativeAnimationLayers()
{
    const CreatureStats& nativeStats = mPtr.getClass().getCreatureStats(mPtr);
    const bool allowed = usesNativeReAnimation() && !isDead() && !isKnockedDown() && !isKnockedOut()
        && !isRecovery() && !mQuickCastActive && !ActiveGuard::raised(mPtr)
        && nativeStats.mGuard.stagger <= 0.f && nativeStats.getDrawState() != DrawState_Nothing
        && mAnimQueue.empty() && mIdleState != CharState_IdleSwim;
    const bool moving = !mCurrentMovement.empty() && !isTurning()
        && mCurrentMovement.find("swim") == std::string::npos && mAnimation->isPlaying(mCurrentMovement);
    float speed = 1.f;
    if (moving) mAnimation->getInfo(mCurrentMovement, nullptr, &speed);
    if (isSneaking()) speed /= 2.8f;

    // Per-bone priorities keep these cosmetic states distinct from the main
    // movement/weapon states. Equal-priority states are evicted by Animation::play.
    auto layer = [&](std::string& current, const std::string& desired, int mask,
                     const MWRender::Animation::AnimPriority& priority)
    {
        if (current != desired)
        {
            mAnimation->disable(current);
            current = desired;
        }
        if (current.empty()) return;
        if (!mAnimation->isPlaying(current))
            mAnimation->play(current, priority, mask, false, speed, "start", "stop", 0.f, ~0ul, true);
        mAnimation->adjustSpeedMult(current, speed);
    };

    std::string shield;
    if (allowed && mWeaponType != ESM::Weapon::None && mWeaponType != ESM::Weapon::Spell
        && mUpperBodyState <= UpperCharState_WeapEquiped && mPtr.getClass().hasInventoryStore(mPtr))
    {
        const MWWorld::InventoryStore& inv = mPtr.getClass().getInventoryStore(mPtr);
        const auto left = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedLeft);
        if (left != inv.end() && left->getTypeName() == typeid(ESM::Armor).name()
            && left->get<ESM::Armor>()->mBase->mData.mType == ESM::Armor::Shield
            && updateCarriedLeftVisible(mWeaponType))
        {
            const char* suffix = mWeaponType == ESM::Weapon::ShortBladeOneHand ? "1sshield"
                : (mWeaponType == ESM::Weapon::MarksmanThrown ? "1tshield" : "shield");
            shield = std::string(moving ? "runforward" : "idle") + suffix;
            if (!mAnimation->hasAnimation(shield)) shield.clear();
        }
    }
    MWRender::Animation::AnimPriority shieldPriority(Priority_Default);
    shieldPriority[MWRender::Animation::BoneGroup_LeftArm] = Priority_Weapon;
    layer(mNativeShieldLayer, shield, MWRender::Animation::BlendMask_LeftArm, shieldPriority);

    std::string bounce;
    if (allowed && moving && mJumpState == JumpState_None
        && mUpperBodyState > UpperCharState_WeapEquiped && mWeaponType != ESM::Weapon::Spell
        && mWeaponType != ESM::Weapon::MarksmanBow && mWeaponType != ESM::Weapon::PickProbe)
    {
        bounce = getWeaponAnimation(mWeaponType) == "weapononehand" ? "runbounce" : "runbouncev3";
        if (!mAnimation->hasAnimation(bounce)) bounce.clear();
    }
    MWRender::Animation::AnimPriority bouncePriority(Priority_Default);
    bouncePriority[MWRender::Animation::BoneGroup_LowerBody] = Priority_Movement + 1;
    layer(mNativeBounceLayer, bounce, MWRender::Animation::BlendMask_LowerBody, bouncePriority);

    // ReAnimation's tension overlay belongs only to the first-person rig, and
    // only to the held max-attack pose. MinAttackToMaxAttack also covers the
    // moving draw: overriding its arms early twists the bow during the hand-off.
    bool tension = false;
    if (allowed && usesNativeFirstPerson() && !mSkipAnim
        && nativeStats.getDrawState() == DrawState_Weapon
        && mWeaponType == ESM::Weapon::MarksmanBow && mCurrentWeapon == "bowandarrow"
        && mUpperBodyState == UpperCharState_MinAttackToMaxAttack)
    {
        const float holdTime = mAnimation->getTextKeyTime(mCurrentWeapon + ": shoot max attack");
        const float currentTime = mAnimation->getCurrentTime(mCurrentWeapon);
        const float start = mAnimation->getTextKeyTime("bowandarrow1: tension start");
        const float end = mAnimation->getTextKeyTime("bowandarrow1: tension end");
        tension = holdTime >= 0.f && currentTime >= 0.f && std::abs(currentTime - holdTime) < 0.001f
            && start >= 0.f && end > start;
    }
    if (!tension) mAnimation->disable("bowandarrow1");
    else if (!mAnimation->isPlaying("bowandarrow1"))
    {
        MWRender::Animation::AnimPriority priority(Priority_Weapon + 1);
        priority[MWRender::Animation::BoneGroup_LowerBody] = Priority_Default;
        mAnimation->play("bowandarrow1", priority, MWRender::Animation::BlendMask_UpperBody,
            false, 1.f, "tension start", "tension end", 0.f, ~0ul, true);
    }
    if (!allowed || mWeaponType == ESM::Weapon::None || mWeaponType == ESM::Weapon::Spell)
    {
        mAnimation->disable(mNativeTail);
        mNativeTail.clear();
    }
}

void CharacterController::update(float duration)
{
    MWBase::World *world = MWBase::Environment::get().getWorld();
    const MWWorld::Class &cls = mPtr.getClass();
    osg::Vec3f movement(0.f, 0.f, 0.f);
    float speed = 0.f;

    ActiveGuard::update(mPtr, duration);
    if (cls.isActor() && (ActiveGuard::raised(mPtr) || cls.getCreatureStats(mPtr).mGuard.stagger > 0.f))
        mAttackingOrSpell = false;

    updateSecurityAction(duration);

    if (mQuickCastActive)
    {
        const CreatureStats& quickStats = cls.getCreatureStats(mPtr);
        const bool interrupted = quickStats.isDead() || quickStats.isParalyzed() || ActiveGuard::raised(mPtr)
            || quickStats.mGuard.stagger > 0.f
            || (mHitState != CharState_None && mHitState != CharState_Block);
        if (interrupted)
            cancelQuickCast(true);
        else if (!mQuickCastGroup.empty() && !mAnimation->isPlaying(mQuickCastGroup))
        {
            // Y065: remote quick-cast packets are RELIABLE_ORDERED, but a local
            // renderer/controller rebuild (cell switch, equipment refresh) can
            // temporarily stop the visual. Re-assert the same one-hand gesture
            // once instead of silently dropping it. Local casts still fail closed
            // when their release key was never reached.
            if (!mQuickCastLocalRelease && !mQuickCastReleased && mAnimation->hasAnimation(mQuickCastGroup))
            {
                // Reuse the released flag on remote playback as a one-shot repair
                // latch. A normal release key sets it too, so we never loop/restart
                // a completed gesture indefinitely when a modded KF lacks Stop.
                mQuickCastReleased = true;
                std::string quickStart = "start";
                std::string quickStop = "stop";
                if (mQuickCastGroup == "spellcast")
                {
                    quickStart = mAttackType + " start";
                    quickStop = mAttackType + " stop";
                }
                MWRender::Animation::AnimPriority quickCastPriority(Priority_Hit);
                mAnimation->play(mQuickCastGroup, quickCastPriority,
                    MWRender::Animation::BlendMask_Torso | MWRender::Animation::BlendMask_LeftArm, true,
                    1.f, quickStart, quickStop, 0.f, 0);
                if (!mAnimation->isPlaying(mQuickCastGroup))
                {
                    mQuickCastActive = false;
                    mQuickCastReleased = false;
                    mQuickCastIsItem = false;
                    mQuickCastGroup.clear();
                    mQuickCastSpellId.clear();
                    mQuickCastItemId.clear();
                }
            }
            else if (mQuickCastLocalRelease && !mQuickCastReleased)
                cancelQuickCast(true);
            else
            {
                mQuickCastActive = false;
                mQuickCastReleased = false;
                mQuickCastLocalRelease = false;
                mQuickCastIsItem = false;
                mQuickCastGroup.clear();
                mQuickCastSpellId.clear();
                mQuickCastItemId.clear();
            }
        }
    }
    updateMagicEffects();

    if (isKnockedOut())
        mTimeUntilWake -= duration;

    bool isPlayer = mPtr == MWMechanics::getPlayer();
    bool isFirstPersonPlayer = isPlayer && MWBase::Environment::get().getWorld()->isFirstPerson();
    bool godmode = isPlayer && MWBase::Environment::get().getWorld()->getGodModeState();

    float scale = mPtr.getCellRef().getScale();

    static const bool normalizeSpeed = Settings::Manager::getBool("normalise race speed", "Game");
    if (!normalizeSpeed && mPtr.getClass().isNpc())
    {
        const ESM::NPC* npc = mPtr.get<ESM::NPC>()->mBase;
        const ESM::Race* race = world->getStore().get<ESM::Race>().find(npc->mRace);
        float weight = npc->isMale() ? race->mData.mWeight.mMale : race->mData.mWeight.mFemale;
        scale *= weight;
    }

    if(!cls.isActor())
        updateAnimQueue();
    else if(!cls.getCreatureStats(mPtr).isDead())
    {
        bool onground = world->isOnGround(mPtr);
        bool incapacitated = ((!godmode && cls.getCreatureStats(mPtr).isParalyzed()) || cls.getCreatureStats(mPtr).getKnockedDown());
        bool inwater = world->isSwimming(mPtr);
        bool flying = world->isFlying(mPtr);
        bool solid = world->isActorCollisionEnabled(mPtr);
        // Can't run and sneak while flying (see speed formula in Npc/Creature::getSpeed)
        bool sneak = cls.getCreatureStats(mPtr).getStance(MWMechanics::CreatureStats::Stance_Sneak) && !flying;
        bool isrunning = cls.getCreatureStats(mPtr).getStance(MWMechanics::CreatureStats::Stance_Run) && !flying;
        CreatureStats &stats = cls.getCreatureStats(mPtr);
        Movement& movementSettings = cls.getMovementSettings(mPtr);

        // ArenaMP: a close melee actor can keep receiving forward AI input even
        // after collision/attack range has made its real movement effectively
        // zero. In single-player this is mostly hidden by local timing, while in
        // MP the same locomotion intent is also replicated and becomes a very
        // visible slow-motion foot shuffle during attacks. Track REAL horizontal
        // displacement here and later suppress only the walk/run animation layer.
        // Do not alter movementSettings: AI/pathing and physics keep their normal
        // intent, so the actor can immediately resume pursuit if the target moves.
        bool suppressMpBlockedMeleeLocomotion = false;
        if (!isPlayer && mwmp::Main::isInitialized())
        {
            mwmp::CellController* cellController = mwmp::Main::get().getCellController();
            const bool isMpActor = cellController
                && (cellController->isLocalActor(mPtr) || cellController->isDedicatedActor(mPtr));

            const osg::Vec3f currentPosition = mPtr.getRefData().getPosition().asVec3();
            if (mMpHasLastObservedPosition && duration > 0.001f && isMpActor)
            {
                const float dx = currentPosition.x() - mMpLastObservedPosition.x();
                const float dy = currentPosition.y() - mMpLastObservedPosition.y();
                const float actualHorizontalSpeed = std::sqrt(dx * dx + dy * dy) / duration;
                const bool hasHorizontalIntent = std::abs(movementSettings.mPosition[0]) > 0.05f
                    || std::abs(movementSettings.mPosition[1]) > 0.05f;
                const bool meleeWeapon = mWeaponType != ESM::Weapon::Spell
                    && mWeaponType < ESM::Weapon::MarksmanBow;

                const bool meleeAttackAnimation =
                    mUpperBodyState >= UpperCharState_StartToMinAttack
                    && mUpperBodyState <= UpperCharState_FollowStartToFollowStop;

                if (meleeAttackAnimation && meleeWeapon && hasHorizontalIntent
                    && actualHorizontalSpeed < 30.f)
                    mMpBlockedMeleeTime += duration;
                else
                    mMpBlockedMeleeTime = 0.f;

                // A short debounce avoids toggling the lower-body state on tiny
                // collision corrections while still stopping the shuffle quickly.
                suppressMpBlockedMeleeLocomotion = mMpBlockedMeleeTime >= 0.08f;
            }
            else
                mMpBlockedMeleeTime = 0.f;

            mMpLastObservedPosition = currentPosition;
            mMpHasLastObservedPosition = true;
        }
        else
        {
            mMpBlockedMeleeTime = 0.f;
            mMpHasLastObservedPosition = false;
        }

        //Force Jump Logic

        bool isMoving = (std::abs(movementSettings.mPosition[0]) > .5 || std::abs(movementSettings.mPosition[1]) > .5);
        if(!inwater && !flying && solid)
        {
            //Force Jump
            if(stats.getMovementFlag(MWMechanics::CreatureStats::Flag_ForceJump))
                movementSettings.mPosition[2] = onground ? 1 : 0;
            //Force Move Jump, only jump if they're otherwise moving
            if(stats.getMovementFlag(MWMechanics::CreatureStats::Flag_ForceMoveJump) && isMoving)
                movementSettings.mPosition[2] = onground ? 1 : 0;
        }

        /*
            Start of tes3mp addition

            Character movement setting rotations get reset here, so we have to assign movement
            settings to the LocalPlayer or a LocalActor now
        */
        if (world->getPlayerPtr() == mPtr)
        {
            mwmp::LocalPlayer *localPlayer = mwmp::Main::get().getLocalPlayer();
            MWMechanics::Movement &movementSettings = cls.getMovementSettings(mPtr);
            localPlayer->direction.pos[0] = movementSettings.mPosition[0];
            localPlayer->direction.pos[1] = movementSettings.mPosition[1];
            localPlayer->direction.pos[2] = movementSettings.mPosition[2];
            localPlayer->direction.rot[0] = movementSettings.mRotation[0];
            localPlayer->direction.rot[1] = movementSettings.mRotation[1];
            localPlayer->direction.rot[2] = movementSettings.mRotation[2];
        }
        else if (mwmp::Main::get().getCellController()->isLocalActor(mPtr))
        {
            mwmp::LocalActor *localActor = mwmp::Main::get().getCellController()->getLocalActor(mPtr);
            MWMechanics::Movement &movementSettings = cls.getMovementSettings(mPtr);
            localActor->direction.pos[0] = movementSettings.mPosition[0];
            localActor->direction.pos[1] = movementSettings.mPosition[1];
            localActor->direction.pos[2] = movementSettings.mPosition[2];
            localActor->direction.rot[0] = movementSettings.mRotation[0];
            localActor->direction.rot[1] = movementSettings.mRotation[1];
            localActor->direction.rot[2] = movementSettings.mRotation[2];
        }
        /*
            End of tes3mp addition
        */

        osg::Vec3f rot = cls.getRotationVector(mPtr);
        osg::Vec3f vec(movementSettings.asVec3());

        movementSettings.mSpeedFactor = std::min(vec.length(), 1.f);
        vec.normalize();

        // TODO: Move this check to mwinput.
        // Joystick analogue movement.
        // Due to the half way split between walking/running, we multiply speed by 2 while walking, unless a keyboard was used.
        if (isPlayer && !isrunning && !sneak && !flying && movementSettings.mSpeedFactor <= 0.5f)
            movementSettings.mSpeedFactor *= 2.f;

        static const bool smoothMovement = Settings::Manager::getBool("smooth movement", "Game");
        if (smoothMovement)
        {
            static const float playerTurningCoef = 1.0 / std::max(0.01f, Settings::Manager::getFloat("smooth movement player turning delay", "Game"));
            float angle = mPtr.getRefData().getPosition().rot[2];
            osg::Vec2f targetSpeed = Misc::rotateVec2f(osg::Vec2f(vec.x(), vec.y()), -angle) * movementSettings.mSpeedFactor;
            osg::Vec2f delta = targetSpeed - mSmoothedSpeed;
            float speedDelta = movementSettings.mSpeedFactor - mSmoothedSpeed.length();
            float deltaLen = delta.length();

            float maxDelta;
            if (isFirstPersonPlayer)
                maxDelta = 1;
            else if (std::abs(speedDelta) < deltaLen / 2)
                // Turning is smooth for player and less smooth for NPCs (otherwise NPC can miss a path point).
                maxDelta = duration * (isPlayer ? playerTurningCoef : 6.f);
            else if (isPlayer && speedDelta < -deltaLen / 2)
                // As soon as controls are released, mwinput switches player from running to walking.
                // So stopping should be instant for player, otherwise it causes a small twitch.
                maxDelta = 1;
            else // In all other cases speeding up and stopping are smooth.
                maxDelta = duration * 3.f;

            if (deltaLen > maxDelta)
                delta *= maxDelta / deltaLen;
            mSmoothedSpeed += delta;

            osg::Vec2f newSpeed = Misc::rotateVec2f(mSmoothedSpeed, angle);
            movementSettings.mSpeedFactor = newSpeed.normalize();
            vec.x() = newSpeed.x();
            vec.y() = newSpeed.y();

            const float eps = 0.001f;
            if (movementSettings.mSpeedFactor < eps)
            {
                movementSettings.mSpeedFactor = 0;
                vec.x() = 0;
                vec.y() = 1;
            }
            else if ((vec.y() < 0) != mIsMovingBackward)
            {
                if (targetSpeed.length() < eps || (movementSettings.mPosition[1] < 0) == mIsMovingBackward)
                    vec.y() = mIsMovingBackward ? -eps : eps;
            }
            vec.normalize();
        }

        float effectiveRotation = rot.z();
        bool canMove = cls.getMaxSpeed(mPtr) > 0;
        static const bool turnToMovementDirection = Settings::Manager::getBool("turn to movement direction", "Game");
        if (!turnToMovementDirection || isFirstPersonPlayer)
        {
            movementSettings.mIsStrafing = std::abs(vec.x()) > std::abs(vec.y()) * 2;
            stats.setSideMovementAngle(0);
        }
        else if (canMove)
        {
            float targetMovementAngle = vec.y() >= 0 ? std::atan2(-vec.x(), vec.y()) : std::atan2(vec.x(), -vec.y());
            movementSettings.mIsStrafing = (stats.getDrawState() != MWMechanics::DrawState_Nothing || inwater)
                                           && std::abs(targetMovementAngle) > osg::DegreesToRadians(60.0f);
            if (movementSettings.mIsStrafing)
                targetMovementAngle = 0;
            float delta = targetMovementAngle - stats.getSideMovementAngle();
            float cosDelta = cosf(delta);

            if ((vec.y() < 0) == mIsMovingBackward)
                movementSettings.mSpeedFactor *= std::min(std::max(cosDelta, 0.f) + 0.3f, 1.f); // slow down when turn
            if (std::abs(delta) < osg::DegreesToRadians(20.0f))
                mIsMovingBackward = vec.y() < 0;

            float maxDelta = osg::PI * duration * (2.5f - cosDelta);
            delta = osg::clampBetween(delta, -maxDelta, maxDelta);
            stats.setSideMovementAngle(stats.getSideMovementAngle() + delta);
            effectiveRotation += delta;
        }

        mAnimation->setLegsYawRadians(stats.getSideMovementAngle());
        if (stats.getDrawState() == MWMechanics::DrawState_Nothing || inwater)
            mAnimation->setUpperBodyYawRadians(stats.getSideMovementAngle() / 2);
        else
            mAnimation->setUpperBodyYawRadians(stats.getSideMovementAngle() / 4);
        if (smoothMovement && !isPlayer && !inwater)
            mAnimation->setUpperBodyYawRadians(mAnimation->getUpperBodyYawRadians() + mAnimation->getHeadYaw() / 2);

        speed = cls.getCurrentSpeed(mPtr);
        vec.x() *= speed;
        vec.y() *= speed;

        if(mHitState != CharState_None && mJumpState == JumpState_None)
            vec = osg::Vec3f();

        CharacterState movestate = CharState_None;
        CharacterState idlestate = CharState_SpecialIdle;
        JumpingState jumpstate = JumpState_None;

        bool forcestateupdate = false;

        mHasMovedInXY = std::abs(vec.x())+std::abs(vec.y()) > 0.0f;
        isrunning = isrunning && mHasMovedInXY;

        // advance athletics
        if(mHasMovedInXY && isPlayer)
        {
            if(inwater)
            {
                mSecondsOfSwimming += duration;
                while(mSecondsOfSwimming > 1)
                {
                    cls.skillUsageSucceeded(mPtr, ESM::Skill::Athletics, 1);
                    mSecondsOfSwimming -= 1;
                }
            }
            else if(isrunning && !sneak)
            {
                mSecondsOfRunning += duration;
                while(mSecondsOfRunning > 1)
                {
                    cls.skillUsageSucceeded(mPtr, ESM::Skill::Athletics, 0);
                    mSecondsOfRunning -= 1;
                }
            }
        }

        // reduce fatigue
        const MWWorld::Store<ESM::GameSetting> &gmst = world->getStore().get<ESM::GameSetting>();
        float fatigueLoss = 0;
        static const float fFatigueRunBase = gmst.find("fFatigueRunBase")->mValue.getFloat();
        static const float fFatigueRunMult = gmst.find("fFatigueRunMult")->mValue.getFloat();
        static const float fFatigueSwimWalkBase = gmst.find("fFatigueSwimWalkBase")->mValue.getFloat();
        static const float fFatigueSwimRunBase = gmst.find("fFatigueSwimRunBase")->mValue.getFloat();
        static const float fFatigueSwimWalkMult = gmst.find("fFatigueSwimWalkMult")->mValue.getFloat();
        static const float fFatigueSwimRunMult = gmst.find("fFatigueSwimRunMult")->mValue.getFloat();
        static const float fFatigueSneakBase = gmst.find("fFatigueSneakBase")->mValue.getFloat();
        static const float fFatigueSneakMult = gmst.find("fFatigueSneakMult")->mValue.getFloat();

        const bool stealthSpecialization = isPlayer && hasStealthSpecialization(mPtr, world);
        const float encumbrance = cls.getEncumbrance(mPtr) <= cls.getCapacity(mPtr)
            ? cls.getNormalizedEncumbrance(mPtr)
            : 1.f;

        if (cls.getEncumbrance(mPtr) <= cls.getCapacity(mPtr))
        {
            // The player sneak upkeep is handled separately below so it also drains while
            // standing still, can stop exactly at 1 fatigue, and can exempt the Stealth
            // specialization ("Бродяга"). NPC behaviour stays unchanged.
            if (sneak && !isPlayer)
                fatigueLoss = fFatigueSneakBase + encumbrance * fFatigueSneakMult;
            else if (!sneak)
            {
                if (inwater)
                {
                    if (!isrunning)
                        fatigueLoss = fFatigueSwimWalkBase + encumbrance * fFatigueSwimWalkMult;
                    else
                        fatigueLoss = fFatigueSwimRunBase + encumbrance * fFatigueSwimRunMult;
                }
                else if (isrunning)
                    fatigueLoss = fFatigueRunBase + encumbrance * fFatigueRunMult;
            }
        }
        fatigueLoss *= duration;
        fatigueLoss *= movementSettings.mSpeedFactor;
        DynamicStat<float> fatigue = cls.getCreatureStats(mPtr).getFatigue();

        if (!godmode)
        {
            fatigue.setCurrent(fatigue.getCurrent() - fatigueLoss, fatigue.getCurrent() < 0);

            // ArenaMP stamina upkeep while sneaking. This is intentionally independent
            // of movement speed: simply holding Sneak costs stamina for every player
            // except the Stealth specialization. The upkeep itself can never reduce
            // fatigue below 1, so Sneak alone cannot knock the player out.
            if (isPlayer && sneak && !stealthSpecialization && fatigue.getCurrent() > 1.f)
            {
                const float vanillaSneakRate = fFatigueSneakBase + encumbrance * fFatigueSneakMult;
                const float sneakRate = std::max(1.f, vanillaSneakRate);
                fatigue.setCurrent(std::max(1.f, fatigue.getCurrent() - sneakRate * duration));
            }

            // ArenaMP charged-attack upkeep. Melee wind-up/hold and bow/crossbow/
            // thrown-weapon draw now consume fatigue continuously, in addition to the
            // normal one-off fatigue cost when the attack is finally released.
            if (isPlayer && isAttackPreparing() && cls.hasInventoryStore(mPtr))
            {
                MWWorld::InventoryStore& inventory = cls.getInventoryStore(mPtr);
                MWWorld::ContainerStoreIterator weapon = inventory.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
                if (weapon != inventory.end() && weapon.getType() == MWWorld::ContainerStore::Type_Weapon)
                {
                    static const float fFatigueAttackBase = gmst.find("fFatigueAttackBase")->mValue.getFloat();
                    static const float fFatigueAttackMult = gmst.find("fFatigueAttackMult")->mValue.getFloat();
                    static const float fWeaponFatigueMult = gmst.find("fWeaponFatigueMult")->mValue.getFloat();

                    float holdRate = fFatigueAttackBase + encumbrance * fFatigueAttackMult;
                    const float charge = std::max(0.1f, std::min(1.f, mAttackStrength));
                    holdRate += weapon->getClass().getWeight(*weapon) * charge * fWeaponFatigueMult;

                    // Some content sets the vanilla attack-fatigue GMSTs very low or to
                    // zero. Keep a small guaranteed upkeep so holding a charged attack
                    // always has a gameplay cost as intended by ArenaMP.
                    holdRate = std::max(1.f, holdRate);
                    holdRate *= ArenaCombatLimits::playerAttackCostMultiplier(
                        cls.getCreatureStats(mPtr).getAttribute(ESM::Attribute::Endurance).getModified());
                    fatigue.setCurrent(fatigue.getCurrent() - holdRate * duration, fatigue.getCurrent() < 0);
                }
            }

            cls.getCreatureStats(mPtr).setFatigue(fatigue);
        }

        float z = cls.getJump(mPtr);
        if(sneak || inwater || flying || incapacitated || !solid || z <= 0)
            vec.z() = 0.0f;

        bool inJump = true;
        bool playLandingSound = false;
        if(!onground && !flying && !inwater && solid)
        {
            // In the air (either getting up —ascending part of jump— or falling).

            forcestateupdate = (mJumpState != JumpState_InAir);
            jumpstate = JumpState_InAir;

            static const float fJumpMoveBase = gmst.find("fJumpMoveBase")->mValue.getFloat();
            static const float fJumpMoveMult = gmst.find("fJumpMoveMult")->mValue.getFloat();
            float factor = fJumpMoveBase + fJumpMoveMult * mPtr.getClass().getSkill(mPtr, ESM::Skill::Acrobatics)/100.f;
            factor = std::min(1.f, factor);
            vec.x() *= factor;
            vec.y() *= factor;
            vec.z()  = 0.0f;
        }
        else if(vec.z() > 0.0f && mJumpState != JumpState_InAir)
        {
            // Started a jump.
            if (z > 0)
            {
                if(vec.x() == 0 && vec.y() == 0)
                    vec = osg::Vec3f(0.0f, 0.0f, z);
                else
                {
                    osg::Vec3f lat (vec.x(), vec.y(), 0.0f);
                    lat.normalize();
                    vec = osg::Vec3f(lat.x(), lat.y(), 1.0f) * z * 0.707f;
                }
            }
        }
        else if(mJumpState == JumpState_InAir && !inwater && !flying && solid)
        {
            forcestateupdate = true;
            jumpstate = JumpState_Landing;
            vec.z() = 0.0f;

            // We should reset idle animation during landing
            mAnimation->disable(mCurrentIdle);

            float height = cls.getCreatureStats(mPtr).land(isPlayer);
            float healthLost = getFallDamage(mPtr, height);

            if (healthLost > 0.0f)
            {
                const float fatigueTerm = cls.getCreatureStats(mPtr).getFatigueTerm();

                // inflict fall damages
                if (!godmode)
                {
                    float realHealthLost = static_cast<float>(healthLost * (1.0f - 0.25f * fatigueTerm));
                    cls.onHit(mPtr, realHealthLost, true, MWWorld::Ptr(), MWWorld::Ptr(), osg::Vec3f(), true);
                }

                const float acrobaticsSkill = cls.getSkill(mPtr, ESM::Skill::Acrobatics);
                if (healthLost > (acrobaticsSkill * fatigueTerm))
                {
                    if (!godmode)
                        cls.getCreatureStats(mPtr).setKnockedDown(true);
                }
                else
                {
                    // report acrobatics progression
                    if (isPlayer)
                        cls.skillUsageSucceeded(mPtr, ESM::Skill::Acrobatics, 1);
                }
            }

            if (mPtr.getClass().isNpc())
                playLandingSound = true;
        }
        else
        {
            if(mPtr.getClass().isNpc() && mJumpState == JumpState_InAir && !flying && solid)
                playLandingSound = true;

            jumpstate = mAnimation->isPlaying(mCurrentJump) ? JumpState_Landing : JumpState_None;

            vec.x() *= scale;
            vec.y() *= scale;
            vec.z() = 0.0f;

            inJump = false;

            if (movementSettings.mIsStrafing)
            {
                if(vec.x() > 0.0f)
                    movestate = (inwater ? (isrunning ? CharState_SwimRunRight : CharState_SwimWalkRight)
                                         : (sneak ? CharState_SneakRight
                                                  : (isrunning ? CharState_RunRight : CharState_WalkRight)));
                else if(vec.x() < 0.0f)
                    movestate = (inwater ? (isrunning ? CharState_SwimRunLeft : CharState_SwimWalkLeft)
                                         : (sneak ? CharState_SneakLeft
                                                  : (isrunning ? CharState_RunLeft : CharState_WalkLeft)));
            }
            else if (vec.length2() > 0.0f)
            {
                if (vec.y() >= 0.0f)
                    movestate = (inwater ? (isrunning ? CharState_SwimRunForward : CharState_SwimWalkForward)
                                         : (sneak ? CharState_SneakForward
                                                  : (isrunning ? CharState_RunForward : CharState_WalkForward)));
                else
                    movestate = (inwater ? (isrunning ? CharState_SwimRunBack : CharState_SwimWalkBack)
                                         : (sneak ? CharState_SneakBack
                                                  : (isrunning ? CharState_RunBack : CharState_WalkBack)));
            }
            else
            {
                // Do not play turning animation for player if rotation speed is very slow.
                // Actual threshold should take framerate in account.
                float rotationThreshold = (isPlayer ? 0.015f : 0.001f) * 60 * duration;

                // It seems only bipedal actors use turning animations.
                // Also do not use turning animations in the first-person view and when sneaking.
                if (!sneak && jumpstate == JumpState_None && !isFirstPersonPlayer && mPtr.getClass().isBipedal(mPtr))
                {
                    if(effectiveRotation > rotationThreshold)
                        movestate = inwater ? CharState_SwimTurnRight : CharState_TurnRight;
                    else if(effectiveRotation < -rotationThreshold)
                        movestate = inwater ? CharState_SwimTurnLeft : CharState_TurnLeft;
                }
            }
        }

        if (playLandingSound)
        {
            world->emitFootstep(mPtr, true, true);
            MWBase::SoundManager *sndMgr = MWBase::Environment::get().getSoundManager();
            std::string sound;
            osg::Vec3f pos(mPtr.getRefData().getPosition().asVec3());
            if (world->isUnderwater(mPtr.getCell(), pos) || world->isWalkingOnWater(mPtr))
                sound = "DefaultLandWater";
            else if (onground)
                sound = "DefaultLand";

            if (!sound.empty())
                sndMgr->playSound3D(mPtr, sound, 1.f, 1.f, MWSound::Type::Foot, MWSound::PlayMode::NoPlayerLocal);
        }

        if (turnToMovementDirection && !isFirstPersonPlayer &&
            (movestate == CharState_SwimRunForward || movestate == CharState_SwimWalkForward ||
             movestate == CharState_SwimRunBack || movestate == CharState_SwimWalkBack))
        {
            float swimmingPitch = mAnimation->getBodyPitchRadians();
            float targetSwimmingPitch = -mPtr.getRefData().getPosition().rot[0];
            float maxSwimPitchDelta = 3.0f * duration;
            swimmingPitch += osg::clampBetween(targetSwimmingPitch - swimmingPitch, -maxSwimPitchDelta, maxSwimPitchDelta);
            mAnimation->setBodyPitchRadians(swimmingPitch);
        }
        else
            mAnimation->setBodyPitchRadians(0);

        static const bool swimUpwardCorrection = Settings::Manager::getBool("swim upward correction", "Game");
        if (inwater && isPlayer && !isFirstPersonPlayer && swimUpwardCorrection)
        {
            static const float swimUpwardCoef = Settings::Manager::getFloat("swim upward coef", "Game");
            static const float swimForwardCoef = sqrtf(1.0f - swimUpwardCoef * swimUpwardCoef);
            vec.z() = std::abs(vec.y()) * swimUpwardCoef;
            vec.y() *= swimForwardCoef;
        }

        // Player can not use smooth turning as NPCs, so we play turning animation a bit to avoid jittering
        if (isPlayer)
        {
            float threshold = mCurrentMovement.find("swim") == std::string::npos ? 0.4f : 0.8f;
            float complete;
            bool animPlaying = mAnimation->getInfo(mCurrentMovement, &complete);
            if (movestate == CharState_None && jumpstate == JumpState_None && isTurning())
            {
                if (animPlaying && complete < threshold)
                    movestate = mMovementState;
            }
        }
        else
        {
            if (mPtr.getClass().isBipedal(mPtr))
            {
                if (mTurnAnimationThreshold > 0)
                    mTurnAnimationThreshold -= duration;

                if (movestate == CharState_TurnRight || movestate == CharState_TurnLeft ||
                    movestate == CharState_SwimTurnRight || movestate == CharState_SwimTurnLeft)
                {
                    mTurnAnimationThreshold = 0.05f;
                }
                else if (movestate == CharState_None && isTurning()
                        && mTurnAnimationThreshold > 0)
                {
                    movestate = mMovementState;
                }
            }
        }

        // ArenaMP: if a melee actor is already attacking but is physically
        // stationary (typically pressed against a player), do not blend a stale
        // walk/run cycle under the attack. Keep vec untouched so pathing/physics
        // can still move the actor on the very next frame.
        if (suppressMpBlockedMeleeLocomotion && jumpstate == JumpState_None)
            movestate = CharState_None;

        if(movestate != CharState_None && !isTurning())
            clearAnimQueue();

        if(mAnimQueue.empty() || inwater || (sneak && mIdleState != CharState_SpecialIdle))
        {
            if (inwater)
                idlestate = CharState_IdleSwim;
            else if (sneak && !inJump)
                idlestate = CharState_IdleSneak;
            else
                idlestate = CharState_Idle;
        }
        else
            updateAnimQueue();

        if (!mSkipAnim)
        {
            // bipedal means hand-to-hand could be used (which is handled in updateWeaponState). an existing InventoryStore means an actual weapon could be used.
            if(cls.isBipedal(mPtr) || cls.hasInventoryStore(mPtr))
                forcestateupdate = updateWeaponState(idlestate) || forcestateupdate;
            else
                forcestateupdate = updateCreatureState() || forcestateupdate;

            refreshCurrentAnims(idlestate, movestate, jumpstate, forcestateupdate);

            updateIdleStormState(inwater);
        }

        if (inJump)
            mMovementAnimationControlled = false;

        if (isTurning())
        {
            // Adjust animation speed from 1.0 to 1.5 multiplier
            if (duration > 0)
            {
                float turnSpeed = std::min(1.5f, std::abs(rot.z()) / duration / static_cast<float>(osg::PI));
                mAnimation->adjustSpeedMult(mCurrentMovement, std::max(turnSpeed, 1.0f));
            }
        }
        else if (mMovementState != CharState_None && mAdjustMovementAnimSpeed)
        {
            // Vanilla caps the played animation speed.
            const float maxSpeedMult = 10.f;
            const float speedMult = speed / mMovementAnimSpeed;
            mAnimation->adjustSpeedMult(mCurrentMovement, std::min(maxSpeedMult, speedMult));
            // Make sure the actual speed is the "expected" speed even though the animation is slower
            scale *= std::max(1.f, speedMult / maxSpeedMult);
        }

        updateNativeAnimationLayers();
        if (!mSkipAnim)
        {
            if(!isKnockedDown() && !isKnockedOut())
            {
                if (rot != osg::Vec3f())
                    world->rotateObject(mPtr, rot.x(), rot.y(), rot.z(), true);
            }
            else //avoid z-rotating for knockdown
            {
                if (rot.x() != 0 && rot.y() != 0)
                    world->rotateObject(mPtr, rot.x(), rot.y(), 0.0f, true);
            }

            if (!mMovementAnimationControlled)
                world->queueMovement(mPtr, vec);
        }

        movement = vec;
        movementSettings.mPosition[0] = movementSettings.mPosition[1] = 0;
        if (movement.z() == 0.f)
            movementSettings.mPosition[2] = 0;
        // Can't reset jump state (mPosition[2]) here in full; we don't know for sure whether the PhysicSystem will actually handle it in this frame
        // due to the fixed minimum timestep used for the physics update. It will be reset in PhysicSystem::move once the jump is handled.

        if (!mSkipAnim)
            updateHeadTracking(duration);
    }
    else if(cls.getCreatureStats(mPtr).isDead())
    {
        // initial start of death animation for actors that started the game as dead
        // not done in constructor since we need to give scripts a chance to set the mSkipAnim flag
        if (!mSkipAnim && mDeathState != CharState_None && mCurrentDeath.empty())
        {
            // Fast-forward death animation to end for persisting corpses or corpses after end of death animation
            if (cls.isPersistent(mPtr) || cls.getCreatureStats(mPtr).isDeathAnimationFinished())
                playDeath(1.f, mDeathState);
        }
    }

    bool isPersist = isPersistentAnimPlaying();
    osg::Vec3f moved = mAnimation->runAnimation(mSkipAnim && !isPersist ? 0.f : duration);
    if(duration > 0.0f)
        moved /= duration;
    else
        moved = osg::Vec3f(0.f, 0.f, 0.f);

    moved.x() *= scale;
    moved.y() *= scale;

    // Ensure we're moving in generally the right direction...
    if (speed > 0.f && moved != osg::Vec3f())
    {
        float l = moved.length();
        if (std::abs(movement.x() - moved.x()) > std::abs(moved.x()) / 2 ||
            std::abs(movement.y() - moved.y()) > std::abs(moved.y()) / 2 ||
            std::abs(movement.z() - moved.z()) > std::abs(moved.z()) / 2)
        {
            moved = movement;
            // For some creatures getSpeed doesn't work, so we adjust speed to the animation.
            // TODO: Fix Creature::getSpeed.
            float newLength = moved.length();
            if (newLength > 0 && !cls.isNpc())
                moved *= (l / newLength);
        }
    }

    if (mFloatToSurface && cls.isActor())
    {
        if (cls.getCreatureStats(mPtr).isDead()
            || (!godmode && cls.getCreatureStats(mPtr).getMagicEffects().get(ESM::MagicEffect::Paralyze).getModifier() > 0))
        {
            moved.z() = 1.0;
        }
    }

    const bool guarding = cls.isActor() && ActiveGuard::raised(mPtr);
    const std::string guardGroup = guarding ? ActiveGuard::animation(mPtr) : std::string();
    if (guardGroup != mGuardAnimation || (!guardGroup.empty() && !mAnimation->getInfo(guardGroup)))
    {
        if (!mGuardAnimation.empty()) mAnimation->disable(mGuardAnimation);
        mGuardAnimation.clear();
        if (!guardGroup.empty() && mAnimation->hasAnimation(guardGroup))
        {
            // Hold the final pose; locomotion continues on the lower body.
            mAnimation->play(guardGroup, Priority_Block, MWRender::Animation::BlendMask_UpperBody,
                false, 2.f, "start", "stop", 0.f, 0);
            mGuardAnimation = guardGroup;
        }
    }
    if (guarding) { moved.x() *= .7f; moved.y() *= .7f; }

    // Update movement
    if(mMovementAnimationControlled && mPtr.getClass().isActor())
        world->queueMovement(mPtr, moved);

    mSkipAnim = false;

    mAnimation->enableHeadAnimation(cls.isActor() && !cls.getCreatureStats(mPtr).isDead());
}

void CharacterController::persistAnimationState()
{
    ESM::AnimationState& state = mPtr.getRefData().getAnimationState();

    state.mScriptedAnims.clear();
    for (AnimationQueue::const_iterator iter = mAnimQueue.begin(); iter != mAnimQueue.end(); ++iter)
    {
        if (!iter->mPersist)
            continue;

        ESM::AnimationState::ScriptedAnimation anim;
        anim.mGroup = iter->mGroup;

        if (iter == mAnimQueue.begin())
        {
            anim.mLoopCount = mAnimation->getCurrentLoopCount(anim.mGroup);
            float complete;
            mAnimation->getInfo(anim.mGroup, &complete, nullptr);
            anim.mTime = complete;
        }
        else
        {
            anim.mLoopCount = iter->mLoopCount;
            anim.mTime = 0.f;
        }

        state.mScriptedAnims.push_back(anim);
    }
}

void CharacterController::unpersistAnimationState()
{
    const ESM::AnimationState& state = mPtr.getRefData().getAnimationState();

    if (!state.mScriptedAnims.empty())
    {
        clearAnimQueue();
        for (ESM::AnimationState::ScriptedAnimations::const_iterator iter = state.mScriptedAnims.begin(); iter != state.mScriptedAnims.end(); ++iter)
        {
            AnimationQueueEntry entry;
            entry.mGroup = iter->mGroup;
            entry.mLoopCount = iter->mLoopCount;
            entry.mPersist = true;

            mAnimQueue.push_back(entry);
        }

        const ESM::AnimationState::ScriptedAnimation& anim = state.mScriptedAnims.front();
        float complete = anim.mTime;
        if (anim.mAbsolute)
        {
            float start = mAnimation->getTextKeyTime(anim.mGroup+": start");
            float stop = mAnimation->getTextKeyTime(anim.mGroup+": stop");
            float time = std::max(start, std::min(stop, anim.mTime));
            complete = (time - start) / (stop - start);
        }

        mAnimation->disable(mCurrentIdle);
        mCurrentIdle.clear();
        mIdleState = CharState_SpecialIdle;

        bool loopfallback = (mAnimQueue.front().mGroup.compare(0,4,"idle") == 0);
        mAnimation->play(anim.mGroup,
                         Priority_Persistent, MWRender::Animation::BlendMask_All, false, 1.0f,
                         "start", "stop", complete, anim.mLoopCount, loopfallback);
    }
}

bool CharacterController::playGroup(const std::string &groupname, int mode, int count, bool persist)
{
    if(!mAnimation || !mAnimation->hasAnimation(groupname))
        return false;

    // We should not interrupt persistent animations by non-persistent ones
    if (isPersistentAnimPlaying() && !persist)
        return false;

    // If this animation is a looped animation (has a "loop start" key) that is already playing
    // and has not yet reached the end of the loop, allow it to continue animating with its existing loop count
    // and remove any other animations that were queued.
    // This emulates observed behavior from the original allows the script "OutsideBanner" to animate banners correctly.
    if (!mAnimQueue.empty() && mAnimQueue.front().mGroup == groupname &&
        mAnimation->getTextKeyTime(mAnimQueue.front().mGroup + ": loop start") >= 0 &&
        mAnimation->isPlaying(groupname))
    {
        float endOfLoop = mAnimation->getTextKeyTime(mAnimQueue.front().mGroup+": loop stop");

        if (endOfLoop < 0) // if no Loop Stop key was found, use the Stop key
            endOfLoop = mAnimation->getTextKeyTime(mAnimQueue.front().mGroup+": stop");

        if (endOfLoop > 0 && (mAnimation->getCurrentTime(mAnimQueue.front().mGroup) < endOfLoop))
        {
            mAnimQueue.resize(1);
            return true;
        }
    }

    count = std::max(count, 1);

    AnimationQueueEntry entry;
    entry.mGroup = groupname;
    entry.mLoopCount = count-1;
    entry.mPersist = persist;

    if(mode != 0 || mAnimQueue.empty() || !isAnimPlaying(mAnimQueue.front().mGroup))
    {
        clearAnimQueue(persist);

        mAnimation->disable(mCurrentIdle);
        mCurrentIdle.clear();

        mIdleState = CharState_SpecialIdle;
        bool loopfallback = (entry.mGroup.compare(0,4,"idle") == 0);
        mAnimation->play(groupname, persist && groupname != "idle" ? Priority_Persistent : Priority_Default,
                            MWRender::Animation::BlendMask_All, false, 1.0f,
                            ((mode==2) ? "loop start" : "start"), "stop", 0.0f, count-1, loopfallback);

        /*
            Start of tes3mp addition

            If we are the cell authority over this actor, we need to record this new
            animation for it
        */
        if (mwmp::Main::get().getCellController()->isLocalActor(mPtr))
        {
            mwmp::LocalActor *actor = mwmp::Main::get().getCellController()->getLocalActor(mPtr);
            actor->animation.groupname = groupname;
            actor->animation.mode = mode;
            actor->animation.count = count;
            actor->animation.persist = persist;
        }
        /*
            End of tes3mp addition
        */
    }
    else
    {
        mAnimQueue.resize(1);
    }

    // "PlayGroup idle" is a special case, used to remove to stop scripted animations playing
    if (groupname == "idle")
        entry.mPersist = false;

    mAnimQueue.push_back(entry);

    return true;
}

void CharacterController::skipAnim()
{
    mSkipAnim = true;
}

bool CharacterController::isPersistentAnimPlaying()
{
    if (!mAnimQueue.empty())
    {
        AnimationQueueEntry& first = mAnimQueue.front();
        return first.mPersist && isAnimPlaying(first.mGroup);
    }

    return false;
}

bool CharacterController::isAnimPlaying(const std::string &groupName)
{
    if(mAnimation == nullptr)
        return false;
    return mAnimation->isPlaying(groupName);
}

void CharacterController::clearAnimQueue(bool clearPersistAnims)
{
    // Do not interrupt scripted animations, if we want to keep them
    if ((!isPersistentAnimPlaying() || clearPersistAnims) && !mAnimQueue.empty())
        mAnimation->disable(mAnimQueue.front().mGroup);

    for (AnimationQueue::iterator it = mAnimQueue.begin(); it != mAnimQueue.end();)
    {
        if (clearPersistAnims || !it->mPersist)
            it = mAnimQueue.erase(it);
        else
            ++it;
    }
}

void CharacterController::forceStateUpdate()
{
    if(!mAnimation)
        return;
    clearAnimQueue();

    // Make sure we canceled the current attack or spellcasting,
    // because we disabled attack animations anyway.
    mCastingManualSpell = false;
    mAttackingOrSpell = false;
    cancelQuickCast(false);
    if (mUpperBodyState != UpperCharState_Nothing)
        mUpperBodyState = UpperCharState_WeapEquiped;

    refreshCurrentAnims(mIdleState, mMovementState, mJumpState, true);

    if(mDeathState != CharState_None)
    {
        playRandomDeath();
    }

    mAnimation->runAnimation(0.f);
}

CharacterController::KillResult CharacterController::kill()
{
    if (mDeathState == CharState_None)
    {
        playRandomDeath();

        mAnimation->disable(mCurrentIdle);

        mIdleState = CharState_None;
        mCurrentIdle.clear();
        return Result_DeathAnimStarted;
    }

    MWMechanics::CreatureStats& cStats = mPtr.getClass().getCreatureStats(mPtr);
    if (isAnimPlaying(mCurrentDeath))
        return Result_DeathAnimPlaying;
    if (!cStats.isDeathAnimationFinished())
    {
        /*
            Start of tes3mp addition
        */
        if (mwmp::Main::get().getCellController()->isLocalActor(mPtr))
        {
            mwmp::Main::get().getCellController()->getLocalActor(mPtr)->creatureStats.mDeathAnimationFinished = true;
        }
        /*
            End of tes3mp addition
        */

        cStats.setDeathAnimationFinished(true);
        return Result_DeathAnimJustFinished;
    }
    return Result_DeathAnimFinished;
}

void CharacterController::resurrect()
{
    if(mDeathState == CharState_None)
        return;

    if(mAnimation)
        mAnimation->disable(mCurrentDeath);
    mCurrentDeath.clear();
    mDeathState = CharState_None;
    mWeaponType = ESM::Weapon::None;
}

void CharacterController::updateContinuousVfx()
{
    // Keeping track of when to stop a continuous VFX seems to be very difficult to do inside the spells code,
    // as it's extremely spread out (ActiveSpells, Spells, InventoryStore effects, etc...) so we do it here.

    // Stop any effects that are no longer active
    std::vector<int> effects;
    mAnimation->getLoopingEffects(effects);

    for (int effectId : effects)
    {
        if (mPtr.getClass().getCreatureStats(mPtr).isDeathAnimationFinished()
            || mPtr.getClass().getCreatureStats(mPtr).getMagicEffects().get(MWMechanics::EffectKey(effectId)).getMagnitude() <= 0)
            mAnimation->removeEffect(effectId);
    }
}

void CharacterController::updateMagicEffects()
{
    if (!mPtr.getClass().isActor())
        return;

    float light = mPtr.getClass().getCreatureStats(mPtr).getMagicEffects().get(ESM::MagicEffect::Light).getMagnitude();
    mAnimation->setLightEffect(light);

    // If you're dead you don't care about whether you've started/stopped being a vampire or not
    if (mPtr.getClass().getCreatureStats(mPtr).isDead())
        return;

    bool vampire = mPtr.getClass().getCreatureStats(mPtr).getMagicEffects().get(ESM::MagicEffect::Vampirism).getMagnitude() > 0.0f;
    mAnimation->setVampire(vampire);
}

void CharacterController::setVisibility(float visibility)
{
    // We should take actor's invisibility in account
    if (mPtr.getClass().isActor())
    {
        float alpha = 1.f;
        if (mPtr.getClass().getCreatureStats(mPtr).getMagicEffects().get(ESM::MagicEffect::Invisibility).getModifier()) // Ignore base magnitude (see bug #3555).
        {
            if (mPtr == getPlayer())
                alpha = 0.25f;
            else
                alpha = 0.05f;
        }
        float chameleon = mPtr.getClass().getCreatureStats(mPtr).getMagicEffects().get(ESM::MagicEffect::Chameleon).getMagnitude();
        if (chameleon)
        {
            alpha *= std::min(0.75f, std::max(0.25f, (100.f - chameleon)/100.f));
        }

        visibility = std::min(visibility, alpha);
    }

    // TODO: implement a dithering shader rather than just change object transparency.
    mAnimation->setAlpha(visibility);
}

void CharacterController::setAttackTypeBasedOnMovement()
{
    float *move = mPtr.getClass().getMovementSettings(mPtr).mPosition;
    if (std::abs(move[1]) > std::abs(move[0]) + 0.2f) // forward-backward
        mAttackType = "thrust";
    else if (std::abs(move[0]) > std::abs(move[1]) + 0.2f) // sideway
        mAttackType = "slash";
    else
        mAttackType = "chop";
}

bool CharacterController::isRandomAttackAnimation(const std::string& group) const
{
    return (group == "attack1" || group == "swimattack1" ||
            group == "attack2" || group == "swimattack2" ||
            group == "attack3" || group == "swimattack3");
}

bool CharacterController::isAttackPreparing() const
{
    return mUpperBodyState == UpperCharState_StartToMinAttack ||
            mUpperBodyState == UpperCharState_MinAttackToMaxAttack;
}

bool CharacterController::isCastingSpell() const
{
    return mQuickCastActive || mCastingManualSpell || mUpperBodyState == UpperCharState_CastingSpell;
}

bool CharacterController::isReadyToBlock() const
{
    return updateCarriedLeftVisible(mWeaponType);
}

bool CharacterController::isWeaponTransition() const
{
    return mUpperBodyState == UpperCharState_EquipingWeap
        || mUpperBodyState == UpperCharState_UnEquipingWeap;
}

bool CharacterController::isKnockedDown() const
{
    return mHitState == CharState_KnockDown ||
            mHitState == CharState_SwimKnockDown;
}

bool CharacterController::isKnockedOut() const
{
    return mHitState == CharState_KnockOut ||
            mHitState == CharState_SwimKnockOut;
}

bool CharacterController::isTurning() const
{
    return mMovementState == CharState_TurnLeft ||
            mMovementState == CharState_TurnRight ||
            mMovementState == CharState_SwimTurnLeft ||
            mMovementState == CharState_SwimTurnRight;
}

bool CharacterController::isRecovery() const
{
    return mHitState == CharState_Hit ||
            mHitState == CharState_SwimHit;
}

bool CharacterController::isAttackingOrSpell() const
{
    return mSecurityPhase != SecurityPhase::None || mQuickCastActive || (mUpperBodyState != UpperCharState_Nothing &&
            mUpperBodyState != UpperCharState_WeapEquiped);
}

bool CharacterController::isSneaking() const
{
    return mIdleState == CharState_IdleSneak ||
            mMovementState == CharState_SneakForward ||
            mMovementState == CharState_SneakBack ||
            mMovementState == CharState_SneakLeft ||
            mMovementState == CharState_SneakRight;
}

bool CharacterController::isRunning() const
{
    return mMovementState == CharState_RunForward ||
            mMovementState == CharState_RunBack ||
            mMovementState == CharState_RunLeft ||
            mMovementState == CharState_RunRight ||
            mMovementState == CharState_SwimRunForward ||
            mMovementState == CharState_SwimRunBack ||
            mMovementState == CharState_SwimRunLeft ||
            mMovementState == CharState_SwimRunRight;
}

void CharacterController::setAttackingOrSpell(bool attackingOrSpell)
{
    mAttackingOrSpell = attackingOrSpell && !mQuickCastActive && (!mPtr.getClass().isActor()
        || (!ActiveGuard::raised(mPtr) && mPtr.getClass().getCreatureStats(mPtr).mGuard.stagger <= 0.f));
}

void CharacterController::castSpell(const std::string spellId, bool manualSpell)
{
    mAttackingOrSpell = true;
    mCastingManualSpell = manualSpell;
    ActionSpell action = ActionSpell(spellId);
    action.prepare(mPtr);
}

bool CharacterController::startSecurityAction(const MWWorld::Ptr& target, const MWWorld::Ptr& tool,
    const std::string& spellId)
{
    if (mSecurityPhase != SecurityPhase::None) return true;
    if (mPtr != getPlayer() || !mAnimation || target.isEmpty()
        || !mPtr.getClass().hasInventoryStore(mPtr)) return false;
    CreatureStats& stats = mPtr.getClass().getCreatureStats(mPtr);
    if (stats.isDead() || stats.isParalyzed() || isKnockedDown() || isKnockedOut() || isRecovery()
        || ActiveGuard::raised(mPtr) || stats.mGuard.stagger > 0.f || mQuickCastActive
        || (isAttackingOrSpell() && !isWeaponTransition())) return false;

    auto& inv = mPtr.getClass().getInventoryStore(mPtr);
    auto selectedTool = inv.end();
    if (spellId.empty())
    {
        for (auto it = inv.begin(); it != inv.end(); ++it)
            if (*it == tool) { selectedTool = it; break; }
        if (selectedTool == inv.end() || tool.getClass().getItemHealth(tool) <= 0) return false;
    }
    else if (!stats.getSpells().hasSpell(spellId)) return false;

    auto* wm = MWBase::Environment::get().getWindowManager();
    const auto right = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
    mSecurityPreviousRight = right == inv.end() ? MWWorld::Ptr() : *right;
    mSecurityPreviousEnchant = wm->getSelectedEnchantItem();
    mSecurityPreviousSpell = wm->getSelectedSpell();
    mSecurityPreviousCastSpell = stats.getSpells().getSelectedSpell();
    mSecurityPreviousDrawState = static_cast<int>(stats.getDrawState());
    mSecurityTarget = target;
    mSecurityCell = mPtr.getCell();
    mSecurityTool = tool;
    mSecuritySpell = spellId;
    mSecurityElapsed = 0.f;
    mSecurityAnimationStarted = false;
    mAttackingOrSpell = false;
    MWBase::Environment::get().getWorld()->getPlayer().setAttackingOrSpell(false);
    if (spellId.empty())
    {
        mSecurityPhase = SecurityPhase::EquipTool;
        if (mSecurityPreviousRight != tool)
        {
            // Unequipping may merge a thrown-weapon/tool stack. Retain the
            // surviving stack rather than later reviving a zero-count Ptr.
            const auto previous = inv.unequipSlot(MWWorld::InventoryStore::Slot_CarriedRight, mPtr, false);
            if (mSecurityPreviousEnchant == mSecurityPreviousRight)
                mSecurityPreviousEnchant = previous == inv.end() ? MWWorld::Ptr() : *previous;
            mSecurityPreviousRight = previous == inv.end() ? MWWorld::Ptr() : *previous;
            inv.equip(MWWorld::InventoryStore::Slot_CarriedRight, selectedTool, mPtr);
        }
        stats.setDrawState(DrawState_Weapon);
    }
    else
    {
        mSecurityPhase = SecurityPhase::SpellAnimation;
        wm->setSelectedSpell(spellId, static_cast<int>(getSpellSuccessChance(spellId, mPtr)));
        castSpell(spellId, false);
    }
    return true;
}

void CharacterController::updateSecurityAction(float duration)
{
    if (mSecurityPhase == SecurityPhase::None) return;
    auto* world = MWBase::Environment::get().getWorld();
    auto* wm = MWBase::Environment::get().getWindowManager();
    CreatureStats& stats = mPtr.getClass().getCreatureStats(mPtr);
    mSecurityElapsed += std::max(0.f, duration);
    if (mPtr != getPlayer() || mPtr.getCell() != mSecurityCell || stats.isDead() || stats.isParalyzed()
        || isKnockedDown() || isKnockedOut() || isRecovery() || ActiveGuard::raised(mPtr)
        || stats.mGuard.stagger > 0.f || wm->isGuiMode() || mSecurityElapsed > 8.f)
    {
        finishSecurityAction();
        return;
    }
    auto& inv = mPtr.getClass().getInventoryStore(mPtr);
    const auto right = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
    if (mSecurityPhase != SecurityPhase::SpellAnimation
        && (right == inv.end() ? mSecurityTool.getRefData().getCount() > 0 : *right != mSecurityTool))
    {
        finishSecurityAction(false); // respect a manual/server equipment change
        return;
    }
    if (mSecurityPhase == SecurityPhase::SpellAnimation && wm->getSelectedSpell() != mSecuritySpell)
    {
        finishSecurityAction(false);
        return;
    }
    // Check the original object, not whichever door happens to be under the
    // crosshair after the equip delay. A tool is consumed only in PickProbe.
    if (mSecurityPhase != SecurityPhase::ToolAnimation
        && (mSecurityTarget.isEmpty() || mSecurityTarget.getRefData().getCount() <= 0
            || world->getFacedObject() != mSecurityTarget
            || world->getDistanceToFacedObject() > world->getMaxActivationDistance()))
    {
        finishSecurityAction();
        return;
    }
    if (mSecurityPhase == SecurityPhase::EquipTool)
    {
        mAttackingOrSpell = mWeaponType == ESM::Weapon::PickProbe
            && mUpperBodyState == UpperCharState_WeapEquiped;
    }
    else if (mSecurityPhase == SecurityPhase::SpellAnimation && !mSecurityAnimationStarted)
        mAttackingOrSpell = true; // preserve a one-shot request across draw frames
    else
    {
        mAttackingOrSpell = false;
        if (mUpperBodyState <= UpperCharState_WeapEquiped && !mAnimation->isPlaying(mCurrentWeapon))
            finishSecurityAction();
    }
}

void CharacterController::finishSecurityAction(bool restoreDrawState)
{
    if (mSecurityPhase == SecurityPhase::None) return;
    const bool spell = mSecurityPhase == SecurityPhase::SpellAnimation;
    mSecurityPhase = SecurityPhase::None; // equipment callbacks cannot restart it
    auto& inv = mPtr.getClass().getInventoryStore(mPtr);
    auto* wm = MWBase::Environment::get().getWindowManager();
    CreatureStats& stats = mPtr.getClass().getCreatureStats(mPtr);
    auto findItem = [&](const MWWorld::Ptr& item) {
        for (auto it = inv.begin(); it != inv.end(); ++it)
            if (*it == item && it->getRefData().getCount() > 0) return it;
        return inv.end();
    };
    const auto right = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
    const bool ownsRight = !spell && (right == inv.end() || *right == mSecurityTool);
    if (ownsRight)
    {
        const auto previous = findItem(mSecurityPreviousRight);
        if (previous != inv.end())
        {
            // A probe/pick already held before activation stays in its slot.
            // Re-equipping it could restack it and invalidate this iterator.
            if (right == inv.end() || *right != *previous)
                inv.equip(MWWorld::InventoryStore::Slot_CarriedRight, previous, mPtr);
        }
        else inv.unequipSlot(MWWorld::InventoryStore::Slot_CarriedRight, mPtr);
        // Unequipping an enchanted weapon can clear its cast-on-use selection.
        // Restore only when the player has not deliberately selected something else.
        const auto enchanted = findItem(mSecurityPreviousEnchant);
        if (enchanted != inv.end() && wm->getSelectedSpell() == mSecurityPreviousSpell
            && (wm->getSelectedEnchantItem().isEmpty()
                || wm->getSelectedEnchantItem() == mSecurityPreviousEnchant
                || wm->getSelectedEnchantItem().getRefData().getCount() <= 0))
        {
            inv.setSelectedEnchantItem(enchanted);
            wm->setSelectedEnchantItem(*enchanted);
        }
    }
    if (spell && wm->getSelectedSpell() == mSecuritySpell)
    {
        if (!mSecurityPreviousSpell.empty() && stats.getSpells().hasSpell(mSecurityPreviousSpell))
            wm->setSelectedSpell(mSecurityPreviousSpell,
                static_cast<int>(getSpellSuccessChance(mSecurityPreviousSpell, mPtr)));
        else wm->unsetSelectedSpell();
        const auto enchanted = findItem(mSecurityPreviousEnchant);
        if (enchanted != inv.end())
        {
            inv.setSelectedEnchantItem(enchanted);
            wm->setSelectedEnchantItem(*enchanted);
        }
        stats.getSpells().setSelectedSpell(stats.getSpells().hasSpell(mSecurityPreviousCastSpell)
            ? mSecurityPreviousCastSpell : std::string());
    }
    if (mAnimation && (mWeaponType == ESM::Weapon::PickProbe || mWeaponType == ESM::Weapon::Spell))
        mAnimation->disable(mCurrentWeapon);
    mAttackingOrSpell = false;
    mCastingManualSpell = false;
    mUpperBodyState = mWeaponType == ESM::Weapon::None ? UpperCharState_Nothing : UpperCharState_WeapEquiped;
    if (restoreDrawState && (spell || ownsRight))
        stats.setDrawState(static_cast<DrawState_>(mSecurityPreviousDrawState));
    mSecurityTarget = mSecurityTool = mSecurityPreviousRight = mSecurityPreviousEnchant = MWWorld::Ptr();
    mSecurityCell = nullptr;
    mSecuritySpell.clear();
    mSecurityPreviousSpell.clear();
    mSecurityPreviousCastSpell.clear();
}

void CharacterController::cancelQuickCast(bool sendFailure)
{
    if (!mQuickCastActive)
        return;

    if (mAnimation && !mQuickCastGroup.empty())
        mAnimation->disable(mQuickCastGroup);

    if (sendFailure && mQuickCastLocalRelease && !mQuickCastReleased && !mQuickCastIsItem)
    {
        if (mwmp::Cast* localCast = MechanicsHelper::getLocalCast(mPtr))
        {
            localCast->success = false;
            localCast->pressed = false;
            localCast->instant = true;
            localCast->shouldSend = true;
        }
    }

    mQuickCastActive = false;
    mQuickCastReleased = false;
    mQuickCastLocalRelease = false;
    mQuickCastIsItem = false;
    mQuickCastGroup.clear();
    mQuickCastSpellId.clear();
    mQuickCastItemId.clear();
}

bool CharacterController::startQuickCast(const std::string& spellId, bool localRelease)
{
    if (mSecurityPhase != SecurityPhase::None) return false;
    // Y070: an empty spellId is intentional when the local player selected an
    // enchanted item. Resolve it below instead of forcing the vanilla prepared
    // spell stance. Remote ITEM packets keep their existing protocol-809 path.
    if (!mAnimation || mQuickCastActive || isDead() || isKnockedDown() || isKnockedOut() || isRecovery())
        return false;
    if (ActiveGuard::raised(mPtr) || mPtr.getClass().getCreatureStats(mPtr).mGuard.stagger > 0.f
        || mPtr.getClass().getCreatureStats(mPtr).isParalyzed())
        return false;
    if (mPtr.getClass().isNpc() && mPtr.getClass().getNpcStats(mPtr).isWerewolf())
        return false;

    // Y065: isAttackingOrSpell() also reports equip/unequip transitions. Those
    // are harmless for a Priority_Hit torso+left-arm overlay and were the main
    // reason a free left hand would sometimes ignore short R. Real attack/cast
    // states remain exclusive.
    const bool weaponAttackActive = mUpperBodyState >= UpperCharState_StartToMinAttack
        && mUpperBodyState <= UpperCharState_FollowStartToFollowStop;
    if (mQuickCastActive || weaponAttackActive || mUpperBodyState == UpperCharState_CastingSpell)
        return false;
    const DrawState_ drawState = mPtr.getClass().getCreatureStats(mPtr).getDrawState();
    if (localRelease && drawState == DrawState_Spell)
        return false;

    MWBase::World* world = MWBase::Environment::get().getWorld();

    // Local quick-cast is explicitly a left-hand action. A shield/torch/other
    // left-slot item blocks it, as does a drawn two-handed weapon whose second
    // hand is implicit rather than stored in Slot_CarriedLeft. Remote playback
    // follows the received cast event and therefore does not re-run this gate.
    if (localRelease && mPtr.getClass().hasInventoryStore(mPtr))
    {
        MWWorld::InventoryStore& inventory = mPtr.getClass().getInventoryStore(mPtr);
        if (inventory.getSlot(MWWorld::InventoryStore::Slot_CarriedLeft) != inventory.end())
            return false;

        if (mPtr.getClass().getCreatureStats(mPtr).getDrawState() == DrawState_Weapon)
        {
            MWWorld::ContainerStoreIterator right = inventory.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
            if (right != inventory.end() && right->getTypeName() == typeid(ESM::Weapon).name())
            {
                const ESM::WeaponType* weaponType = getWeaponType(right->get<ESM::Weapon>()->mBase->mData.mType);
                if (weaponType && (weaponType->mFlags & ESM::WeaponType::TwoHanded))
                    return false;
            }
        }
    }

    const MWWorld::ESMStore& store = world->getStore();
    const ESM::Spell* spell = spellId.empty() ? nullptr : store.get<ESM::Spell>().search(spellId);
    const ESM::Enchantment* enchantment = nullptr;
    const std::vector<ESM::ENAMstruct>* effects = nullptr;
    std::string sourceId = spellId;
    std::string itemId;
    bool isMagicItem = false;

    if (spell)
    {
        if (spell->mEffects.mList.empty()
            || (spell->mData.mType != ESM::Spell::ST_Spell && spell->mData.mType != ESM::Spell::ST_Power))
            return false;
        effects = &spell->mEffects.mList;
    }
    else
    {
        if (!mPtr.getClass().hasInventoryStore(mPtr))
            return false;
        MWWorld::InventoryStore& inv = mPtr.getClass().getInventoryStore(mPtr);
        MWWorld::ContainerStoreIterator selectedItem = inv.getSelectedEnchantItem();
        if (selectedItem == inv.end())
            return false;
        sourceId = selectedItem->getClass().getEnchantment(*selectedItem);
        enchantment = store.get<ESM::Enchantment>().search(sourceId);
        if (!enchantment || enchantment->mEffects.mList.empty()
            || (enchantment->mData.mType != ESM::Enchantment::WhenUsed
                && enchantment->mData.mType != ESM::Enchantment::CastOnce))
            return false;
        effects = &enchantment->mEffects.mList;
        itemId = selectedItem->getCellRef().getRefId();
        isMagicItem = true;
    }

    Spells& quickSpells = mPtr.getClass().getCreatureStats(mPtr).getSpells();
    const std::string previousSelectedSpell = quickSpells.getSelectedSpell();
    if (isMagicItem)
        quickSpells.setSelectedSpell("");
    else
        quickSpells.setSelectedSpell(spellId);

    if (localRelease && !world->startSpellCast(mPtr))
    {
        quickSpells.setSelectedSpell(previousSelectedSpell);
        return false;
    }

    // Y080: only commit an interrupted transition after cast validation succeeds.
    // Changing just mUpperBodyState strands autoDisable=false unequip layers.
    if (mUpperBodyState == UpperCharState_EquipingWeap || mUpperBodyState == UpperCharState_UnEquipingWeap)
    {
        mAnimation->disable(mCurrentWeapon);
        mAnimation->disable("shield");
        mAnimation->disable(mNativeTail);
        mNativeTail.clear();
        int weaponType = drawState == DrawState_Weapon ? ESM::Weapon::HandToHand
            : drawState == DrawState_Spell ? ESM::Weapon::Spell : ESM::Weapon::None;
        getActiveWeapon(mPtr, &weaponType);
        mWeaponType = weaponType;
        mCurrentWeapon = getWeaponAnimation(mWeaponType);
        mAnimation->setWeaponGroup(mCurrentWeapon,
            getWeaponType(mWeaponType)->mWeaponClass == ESM::WeaponType::Ranged);
        mAnimation->showWeapons(mWeaponType != ESM::Weapon::None && mWeaponType != ESM::Weapon::Spell);
        mAnimation->showCarriedLeft(updateCarriedLeftVisible(mWeaponType));
        mUpperBodyState = mWeaponType == ESM::Weapon::None ? UpperCharState_Nothing : UpperCharState_WeapEquiped;
        mWeaponTransitionElapsed = 0.f;
        // Force the matching idle to be rebuilt on the next controller update.
        mAnimation->disable(mCurrentIdle);
        mIdleState = CharState_None;
    }

    // Protocol 809 has no pressed/released fields for Cast::ITEM, so only normal
    // spells send the early visual packet. Enchanted items are sent through the
    // existing ITEM packet on the release key without changing the wire format.
    if (localRelease && !isMagicItem)
    {
        if (mwmp::Cast* localCast = MechanicsHelper::getLocalCast(mPtr))
        {
            MechanicsHelper::resetCast(localCast);
            localCast->type = mwmp::Cast::REGULAR;
            localCast->spellId = spellId;
            localCast->pressed = true;
            localCast->instant = true;
            localCast->shouldSend = true;
        }
    }
    quickSpells.setSelectedSpell(previousSelectedSpell);

    MWMechanics::CastSpell cast(mPtr, nullptr, false, false);
    cast.playSpellCastingEffects(sourceId, isMagicItem);

    // Quick magic is a left-hand cast. This also removes the old mirrored/two-hand
    // look where the same VFX was attached to both hands.
    const ESM::MagicEffect* handEffect = store.get<ESM::MagicEffect>().search(effects->back().mEffectID);
    const ESM::Static* castStatic = store.get<ESM::Static>().search("VFX_Hands");
    if (handEffect && castStatic && mAnimation->getNode("Bip01 L Hand"))
        mAnimation->addEffect("meshes\\" + castStatic->mModel, -1, false, "Bip01 L Hand", handEffect->mParticle);

    const ESM::ENAMstruct& firstEffect = effects->front();
    std::string group = firstEffect.mRange == ESM::RT_Self ? "quickbuff" : "quickcast";
    const ESM::MagicEffect* firstMagic = store.get<ESM::MagicEffect>().search(firstEffect.mEffectID);
    if (firstMagic)
    {
        const int school = firstMagic->mData.mSchool;
        if (firstEffect.mRange == ESM::RT_Touch)
            group = (school == 2 || school == 5) ? "qcdrain" : "qctouch";
        else if (school == 1)
            group = "qcconj";
        else if (school == 3)
            group = "qcill";
        else if (school == 0 && firstEffect.mRange == ESM::RT_Target)
            group = "qcalt";
        else if (school == 4 && firstEffect.mRange == ESM::RT_Self)
            group = "qcsnap";
    }
    if (!mAnimation->hasAnimation(group))
        group = "quickcast";

    // Y062: normal third person intentionally does not load the OSSC full-body
    // quickcast KF. It contains Toe0/lower-body tracks that do not match the
    // standard xbase_anim skeleton. Use the dedicated third-person magic source
    // (SpellCast from thirdperson_alt/xmagic.kf) instead. The same torso+left-arm
    // blend mask keeps quick magic one-handed and does not disturb locomotion.
    if (!mAnimation->hasAnimation(group) && mAnimation->hasAnimation("spellcast"))
        group = "spellcast";

    if (!mAnimation->hasAnimation(group))
    {
        // Keep the mechanic usable even when no casting animation is available.
        if (localRelease)
            world->castSpell(mPtr, false);
        return true;
    }

    switch (firstEffect.mRange)
    {
        case ESM::RT_Self: mAttackType = "self"; break;
        case ESM::RT_Touch: mAttackType = "touch"; break;
        case ESM::RT_Target: mAttackType = "target"; break;
        default: mAttackType = "target"; break;
    }

    mAttackingOrSpell = false;
    mQuickCastActive = true;
    mQuickCastReleased = false;
    mQuickCastLocalRelease = localRelease;
    mQuickCastIsItem = isMagicItem;
    mQuickCastGroup = group;
    mQuickCastSpellId = sourceId;
    mQuickCastItemId = itemId;

    std::string quickStart = "start";
    std::string quickStop = "stop";
    if (group == "spellcast")
    {
        quickStart = mAttackType + " start";
        quickStop = mAttackType + " stop";
    }

    MWRender::Animation::AnimPriority quickCastPriority(Priority_Hit);
    mAnimation->play(group, quickCastPriority,
        MWRender::Animation::BlendMask_Torso | MWRender::Animation::BlendMask_LeftArm, true,
        1.f, quickStart, quickStop, 0.f, 0);
    return true;
}

void CharacterController::setAIAttackType(const std::string& attackType)
{
    mAttackType = attackType;
}

void CharacterController::setAttackTypeRandomly(std::string& attackType)
{
    float random = Misc::Rng::rollProbability();
    if (random >= 2/3.f)
        attackType = "thrust";
    else if (random >= 1/3.f)
        attackType = "slash";
    else
        attackType = "chop";
}

bool CharacterController::readyToPrepareAttack() const
{
    return (mHitState == CharState_None || mHitState == CharState_Block)
            && mUpperBodyState <= UpperCharState_WeapEquiped;
}

bool CharacterController::readyToStartAttack() const
{
    if (mHitState != CharState_None && mHitState != CharState_Block)
        return false;

    if (mPtr.getClass().hasInventoryStore(mPtr) || mPtr.getClass().isBipedal(mPtr))
        return mUpperBodyState == UpperCharState_WeapEquiped;
    else
        return mUpperBodyState == UpperCharState_Nothing;
}

float CharacterController::getAttackStrength() const
{
    return mAttackStrength;
}

/*
    Start of tes3mp addition

    Make it possible to get the current attack type from elsewhere in the code
*/
std::string CharacterController::getAttackType() const
{
    return mAttackType;
}
/*
    End of tes3mp addition
*/

void CharacterController::setActive(int active)
{
    mAnimation->setActive(active);
}

void CharacterController::setHeadTrackTarget(const MWWorld::ConstPtr &target)
{
    mHeadTrackTarget = target;
}

void CharacterController::playSwishSound(float attackStrength)
{
    // ArenaMP: native Combat Sounds Overhaul compatibility.  When CSO sound files
    // are present, replace the vanilla swish at the exact release point.  With no
    // CSO assets installed this returns false and the original sound path is kept.
    if (CombatSoundManager::playSwing(mPtr, mWeapon, mAttackType, attackStrength))
        return;

    MWBase::SoundManager *sndMgr = MWBase::Environment::get().getSoundManager();

    std::string sound = "Weapon Swish";
    if(attackStrength < 0.5f)
        sndMgr->playSound3D(mPtr, sound, 1.0f, 0.8f); //Weak attack
    else if(attackStrength < 1.0f)
        sndMgr->playSound3D(mPtr, sound, 1.0f, 1.0f); //Medium attack
    else
        sndMgr->playSound3D(mPtr, sound, 1.0f, 1.2f); //Strong attack
}

void CharacterController::updateHeadTracking(float duration)
{
    const osg::Node* head = mAnimation->getNode("Bip01 Head");
    if (!head)
        return;

    double zAngleRadians = 0.f;
    double xAngleRadians = 0.f;

    if (!mHeadTrackTarget.isEmpty())
    {
        osg::NodePathList nodepaths = head->getParentalNodePaths();
        if (nodepaths.empty())
            return;
        osg::Matrixf mat = osg::computeLocalToWorld(nodepaths[0]);
        osg::Vec3f headPos = mat.getTrans();

        osg::Vec3f direction;
        if (const MWRender::Animation* anim = MWBase::Environment::get().getWorld()->getAnimation(mHeadTrackTarget))
        {
            const osg::Node* node = anim->getNode("Head");
            if (node == nullptr)
                node = anim->getNode("Bip01 Head");
            if (node != nullptr)
            {
                nodepaths = node->getParentalNodePaths();
                if (!nodepaths.empty())
                    direction = osg::computeLocalToWorld(nodepaths[0]).getTrans() - headPos;
            }
            else
                // no head node to look at, fall back to look at center of collision box
                direction = MWBase::Environment::get().getWorld()->aimToTarget(mPtr, mHeadTrackTarget, false);
        }
        direction.normalize();

        if (!mPtr.getRefData().getBaseNode())
            return;
        const osg::Vec3f actorDirection = mPtr.getRefData().getBaseNode()->getAttitude() * osg::Vec3f(0,1,0);

        zAngleRadians = std::atan2(actorDirection.x(), actorDirection.y()) - std::atan2(direction.x(), direction.y());
        zAngleRadians = Misc::normalizeAngle(zAngleRadians - mAnimation->getHeadYaw()) + mAnimation->getHeadYaw();
        zAngleRadians *= (1 - direction.z() * direction.z());
        xAngleRadians = std::asin(direction.z());
    }

    const double xLimit = osg::DegreesToRadians(40.0);
    const double zLimit = osg::DegreesToRadians(30.0);
    double zLimitOffset = mAnimation->getUpperBodyYawRadians();
    xAngleRadians = osg::clampBetween(xAngleRadians, -xLimit, xLimit);
    zAngleRadians = osg::clampBetween(zAngleRadians, -zLimit + zLimitOffset, zLimit + zLimitOffset);

    float factor = duration*5;
    factor = std::min(factor, 1.f);
    xAngleRadians = (1.f-factor) * mAnimation->getHeadPitch() + factor * xAngleRadians;
    zAngleRadians = (1.f-factor) * mAnimation->getHeadYaw() + factor * zAngleRadians;

    mAnimation->setHeadPitch(xAngleRadians);
    mAnimation->setHeadYaw(zAngleRadians);
}

}
