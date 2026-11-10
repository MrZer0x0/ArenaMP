#include <components/openmw-mp/TimedLog.hpp>
#include <algorithm>
#include <cmath>

#include "../mwbase/environment.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwbase/windowmanager.hpp"

#include "../mwdialogue/dialoguemanagerimp.hpp"

#include "../mwmechanics/aiactivate.hpp"
#include "../mwmechanics/aicombat.hpp"
#include "../mwmechanics/aiescort.hpp"
#include "../mwmechanics/aifollow.hpp"
#include "../mwmechanics/aitravel.hpp"
#include "../mwmechanics/aiwander.hpp"

#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/mechanicsmanagerimp.hpp"
#include "../mwmechanics/movement.hpp"

#include "../mwrender/animation.hpp"

#include "../mwworld/action.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwworld/worldimp.hpp"

#include "DedicatedActor.hpp"
#include "Main.hpp"
#include "CellController.hpp"
#include "MechanicsHelper.hpp"
#include "InteractionAnimationSync.hpp"

namespace
{
    bool interactionAnimationIsBlocked(const MWWorld::Ptr& ptr)
    {
        if (ptr.isEmpty())
            return true;

        const MWMechanics::CreatureStats& stats = ptr.getClass().getCreatureStats(ptr);
        const MWMechanics::Movement& movement = ptr.getClass().getMovementSettings(ptr);
        const bool moving = std::abs(movement.mPosition[0]) > 0.05f
            || std::abs(movement.mPosition[1]) > 0.05f
            || std::abs(movement.mPosition[2]) > 0.05f;

        return moving || stats.isDead() || stats.getKnockedDown()
            || stats.getAiSequence().isInCombat()
            || stats.getDrawState() != MWMechanics::DrawState_Nothing
            || MWBase::Environment::get().getWorld()->isSwimming(ptr);
    }
}

using namespace mwmp;

DedicatedActor::DedicatedActor()
{
    drawState = MWMechanics::DrawState_::DrawState_Nothing;
    movementFlags = 0;
    animation.groupname = "";
    sound = "";

    hasPositionData = false;
    hasStatsDynamicData = false;
    hasReceivedInitialEquipment = false;
    hasChangedCell = true;

    attack.pressed = false;
    cast.pressed = false;

    mInteractionAnimationActive = false;
}

DedicatedActor::~DedicatedActor()
{
    cancelInteractionAnimation();
}

void DedicatedActor::update(float dt)
{
    // Never skip a whole remote-actor update because of a slow frame. Doing so
    // makes non-authority NPCs visibly run in slow motion and compounds the lag
    // after a hitch. move() clamps its interpolation factor and snaps when needed.
    if (dt > 0.f)
        move(dt);

    setAnimFlags();
    updateInteractionAnimation();
    setStatsDynamic();

    // X034: ActorAI can arrive in the same burst as ActorCellChange while its
    // target is still being instantiated in this cell. Retry resolution instead
    // of permanently dropping combat and waiting for the next hit to wake it.
    if (mAiResolveRetry > 0.f)
    {
        mAiResolveRetry -= std::max(0.f, dt);
        if (mAiResolveRetry <= 0.f)
        {
            mAiResolveRetry = 0.f;
            setAi();
        }
    }
}

void DedicatedActor::setCell(MWWorld::CellStore *cellStore)
{
    MWBase::World *world = MWBase::Environment::get().getWorld();

    ptr = world->moveObject(ptr, cellStore, position.pos[0], position.pos[1], position.pos[2]);
    setMovementSettings();
    // moveObject can bind a recreated original reference on the return leg.
    setStatsDynamic();

    hasChangedCell = true;
}

