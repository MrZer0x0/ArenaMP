#include <components/openmw-mp/TimedLog.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>

#include "../mwbase/environment.hpp"

#include "../mwmechanics/mechanicsmanagerimp.hpp"
#include "../mwmechanics/aisequence.hpp"
#include "../mwmechanics/movement.hpp"

#include "../mwrender/animation.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwworld/worldimp.hpp"

#include "LocalActor.hpp"
#include "Main.hpp"
#include "Networking.hpp"
#include "ActorList.hpp"
#include "MechanicsHelper.hpp"

using namespace mwmp;

LocalActor::LocalActor()
{
    hasSentData = false;
    posWasChanged = false;
    stopPositionResends = 0;
    equipmentChanged = false;

    wasRunning = false;
    wasSneaking = false;
    wasForceJumping = false;
    wasForceMoveJumping = false;
    wasFlying = false;

    attack.type = Attack::MELEE;
    attack.shouldSend = false;
    attack.instant = false;
    attack.pressed = false;

    cast.type = Cast::REGULAR;
    cast.shouldSend = false;
    cast.instant = false;
    cast.pressed = false;

    killer.isPlayer = false;
    killer.refId = "";
    killer.name = "";

    creatureStats.mDead = false;
    creatureStats.mDeathAnimationFinished = false;
}

LocalActor::~LocalActor()
{

}

void LocalActor::update(bool forceUpdate)
{
    updateStatsDynamic(forceUpdate);
    updateEquipment(forceUpdate, false);
    updateAiState(forceUpdate);

    if (forceUpdate || !creatureStats.mDeathAnimationFinished)
    {
        updatePosition(forceUpdate);
        updateAnimFlags(forceUpdate);
        updateAnimPlay();
        updateSpeech();
        updateAttackOrCast();
    }

    hasSentData = true;
}

void LocalActor::updateAiState(bool forceUpdate)
{
    if (ptr.isEmpty() || !ptr.getClass().isActor() || !ptr.isInCell()
        || ptr.getCell() == nullptr || ptr.getCell()->getCell() == nullptr)
        return;

    MWMechanics::AiSequence& sequence = ptr.getClass().getCreatureStats(ptr).getAiSequence();
    // Y092: an authority must never publish a home anchor for authored followers.
    if (sequence.hasPackage(MWMechanics::AiPackageTypeId::Follow)
        || sequence.hasPackage(MWMechanics::AiPackageTypeId::Escort))
        sequence.clearReturnHomeState();
    const bool inCombat = sequence.isInCombat();
    MWWorld::Ptr activeTarget;
    const bool hasTarget = inCombat && sequence.getCombatTarget(activeTarget) && !activeTarget.isEmpty();
    const int targetActorId = hasTarget
        ? activeTarget.getClass().getCreatureStats(activeTarget).getActorId() : -1;
    // Hash the complete aggro set, not only the current primary target. With two
    // players the primary can stay unchanged while a second target is added or
    // removed; that semantic change must be published immediately.
    std::vector<int> targetIds;
    std::vector<MWWorld::Ptr> combatTargets;
    sequence.getCombatTargets(combatTargets);
    for (const MWWorld::Ptr& target : combatTargets)
    {
        if (!target.isEmpty() && target.getClass().isActor())
            targetIds.push_back(target.getClass().getCreatureStats(target).getActorId());
    }
    std::sort(targetIds.begin(), targetIds.end());
    std::size_t targetSignature = 1469598103934665603ull;
    for (int id : targetIds)
    {
        targetSignature ^= static_cast<std::size_t>(id);
        targetSignature *= 1099511628211ull;
    }

    const std::size_t doorCount = sequence.getReturnHomeDoorTransitionCount();
    MWMechanics::AiReturnHomeState homeState;
    const bool hasHome = sequence.getReturnHomeState(homeState);

    // X044: the very first update of a freshly created LocalActor has no previous
    // state to compare against. Take a silent baseline instead of announcing
    // COMBAT_END for every idle NPC in the cell the moment we gain authority.
    if (!mAiStateInitialized)
    {
        mAiStateInitialized = true;
        mLastAiWasCombat = inCombat;
        mLastAiTargetActorId = targetActorId;
        mLastAiTargetSignature = targetSignature;
        mLastAiDoorCount = doorCount;
        mLastAiHadHome = hasHome;
        mAiHeartbeatTimer = 0.f;

        if (!inCombat && !hasHome)
            return;
    }

    // update() has no dt, so use the networking frame cadence as a cheap heartbeat
    // counter: send every 30 authority updates in combat, plus every semantic change.
    mAiHeartbeatTimer += 1.f;

    // X044: an idle NPC with no fight and no suspended return route has no AI
    // state worth repeating. Heartbeats are only useful while something can
    // actually be resumed by another authority.
    const bool heartbeatDue = (inCombat || hasHome)
        && mAiHeartbeatTimer >= (inCombat ? 30.f : 90.f);
    const bool changed = inCombat != mLastAiWasCombat || targetActorId != mLastAiTargetActorId
        || targetSignature != mLastAiTargetSignature
        || doorCount != mLastAiDoorCount || hasHome != mLastAiHadHome;

    // A forced update is only worth a packet when there is AI state to carry;
    // otherwise every authority change would broadcast COMBAT_END for every
    // idle NPC in the cell.
    if ((forceUpdate && (inCombat || hasHome)) || changed || heartbeatDue)
    {
        // X044: this used to reset() and send the *shared* networking ActorList
        // in the middle of Cell::updateLocal's batching loop. reset() clears the
        // queued position/animFlags/stats entries of every actor processed
        // earlier in the same tick, so those packets were silently dropped and
        // remote clients saw NPCs standing still or teleporting. Use a private
        // list, exactly like sendEquipment()/sendDeath() already do.
        ActorList aiList;
        aiList.cell = *ptr.getCell()->getCell();
        aiList.addAiStateActor(ptr, inCombat ? BaseActorList::COMBAT : BaseActorList::COMBAT_END);
        aiList.sendAiActors();
        mAiHeartbeatTimer = 0.f;
    }

    mLastAiWasCombat = inCombat;
    mLastAiTargetActorId = targetActorId;
    mLastAiTargetSignature = targetSignature;
    mLastAiDoorCount = doorCount;
    mLastAiHadHome = hasHome;
}

