#include <limits>

#include <components/esm/cellid.hpp>
#include <components/esm/loadcrea.hpp>
#include <components/esm/loadnpc.hpp>
#include <components/openmw-mp/TimedLog.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/mechanicsmanager.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/livecellref.hpp"
#include "../mwworld/worldimp.hpp"

#include "../mwmechanics/xpleveling.hpp"
#include "../mwmechanics/aitravel.hpp"
#include "../mwmechanics/aisequence.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/movement.hpp"

#include "Cell.hpp"
#include "Main.hpp"
#include "Networking.hpp"
#include "LocalPlayer.hpp"
#include "CellController.hpp"
#include "MechanicsHelper.hpp"

using namespace mwmp;

namespace
{
    /// X044: repair the AI state of an actor that is becoming ours.
    ///
    /// 1. DedicatedActor::setAi() forces AI_Fight to 0 so a puppet copy never
    ///    starts its own fights. That setting stays on the reference, so an actor
    ///    that later came under our authority remained permanently pacified and
    ///    only ever reacted to being hit. The rating is restored from the actor's
    ///    own record, but only if it is still the zero we wrote ourselves - a
    ///    script that legitimately set Fight to 0 is preserved.
    /// 2. If the AiSequence is completely empty (see the recovery note below),
    ///    the record's own AI packages are refilled so the NPC resumes wandering.
    void restoreOwnedActorAi(const MWWorld::Ptr& ptr)
    {
        if (ptr.isEmpty() || !ptr.getClass().isActor())
            return;

        MWMechanics::CreatureStats& stats = ptr.getClass().getCreatureStats(ptr);

        int recordFight = 0;
        const ESM::AIPackageList* recordPackages = nullptr;

        if (ptr.getTypeName() == typeid(ESM::NPC).name())
        {
            const MWWorld::LiveCellRef<ESM::NPC>* ref = ptr.get<ESM::NPC>();
            if (ref != nullptr && ref->mBase != nullptr)
            {
                recordFight = ref->mBase->mAiData.mFight;
                recordPackages = &ref->mBase->mAiPackage;
            }
        }
        else if (ptr.getTypeName() == typeid(ESM::Creature).name())
        {
            const MWWorld::LiveCellRef<ESM::Creature>* ref = ptr.get<ESM::Creature>();
            if (ref != nullptr && ref->mBase != nullptr)
            {
                recordFight = ref->mBase->mAiData.mFight;
                recordPackages = &ref->mBase->mAiPackage;
            }
        }

        if (recordFight > 0
            && stats.getAiSetting(MWMechanics::CreatureStats::AI_Fight).getBase() == 0)
        {
            LOG_APPEND(TimedLog::LOG_VERBOSE, "- Restoring natural fight rating %i", recordFight);
            stats.setAiSetting(MWMechanics::CreatureStats::AI_Fight, recordFight);
        }

        // X044 recovery: an actor whose AiSequence was wiped by the old
        // unconditional setAi() has lost the Wander/Travel packages that came
        // from its own record, and nothing rebuilds them while the cell stays
        // loaded - the NPC simply stands still. Refill from the record when we
        // take ownership of an actor that has no AI package left at all.
        if (recordPackages != nullptr && !recordPackages->mList.empty()
            && stats.getAiSequence().isEmpty() && !stats.isDead())
        {
            LOG_APPEND(TimedLog::LOG_VERBOSE, "%s", "- Refilling an empty AI sequence from the actor's record");
            stats.getAiSequence().fill(*recordPackages);
        }
    }

