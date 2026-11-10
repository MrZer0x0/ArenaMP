#include "onstrikestacks.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <unordered_map>

#include <components/esm/loadmgef.hpp>
#include <components/settings/settings.hpp>

#include "creaturestats.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/ptr.hpp"

namespace
{
    enum HeavyGroup
    {
        Group_Control = 0,  // takes control of the character away
        Group_Reflect,      // reflect / spell absorption
        Group_Mind,         // charm, demoralize, frenzy, calm, rally
        Group_Drain,        // lasting attribute and equipment damage
        Group_Count
    };

    int heavyGroup(short effectId)
    {
        switch (effectId)
        {
            case ESM::MagicEffect::Paralyze:
            case ESM::MagicEffect::Silence:
            case ESM::MagicEffect::Blind:
            case ESM::MagicEffect::Sound:
                return Group_Control;

            case ESM::MagicEffect::Reflect:
            case ESM::MagicEffect::SpellAbsorption:
                return Group_Reflect;

            case ESM::MagicEffect::Charm:
            case ESM::MagicEffect::CalmHumanoid:
            case ESM::MagicEffect::CalmCreature:
            case ESM::MagicEffect::FrenzyHumanoid:
            case ESM::MagicEffect::FrenzyCreature:
            case ESM::MagicEffect::DemoralizeHumanoid:
            case ESM::MagicEffect::DemoralizeCreature:
            case ESM::MagicEffect::RallyHumanoid:
            case ESM::MagicEffect::RallyCreature:
                return Group_Mind;

            case ESM::MagicEffect::DamageAttribute:
            case ESM::MagicEffect::DrainAttribute:
            case ESM::MagicEffect::AbsorbAttribute:
            case ESM::MagicEffect::DisintegrateWeapon:
            case ESM::MagicEffect::DisintegrateArmor:
            case ESM::MagicEffect::Soultrap:
                return Group_Drain;

            default:
                return -1;
        }
    }

    // Settings::Manager throws on an unknown key, and this file has to keep
    // working against a settings-default.cfg that predates U035.
    int settingInt(const char* name, int fallback)
    {
        try
        {
            return Settings::Manager::getInt(name, "Game");
        }
        catch (const std::exception&)
        {
            return fallback;
        }
    }

    float settingFloat(const char* name, float fallback)
    {
        try
        {
            return Settings::Manager::getFloat(name, "Game");
        }
        catch (const std::exception&)
        {
            return fallback;
        }
    }

    int requiredHits()
    {
        // 0 or 1 means "apply on the first hit", i.e. vanilla behaviour.
        return std::max(0, std::min(16, settingInt("combat on strike heavy stack", 4)));
    }

    double decaySeconds()
    {
        return static_cast<double>(std::max(0.5f, settingFloat("combat on strike stack decay", 6.f)));
    }

    double monotonicSeconds()
    {
        using namespace std::chrono;
        return duration_cast<duration<double>>(steady_clock::now().time_since_epoch()).count();
    }

    struct PairStacks
    {
        std::uint8_t mHits[Group_Count] = {0, 0, 0, 0};
        double mLastHit[Group_Count] = {0.0, 0.0, 0.0, 0.0};
        unsigned int mLastToken[Group_Count] = {0, 0, 0, 0};
        bool mLastDecision[Group_Count] = {false, false, false, false};
        double mTouched = 0.0;
    };

    std::unordered_map<std::uint64_t, PairStacks>& stackMap()
    {
        static std::unordered_map<std::uint64_t, PairStacks> sStacks;
        return sStacks;
    }

    std::uint64_t pairKey(int attackerId, int victimId)
    {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(attackerId)) << 32)
            | static_cast<std::uint64_t>(static_cast<std::uint32_t>(victimId));
    }

    bool actorIds(const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim, int& attackerId, int& victimId)
    {
        if (attacker.isEmpty() || victim.isEmpty())
            return false;
        if (!attacker.getClass().isActor() || !victim.getClass().isActor())
            return false;

        attackerId = attacker.getClass().getCreatureStats(attacker).getActorId();
        victimId = victim.getClass().getCreatureStats(victim).getActorId();
        return attackerId != -1 && victimId != -1 && attackerId != victimId;
    }

    // Keep the table from growing without bound in a long session: once it gets
    // large, drop everything that has not been touched for well past the decay
    // window. Those entries are dead weight - their stacks have already expired.
    void collectGarbage(double now)
    {
        auto& stacks = stackMap();
        if (stacks.size() < 256)
            return;

        const double cutoff = now - decaySeconds() * 4.0;
        for (auto it = stacks.begin(); it != stacks.end();)
        {
            if (it->second.mTouched < cutoff)
                it = stacks.erase(it);
            else
                ++it;
        }
    }
}

namespace MWMechanics
{
    namespace OnStrikeStacks
    {
        bool isHeavyEffect(short effectId)
        {
            return heavyGroup(effectId) >= 0;
        }

        unsigned int nextStrikeToken()
        {
            static unsigned int sToken = 0;
            // 0 is reserved for "no token", so skip it on wrap-around.
            if (++sToken == 0)
                sToken = 1;
            return sToken;
        }

        bool chargeAndTest(const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim,
            short effectId, unsigned int strikeToken)
        {
            const int group = heavyGroup(effectId);
            if (group < 0)
                return true;

            const int needed = requiredHits();
            if (needed <= 1)
                return true;

            int attackerId = -1;
            int victimId = -1;
            if (!actorIds(attacker, victim, attackerId, victimId))
                return true;

            const double now = monotonicSeconds();
            collectGarbage(now);

            PairStacks& entry = stackMap()[pairKey(attackerId, victimId)];
            entry.mTouched = now;

            // Same strike, second effect of the same group: reuse the verdict
            // instead of charging the stack twice.
            if (strikeToken != 0 && entry.mLastToken[group] == strikeToken)
                return entry.mLastDecision[group];

            if (entry.mLastHit[group] > 0.0 && now - entry.mLastHit[group] > decaySeconds())
                entry.mHits[group] = 0;
            entry.mLastHit[group] = now;

            entry.mHits[group] = static_cast<std::uint8_t>(entry.mHits[group] + 1);
            const bool apply = entry.mHits[group] >= static_cast<std::uint8_t>(needed);
            if (apply)
                entry.mHits[group] = 0;

            entry.mLastToken[group] = strikeToken;
            entry.mLastDecision[group] = apply;
            return apply;
        }

        int currentStack(const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim, short effectId)
        {
            const int group = heavyGroup(effectId);
            if (group < 0 || requiredHits() <= 1)
                return 0;

            int attackerId = -1;
            int victimId = -1;
            if (!actorIds(attacker, victim, attackerId, victimId))
                return 0;

            auto& stacks = stackMap();
            const auto it = stacks.find(pairKey(attackerId, victimId));
            if (it == stacks.end())
                return 0;

            const double now = monotonicSeconds();
            if (it->second.mLastHit[group] > 0.0 && now - it->second.mLastHit[group] > decaySeconds())
                return 0;
            return it->second.mHits[group];
        }

        void forgetActor(int actorId)
        {
            if (actorId == -1)
                return;

            auto& stacks = stackMap();
            const std::uint32_t target = static_cast<std::uint32_t>(actorId);
            for (auto it = stacks.begin(); it != stacks.end();)
            {
                const std::uint32_t high = static_cast<std::uint32_t>(it->first >> 32);
                const std::uint32_t low = static_cast<std::uint32_t>(it->first & 0xFFFFFFFFull);
                if (high == target || low == target)
                    it = stacks.erase(it);
                else
                    ++it;
            }
        }
    }
}