void DedicatedActor::move(float dt)
{
    ESM::Position refPos = ptr.getRefData().getPosition();
    MWBase::World *world = MWBase::Environment::get().getWorld();

    const float dx = position.pos[0] - refPos.pos[0];
    const float dy = position.pos[1] - refPos.pos[1];
    const float dz = position.pos[2] - refPos.pos[2];
    const float distanceSquared = dx * dx + dy * dy + dz * dz;
    const float maxInterpolationDistance = 96.f;
    const bool shouldInterpolate = distanceSquared < maxInterpolationDistance * maxInterpolationDistance;

    // Don't interpolate immediately after a cell transition. For normal packets
    // use a clamped catch-up factor: a 100+ ms frame becomes a snap to the latest
    // authoritative sample instead of a skipped frame/freeze.
    if (shouldInterpolate && !hasChangedCell)
    {
        const float interpolation = std::min(1.f, std::max(0.f, dt * 22.f));

        if (distanceSquared < 0.25f || interpolation >= 1.f)
        {
            refPos.pos[0] = position.pos[0];
            refPos.pos[1] = position.pos[1];
            refPos.pos[2] = position.pos[2];
        }
        else
        {
            const osg::Vec3f lerp = MechanicsHelper::getLinearInterpolation(
                refPos.asVec3(), position.asVec3(), interpolation);
            refPos.pos[0] = lerp.x();
            refPos.pos[1] = lerp.y();
            refPos.pos[2] = lerp.z();
        }

        world->moveObject(ptr, refPos.pos[0], refPos.pos[1], refPos.pos[2]);
    }
    else
    {
        setPosition();
        hasChangedCell = false;
    }

    // Movement animation comes only from the authoritative CharacterController
    // direction. Do not synthesize walking from interpolation residuals: that was
    // the cause of legs continuing to walk after the authority had already sent
    // a stop vector.
    setMovementSettings();
    world->rotateObject(ptr, position.rot[0], position.rot[1], position.rot[2]);
}

void DedicatedActor::setMovementSettings()
{
    MWMechanics::Movement *move = &ptr.getClass().getMovementSettings(ptr);
    move->mPosition[0] = direction.pos[0];
    move->mPosition[1] = direction.pos[1];
    move->mPosition[2] = direction.pos[2];

    // Make sure the values are valid, or we'll get an infinite error loop
    if (!isnan(direction.rot[0]) && !isnan(direction.rot[1]) && !isnan(direction.rot[2]))
    {
        move->mRotation[0] = direction.rot[0];
        move->mRotation[1] = direction.rot[1];
        move->mRotation[2] = direction.rot[2];
    }
}

void DedicatedActor::setPosition()
{
    MWBase::World *world = MWBase::Environment::get().getWorld();
    world->moveObject(ptr, position.pos[0], position.pos[1], position.pos[2]);
}

void DedicatedActor::setAnimFlags()
{
    using namespace MWMechanics;

    MWMechanics::CreatureStats *ptrCreatureStats = &ptr.getClass().getCreatureStats(ptr);

    ptrCreatureStats->setDrawState(static_cast<MWMechanics::DrawState_>(drawState));

    const bool guardHeld = (movementFlags & CreatureStats::Flag_ActiveGuard) != 0
        && drawState == MWMechanics::DrawState_Weapon;
    if (guardHeld && !ptrCreatureStats->getMovementFlag(CreatureStats::Flag_ActiveGuard))
    {
        ptrCreatureStats->mGuard.age = 0.f;
        ptrCreatureStats->mGuard.previous = false;
    }
    ptrCreatureStats->setMovementFlag(CreatureStats::Flag_ActiveGuard, guardHeld);
    ptrCreatureStats->setMovementFlag(CreatureStats::Flag_Run, (movementFlags & CreatureStats::Flag_Run) != 0);
    ptrCreatureStats->setMovementFlag(CreatureStats::Flag_Sneak, (movementFlags & CreatureStats::Flag_Sneak) != 0);
    ptrCreatureStats->setMovementFlag(CreatureStats::Flag_ForceJump, (movementFlags & CreatureStats::Flag_ForceJump) != 0);
    ptrCreatureStats->setMovementFlag(CreatureStats::Flag_ForceMoveJump, (movementFlags & CreatureStats::Flag_ForceMoveJump) != 0);
}

void DedicatedActor::setStatsDynamic()
{
    // Only set dynamic stats if we have received at least one packet about them
    if (!hasStatsDynamicData) return;

    MWMechanics::CreatureStats *ptrCreatureStats = &ptr.getClass().getCreatureStats(ptr);
    MWMechanics::DynamicStat<float> value;

    // Resurrect this Actor if it's not supposed to be dead according to its authority
    if (creatureStats.mDynamic[0].mCurrent > 0)
        MWBase::Environment::get().getMechanicsManager()->resurrect(ptr);

    for (int i = 0; i < 3; ++i)
    {
        value.readState(creatureStats.mDynamic[i]);
        ptrCreatureStats->setDynamic(i, value);
    }
}