bool LocalActor::hasValidDestinationCell() const
{
    // X024: choke-point guard against OpenMW's transitional exterior sentinel.
    //
    // While an exterior CellStore is being unloaded, CellStore::getCell() can
    // briefly report the grid INT_MIN,INT_MIN. X022 rejected that in
    // Cell::updateLocal and again in the server's SaveActorCellChanges, but the
    // check belongs here, at the one place that actually serialises the
    // destination - otherwise any present or future caller of updateCell() can
    // still leak it. The client log shows exactly that leak reaching the server,
    // which then created a real JSON cell named "-2147483648, -2147483648".
    if (ptr.isEmpty() || !ptr.isInCell())
        return false;

    const ESM::Cell* destination = ptr.getCell()->getCell();
    if (destination == nullptr)
        return false;

    if (destination->isExterior()
        && (destination->mData.mX == std::numeric_limits<int>::min()
            || destination->mData.mY == std::numeric_limits<int>::min()))
        return false;

    return true;
}

void LocalActor::updateCell()
{
    if (!hasValidDestinationCell())
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
            "Refusing to send ID_ACTOR_CELL_CHANGE about %s %i-%i: destination cell is transitional",
            refId.c_str(), refNum, mpNum);
        return;
    }

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE, "Sending ID_ACTOR_CELL_CHANGE about %s %i-%i in cell %s to server",
                       refId.c_str(), refNum, mpNum, cell.getShortDescription().c_str());

    LOG_APPEND(TimedLog::LOG_VERBOSE, "- Moved to cell %s", ptr.getCell()->getCell()->getShortDescription().c_str());

    cell = *ptr.getCell()->getCell();
    position = ptr.getRefData().getPosition();
    isFollowerCellChange = false;

    mwmp::Main::get().getNetworking()->getActorList()->addCellChangeActor(*this);
}

void LocalActor::updatePosition(bool forceUpdate)
{
    const ESM::Position ptrPosition = ptr.getRefData().getPosition();
    const float dx = ptrPosition.pos[0] - position.pos[0];
    const float dy = ptrPosition.pos[1] - position.pos[1];
    const float dz = ptrPosition.pos[2] - position.pos[2];
    const float positionDeltaSquared = dx * dx + dy * dy + dz * dz;
    const bool actualPositionChanged = positionDeltaSquared > 0.0625f; // ignore sub-0.25-unit jitter

    bool posIsChanging = false;

    if (creatureStats.mDead)
    {
        posIsChanging = actualPositionChanged;
    }
    else
    {
        const float directionEpsilon = 0.001f;
        const bool hasTranslationDirection =
            std::abs(direction.pos[0]) > directionEpsilon ||
            std::abs(direction.pos[1]) > directionEpsilon ||
            std::abs(direction.pos[2]) > directionEpsilon;

        posIsChanging = hasTranslationDirection || actualPositionChanged ||
            std::abs(direction.rot[0]) > directionEpsilon ||
            std::abs(direction.rot[1]) > directionEpsilon ||
            std::abs(direction.rot[2]) > directionEpsilon ||
            !MWBase::Environment::get().getWorld()->isOnGround(ptr);

        // Keep the movement vector exactly as CharacterController produced it.
        // EncoreMP treats direction as authoritative movement input; synthesizing
        // it here made packet state depend on network/update timing. We still use
        // actualPositionChanged above so scripted/root-motion movement continues
        // to send ActorPosition packets even when CharacterController has no input.
    }

    const bool wasChanging = posWasChanged;
    if (posIsChanging)
        stopPositionResends = 0;
    else if (wasChanging)
        stopPositionResends = 3;

    if (forceUpdate || posIsChanging || wasChanging || stopPositionResends > 0)
    {
        posWasChanged = posIsChanging;
        position = ptrPosition;
        mwmp::Main::get().getNetworking()->getActorList()->addPositionActor(*this);

        if (!posIsChanging && stopPositionResends > 0)
            --stopPositionResends;
    }
}

