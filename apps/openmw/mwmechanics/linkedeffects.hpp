#ifndef MWMECHANICS_LINKEDEFFECTS_H
#define MWMECHANICS_LINKEDEFFECTS_H

#include <string>

namespace ESM
{
    struct ActiveEffect;
    struct EffectList;
    struct ENAMstruct;
    struct MagicEffect;
    struct Spell;
}

namespace MWWorld
{
    class Ptr;
}

namespace MWMechanics
{

    // Try to reflect a spell effect from caster off the actor being hit (target).
    // Target is deliberately first to match CastSpell::inflict and avoid swapping
    // the owner of Reflect with the original caster.
    bool reflectEffect(const ESM::ENAMstruct& effect, const ESM::MagicEffect* magicEffect,
                       const MWWorld::Ptr& target, const MWWorld::Ptr& caster, ESM::EffectList& reflectedEffects);

    // Try to absorb a stat (skill, attribute, etc.) from the target and transfer it to the caster.
    void absorbStat(const ESM::ENAMstruct& effect, const ESM::ActiveEffect& appliedEffect,
                    const MWWorld::Ptr& caster, const MWWorld::Ptr& target, bool reflected, const std::string& source);
}

#endif