void DedicatedActor::setEquipment()
{
    if (!ptr.getClass().hasInventoryStore(ptr))
        return;

    MWWorld::InventoryStore& invStore = ptr.getClass().getInventoryStore(ptr);

    // Y020: ActorEquipment is a snapshot of what is equipped, not an instruction
    // to create a new inventory object whenever durability/charge changed.  On an
    // authority hand-off a DedicatedActor may have an empty equipment slot while
    // the same refId is already present in its container with an older charge.
    // The old charge-sensitive hasItem() test then added another copy.  Repeated
    // hand-offs made those copies become real corpse loot when the authority sent
    // its container back to the server.
    auto findByRefId = [&](const std::string& refId, int preferredCharge)
    {
        MWWorld::ContainerStoreIterator fallback = invStore.end();

        for (MWWorld::ContainerStoreIterator item = invStore.begin(); item != invStore.end(); ++item)
        {
            if (!Misc::StringUtils::ciEqual(item->getCellRef().getRefId(), refId))
                continue;

            if (fallback == invStore.end())
                fallback = item;

            if (item->getCellRef().getCharge() == preferredCharge)
                return item;
        }

        return fallback;
    };

    for (int slot = 0; slot < MWWorld::InventoryStore::Slots; ++slot)
    {
        const int count = equipmentItems[slot].count;

        // If we've somehow received a corrupted item with a count lower than 0, ignore it
        if (count < 0)
            continue;

        MWWorld::ContainerStoreIterator equipped = invStore.getSlot(slot);

        const std::string& packetRefId = equipmentItems[slot].refId;
        const int packetCharge = equipmentItems[slot].charge;
        const float packetEnchantmentCharge = equipmentItems[slot].enchantmentCharge;
        bool equal = false;

        if (equipped != invStore.end())
        {
            const std::string storeRefId = equipped->getCellRef().getRefId();

            if (!Misc::StringUtils::ciEqual(storeRefId, packetRefId))
                invStore.unequipSlot(slot, ptr);
            else
            {
                // Same equipped object: update mutable state in-place.  Durability
                // and enchantment charge changes must not create a second object.
                equal = true;
                equipped->getCellRef().setCharge(packetCharge);
                equipped->getCellRef().setEnchantmentCharge(packetEnchantmentCharge);

                if (!equipmentItems[slot].poisonId.empty() && equipmentItems[slot].poisonCharges > 0)
                    equipped->getRefData().setPoison(equipmentItems[slot].poisonId, equipmentItems[slot].poisonCharges);
                else
                    equipped->getRefData().clearPoison();
            }
        }

        if (packetRefId.empty() || equal)
            continue;

        // Reuse an existing copy by refId even when its old condition differs
        // from the authoritative packet.  Only create an item if this actor truly
        // has no such refId in its inventory.
        MWWorld::ContainerStoreIterator item = findByRefId(packetRefId, packetCharge);
        if (item == invStore.end())
            item = ptr.getClass().getContainerStore(ptr).add(packetRefId, count, ptr);

        if (item != invStore.end())
        {
            item->getCellRef().setCharge(packetCharge);
            item->getCellRef().setEnchantmentCharge(packetEnchantmentCharge);

            if (!equipmentItems[slot].poisonId.empty() && equipmentItems[slot].poisonCharges > 0)
                item->getRefData().setPoison(equipmentItems[slot].poisonId, equipmentItems[slot].poisonCharges);
            else
                item->getRefData().clearPoison();
        }

        // The candidate now carries the packet charge, so the existing helper
        // resolves and equips that exact object without adding anything else.
        equipItem(packetRefId, packetCharge, !hasReceivedInitialEquipment);
    }

    hasReceivedInitialEquipment = true;
}