    /// X024: an actor handed over to us may already be locked into a fight whose
    /// target this client cannot resolve at all - the other player has left, or is
    /// in a coordinate space we do not share. AiCombat is non-cancellable and its
    /// give-up timers used to live in AiState, which is recreated from scratch by
    /// exactly this hand-off, so in a busy area the countdown was restarted over
    /// and over and the NPC stayed frozen with its weapon drawn. Whoever takes
    /// ownership drops such a fight immediately instead of inheriting it.
    void dropUnresolvableCombat(const MWWorld::Ptr& ptr)
    {
        if (ptr.isEmpty() || !ptr.getClass().isActor())
            return;

        MWMechanics::CreatureStats& stats = ptr.getClass().getCreatureStats(ptr);
        if (stats.isDead())
            return;

        MWMechanics::AiSequence& sequence = stats.getAiSequence();
        if (!sequence.isInCombat())
            return;

        // AiSequence has no getTarget() in this 0.47-based branch; the active
        // package carries it.
        MWWorld::Ptr target;
        if (!sequence.isEmpty())
            target = sequence.getActivePackage().getTarget();

        bool reachable = false;

        if (!target.isEmpty() && target.isInCell()
            && target.getRefData().getCount() > 0 && !target.getRefData().isDeleted()
            && !target.getClass().getCreatureStats(target).isDead()
            && ptr.isInCell())
        {
            // Same cell, or two exterior cells, which share one coordinate space.
            reachable = target.getCell() == ptr.getCell()
                || (target.getCell()->isExterior() && ptr.getCell()->isExterior());
        }

        if (!reachable)
        {
            LOG_APPEND(TimedLog::LOG_INFO,
                "%s", "- Dropping an inherited combat package whose target is not reachable from here");
            sequence.stopCombat();

            MWMechanics::Movement& movement = ptr.getClass().getMovementSettings(ptr);
            movement.mPosition[0] = 0.f;
            movement.mPosition[1] = 0.f;
        }
    }
}

mwmp::Cell::Cell(MWWorld::CellStore* cellStore)
{
    store = cellStore;
    shouldInitializeActors = false;

    updateTimer = 0;
}

Cell::~Cell()
{

}

void Cell::updateLocal(bool forceUpdate)
{
    if (localActors.empty())
        return;

    const float timeoutSec = 0.025;

    if (!forceUpdate && (updateTimer += MWBase::Environment::get().getFrameDuration()) < timeoutSec)
        return;
    else
        updateTimer = 0;

    CellController *cellController = Main::get().getCellController();
    ActorList *actorList = mwmp::Main::get().getNetworking()->getActorList();
    actorList->reset();

    actorList->cell = *store->getCell();

    for (auto it = localActors.begin(); it != localActors.end();)
    {
        LocalActor *actor = it->second;

        MWWorld::CellStore *newStore = actor->getPtr().getCell();

        if (newStore != store)
        {
            std::string mapIndex = it->first;

            // X022: while an exterior CellStore is being unloaded, OpenMW can
            // transiently expose the sentinel grid INT_MIN,INT_MIN. Never turn
            // that internal transitional state into an authoritative network
            // ActorCellChange (the old code made the server create a real JSON
            // cell named "-2147483648, -2147483648").
            const ESM::Cell* newCellRecord = newStore != nullptr ? newStore->getCell() : nullptr;
            const bool invalidDestination = newCellRecord == nullptr
                || (newCellRecord->isExterior()
                    && (newCellRecord->mData.mX == std::numeric_limits<int>::min()
                        || newCellRecord->mData.mY == std::numeric_limits<int>::min()));

            if (invalidDestination)
            {
                LOG_APPEND(TimedLog::LOG_WARN,
                    "- Dropping transitional ActorCellChange for LocalActor %s; keeping server's last valid cell",
                    mapIndex.c_str());
                cellController->removeLocalActorRecord(mapIndex);
                delete actor;
                localActors.erase(it++);
                continue;
            }

            // Flush an exact snapshot in the source cell before its ordered
            // CellChange packet. updateCell skips normal update() this frame.
            actor->updateStatsDynamic(true);
            actor->updateCell();

            // If the cell this actor has moved to is under our authority, move them to it
            if (cellController->hasLocalAuthority(actor->cell))
            {
                LOG_APPEND(TimedLog::LOG_VERBOSE, "- Moving LocalActor %s to our authority in %s",
                    mapIndex.c_str(), actor->cell.getShortDescription().c_str());
                Cell *newCell = cellController->getCell(actor->cell);
                newCell->localActors[mapIndex] = actor;
                cellController->setLocalActorRecord(mapIndex, newCell->getShortDescription());
            }
            else
            {
                LOG_APPEND(TimedLog::LOG_VERBOSE, "- Deleting LocalActor %s which is no longer under our authority",
                    mapIndex.c_str(), getShortDescription().c_str());
                cellController->removeLocalActorRecord(mapIndex);
                delete actor;
            }

            localActors.erase(it++);
        }
        else
        {
            if (!actor->getPtr().getRefData().isEnabled() || actor->getPtr().getRefData().isDeleted())
            {
                const std::string mapIndex = it->first;
                LOG_APPEND(TimedLog::LOG_VERBOSE, "- Removing LocalActor %s because its reference is disabled/deleted",
                    mapIndex.c_str(), getShortDescription().c_str());
                cellController->removeLocalActorRecord(mapIndex);
                delete actor;
                it = localActors.erase(it);
                continue;
            }

            // Forcibly update this local actor if its data has never been sent before;
            // otherwise, use the current forceUpdate value.
            actor->update(actor->hasSentData ? forceUpdate : true);
            ++it;
        }
    }

    actorList->sendPositionActors();
    actorList->sendAnimFlagsActors();
    actorList->sendAnimPlayActors();
    actorList->sendSpeechActors();
    actorList->sendDeathActors();
    actorList->sendStatsDynamicActors();
    actorList->sendEquipmentActors();
    actorList->sendAttackActors();
    actorList->sendCastActors();
    actorList->sendCellChangeActors();
}

