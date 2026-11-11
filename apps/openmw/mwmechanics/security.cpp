#include "security.hpp"

#include <algorithm>
#include <chrono>
#include <components/esm/loadmgef.hpp>
#include <components/esm/loadspel.hpp>
#include "../mwworld/inventorystore.hpp"
#include "../mwworld/actiontrap.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/soundmanager.hpp"
#include "spells.hpp"

/*
    Start of tes3mp addition

    Include additional headers for multiplayer purposes
*/
#include "../mwmp/Main.hpp"
#include "../mwmp/Networking.hpp"
#include "../mwmp/ObjectList.hpp"
#include "../mwmp/LocalPlayer.hpp"
#include "../mwmp/MechanicsHelper.hpp"
#include "actorutil.hpp"
/*
    End of tes3mp addition
*/

#include "../mwworld/cellstore.hpp"

#include <components/misc/rng.hpp>

#include "../mwworld/class.hpp"
#include "../mwworld/containerstore.hpp"
#include "../mwworld/esmstore.hpp"

#include "../mwbase/world.hpp"
#include "../mwbase/environment.hpp"
#include "../mwbase/mechanicsmanager.hpp"

#include "creaturestats.hpp"
#include "xpleveling.hpp"

namespace MWMechanics
{

    namespace
    {
        void consumeToolUse(const MWWorld::Ptr& actor, const MWWorld::Ptr& tool, int remaining)
        {
            auto* local = actor == getPlayer() ? mwmp::Main::get().getLocalPlayer() : nullptr;
            const auto old = MechanicsHelper::getItem(tool, 1);
            struct PacketGuard {
                mwmp::LocalPlayer* player; bool previous;
                explicit PacketGuard(mwmp::LocalPlayer* p) : player(p), previous(p && p->avoidSendingInventoryPackets)
                { if (player) player->avoidSendingInventoryPackets = true; }
                ~PacketGuard() { if (player) player->avoidSendingInventoryPackets = previous; }
            };
            bool send = local && !local->avoidSendingInventoryPackets;
            {
                PacketGuard guard(local);
                tool.getContainerStore()->unstack(tool, actor, 1);
                tool.getCellRef().setCharge(remaining);
                if (remaining == 0) tool.getContainerStore()->remove(tool, 1, actor);
            }
            if (send)
            {
                local->sendItemChange(old, mwmp::InventoryChanges::REMOVE);
                if (remaining > 0) local->sendItemChange(tool, 1, mwmp::InventoryChanges::ADD);
            }
        }
    }

