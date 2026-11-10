#include "linkedeffects.hpp"

#include <algorithm>

#include <components/misc/rng.hpp>
#include <components/settings/settings.hpp>
#include <components/esm/loadclas.hpp>
#include <components/esm/loadnpc.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"

#include "../mwmp/PlayerList.hpp"

#include "../mwrender/animation.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/inventorystore.hpp"

#include "actorutil.hpp"
#include "creaturestats.hpp"
#include "magiceffects.hpp"

namespace MWMechanics
{

    namespace
    {
        class GetReflectionProbability : public MWMechanics::EffectSourceVisitor
        {
        public:
            float mProbability{0.f};

            void visit(MWMechanics::EffectKey key, int /*effectIndex*/,
                       const std::string& /*sourceName*/, const std::string& /*sourceId*/, int /*casterActorId*/,
                       float magnitude, float /*remainingTime*/, float /*totalTime*/) override
            {
                if (key.mId != ESM::MagicEffect::Reflect || magnitude <= 0.f)
                    return;

                // Vanilla-style independent sources: 20% + 20% is 36%, not 40%.
                // This also prevents several pieces of Reflect equipment from
                // accidentally becoming a guaranteed 100% wall just by addition.
                const float sourceProbability = std::clamp(magnitude / 100.f, 0.f, 1.f);
                mProbability = 1.f - (1.f - mProbability) * (1.f - sourceProbability);
            }
        };

        int getReflectChance(const MWWorld::Ptr& target)
        {
            if (target.isEmpty() || !target.getClass().isActor())
                return 0;

            CreatureStats& stats = target.getClass().getCreatureStats(target);
            const float aggregate = stats.getMagicEffects().get(ESM::MagicEffect::Reflect).getMagnitude();

            float chance = 0.f;
            if (aggregate > 0.f)
            {
                GetReflectionProbability check;
                stats.getActiveSpells().visitEffectSources(check);
                stats.getSpells().visitEffectSources(check);
                if (target.getClass().hasInventoryStore(target))
                    target.getClass().getInventoryStore(target).visitEffectSources(check);

                // Some engine/mod effects may contribute directly to MagicEffects and
                // not expose an EffectSourceVisitor source. Preserve those by falling
                // back to the aggregate value only when no concrete source was found.
                float probability = check.mProbability;
                if (probability <= 0.f)
                    probability = std::clamp(aggregate / 100.f, 0.f, 1.f);

                chance = std::clamp(probability * 100.f, 0.f, 100.f);

                // Y061: NPC level amplifies an existing Reflect source, but never creates
                // reflection from zero. L1 = x1, L100 = x2, capped at 100%.
                if (target != getPlayer() && !mwmp::PlayerList::isDedicatedPlayer(target))
                {
                    const int level = std::clamp(stats.getLevel(), 1, 100);
                    const float levelMultiplier = 1.f + static_cast<float>(level - 1) / 99.f;
                    chance = std::min(100.f, chance * levelMultiplier);
                }
            }

            // ArenaMP Y083: veteran magic-specialisation NPCs gain innate Reflect.
            // From level 30 the chance starts at 15% and grows smoothly to 35%
            // at level 100. It is combined independently with equipment/spell Reflect,
            // so paralysis and other harmful reflectable spells can bounce without
            // turning every high-level mage into a guaranteed 100% magic wall.
            if (target.getClass().isNpc() && target != getPlayer()
                && !mwmp::PlayerList::isDedicatedPlayer(target))
            {
                const int level = stats.getLevel();
                if (level >= 30)
                {
                    const ESM::NPC* npc = target.get<ESM::NPC>()->mBase;
                    const ESM::Class* klass = MWBase::Environment::get().getWorld()->getStore()
                        .get<ESM::Class>().search(npc->mClass);
                    if (klass && klass->mData.mSpecialization == ESM::Class::Magic)
                    {
                        const float t = std::clamp(static_cast<float>(level - 30) / 70.f, 0.f, 1.f);
                        const float innateChance = 15.f + 20.f * t;
                        const float sourceProbability = std::clamp(chance / 100.f, 0.f, 1.f);
                        const float innateProbability = innateChance / 100.f;
                        chance = 100.f * (1.f - (1.f - sourceProbability) * (1.f - innateProbability));
                    }
                }
            }

            return static_cast<int>(std::clamp(chance, 0.f, 100.f));
        }
    }

    bool reflectEffect(const ESM::ENAMstruct& effect, const ESM::MagicEffect* magicEffect,
                       const MWWorld::Ptr& target, const MWWorld::Ptr& caster, ESM::EffectList& reflectedEffects)
    {
        // Important invariant: Reflect belongs to the actor being hit (target).
        // Keeping target first in this API prevents the historical caster/target
        // mix-up where wearing Reflect could behave as if it were on the enemy.
        if (target.isEmpty() || caster.isEmpty() || caster == target || !target.getClass().isActor())
            return false;

        const bool isHarmful = magicEffect->mData.mFlags & ESM::MagicEffect::Harmful;
        const bool isUnreflectable = magicEffect->mData.mFlags & ESM::MagicEffect::Unreflectable;
        if (!isHarmful || isUnreflectable)
            return false;

        const int reflectChance = getReflectChance(target);
        if (reflectChance <= 0 || Misc::Rng::roll0to99() >= reflectChance)
            return false;

        const ESM::Static* reflectStatic = MWBase::Environment::get().getWorld()->getStore().get<ESM::Static>().find ("VFX_Reflect");
        MWRender::Animation* animation = MWBase::Environment::get().getWorld()->getAnimation(target);
        if (animation && !reflectStatic->mModel.empty())
            animation->addEffect("meshes\\" + reflectStatic->mModel, ESM::MagicEffect::Reflect, false, std::string());
        reflectedEffects.mList.emplace_back(effect);
        return true;
    }

    void absorbStat(const ESM::ENAMstruct& effect, const ESM::ActiveEffect& appliedEffect,
                    const MWWorld::Ptr& caster, const MWWorld::Ptr& target, bool reflected, const std::string& source)
    {
        if (caster.isEmpty() || caster == target)
            return;

        if (!target.getClass().isActor() || !caster.getClass().isActor())
            return;

        // Make sure callers don't do something weird
        if (effect.mEffectID < ESM::MagicEffect::AbsorbAttribute || effect.mEffectID > ESM::MagicEffect::AbsorbSkill)
            throw std::runtime_error("invalid absorb stat effect");

        if (appliedEffect.mMagnitude == 0)
            return;

        std::vector<ActiveSpells::ActiveEffect> absorbEffects;
        ActiveSpells::ActiveEffect absorbEffect = appliedEffect;
        absorbEffect.mMagnitude *= -1;
        absorbEffect.mEffectIndex = appliedEffect.mEffectIndex;
        absorbEffects.emplace_back(absorbEffect);

        // Morrowind negates reflected Absorb spells so the original caster won't be harmed.
        if (reflected && Settings::Manager::getBool("classic reflected absorb spells behavior", "Game"))
        {
            target.getClass().getCreatureStats(target).getActiveSpells().addSpell(std::string(), true,
                            absorbEffects, source, caster.getClass().getCreatureStats(caster).getActorId());
            return;
        }

        caster.getClass().getCreatureStats(caster).getActiveSpells().addSpell(std::string(), true,
                        absorbEffects, source, target.getClass().getCreatureStats(target).getActorId());
    }
}