void Cell::updateDedicated(float dt)
{
    if (dedicatedActors.empty()) return;

    CellController *cellController = Main::get().getCellController();
    for (auto it = dedicatedActors.begin(); it != dedicatedActors.end();)
    {
        DedicatedActor *actor = it->second;
        if (!actor->getPtr().getRefData().isEnabled() || actor->getPtr().getRefData().isDeleted())
        {
            const std::string mapIndex = it->first;
            LOG_APPEND(TimedLog::LOG_VERBOSE, "- Removing DedicatedActor %s because its reference is disabled/deleted",
                mapIndex.c_str(), getShortDescription().c_str());
            cellController->removeDedicatedActorRecord(mapIndex);
            delete actor;
            it = dedicatedActors.erase(it);
            continue;
        }

        actor->update(dt);
        ++it;
    }

    // Are we the authority over this cell? If so, uninitialize DedicatedActors
    // after the above update.
    if (hasLocalAuthority())
        uninitializeDedicatedActors();
}

void Cell::readPositions(ActorList& actorList)
{
    initializeDedicatedActors(actorList);

    if (dedicatedActors.empty()) return;
    
    for (const auto &baseActor : actorList.baseActors)
    {
        std::string mapIndex = Main::get().getCellController()->generateMapIndex(baseActor);

        if (dedicatedActors.count(mapIndex) > 0)
        {
            DedicatedActor *actor = dedicatedActors[mapIndex];
            actor->position = baseActor.position;
            actor->direction = baseActor.direction;

            if (!actor->hasPositionData)
            {
                actor->hasPositionData = true;

                // Snap the first authoritative sample immediately. Subsequent
                // samples are handled by DedicatedActor::move() with bounded
                // interpolation and authoritative movement animation.
                actor->setPosition();
            }
        }
    }
}

void Cell::readAnimFlags(ActorList& actorList)
{
    for (const auto &baseActor : actorList.baseActors)
    {
        std::string mapIndex = Main::get().getCellController()->generateMapIndex(baseActor);

        if (dedicatedActors.count(mapIndex) > 0)
        {
            DedicatedActor *actor = dedicatedActors[mapIndex];
            actor->movementFlags = baseActor.movementFlags;
            actor->drawState = baseActor.drawState;
            actor->isFlying = baseActor.isFlying;
        }
    }
}