    bool Security::autoActivate(const MWWorld::Ptr& target, const MWWorld::Ptr& actor)
    {
        if (actor != getPlayer() || !actor.getClass().hasInventoryStore(actor)) return false;
        const bool trapped = !target.getCellRef().getTrap().empty();
        const bool locked = target.getCellRef().getLockLevel() > 0;
        if (!trapped && !locked) return false;
        auto* wm = MWBase::Environment::get().getWindowManager();
        auto* world = MWBase::Environment::get().getWorld();
        auto* mechanics = MWBase::Environment::get().getMechanicsManager();
        auto& stats = actor.getClass().getCreatureStats(actor);
        if (stats.isDead() || stats.isParalyzed() || stats.getKnockedDown()
            || (mechanics->isAttackingOrSpell(actor) && !mechanics->isWeaponTransition(actor))) return true;
        if (world->getDistanceToFacedObject() > world->getMaxActivationDistance()) return false;
        static auto nextAttempt = std::chrono::steady_clock::time_point::min();
        const auto now = std::chrono::steady_clock::now();
        if (now < nextAttempt) return true;
        nextAttempt = now + std::chrono::milliseconds(550);

        auto& inventory = actor.getClass().getInventoryStore(actor);
        const auto right = inventory.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
        const bool selectedTool = right != inventory.end()
            && (right->getTypeName() == typeid(ESM::Lockpick).name() || right->getTypeName() == typeid(ESM::Probe).name());
        const std::string spellId = wm->getSelectedSpell();
        const ESM::Spell* spell = spellId.empty() ? nullptr : world->getStore().get<ESM::Spell>().search(spellId);
        if (!trapped && locked && spell && stats.getSpells().hasSpell(spell)
            && (!selectedTool || stats.getDrawState() == DrawState_Spell))
        {
            const bool opens = std::any_of(spell->mEffects.mList.begin(), spell->mEffects.mList.end(),
                [](const ESM::ENAMstruct& effect) { return effect.mEffectID == ESM::MagicEffect::Open && effect.mRange != 0; });
            if (opens)
            {
                // Ordinary casting owns mana, chance, animation and network release.
                // An Open spell never silently removes a trap.
                mechanics->startSecurityAction(actor, target, MWWorld::Ptr(), spellId);
                return true;
            }
        }
        if (locked && target.getCellRef().getLockLevel() == ESM::UnbreakableLock && !trapped)
        { wm->messageBox("#{sLockImpossible}"); return true; }
        MWWorld::Ptr tool;
        float best = -1.f;
        for (const auto& candidate : inventory)
        {
            const bool correct = trapped ? candidate.getTypeName() == typeid(ESM::Probe).name()
                : candidate.getTypeName() == typeid(ESM::Lockpick).name();
            if (!correct || candidate.getClass().getItemHealth(candidate) <= 0) continue;
            if (right != inventory.end() && candidate == *right) { tool = candidate; break; }
            const float quality = trapped ? candidate.get<ESM::Probe>()->mBase->mData.mQuality
                : candidate.get<ESM::Lockpick>()->mBase->mData.mQuality;
            if (quality > best) { best = quality; tool = candidate; }
        }
        if (tool.isEmpty() && !trapped)
        {
            // Fall back to a learned Open spell when no usable lockpick exists.
            // Casting still owns mana, failure chance and the animation release.
            const ESM::Spell* opening = nullptr;
            for (const auto& entry : stats.getSpells())
            {
                const ESM::Spell* known = entry.first;
                if (known->mData.mType != ESM::Spell::ST_Spell) continue;
                const bool opens = std::any_of(known->mEffects.mList.begin(), known->mEffects.mList.end(),
                    [](const ESM::ENAMstruct& effect) {
                        return effect.mEffectID == ESM::MagicEffect::Open && effect.mRange != 0;
                    });
                if (opens && (!opening || known->mData.mCost < opening->mData.mCost)) opening = known;
            }
            if (opening)
            {
                mechanics->startSecurityAction(actor, target, MWWorld::Ptr(), opening->mId);
                return true;
            }
        }
        if (tool.isEmpty())
        {
            if (trapped)
            {
                // No usable probe: activate the trap instead of blocking the door.
                // ActionTrap applies the spell to the player and sends the trap packet
                // to the server, which clears it. The lock (if any) stays locked.
                MWWorld::ActionTrap trapAction(target.getCellRef().getTrap(), target);
                trapAction.setSound("Disarm Trap Fail");
                trapAction.execute(actor);
                return true;
            }
            wm->messageBox("#{sLockImpossible}");
            return true;
        }
        mechanics->startSecurityAction(actor, target, tool, std::string());
        // Even success waits for the server lock/trap packet. A second activation
        // opens the now-safe object; never open it optimistically in this frame.
        return true;
    }

    Security::Security(const MWWorld::Ptr &actor)
        : mActor(actor)
    {
        CreatureStats& creatureStats = actor.getClass().getCreatureStats(actor);
        mAgility = creatureStats.getAttribute(ESM::Attribute::Agility).getModified();
        mLuck = creatureStats.getAttribute(ESM::Attribute::Luck).getModified();
        mSecuritySkill = static_cast<float>(actor.getClass().getSkill(actor, ESM::Skill::Security));
        mFatigueTerm = creatureStats.getFatigueTerm();
    }