void LocalActor::updateAnimFlags(bool forceUpdate)
{
    MWBase::World *world = MWBase::Environment::get().getWorld();
    MWMechanics::CreatureStats ptrCreatureStats = ptr.getClass().getCreatureStats(ptr);

    using namespace MWMechanics;

    const bool guarding = ptrCreatureStats.getMovementFlag(CreatureStats::Flag_ActiveGuard);
    bool isRunning = ptrCreatureStats.getMovementFlag(CreatureStats::Flag_Run);
    bool isSneaking = ptrCreatureStats.getMovementFlag(CreatureStats::Flag_Sneak)
        || ptrCreatureStats.getMovementFlag(CreatureStats::Flag_ForceSneak);
    bool isForceJumping = ptrCreatureStats.getMovementFlag(CreatureStats::Flag_ForceJump);
    bool isForceMoveJumping = ptrCreatureStats.getMovementFlag(CreatureStats::Flag_ForceMoveJump);

    isFlying = world->isFlying(ptr);

    MWMechanics::DrawState_ currentDrawState = ptr.getClass().getCreatureStats(ptr).getDrawState();

    if (((movementFlags & CreatureStats::Flag_ActiveGuard) != 0) != guarding || wasRunning != isRunning || wasSneaking != isSneaking ||
        wasForceJumping != isForceJumping || wasForceMoveJumping != isForceMoveJumping ||
        lastDrawState != currentDrawState || wasFlying != isFlying ||
        forceUpdate)
    {

        wasRunning = isRunning;
        wasSneaking = isSneaking;
        wasForceJumping = isForceJumping;
        wasForceMoveJumping = isForceMoveJumping;
        lastDrawState = currentDrawState;

        wasFlying = isFlying;

        movementFlags = 0;

#define __SETFLAG(flag, value) (value) ? (movementFlags | flag) : (movementFlags & ~flag)

        movementFlags = __SETFLAG(CreatureStats::Flag_ActiveGuard, guarding);
        movementFlags = __SETFLAG(CreatureStats::Flag_Sneak, isSneaking);
        movementFlags = __SETFLAG(CreatureStats::Flag_Run, isRunning);
        movementFlags = __SETFLAG(CreatureStats::Flag_ForceJump, isForceJumping);
        movementFlags = __SETFLAG(CreatureStats::Flag_ForceMoveJump, isForceMoveJumping);

#undef __SETFLAG

        drawState = currentDrawState;

        mwmp::Main::get().getNetworking()->getActorList()->addAnimFlagsActor(*this);
    }
}

void LocalActor::updateAnimPlay()
{
    if (!animation.groupname.empty())
    {
        mwmp::Main::get().getNetworking()->getActorList()->addAnimPlayActor(*this);
        animation.groupname.clear();
    }
}

void LocalActor::updateSpeech()
{
    if (!sound.empty())
    {
        mwmp::Main::get().getNetworking()->getActorList()->addSpeechActor(*this);
        sound.clear();
    }
}