void Cell::readAnimPlay(ActorList& actorList)
{
    for (const auto &baseActor : actorList.baseActors)
    {
        std::string mapIndex = Main::get().getCellController()->generateMapIndex(baseActor);

        // Y097: a server-directed packet is attached to its receiving player.
        // The authority has a LocalActor, so the old dedicated-only path silently
        // ignored the command on the one client actually controlling this NPC.
        const auto local = localActors.find(mapIndex);
        if (local != localActors.end() && Main::get().getLocalPlayer()
            && actorList.guid == Main::get().getLocalPlayer()->guid)
        {
            const auto& animation = baseActor.animation;
            if (!animation.groupname.empty() && animation.groupname.size() <= 128
                && animation.mode >= 0 && animation.mode <= 2
                && animation.count >= 1 && animation.count <= 1000
                && animation.groupname.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-") == std::string::npos)
            {
                // Apply directly, without filling LocalActor::animation: that
                // field would send the same command back through updateAnimPlay.
                MWBase::Environment::get().getMechanicsManager()->playAnimationGroup(
                    local->second->getPtr(), animation.groupname, animation.mode, animation.count, animation.persist);
            }
            continue;
        }

        if (dedicatedActors.count(mapIndex) > 0)
        {
            DedicatedActor *actor = dedicatedActors[mapIndex];
            actor->animation.groupname = baseActor.animation.groupname;
            actor->animation.mode = baseActor.animation.mode;
            actor->animation.count = baseActor.animation.count;
            actor->animation.persist = baseActor.animation.persist;
            actor->playAnimation();
        }
    }
}

void Cell::readStatsDynamic(ActorList& actorList)
{
    initializeDedicatedActors(actorList);

    if (dedicatedActors.empty()) return;

    for (const auto &baseActor : actorList.baseActors)
    {
        std::string mapIndex = Main::get().getCellController()->generateMapIndex(baseActor);

        if (dedicatedActors.count(mapIndex) > 0)
        {
            DedicatedActor *actor = dedicatedActors[mapIndex];
            actor->creatureStats = baseActor.creatureStats;

            if (!actor->hasStatsDynamicData)
            {
                actor->hasStatsDynamicData = true;

                // If this is our first packet about this actor's dynamic stats, force an update
                // now instead of waiting for its frame
                //
                // That way, if this actor is about to become a LocalActor, initial data about it
                // received from the server still gets set
                actor->setStatsDynamic();
            }
        }
    }
}

void Cell::readDeath(ActorList& actorList)
{
    initializeDedicatedActors(actorList);

    if (dedicatedActors.empty()) return;

    for (const auto &baseActor : actorList.baseActors)
    {
        std::string mapIndex = Main::get().getCellController()->generateMapIndex(baseActor);

        if (dedicatedActors.count(mapIndex) > 0)
        {
            DedicatedActor *actor = dedicatedActors[mapIndex];
            const bool wasAlreadyDead = actor->creatureStats.mDead;
            actor->creatureStats.mDead = true;
            actor->creatureStats.mDynamic[0].mCurrent = 0;

            // Actor authority is not necessarily the player who landed the kill.
            // The authority already awards its own local kill through actorKilled();
            // all other clients receive ActorDeath, so the killer's client can award
            // the same data-driven XP when its GUID matches the packet killer.
            LocalPlayer* localPlayer = Main::get().getLocalPlayer();
            if (!wasAlreadyDead && localPlayer != nullptr && baseActor.killer.isPlayer
                && baseActor.killer.guid == localPlayer->guid)
            {
                MWMechanics::XPLeveling::awardKill(actor->getPtr(), localPlayer->getPlayerPtr());
            }

            Main::get().getCellController()->setQueuedDeathState(actor->getPtr(), baseActor.deathState);

            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received ID_ACTOR_DEATH about %s %i-%i in cell %s\n- deathState: %d\n-isInstantDeath: %s",
                actor->refId.c_str(), actor->refNum, actor->mpNum, getShortDescription().c_str(),
                baseActor.deathState, baseActor.isInstantDeath ? "true" : "false");

            if (baseActor.isInstantDeath)
            {
                actor->getPtr().getClass().getCreatureStats(actor->getPtr()).setDeathAnimationFinished(true);
                MWBase::Environment::get().getWorld()->enableActorCollision(actor->getPtr(), false);
            }
        }
    }
}

void Cell::readEquipment(ActorList& actorList)
{
    initializeDedicatedActors(actorList);

    if (dedicatedActors.empty()) return;

    for (const auto &baseActor : actorList.baseActors)
    {
        std::string mapIndex = Main::get().getCellController()->generateMapIndex(baseActor);

        if (dedicatedActors.count(mapIndex) > 0)
        {
            DedicatedActor *actor = dedicatedActors[mapIndex];

            for (int slot = 0; slot < 19; ++slot)
                actor->equipmentItems[slot] = baseActor.equipmentItems[slot];

            actor->setEquipment();
        }
    }

    if (hasLocalAuthority())
        uninitializeDedicatedActors(actorList);
}

void Cell::readSpeech(ActorList& actorList)
{
    initializeDedicatedActors(actorList);

    if (dedicatedActors.empty()) return;

    for (const auto &baseActor : actorList.baseActors)
    {
        std::string mapIndex = Main::get().getCellController()->generateMapIndex(baseActor);

        if (dedicatedActors.count(mapIndex) > 0)
        {
            DedicatedActor *actor = dedicatedActors[mapIndex];
            actor->sound = baseActor.sound;
            actor->playSound();
        }
    }

    if (hasLocalAuthority())
        uninitializeDedicatedActors(actorList);
}