    void Security::pickLock(const MWWorld::Ptr &lock, const MWWorld::Ptr &lockpick,
                            std::string& resultMessage, std::string& resultSound)
    {
        if (lock.getCellRef().getLockLevel() <= 0 ||
            lock.getCellRef().getLockLevel() == ESM::UnbreakableLock ||
            !lock.getClass().hasToolTip(lock)) //If it's unlocked or can not be unlocked back out immediately
            return;

        int uses = lockpick.getClass().getItemHealth(lockpick);
        if (uses == 0)
            return;

        int lockStrength = lock.getCellRef().getLockLevel();

        float pickQuality = lockpick.get<ESM::Lockpick>()->mBase->mData.mQuality;

        float fPickLockMult = MWBase::Environment::get().getWorld()->getStore().get<ESM::GameSetting>().find("fPickLockMult")->mValue.getFloat();

        float x = 0.2f * mAgility + 0.1f * mLuck + mSecuritySkill;
        x *= pickQuality * mFatigueTerm;
        x += fPickLockMult * lockStrength;

        MWBase::Environment::get().getMechanicsManager()->unlockAttempted(mActor, lock);

        resultSound = "Open Lock Fail";
        if (x <= 0)
            resultMessage = "#{sLockImpossible}";
        else
        {
            if (Misc::Rng::roll0to99() <= x)
            {
                /*
                    Start of tes3mp change (major)

                    Disable unilateral locking on this client and expect the server's reply to our
                    packet to do it instead
                */
                //lock.getCellRef().unlock();
                /*
                    End of tes3mp change (major)
                */

                /*
                    Start of tes3mp addition

                    Send an ID_OBJECT_LOCK packet every time an object is unlocked here
                */
                mwmp::ObjectList *objectList = mwmp::Main::get().getNetworking()->getObjectList();
                objectList->reset();
                objectList->packetOrigin = mwmp::CLIENT_GAMEPLAY;
                objectList->addObjectLock(lock, 0);
                objectList->sendObjectLock();
                /*
                    End of tes3mp addition
                */

                resultMessage = "#{sLockSuccess}";
                resultSound = "Open Lock";
                const float xpFactor = XPLeveling::isEnabled()
                    ? 1.f + std::max(0, lockStrength) / 50.f
                    : 1.f;
                mActor.getClass().skillUsageSucceeded(mActor, ESM::Skill::Security, 1, xpFactor);
            }
            else
                resultMessage = "#{sLockFail}";
        }

        consumeToolUse(mActor, lockpick, uses - 1);
    }

    void Security::probeTrap(const MWWorld::Ptr &trap, const MWWorld::Ptr &probe,
                             std::string& resultMessage, std::string& resultSound)
    {
        if (trap.getCellRef().getTrap().empty())
            return;

        int uses = probe.getClass().getItemHealth(probe);
        if (uses == 0)
            return;

        float probeQuality = probe.get<ESM::Probe>()->mBase->mData.mQuality;

        const ESM::Spell* trapSpell = MWBase::Environment::get().getWorld()->getStore().get<ESM::Spell>().find(trap.getCellRef().getTrap());
        int trapSpellPoints = trapSpell->mData.mCost;

        float fTrapCostMult = MWBase::Environment::get().getWorld()->getStore().get<ESM::GameSetting>().find("fTrapCostMult")->mValue.getFloat();

        float x = 0.2f * mAgility + 0.1f * mLuck + mSecuritySkill;
        x += fTrapCostMult * trapSpellPoints;
        x *= probeQuality * mFatigueTerm;

        MWBase::Environment::get().getMechanicsManager()->unlockAttempted(mActor, trap);

        resultSound = "Disarm Trap Fail";
        if (x <= 0)
            resultMessage = "#{sTrapImpossible}";
        else
        {
            if (Misc::Rng::roll0to99() <= x)
            {
                /*
                    Start of tes3mp change (major)

                    Disable unilateral trap disarming on this client and expect the server's reply to our
                    packet to do it instead
                */
                //trap.getCellRef().setTrap("");
                /*
                    End of tes3mp change (major)
                */

                resultSound = "Disarm Trap";
                resultMessage = "#{sTrapSuccess}";
                const float xpFactor = XPLeveling::isEnabled()
                    ? 1.f + std::max(0, trapSpellPoints) / 50.f
                    : 1.f;
                mActor.getClass().skillUsageSucceeded(mActor, ESM::Skill::Security, 0, xpFactor);

                /*
                    Start of tes3mp addition

                    Send an ID_OBJECT_TRAP packet every time a trap is disarmed
                */
                mwmp::ObjectList *objectList = mwmp::Main::get().getNetworking()->getObjectList();
                objectList->reset();
                objectList->packetOrigin = mwmp::CLIENT_GAMEPLAY;
                objectList->addObjectTrap(trap, trap.getRefData().getPosition(), true);
                objectList->sendObjectTrap();
                /*
                    End of tes3mp addition
                */
            }
            else
                resultMessage = "#{sTrapFail}";
        }

        consumeToolUse(mActor, probe, uses - 1);
    }

}