void DedicatedActor::setAi()
{
    if (ptr.isEmpty())
        return;

    // X044: never act on AI state that was never actually received. aiAction
    // defaults to CANCEL(0), and CANCEL clears the whole AiSequence, so an
    // unconditional setAi() (for example from prepareDedicatedActorsForAuthority)
    // silently deleted the Wander/Travel packages of every NPC in the cell and
    // left them standing still forever.
    if (!hasReceivedAi)
        return;

    MWMechanics::CreatureStats& creatureStats = ptr.getClass().getCreatureStats(ptr);
    MWMechanics::AiSequence& sequence = creatureStats.getAiSequence();
    creatureStats.setAiSetting(MWMechanics::CreatureStats::AI_Fight, 0);

    // Y092: Follow-no-home. Clear only the return-home state, not quest AI.
    const bool following = aiAction == BaseActorList::FOLLOW || aiAction == BaseActorList::ESCORT
        || sequence.hasPackage(MWMechanics::AiPackageTypeId::Follow)
        || sequence.hasPackage(MWMechanics::AiPackageTypeId::Escort);
    if (following)
    {
        aiHasReturnHome = false;
        sequence.clearReturnHomeState();
    }

    auto applyReturnHome = [&]()
    {
        if (!aiHasReturnHome)
            return;
        MWMechanics::AiReturnHomeState state;
        state.mHomeCell = aiHomeCell;
        state.mHomePosition = aiHomePosition;
        for (const ActorAiDoorBreadcrumb& src : aiDoorBreadcrumbs)
        {
            MWMechanics::AiReturnHomeState::DoorBreadcrumb dst;
            dst.mFromCell = src.fromCell;
            dst.mFromPosition = src.fromPosition;
            dst.mToCell = src.toCell;
            dst.mToPosition = src.toPosition;
            state.mDoorBreadcrumbs.push_back(dst);
        }
        sequence.restoreReturnHomeState(state, ptr);
    };

    LOG_APPEND(TimedLog::LOG_VERBOSE, "- actor cellRef: %s %i-%i",
        ptr.getCellRef().getRefId().c_str(), ptr.getCellRef().getRefNum().mIndex, ptr.getCellRef().getMpNum());

    if (aiAction == BaseActorList::COMBAT_END)
    {
        LOG_APPEND(TimedLog::LOG_VERBOSE, "-- Ending synchronized combat state");
        sequence.stopCombat();
        sequence.stopPursuit();
        if (aiHasReturnHome)
            applyReturnHome();
        else
            sequence.clearReturnHomeState();
        MWMechanics::Movement& movement = ptr.getClass().getMovementSettings(ptr);
        movement.mPosition[0] = 0.f;
        movement.mPosition[1] = 0.f;
        return;
    }

    if (aiAction == BaseActorList::CANCEL)
    {
        LOG_APPEND(TimedLog::LOG_VERBOSE, "-- Cancelling AI sequence");
        sequence.clear();
        return;
    }

    if (aiHasReturnHome)
        applyReturnHome();

    if (aiAction == BaseActorList::TRAVEL)
    {
        if (following)
            return; // Y092: a late watchdog/move-aside order cannot replace Follow.
        LOG_APPEND(TimedLog::LOG_VERBOSE, "-- Travelling home to %f, %f, %f",
            aiCoordinates.pos[0], aiCoordinates.pos[1], aiCoordinates.pos[2]);
        if (!aiHasReturnHome)
        {
            MWMechanics::AiTravel package(aiCoordinates.pos[0], aiCoordinates.pos[1], aiCoordinates.pos[2]);
            sequence.stack(package, ptr, true);
        }
        return;
    }

    if (aiAction == BaseActorList::WANDER)
    {
        std::vector<unsigned char> idleList;
        MWMechanics::AiWander package(aiDistance, aiDuration, -1, idleList, aiShouldRepeat);
        sequence.stack(package, ptr, true);
        return;
    }

    auto resolveTarget = [](const Target& target) -> MWWorld::Ptr
    {
        if (target.isPlayer)
            return MechanicsHelper::getPlayerPtr(target);
        if (Main::get().getCellController()->isLocalActor(target.refNum, target.mpNum))
            return Main::get().getCellController()->getLocalActor(target.refNum, target.mpNum)->getPtr();
        if (Main::get().getCellController()->isDedicatedActor(target.refNum, target.mpNum))
            return Main::get().getCellController()->getDedicatedActor(target.refNum, target.mpNum)->getPtr();
        return MWBase::Environment::get().getWorld()->searchPtrViaUniqueIndex(target.refNum, target.mpNum);
    };

    if (aiAction == BaseActorList::COMBAT)
    {
        // Heartbeats are complete snapshots, so stale combat packages are removed
        // before recreating the authoritative target set.
        sequence.stopCombat();
        std::vector<Target> targets = aiCombatTargets;
        if (targets.empty() && hasAiTarget)
            targets.push_back(aiTarget);

        bool unresolvedTarget = false;
        // AiSequence inserts equal-priority Combat packages before existing ones.
        // Iterate backwards so targets[0] (the authority's current focus) is
        // inserted last and remains the active combat package after hand-off.
        for (auto it = targets.rbegin(); it != targets.rend(); ++it)
        {
            MWWorld::Ptr targetPtr = resolveTarget(*it);
            if (targetPtr.isEmpty())
            {
                unresolvedTarget = true;
                continue;
            }
            if (!targetPtr.getClass().getCreatureStats(targetPtr).isDead())
                sequence.stack(MWMechanics::AiCombat(targetPtr), ptr, false);
        }
        // Retry while any advertised target is still loading, even if another
        // target already resolved. This is essential for 2+ player cells.
        mAiResolveRetry = unresolvedTarget ? 0.5f : 0.f;
        return;
    }

    if (!hasAiTarget)
        return;

    MWWorld::Ptr targetPtr = resolveTarget(aiTarget);
    if (targetPtr.isEmpty())
        return;

    if (aiAction == BaseActorList::ACTIVATE)
        sequence.stack(MWMechanics::AiActivate(targetPtr), ptr, true);
    else if (aiAction == BaseActorList::ESCORT)
        sequence.stack(MWMechanics::AiEscort(targetPtr.getCellRef().getRefId(), aiDuration,
            aiCoordinates.pos[0], aiCoordinates.pos[1], aiCoordinates.pos[2]), ptr, true);
    else if (aiAction == BaseActorList::FOLLOW)
    {
        MWMechanics::AiFollow package(targetPtr);
        package.allowAnyDistance(true);
        sequence.stack(package, ptr, true);
    }
}