void LocalActor::updateStatsDynamic(bool forceUpdate)
{
    MWMechanics::CreatureStats *ptrCreatureStats = &ptr.getClass().getCreatureStats(ptr);
    MWMechanics::DynamicStat<float> health(ptrCreatureStats->getHealth());
    MWMechanics::DynamicStat<float> magicka(ptrCreatureStats->getMagicka());
    MWMechanics::DynamicStat<float> fatigue(ptrCreatureStats->getFatigue());

    // Update stats when they become 0 or they have changed enough
    //
    // Also check for an oldHealth of 0 changing to something else for resurrected NPCs

    auto needUpdate = [](MWMechanics::DynamicStat<float> &oldVal, MWMechanics::DynamicStat<float> &newVal, int limit) {
        return oldVal != newVal && (newVal.getCurrent() == 0 || oldVal.getCurrent() == 0
                                    || abs(oldVal.getCurrent() - newVal.getCurrent()) >= limit);
    };

    if (forceUpdate || needUpdate(oldHealth, health, 3) || needUpdate(oldMagicka, magicka, 7) ||
        needUpdate(oldFatigue, fatigue, 7))
    {
        oldHealth = health;
        oldMagicka = magicka;
        oldFatigue = fatigue;

        health.writeState(creatureStats.mDynamic[0]);
        magicka.writeState(creatureStats.mDynamic[1]);
        fatigue.writeState(creatureStats.mDynamic[2]);

        creatureStats.mDead = ptrCreatureStats->isDead();
        creatureStats.mDeathAnimationFinished = ptrCreatureStats->isDeathAnimationFinished();

        mwmp::Main::get().getNetworking()->getActorList()->addStatsDynamicActor(*this);
    }
}

void LocalActor::updateEquipment(bool forceUpdate, bool sendImmediately)
{
    if (!ptr.getClass().hasInventoryStore(ptr))
        return;

    MWWorld::InventoryStore &invStore = ptr.getClass().getInventoryStore(ptr);
    
    // If we've never sent any data, autoEquip the actor just in case its inventory
    // slots have been cleared by a previous Container packet
    if (!hasSentData)
        invStore.autoEquip(ptr);

    if (forceUpdate)
        equipmentChanged = true;

    for (int slot = 0; slot < MWWorld::InventoryStore::Slots; slot++)
    {
        auto &item = equipmentItems[slot];
        MWWorld::ContainerStoreIterator it = invStore.getSlot(slot);

        if (it != invStore.end())
        {
            auto &cellRef = it->getCellRef();
            // Y094: publish mutable condition even when the refId is unchanged.
            // Server apparel correction must use the current durability/charge.
            if (!::Misc::StringUtils::ciEqual(cellRef.getRefId(), item.refId)
                || cellRef.getCharge() != item.charge
                || cellRef.getEnchantmentCharge() != item.enchantmentCharge
                || it->getRefData().getCount() != item.count
                || it->getRefData().getPoisonId() != item.poisonId
                || it->getRefData().getPoisonCharges() != item.poisonCharges)
            {
                equipmentChanged = true;

                item.refId = cellRef.getRefId();
                item.charge = cellRef.getCharge();
                item.enchantmentCharge = it->getCellRef().getEnchantmentCharge();
                item.count = it->getRefData().getCount();
                item.poisonId = it->getRefData().getPoisonId();
                item.poisonCharges = it->getRefData().getPoisonCharges();
            }
        }
        else if (!item.refId.empty())
        {
            equipmentChanged = true;
            item.refId = "";
            item.count = 0;
            item.charge = -1;
            item.enchantmentCharge = -1;
            item.poisonId.clear();
            item.poisonCharges = 0;
        }
    }

    if (equipmentChanged)
    {
        if (sendImmediately)
            sendEquipment();
        else
            mwmp::Main::get().getNetworking()->getActorList()->addEquipmentActor(*this);

        equipmentChanged = false;
    }
}

void LocalActor::updateAttackOrCast()
{
    if (attack.shouldSend)
    {
        mwmp::Main::get().getNetworking()->getActorList()->addAttackActor(*this);
        attack.shouldSend = false;
    }
    else if (cast.shouldSend)
    {
        mwmp::Main::get().getNetworking()->getActorList()->addCastActor(*this);
        cast.shouldSend = false;
        cast.hasProjectile = false;
    }
}

void LocalActor::sendEquipment()
{
    ActorList actorList;
    actorList.cell = cell;
    actorList.addActor(*this);
    Main::get().getNetworking()->getActorPacket(ID_ACTOR_EQUIPMENT)->setActorList(&actorList);
    Main::get().getNetworking()->getActorPacket(ID_ACTOR_EQUIPMENT)->Send();
}