void Cell::readSpellsActive(ActorList& actorList)
{
    initializeDedicatedActors(actorList);

    if (dedicatedActors.empty()) return;

    for (const auto& baseActor : actorList.baseActors)
    {
        std::string mapIndex = Main::get().getCellController()->generateMapIndex(baseActor);

        if (dedicatedActors.count(mapIndex) > 0)
        {
            DedicatedActor* actor = dedicatedActors[mapIndex];
            actor->spellsActiveChanges = baseActor.spellsActiveChanges;

            int spellsActiveAction = baseActor.spellsActiveChanges.action;

            if (spellsActiveAction == SpellsActiveChanges::ADD)
                actor->addSpellsActive();
            else if (spellsActiveAction == SpellsActiveChanges::REMOVE)
                actor->removeSpellsActive();
            else
                actor->setSpellsActive();
        }
    }

    if (hasLocalAuthority())
        uninitializeDedicatedActors(actorList);
}

void Cell::readAi(ActorList& actorList)
{
    initializeDedicatedActors(actorList);

    if (dedicatedActors.empty()) return;

    for (const auto &baseActor : actorList.baseActors)
    {
        std::string mapIndex = Main::get().getCellController()->generateMapIndex(baseActor);

        if (dedicatedActors.count(mapIndex) > 0)
        {
            DedicatedActor *actor = dedicatedActors[mapIndex];
            actor->aiAction = baseActor.aiAction;
            actor->aiDistance = baseActor.aiDistance;
            actor->aiDuration = baseActor.aiDuration;
            actor->aiShouldRepeat = baseActor.aiShouldRepeat;
            actor->aiCoordinates = baseActor.aiCoordinates;
            actor->hasAiTarget = baseActor.hasAiTarget;
            actor->aiTarget = baseActor.aiTarget;
            actor->aiCombatTargets = baseActor.aiCombatTargets;
            actor->aiHasReturnHome = baseActor.aiHasReturnHome;
            actor->aiHomeCell = baseActor.aiHomeCell;
            actor->aiHomePosition = baseActor.aiHomePosition;
            actor->aiDoorBreadcrumbs = baseActor.aiDoorBreadcrumbs;
            actor->hasReceivedAi = true;
            actor->setAi();
        }
    }

    if (hasLocalAuthority())
        uninitializeDedicatedActors(actorList);
}

void Cell::readAttack(ActorList& actorList)
{
    // Unlike every other read* handler, this one used to skip actor initialization.
    // An ActorAttack packet is addressed to the cell the *sender* believed the actor
    // was in; right after a door transition the receiver may already have moved the
    // DedicatedActor elsewhere, so the packet found nothing and the hit was silently
    // dropped - the NPC kept pursuing (position packets self-initialize) but stopped
    // dealing damage, and Npc::hit() returns early for DedicatedActors so no client
    // applied it locally either.
    initializeDedicatedActors(actorList);

    CellController *cellController = Main::get().getCellController();

    for (const auto &baseActor : actorList.baseActors)
    {
        std::string mapIndex = cellController->generateMapIndex(baseActor);

        DedicatedActor *actor = nullptr;

        if (dedicatedActors.count(mapIndex) > 0)
            actor = dedicatedActors[mapIndex];
        else if (cellController->isDedicatedActor(baseActor.refNum, baseActor.mpNum))
        {
            // The actor is tracked under another cell after a transition - route the
            // packet there instead of discarding it.
            actor = cellController->getDedicatedActor(baseActor.refNum, baseActor.mpNum);
        }

        if (!actor || actor->getPtr().isEmpty())
            continue;

        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Reading ActorAttack about %s", mapIndex.c_str());

        actor->attack = baseActor.attack;

        MechanicsHelper::processAttack(actor->attack, actor->getPtr());
    }
}