void DedicatedActor::playAnimation()
{
    if (!animation.groupname.empty())
    {
        std::string consumableRefId;
        if (decodeConsumableAnimation(animation.groupname, consumableRefId))
        {
            mwmp::playConsumableAnimation(ptr, consumableRefId);
            animation.groupname.clear();
            return;
        }

        std::string ambientConsumableRefId;
        if (decodeAmbientConsumableAnimation(animation.groupname, ambientConsumableRefId))
        {
            mwmp::playAmbientConsumableAnimation(ptr, ambientConsumableRefId);
            animation.groupname.clear();
            return;
        }

        InteractionAnimationData interactionData;
        if (decodeInteractionAnimation(animation.groupname, interactionData))
        {
            if (interactionData.stop)
            {
                if (mInteractionAnimationActive)
                    cancelInteractionAnimation();
                else
                    stopInteractionAnimation(ptr, interactionData);
            }
            else if (!interactionAnimationIsBlocked(ptr))
            {
                cancelInteractionAnimation();
                mInteractionAnimation = interactionData;
                mInteractionAnimationActive = playInteractionAnimation(ptr, mInteractionAnimation);
            }
            animation.groupname.clear();
            return;
        }

        MWBase::Environment::get().getMechanicsManager()->playAnimationGroup(ptr,
            animation.groupname, animation.mode, animation.count, animation.persist);

        animation.groupname.clear();
    }
}

void DedicatedActor::cancelInteractionAnimation()
{
    if (!mInteractionAnimationActive)
        return;

    stopInteractionAnimation(ptr, mInteractionAnimation);
    mInteractionAnimation = InteractionAnimationData();
    mInteractionAnimationActive = false;
}

void DedicatedActor::updateInteractionAnimation()
{
    if (!mInteractionAnimationActive)
        return;

    // Network interpolation may begin before the authority's reliable stop
    // packet arrives. Cancel locally on the first moving frame so a cosmetic
    // ArmsFolded/ArmsAtBack/gesture layer can never suppress locomotion.
    if (interactionAnimationIsBlocked(ptr))
    {
        cancelInteractionAnimation();
        return;
    }

    ensureInteractionAnimationProp(ptr, mInteractionAnimation);
}