void LocalActor::sendSpellsActive()
{
    MWMechanics::ActiveSpells& activeSpells = ptr.getClass().getCreatureStats(ptr).getActiveSpells();
    spellsActiveChanges.activeSpells.clear();

    for (const auto& ptrSpell : activeSpells)
    {
        mwmp::ActiveSpell packetSpell;
        packetSpell.id = ptrSpell.first;
        packetSpell.params.mDisplayName = ptrSpell.second.mDisplayName;
        packetSpell.params.mEffects = ptrSpell.second.mEffects;
        packetSpell.timestampDay = ptrSpell.second.mTimeStamp.getDay();
        packetSpell.timestampHour = ptrSpell.second.mTimeStamp.getHour();
        spellsActiveChanges.activeSpells.push_back(packetSpell);
    }

    spellsActiveChanges.action = mwmp::SpellsActiveChanges::SET;
    ActorList actorList;
    actorList.cell = cell;
    actorList.addActor(*this);
    Main::get().getNetworking()->getActorPacket(ID_ACTOR_SPELLS_ACTIVE)->setActorList(&actorList);
    Main::get().getNetworking()->getActorPacket(ID_ACTOR_SPELLS_ACTIVE)->Send();
}

void LocalActor::sendSpellsActiveAddition(const std::string id, bool isStackingSpell, const MWMechanics::ActiveSpells::ActiveSpellParams& params)
{
    // Skip any bugged spells that somehow have clientside-only dynamic IDs
    if (id.find("$dynamic") != std::string::npos)
        return;

    spellsActiveChanges.activeSpells.clear();

    const MWWorld::Ptr& caster = MWBase::Environment::get().getWorld()->searchPtrViaActorId(params.mCasterActorId);

    mwmp::ActiveSpell spell;
    spell.id = id;
    spell.isStackingSpell = isStackingSpell;
    spell.caster = MechanicsHelper::getTarget(caster);
    spell.timestampDay = params.mTimeStamp.getDay();
    spell.timestampHour = params.mTimeStamp.getHour();
    spell.params.mEffects = params.mEffects;
    spell.params.mDisplayName = params.mDisplayName;
    spellsActiveChanges.activeSpells.push_back(spell);

    spellsActiveChanges.action = mwmp::SpellsActiveChanges::ADD;

    ActorList actorList;
    actorList.cell = cell;
    actorList.addActor(*this);
    Main::get().getNetworking()->getActorPacket(ID_ACTOR_SPELLS_ACTIVE)->setActorList(&actorList);
    Main::get().getNetworking()->getActorPacket(ID_ACTOR_SPELLS_ACTIVE)->Send();
}

void LocalActor::sendSpellsActiveRemoval(const std::string id, bool isStackingSpell, MWWorld::TimeStamp timestamp)
{
    // Skip any bugged spells that somehow have clientside-only dynamic IDs
    if (id.find("$dynamic") != std::string::npos)
        return;

    spellsActiveChanges.activeSpells.clear();

    mwmp::ActiveSpell spell;
    spell.id = id;
    spell.isStackingSpell = isStackingSpell;
    spell.timestampDay = timestamp.getDay();
    spell.timestampHour = timestamp.getHour();
    spellsActiveChanges.activeSpells.push_back(spell);

    spellsActiveChanges.action = mwmp::SpellsActiveChanges::REMOVE;

    ActorList actorList;
    actorList.cell = cell;
    actorList.addActor(*this);
    Main::get().getNetworking()->getActorPacket(ID_ACTOR_SPELLS_ACTIVE)->setActorList(&actorList);
    Main::get().getNetworking()->getActorPacket(ID_ACTOR_SPELLS_ACTIVE)->Send();
}

void LocalActor::sendDeath(char newDeathState)
{
    deathState = newDeathState;

    if (MechanicsHelper::isEmptyTarget(killer))
        killer = MechanicsHelper::getTarget(ptr);

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending ID_ACTOR_DEATH about %s %i-%i in cell %s to server\n- deathState: %d",
        refId.c_str(), refNum, mpNum, cell.getShortDescription().c_str(), deathState);

    ActorList actorList;
    actorList.cell = cell;
    actorList.addActor(*this);
    Main::get().getNetworking()->getActorPacket(ID_ACTOR_DEATH)->setActorList(&actorList);
    Main::get().getNetworking()->getActorPacket(ID_ACTOR_DEATH)->Send();

    MechanicsHelper::clearTarget(killer);
}

MWWorld::Ptr LocalActor::getPtr()
{
    return ptr;
}

void LocalActor::setPtr(const MWWorld::Ptr& newPtr)
{
    ptr = newPtr;

    refId = ptr.getCellRef().getRefId();
    refNum = ptr.getCellRef().getRefNum().mIndex;
    mpNum = ptr.getCellRef().getMpNum();

    lastDrawState = ptr.getClass().getCreatureStats(ptr).getDrawState();
    oldHealth = ptr.getClass().getCreatureStats(ptr).getHealth();
    oldMagicka = ptr.getClass().getCreatureStats(ptr).getMagicka();
    oldFatigue = ptr.getClass().getCreatureStats(ptr).getFatigue();
}