void Cell::readCast(ActorList& actorList)
{
    initializeDedicatedActors(actorList);

    CellController *cellController = Main::get().getCellController();

    for (const auto &baseActor : actorList.baseActors)
    {
        std::string mapIndex = cellController->generateMapIndex(baseActor);

        DedicatedActor *actor = nullptr;

        if (dedicatedActors.count(mapIndex) > 0)
            actor = dedicatedActors[mapIndex];
        else if (cellController->isDedicatedActor(baseActor.refNum, baseActor.mpNum))
            actor = cellController->getDedicatedActor(baseActor.refNum, baseActor.mpNum);

        if (actor && !actor->getPtr().isEmpty())
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE, "Reading ActorCast about %s", mapIndex.c_str());

            actor->cast = baseActor.cast;

            // Set the correct drawState here if we've somehow we've missed a previous
            // AnimFlags packet
            // Y092: repair actual stance, not just the network cache. Item/instant
            // casts must not force a prepared spell stance over quick casting.
            if (actor->cast.type == mwmp::Cast::REGULAR && actor->cast.pressed && !actor->cast.instant
                && (actor->drawState != MWMechanics::DrawState_Spell
                    || actor->getPtr().getClass().getCreatureStats(actor->getPtr()).getDrawState()
                        != MWMechanics::DrawState_Spell))
            {
                actor->drawState = MWMechanics::DrawState_::DrawState_Spell;
                actor->setAnimFlags();
            }

            MechanicsHelper::processCast(actor->cast, actor->getPtr());
        }
    }
}

void Cell::readCellChange(ActorList& actorList)
{
    initializeDedicatedActors(actorList);

    if (dedicatedActors.empty()) return;

    CellController *cellController = Main::get().getCellController();

    for (const auto &baseActor : actorList.baseActors)
    {
        std::string mapIndex = Main::get().getCellController()->generateMapIndex(baseActor);

        // Is a packet mistakenly moving the actor to the cell it's already in? If so, ignore it
        if (Misc::StringUtils::ciEqual(getShortDescription(), baseActor.cell.getShortDescription()))
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Server says DedicatedActor %s moved to %s, but it was already there",
                mapIndex.c_str(), getShortDescription().c_str());
            continue;
        }

        if (dedicatedActors.count(mapIndex) > 0)
        {
            DedicatedActor *dedicatedActor = dedicatedActors[mapIndex];

            // X022: preserve a return-home anchor on every observer before the
            // actor is moved to another cell. Normally AiSequence::stack creates
            // it when Combat/Pursue/Cast starts, but an authority/packet race can
            // deliver the cell move first on the client that later becomes the
            // authority. Without this anchor that client treats the pursuit cell
            // as the actor's home and the NPC never returns.
            MWWorld::Ptr actorPtr = dedicatedActor->getPtr();
            if (!actorPtr.isEmpty() && actorPtr.getCell() != nullptr
                && actorPtr.getCell()->getCell() != nullptr)
            {
                MWMechanics::AiSequence& sequence
                    = actorPtr.getClass().getCreatureStats(actorPtr).getAiSequence();
                const MWMechanics::AiPackageTypeId activeType = sequence.getTypeId();
                const bool temporaryAi = sequence.isInCombat()
                    || activeType == MWMechanics::AiPackageTypeId::Pursue
                    || activeType == MWMechanics::AiPackageTypeId::Cast;

                // Y092: Follow-no-home, including combat layered over an escort.
                const bool following = sequence.hasPackage(MWMechanics::AiPackageTypeId::Follow)
                    || sequence.hasPackage(MWMechanics::AiPackageTypeId::Escort);
                if (following)
                    sequence.clearReturnHomeState();
                if (temporaryAi && !following
                    && !sequence.hasPackage(MWMechanics::AiPackageTypeId::InternalTravel))
                {
                    const ESM::Position homePosition = actorPtr.getRefData().getPosition();
                    const ESM::Cell* homeCell = actorPtr.getCell()->getCell();
                    const std::string homeCellName = homeCell->isExterior()
                        ? std::string() : homeCell->mName;
                    MWMechanics::AiInternalTravel returnHome(
                        homePosition, homeCell->getCellId(), homeCellName);
                    sequence.stack(returnHome, actorPtr, false);
                }
            }

            dedicatedActor->cell = baseActor.cell;
            dedicatedActor->position = baseActor.position;
            dedicatedActor->direction = baseActor.direction;

            LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE, "Server says DedicatedActor %s moved to %s",
                mapIndex.c_str(), dedicatedActor->cell.getShortDescription().c_str());

            MWWorld::CellStore *newStore = cellController->getCellStore(dedicatedActor->cell);
            dedicatedActor->setCell(newStore);

            // If the cell this actor has moved to is active and not under our authority, move them to it
            if (cellController->isActiveWorldCell(dedicatedActor->cell) && !cellController->hasLocalAuthority(dedicatedActor->cell))
            {
                LOG_APPEND(TimedLog::LOG_VERBOSE, "- Moving DedicatedActor %s to our active cell %s",
                    mapIndex.c_str(), dedicatedActor->cell.getShortDescription().c_str());
                cellController->initializeCell(dedicatedActor->cell);
                Cell *newCell = cellController->getCell(dedicatedActor->cell);
                newCell->dedicatedActors[mapIndex] = dedicatedActor;
                cellController->setDedicatedActorRecord(mapIndex, newCell->getShortDescription());
            }
            else
            {
                if (cellController->hasLocalAuthority(dedicatedActor->cell))
                {
                    LOG_APPEND(TimedLog::LOG_VERBOSE, "- Creating new LocalActor based on %s in %s",
                        mapIndex.c_str(), dedicatedActor->cell.getShortDescription().c_str());
                    Cell *newCell = cellController->getCell(dedicatedActor->cell);
                    LocalActor *localActor = new LocalActor();
                    localActor->cell = dedicatedActor->cell;
                    dropUnresolvableCombat(dedicatedActor->getPtr());
                    restoreOwnedActorAi(dedicatedActor->getPtr());
                    localActor->setPtr(dedicatedActor->getPtr());
                    localActor->position = dedicatedActor->position;
                    localActor->direction = dedicatedActor->direction;
                    localActor->movementFlags = dedicatedActor->movementFlags;
                    localActor->drawState = dedicatedActor->drawState;
                    localActor->isFlying = dedicatedActor->isFlying;
                    localActor->creatureStats = dedicatedActor->creatureStats;

                    newCell->localActors[mapIndex] = localActor;
                    cellController->setLocalActorRecord(mapIndex, newCell->getShortDescription());
                }

                LOG_APPEND(TimedLog::LOG_VERBOSE, "- Deleting DedicatedActor %s which is no longer needed",
                    mapIndex.c_str(), getShortDescription().c_str());
                cellController->removeDedicatedActorRecord(mapIndex);
                delete dedicatedActor;
            }

            dedicatedActors.erase(mapIndex);
        }
    }
}