void DedicatedActor::playSound()
{
    if (!sound.empty())
    {
        MWBase::Environment::get().getSoundManager()->say(ptr, sound);

        MWBase::WindowManager *winMgr = MWBase::Environment::get().getWindowManager();
        if (winMgr->getSubtitlesEnabled()
            && !winMgr->containsMode(MWGui::GM_Dialogue))
            winMgr->messageBox(MWBase::Environment::get().getDialogueManager()->getVoiceCaption(sound), MWGui::ShowInDialogueMode_Never);

        sound.clear();
    }
}

bool DedicatedActor::hasItem(std::string itemId, int charge)
{
    for (const auto &itemPtr : ptr.getClass().getInventoryStore(ptr))
    {
        if (::Misc::StringUtils::ciEqual(itemPtr.getCellRef().getRefId(), itemId) && itemPtr.getCellRef().getCharge() == charge)
            return true;
    }

    return false;
}

void DedicatedActor::equipItem(std::string itemId, int charge, bool noSound)
{
    for (const auto &itemPtr : ptr.getClass().getInventoryStore(ptr))
    {
        if (::Misc::StringUtils::ciEqual(itemPtr.getCellRef().getRefId(), itemId) && itemPtr.getCellRef().getCharge() == charge)
        {
            std::shared_ptr<MWWorld::Action> action = itemPtr.getClass().use(itemPtr);
            action->execute(ptr, noSound);
            break;
        }
    }
}

void DedicatedActor::addSpellsActive()
{
    MWMechanics::ActiveSpells& activeSpells = getPtr().getClass().getCreatureStats(getPtr()).getActiveSpells();

    for (const auto& activeSpell : spellsActiveChanges.activeSpells)
    {
        MWWorld::TimeStamp timestamp = MWWorld::TimeStamp(activeSpell.timestampHour, activeSpell.timestampDay);
        int casterActorId = MechanicsHelper::getActorId(activeSpell.caster);

        MechanicsHelper::createSpellGfx(getPtr(), activeSpell.params.mEffects);

        // Don't do a check for a spell's existence, because active effects from potions need to be applied here too
        activeSpells.addSpell(activeSpell.id, activeSpell.isStackingSpell, activeSpell.params.mEffects, activeSpell.params.mDisplayName, casterActorId, timestamp, false);
    }
}

void DedicatedActor::removeSpellsActive()
{
    MWMechanics::ActiveSpells& activeSpells = getPtr().getClass().getCreatureStats(getPtr()).getActiveSpells();

    for (const auto& activeSpell : spellsActiveChanges.activeSpells)
    {
        // Remove stacking spells based on their timestamps
        if (activeSpell.isStackingSpell)
        {
            MWWorld::TimeStamp timestamp = MWWorld::TimeStamp(activeSpell.timestampHour, activeSpell.timestampDay);
            activeSpells.removeSpellByTimestamp(activeSpell.id, timestamp);
        }
        else
        {
            activeSpells.removeEffects(activeSpell.id);
        }
    }
}

void DedicatedActor::setSpellsActive()
{
    MWMechanics::ActiveSpells& activeSpells = getPtr().getClass().getCreatureStats(getPtr()).getActiveSpells();
    activeSpells.clear();

    // Proceed by adding spells active
    addSpellsActive();
}

MWWorld::Ptr DedicatedActor::getPtr()
{
    return ptr;
}

void DedicatedActor::setPtr(const MWWorld::Ptr& newPtr)
{
    ptr = newPtr;

    refId = ptr.getCellRef().getRefId();
    refNum = ptr.getCellRef().getRefNum().mIndex;
    mpNum = ptr.getCellRef().getMpNum();

    position = ptr.getRefData().getPosition();
    drawState = ptr.getClass().getCreatureStats(ptr).getDrawState();
}

void DedicatedActor::reloadPtr()
{
    MWBase::World* world = MWBase::Environment::get().getWorld();
    world->disable(ptr);
    world->enable(ptr);
}
