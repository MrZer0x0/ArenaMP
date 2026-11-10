#ifndef OPENMW_MWMECHANICS_ARENACOMBATACTORS_H
#define OPENMW_MWMECHANICS_ARENACOMBATACTORS_H
#include "arenacombatlimits.hpp"
#include "actorutil.hpp"
#include "creaturestats.hpp"
#include "aisequence.hpp"
#include "aipackage.hpp"
#include "../mwworld/class.hpp"
#include "../mwmp/PlayerList.hpp"
namespace MWMechanics { namespace ArenaCombatLimits {
inline bool networkPlayer(const MWWorld::Ptr& actor)
{
    return !actor.isEmpty() && actor.getClass().isActor()
        && (actor == getPlayer() || mwmp::PlayerList::isDedicatedPlayer(actor));
}
// Use an owner only when its summon map actually contains this actor. Follow
// packages alone must never classify an escort/commanded NPC as a summon.
inline MWWorld::Ptr summonOwner(const MWWorld::Ptr& actor)
{
    if (actor.isEmpty() || !actor.getClass().isActor()) return MWWorld::Ptr();
    auto& stats = actor.getClass().getCreatureStats(actor);
    const int id = stats.getActorId();
    for (const auto& package : stats.getAiSequence())
    {
        if (!package || !package->followTargetThroughDoors()) continue;
        const auto owner = package->getTarget();
        if (owner.isEmpty() || !owner.getClass().isActor()) continue;
        for (const auto& entry : owner.getClass().getCreatureStats(owner).getSummonedCreatureMap())
            if (entry.second == id) return owner;
    }
    return MWWorld::Ptr();
}
inline float burstFraction(const MWWorld::Ptr& target, const MWWorld::Ptr& source)
{
    if (target.isEmpty() || !target.getClass().isActor()) return 0.f;
    auto& targetStats = target.getClass().getCreatureStats(target);
    const bool targetPlayer = networkPlayer(target);
    const auto owner = summonOwner(source);
    const auto effectiveSource = owner.isEmpty() ? source : owner;
    const bool sourcePlayer = networkPlayer(effectiveSource);
    // Y114: only cross-side player/hostile PvE uses the 10..60% level rule.
    // Player-versus-player keeps its independent protection; NPC-versus-NPC
    // and world hazards retain the previous behavior.
    if (!effectiveSource.isEmpty() && effectiveSource.getClass().isActor()
        && targetPlayer != sourcePlayer)
        return pveLevelDamageFraction(effectiveSource.getClass().getCreatureStats(effectiveSource).getLevel(),
            targetStats.getLevel());
    return targetPlayer ? (sourcePlayer && effectiveSource != target ? 0.45f : 0.65f)
        : npcBurstFraction(targetStats.getLevel(), targetStats.getHealth().getModified());
}

inline bool pveOpponents(const MWWorld::Ptr& target, const MWWorld::Ptr& attacker)
{
    if (target.isEmpty() || !target.getClass().isActor() || attacker.isEmpty()
        || !attacker.getClass().isActor() || target == attacker)
        return false;
    const auto owner = summonOwner(attacker);
    const auto source = owner.isEmpty() ? attacker : owner;
    return networkPlayer(target) != networkPlayer(source);
}

// The enchanted strike and the physical hit occur in the same synchronous
// attack call. A scoped shared budget prevents them from each taking 60% HP.
// Nested calls restore the previous scope. Only PvE receives this guard.
struct ImpactBudget
{
    MWWorld::Ptr victim;
    float remaining = 0.f;
    ImpactBudget* previous = nullptr;
};
inline thread_local ImpactBudget* activeImpactBudget = nullptr;
class ScopedImpactBudget
{
    ImpactBudget mBudget;
    bool mEnabled = false;
public:
    ScopedImpactBudget(const MWWorld::Ptr& target, const MWWorld::Ptr& attacker)
    {
        if (!pveOpponents(target, attacker)) return;
        auto& targetStats = target.getClass().getCreatureStats(target);
        mBudget.victim = target;
        mBudget.remaining = std::max(0.f, targetStats.getHealth().getModified())
            * burstFraction(target, attacker);
        mBudget.previous = activeImpactBudget;
        activeImpactBudget = &mBudget;
        mEnabled = true;
    }
    ~ScopedImpactBudget()
    {
        if (mEnabled) activeImpactBudget = mBudget.previous;
    }
    ScopedImpactBudget(const ScopedImpactBudget&) = delete;
    ScopedImpactBudget& operator=(const ScopedImpactBudget&) = delete;
};
inline float reserveActiveImpactDamage(const MWWorld::Ptr& target, float amount)
{
    if (amount <= 0.f || !activeImpactBudget || target.isEmpty() || !target.getClass().isActor()
        || target != activeImpactBudget->victim)
        return amount;
    const float approved = std::min(amount, activeImpactBudget->remaining);
    activeImpactBudget->remaining = std::max(0.f, activeImpactBudget->remaining - approved);
    return approved;
}
}}
#endif
