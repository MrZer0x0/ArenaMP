#ifndef GAME_MWMECHANICS_ONSTRIKESTACKS_H
#define GAME_MWMECHANICS_ONSTRIKESTACKS_H

namespace MWWorld
{
    class Ptr;
}

namespace MWMechanics
{
    // ArenaMP U035 -----------------------------------------------------------
    // Heavy on-strike enchantment effects (paralysis, silence, reflect, stat
    // drain, ...) no longer land on the first blow. The attacker has to build a
    // stack on one and the same target: by default the first three hits are
    // absorbed and only the fourth applies the effect, after which the stack
    // resets.
    //
    // The counter lives on the pair (attacker, victim), not on the weapon, so
    // swapping weapons mid-fight keeps the progress while switching target
    // throws it away. Stacks decay on their own, so an old fight cannot be
    // resumed hours later with a free stun.
    // ------------------------------------------------------------------------
    namespace OnStrikeStacks
    {
        /// Effects that have to be earned. Everything else (damage, healing,
        /// buffs) is unaffected and still lands on every hit.
        bool isHeavyEffect(short effectId);

        /// One token per weapon strike. An enchantment carrying two effects of
        /// the same group must only charge the stack once.
        unsigned int nextStrikeToken();

        /// Charges the stack for this attacker/victim/effect group.
        /// @return true when the effect must be applied right now (the stack
        ///         was full and has been reset), false when this hit was
        ///         absorbed into the stack and the effect must be skipped.
        /// Repeated calls with the same strikeToken return the same answer
        /// without charging again.
        bool chargeAndTest(const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim,
            short effectId, unsigned int strikeToken);

        /// Stack size currently built up, for HUD/debug use. 0 when the feature
        /// is disabled or the effect is not gated.
        int currentStack(const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim, short effectId);

        /// Drops every stack that involves this actor. Cheap; safe to call for
        /// actors that never had a stack.
        void forgetActor(int actorId);
    }
}

#endif