void Cell::initializeLocalActor(const MWWorld::Ptr& ptr)
{
    std::string mapIndex = Main::get().getCellController()->generateMapIndex(ptr);
    LOG_APPEND(TimedLog::LOG_VERBOSE, "- Initializing LocalActor %s in %s", mapIndex.c_str(), getShortDescription().c_str());

    dropUnresolvableCombat(ptr);
    restoreOwnedActorAi(ptr);

    LocalActor *actor = new LocalActor();
    actor->cell = *store->getCell();
    actor->setPtr(ptr);

    localActors[mapIndex] = actor;

    Main::get().getCellController()->setLocalActorRecord(mapIndex, getShortDescription());

    LOG_APPEND(TimedLog::LOG_VERBOSE, "- Successfully initialized LocalActor %s in %s", mapIndex.c_str(), getShortDescription().c_str());
}

void Cell::initializeLocalActors()
{
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE, "Initializing LocalActors in %s", getShortDescription().c_str());

    for (const auto &mergedRef : store->getMergedRefs())
    {
        if (mergedRef->mClass->isActor())
        {
            MWWorld::Ptr ptr(mergedRef, store);

            // If this Ptr is lacking a unique index, ignore it
            if (ptr.getCellRef().getRefNum().mIndex == 0 && ptr.getCellRef().getMpNum() == 0) continue;

            // If this Ptr is disabled or deleted, ignore it
            if (!ptr.getRefData().isEnabled() || ptr.getRefData().isDeleted()) continue;

            std::string mapIndex = Main::get().getCellController()->generateMapIndex(ptr);

            // Only initialize this actor if it isn't already initialized
            if (localActors.count(mapIndex) == 0)
                initializeLocalActor(ptr);
        }
    }

    LOG_APPEND(TimedLog::LOG_VERBOSE, "- Successfully initialized LocalActors in %s", getShortDescription().c_str());
}

void Cell::initializeDedicatedActor(const MWWorld::Ptr& ptr)
{
    if (ptr.isEmpty() || !ptr.getRefData().isEnabled() || ptr.getRefData().isDeleted())
        return;

    std::string mapIndex = Main::get().getCellController()->generateMapIndex(ptr);
    LOG_APPEND(TimedLog::LOG_VERBOSE, "- Initializing DedicatedActor %s in %s", mapIndex.c_str(), getShortDescription().c_str());

    DedicatedActor *actor = new DedicatedActor();
    actor->cell = *store->getCell();
    actor->setPtr(ptr);

    dedicatedActors[mapIndex] = actor;

    Main::get().getCellController()->setDedicatedActorRecord(mapIndex, getShortDescription());

    LOG_APPEND(TimedLog::LOG_VERBOSE, "- Successfully initialized DedicatedActor %s in %s", mapIndex.c_str(), getShortDescription().c_str());
}

