#include "activeguard.hpp"
#include "combat.hpp"
#include "guardrules.hpp"
#include "creaturestats.hpp"
#include "npcstats.hpp"
#include "actorutil.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwmp/Main.hpp"
#include "../mwmp/CellController.hpp"
#include "../mwmp/PlayerList.hpp"
#include "../mwmp/MechanicsHelper.hpp"
#include <components/openmw-mp/Base/BaseStructs.hpp>
#include <components/misc/rng.hpp>
#include <components/esm/loadarmo.hpp>
#include <components/esm/loadweap.hpp>
#include <components/esm/attr.hpp>
#include <components/esm/loadskil.hpp>
#include <algorithm>
#include <cmath>

namespace MWMechanics::ActiveGuard
{
namespace
{
    bool local(const MWWorld::Ptr& p)
    {
        return p == getPlayer() || mwmp::Main::get().getCellController()->isLocalActor(p);
    }
    struct Tool { MWWorld::Ptr item; bool valid = false; bool shield = false; int type = -1; float skill = 0; };
    Tool tool(const MWWorld::Ptr& p)
    {
        Tool t;
        if (p.isEmpty() || !p.getClass().hasInventoryStore(p) || !p.getClass().isBipedal(p)) return t;
        auto& inv = p.getClass().getInventoryStore(p);
        auto right = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
        auto left = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedLeft);
        int type = -1;
        if (right != inv.end() && right->getTypeName() == typeid(ESM::Weapon).name())
            type = right->get<ESM::Weapon>()->mBase->mData.mType;
        // Never borrow a shield or an unarmed stance while holding a ranged
        // weapon, ammunition, or a non-weapon tool in the right hand.
        if (right != inv.end() && (type < ESM::Weapon::ShortBladeOneHand
            || type > ESM::Weapon::AxeTwoHand)) return t;
        const bool oneHand = type == -1 || type == ESM::Weapon::ShortBladeOneHand
            || type == ESM::Weapon::LongBladeOneHand || type == ESM::Weapon::BluntOneHand || type == ESM::Weapon::AxeOneHand;
        if (oneHand && left != inv.end() && left->getTypeName() == typeid(ESM::Armor).name()
            && left->get<ESM::Armor>()->mBase->mData.mType == ESM::Armor::Shield)
        {
            t.item = *left; t.shield = true; t.valid = true;
            t.skill = p.getClass().getSkill(p, ESM::Skill::Block);
        }
        else if (right != inv.end())
        {
            if (type < ESM::Weapon::ShortBladeOneHand || type > ESM::Weapon::AxeTwoHand) return t;
            t.item = *right; t.valid = true; t.type = type;
            t.skill = .65f * p.getClass().getSkill(p, right->getClass().getEquipmentSkill(*right))
                + .35f * p.getClass().getSkill(p, ESM::Skill::Block);
        }
        else { t.valid = true; t.skill = p.getClass().getSkill(p, ESM::Skill::HandToHand); }
        if (!t.item.isEmpty() && t.item.getClass().hasItemHealth(t.item) && t.item.getClass().getItemHealth(t.item) <= 0)
            t.valid = false;
        return t;
    }
    bool stateAllowed(const MWWorld::Ptr& p)
    {
        if (p.isEmpty() || !p.getClass().isActor()) return false;
        if (p.getClass().isNpc() && p.getClass().getNpcStats(p).isWerewolf()) return false;
        auto& s = p.getClass().getCreatureStats(p);
        return !s.isDead() && !s.getKnockedDown() && !s.getHitRecovery() && !s.isParalyzed()
            && s.getDrawState() == DrawState_Weapon && s.getFatigue().getCurrent() > 0 && s.mGuard.stagger <= 0;
    }
}
bool canGuardWithEquipment(const MWWorld::Ptr& p) { return tool(p).valid; }
bool available(const MWWorld::Ptr& p) { return stateAllowed(p) && canGuardWithEquipment(p); }
bool raised(const MWWorld::Ptr& p)
{
    return available(p) && p.getClass().getCreatureStats(p).getMovementFlag(CreatureStats::Flag_ActiveGuard);
}
void request(const MWWorld::Ptr& p, bool held)
{
    if (p.isEmpty() || !p.getClass().isActor()) return;
    auto& s = p.getClass().getCreatureStats(p);
    const bool was = s.getMovementFlag(CreatureStats::Flag_ActiveGuard);
    held = held && available(p) && (was || s.mGuard.cooldown <= 0.f);
    if (held && !was) s.mGuard.age = 0.f;
    if (!held && was) s.mGuard.cooldown = std::max(s.mGuard.cooldown, .45f);
    s.setMovementFlag(CreatureStats::Flag_ActiveGuard, held);
}
void update(const MWWorld::Ptr& p, float dt)
{
    if (p.isEmpty() || !p.getClass().isActor()) return;
    auto& s = p.getClass().getCreatureStats(p);
    auto& g = s.mGuard;
    if (!std::isfinite(dt) || dt <= 0.f) return;
    g.cooldown = std::max(0.f, g.cooldown - dt);
    g.stagger = std::max(0.f, g.stagger - dt);
    if (local(p) && !available(p)) request(p, false);
    bool held = s.getMovementFlag(CreatureStats::Flag_ActiveGuard);
    if (held && !g.previous) g.age = 0.f;
    else if (held) g.age += dt;
    g.previous = held;
    if (!local(p)) return; // Remote AI and guard costs belong to their owner.
    if (held)
    {
        auto fatigue = s.getFatigue();
        fatigue.setCurrent(std::max(0.f, fatigue.getCurrent() - 4.f * dt));
        s.setFatigue(fatigue);
    }
    if (p == getPlayer()) return;
    // Evaluate NPC reactions at 5 Hz, only on the actor-owning client.
    g.aiTimer -= dt;
    if (g.aiTimer > 0.f) return;
    g.aiTimer = .2f;
    if (held) { if (g.age > .65f) request(p, false); return; }
    MWWorld::Ptr enemy;
    if (g.cooldown > 0.f || !s.getAiSequence().getCombatTarget(enemy) || enemy.isEmpty()
        || !available(p) || MWBase::Environment::get().getMechanicsManager()->isAttackingOrSpell(p)) return;
    if ((enemy.getRefData().getPosition().asVec3() - p.getRefData().getPosition().asVec3()).length2() > 90000.f) return;
    if (!MWBase::Environment::get().getMechanicsManager()->isAttackingOrSpell(enemy)) return;
    const float chance = std::clamp(.2f + tool(p).skill * .006f, .2f, .8f);
    if (Misc::Rng::rollProbability() < chance) request(p, true);
}
std::string animation(const MWWorld::Ptr& p)
{
    const auto t = tool(p);
    if (t.shield) return "shieldraisev2";
    if (t.item.isEmpty()) return "h2hblock";
    switch (t.type)
    {
        case ESM::Weapon::LongBladeTwoHand: return "2hswhighguardv2";
        case ESM::Weapon::BluntTwoWide:
        case ESM::Weapon::SpearTwoWide: return "2hstaffspearguard";
        case ESM::Weapon::BluntTwoClose:
        case ESM::Weapon::AxeTwoHand: return "2haxebluntguard";
        default: return "1hsupportedhighguard";
    }
}
void replay(const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim, const mwmp::Attack& r)
{
    if (victim.isEmpty() || !victim.getClass().isActor() || r.guardResult == 0 || r.guardResult > 3) return;
    auto& s = victim.getClass().getCreatureStats(victim);
    if (local(victim))
    {
        if (std::isfinite(r.guardFatigue))
        {
            auto f = s.getFatigue();
            f.setCurrent(std::max(0.f, f.getCurrent() - std::clamp(r.guardFatigue, 0.f, 500.f)));
            s.setFatigue(f);
        }
        const auto t = tool(victim);
        if (!t.item.isEmpty() && t.item.getCellRef().getRefId() == r.guardItem && std::isfinite(r.guardWear)
            && t.item.getClass().hasItemHealth(t.item))
        {
            // Y110: the owning client applies armour skills once. guardWear on
            // the wire stays the raw guard cost; weapon parry wear is unchanged.
            const float rawWear = std::clamp(r.guardWear, 0.f, 50.f);
            const int wear = t.shield ? getArmorWear(victim, t.item, rawWear) : int(rawWear);
            const int health = std::max(0, t.item.getClass().getItemHealth(t.item) - wear);
            t.item.getCellRef().setCharge(health);
            if (health == 0) victim.getClass().getInventoryStore(victim).unequipItem(t.item, victim);
        }
        if (victim == getPlayer() && r.guardResult != 3)
            victim.getClass().skillUsageSucceeded(victim, ESM::Skill::Block, 0);
    }
    if (r.guardResult == 3)
    {
        s.mGuard.stagger = .65f;
        request(victim, false);
        s.setHitRecovery(true);
    }
    if (r.guardResult == 2 && r.type == mwmp::Attack::MELEE && !attacker.isEmpty() && attacker.getClass().isActor())
    {
        auto& a = attacker.getClass().getCreatureStats(attacker);
        a.mGuard.stagger = .55f;
        request(attacker, false);
        a.setHitRecovery(true);
    }
    MWBase::Environment::get().getSoundManager()->playSound3D(victim, "Heavy Armor Hit", .8f, 1.f);
}
bool resolve(const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim, const MWWorld::Ptr& weapon,
    float& damage, bool ranged, mwmp::Attack* packet)
{
    if (packet) { packet->guardResult = 0; packet->guardFatigue = 0; packet->guardWear = 0; packet->guardItem.clear(); packet->block = false; }
    if (!raised(victim) || attacker.isEmpty() || !MechanicsHelper::isFriendlyFireAllowed(attacker, victim)) return false;
    const auto t = tool(victim);
    if (ranged && !t.shield && t.skill < 90.f) return false;
    if (!weapon.isEmpty() && t.item.isEmpty() && t.skill < 90.f) return false;
    osg::Vec3f direction = attacker.getRefData().getPosition().asVec3() - victim.getRefData().getPosition().asVec3();
    direction.z() = 0.f;
    if (direction.normalize() == 0.f) return false;
    const float yaw = victim.getRefData().getPosition().rot[2];
    const osg::Vec3f forward(std::sin(yaw), std::cos(yaw), 0.f);
    const float arcCos = t.shield ? .5f : (t.item.isEmpty() ? .766044f : .642788f);
    if (direction * forward < arcCos) return false;
    auto& s = victim.getClass().getCreatureStats(victim);
    const auto outcome = GuardRules::resolve(damage, s.getFatigue().getCurrent(), t.skill, s.mGuard.age, t.shield, ranged);
    if (!outcome.kind) return false;
    mwmp::Attack result;
    result.type = ranged ? mwmp::Attack::RANGED : mwmp::Attack::MELEE;
    result.guardResult = outcome.kind;
    result.guardFatigue = outcome.fatigue;
    result.guardWear = outcome.wear;
    if (!t.item.isEmpty()) result.guardItem = t.item.getCellRef().getRefId();
    result.block = outcome.kind == 2;
    damage = outcome.damage;
    if (packet)
    {
        packet->guardResult = result.guardResult; packet->guardFatigue = result.guardFatigue;
        packet->guardWear = result.guardWear; packet->guardItem = result.guardItem; packet->block = result.block;
    }
    replay(attacker, victim, result);
    return true;
}
}