void Cell::initializeDedicatedActors(ActorList& actorList)
{
    for (const auto &baseActor : actorList.baseActors)
    {
        std::string mapIndex = Main::get().getCellController()->generateMapIndex(baseActor);

        // If this key doesn't exist, create it
        if (dedicatedActors.count(mapIndex) == 0)
        {
            MWWorld::Ptr ptrFound = store->searchExact(baseActor.refNum, baseActor.mpNum, baseActor.refId, true);

            if (!ptrFound) continue;

            initializeDedicatedActor(ptrFound);
        }
    }
}

void Cell::uninitializeLocalActors()
{
    for (const auto &actor : localActors)
    {
        Main::get().getCellController()->removeLocalActorRecord(actor.first);
        delete actor.second;
    }

    localActors.clear();
}

void Cell::uninitializeDedicatedActors(ActorList& actorList)
{
    for (const auto &baseActor : actorList.baseActors)
    {
        const std::string mapIndex = Main::get().getCellController()->generateMapIndex(baseActor);
        auto actorIt = dedicatedActors.find(mapIndex);
        if (actorIt == dedicatedActors.end())
            continue;

        Main::get().getCellController()->removeDedicatedActorRecord(mapIndex);
        delete actorIt->second;
        dedicatedActors.erase(actorIt);
    }
}

void Cell::uninitializeDedicatedActors()
{
    for (const auto &actor : dedicatedActors)
    {
        Main::get().getCellController()->removeDedicatedActorRecord(actor.first);
        delete actor.second;
    }

    dedicatedActors.clear();
}

void Cell::uninitializeActor(const MWWorld::Ptr& ptr)
{
    if (ptr.isEmpty())
        return;

    CellController *cellController = Main::get().getCellController();
    const std::string mapIndex = cellController->generateMapIndex(ptr);

    auto localIt = localActors.find(mapIndex);
    if (localIt != localActors.end())
    {
        cellController->removeLocalActorRecord(mapIndex);
        delete localIt->second;
        localActors.erase(localIt);
    }

    auto dedicatedIt = dedicatedActors.find(mapIndex);
    if (dedicatedIt != dedicatedActors.end())
    {
        cellController->removeDedicatedActorRecord(mapIndex);
        delete dedicatedIt->second;
        dedicatedActors.erase(dedicatedIt);
    }
}

void Cell::prepareDedicatedActorsForAuthority()
{
    // Apply the last complete network snapshot to the real Ptr before replacing
    // DedicatedActor wrappers with LocalActors. This keeps position, movement
    // flags and AI state continuous across an authority handoff.
    for (const auto& entry : dedicatedActors)
    {
        DedicatedActor *actor = entry.second;
        if (!actor || actor->getPtr().isEmpty())
            continue;

        if (actor->hasPositionData)
        {
            actor->setPosition();
            actor->setMovementSettings();
        }
        actor->setAnimFlags();
        actor->setStatsDynamic();
        // Re-resolve AI targets immediately before converting to LocalActor.
        // This closes the door-arrival race where the target did not exist when
        // the cached snapshot first arrived.
        //
        // X044: setAi() is a no-op for an actor that never received an AI packet,
        // so a plain idle NPC keeps the Wander package it was loaded with instead
        // of having its whole AiSequence cancelled by the default CANCEL action.
        if (actor->hasReceivedAi)
            actor->setAi();

        // We are about to own this actor, so undo the puppet-only pacification.
        restoreOwnedActorAi(actor->getPtr());
    }
}

LocalActor *Cell::getLocalActor(std::string actorIndex)
{
    return localActors.at(actorIndex);
}

DedicatedActor *Cell::getDedicatedActor(std::string actorIndex)
{
    return dedicatedActors.at(actorIndex);
}

bool Cell::hasLocalAuthority()
{
    return authorityGuid == Main::get().getLocalPlayer()->guid;
}

void Cell::setAuthority(const RakNet::RakNetGUID& guid)
{
    authorityGuid = guid;
}

MWWorld::CellStore *Cell::getCellStore()
{
    return store;
}

std::string Cell::getShortDescription()
{
    return store->getCell()->getShortDescription();
}
