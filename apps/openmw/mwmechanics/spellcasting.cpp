#include "../mwmechanics/arenacombatactors.hpp"
#include "spellcasting.hpp"

#include <algorithm>
#include <components/misc/constants.hpp>
#include <components/misc/rng.hpp>

/*
    Start of tes3mp addition

    Include additional headers for multiplayer purposes
*/
#include <components/openmw-mp/TimedLog.hpp>
#include "../mwmp/Main.hpp"
#include "../mwmp/Networking.hpp"
#include "../mwmp/PlayerList.hpp"
#include "../mwmp/LocalPlayer.hpp"
#include "../mwmp/ObjectList.hpp"
#include "../mwmp/CellController.hpp"
#include "../mwmp/MechanicsHelper.hpp"
#include "../mwmp/GUIController.hpp"
/*
    End of tes3mp addition
*/

#include "../mwbase/windowmanager.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"

#include "../mwworld/containerstore.hpp"
#include "../mwworld/actionteleport.hpp"
#include "../mwworld/player.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/inventorystore.hpp"

#include "../mwrender/animation.hpp"

#include <exception>

#include <components/settings/settings.hpp>

#include "actorutil.hpp"
#include "aifollow.hpp"
#include "onstrikestacks.hpp"
#include "creaturestats.hpp"
#include "linkedeffects.hpp"
#include "spellabsorption.hpp"
#include "spellresistance.hpp"
#include "spellutil.hpp"
#include "summoning.hpp"
#include "tickableeffects.hpp"
#include "weapontype.hpp"


// EncoreMP additions

#include "difficultyscaling.hpp"




namespace MWMechanics
{
    namespace
    {

        bool isArenaNetworkPlayer(const MWWorld::Ptr& actor)
        {
            return !actor.isEmpty() && actor.getClass().isActor()
                && (actor == getPlayer() || mwmp::PlayerList::isDedicatedPlayer(actor));
        }

        bool isArenaNpcLevelActor(const MWWorld::Ptr& actor)
        {
            return !actor.isEmpty() && actor.getClass().isActor() && !isArenaNetworkPlayer(actor);
        }

        float arenaNpcLevelMultiplier(const MWWorld::Ptr& actor)
        {
            if (!isArenaNpcLevelActor(actor)) return 1.f;
            return ArenaCombatLimits::offense(actor.getClass().getCreatureStats(actor).getLevel());
    }

        float arenaNpcIncomingDamageMultiplier(const MWWorld::Ptr& actor)
        {
        if (!isArenaNpcLevelActor(actor)) return 1.f;
        return MWMechanics::ArenaCombatLimits::defense(actor.getClass().getCreatureStats(actor).getLevel());
    }

        float arenaHealthDamageCapFraction(const MWWorld::Ptr& target, const MWWorld::Ptr& caster)
        {
            return MWMechanics::ArenaCombatLimits::burstFraction(target, caster);
        }

        float arenaProjectedDirectDamageDuration(int effectId, const ESM::ENAMstruct& effect,
            const ESM::MagicEffect& magicEffect)
        {
            // Drain Health is a temporary stat reduction, not damage-per-second.
            if (effectId == ESM::MagicEffect::DrainHealth)
                return 1.f;
            if (magicEffect.mData.mFlags & ESM::MagicEffect::AppliedOnce)
                return 1.f;
            if (magicEffect.mData.mFlags & ESM::MagicEffect::NoDuration)
                return 1.f;
            return std::max(1.f, static_cast<float>(effect.mDuration));
        }

        float arenaNpcDamageToPlayerMultiplier(const MWWorld::Ptr& actor)
        {
            if (!isArenaNpcLevelActor(actor))
                return 1.f;

            const int level = actor.getClass().getCreatureStats(actor).getLevel();
            const float lowLevelMultiplier = std::clamp(
                Settings::Manager::getFloat("npc damage low level multiplier", "Game"), 0.f, 5.f);
            const float level100Multiplier = std::clamp(
                Settings::Manager::getFloat("npc damage level 100 multiplier", "Game"), 0.f, 5.f);
            const float over100Multiplier = std::clamp(
                Settings::Manager::getFloat("npc damage over 100 multiplier", "Game"), 0.f, 5.f);

            if (level <= 10)
                return lowLevelMultiplier;
            if (level > 100)
                return over100Multiplier;

            const float t = static_cast<float>(level - 10) / 90.f;
            return lowLevelMultiplier + t * (level100Multiplier - lowLevelMultiplier);
        }

        float arenaPlayerStaminaSpellMultiplier(const MWWorld::Ptr& actor)
        {
            if (!isArenaNetworkPlayer(actor))
                return 1.f;
            const CreatureStats& stats = actor.getClass().getCreatureStats(actor);
            const float maximum = stats.getFatigue().getModified();
            if (maximum <= 0.f)
                return 1.f;
            const float ratio = std::clamp(stats.getFatigue().getCurrent() / maximum, 0.f, 1.f);
            return .70f + .30f * std::clamp(ratio / .30f, 0.f, 1.f);
        }

        bool arenaIsDirectDamageEffect(int effectId)
        {
            return effectId == ESM::MagicEffect::FireDamage
                || effectId == ESM::MagicEffect::FrostDamage
                || effectId == ESM::MagicEffect::ShockDamage
                || effectId == ESM::MagicEffect::DrainHealth
                || effectId == ESM::MagicEffect::DamageHealth
                || effectId == ESM::MagicEffect::Poison
                || effectId == ESM::MagicEffect::SunDamage
                || effectId == ESM::MagicEffect::AbsorbHealth;
        }

        bool arenaIsSummonedCreature(const MWWorld::Ptr& actor)
        {
            if (actor.isEmpty() || !actor.getClass().isActor())
                return false;

            CreatureStats& actorStats = actor.getClass().getCreatureStats(actor);
            const int actorId = actorStats.getActorId();
            if (actorId < 0)
                return false;

            for (const auto& package : actorStats.getAiSequence())
            {
                if (!package || !package->followTargetThroughDoors())
                    continue;

                const MWWorld::Ptr master = package->getTarget();
                if (master.isEmpty() || !master.getClass().isActor())
                    continue;

                const auto& summons = master.getClass().getCreatureStats(master).getSummonedCreatureMap();
                for (const auto& summon : summons)
                {
                    if (summon.second == actorId)
                        return true;
                }
            }
            return false;
        }
    }

    CastSpell::CastSpell(const MWWorld::Ptr &caster, const MWWorld::Ptr &target, const bool fromProjectile, const bool manualSpell)
        : mCaster(caster)
        , mTarget(target)
        , mEnchantmentType(-1)
        , mSourceType(SourceType::Spell)
        , mFromProjectile(fromProjectile)
        , mManualSpell(manualSpell)
    {
    }

    void CastSpell::launchMagicBolt ()
    {
        osg::Vec3f fallbackDirection(0, 1, 0);
        osg::Vec3f offset(0, 0, 0);
        if (!mTarget.isEmpty() && mTarget.getClass().isActor())
            offset.z() = MWBase::Environment::get().getWorld()->getHalfExtents(mTarget).z();

        // Fall back to a "caster to target" direction if we have no other means of determining it
        // (e.g. when cast by a non-actor)
        if (!mTarget.isEmpty())
            fallbackDirection =
                (mTarget.getRefData().getPosition().asVec3() + offset) -
                (mCaster.getRefData().getPosition().asVec3());

        MWBase::Environment::get().getWorld()->launchMagicBolt(mId, mCaster, fallbackDirection);
    }

    void CastSpell::inflict(const MWWorld::Ptr &target, const MWWorld::Ptr &caster,
                            const ESM::EffectList &effects, ESM::RangeType range, bool reflected, bool exploded)
    {
        const bool targetIsActor = !target.isEmpty() && target.getClass().isActor();
        if (targetIsActor)
        {
            // Early-out for characters that have departed.
            const auto& stats = target.getClass().getCreatureStats(target);
            if (stats.isDead())
            {
                // Y073: recovery must run before the corpse early-out. inflict()
                // is reached after a successful cast/projectile impact, so normal
                // magicka, hit and spell success rules still apply.
                if (mwmp::PlayerList::isDedicatedPlayer(target) && caster == getPlayer()
                    && (range == ESM::RT_Touch || range == ESM::RT_Target) && !reflected)
                {
                    for (const ESM::ENAMstruct& effect : effects.mList)
                    {
                        if (effect.mRange == range && effect.mEffectID == ESM::MagicEffect::RestoreHealth
                            && (effect.mMagnMin > 0 || effect.mMagnMax > 0))
                        {
                            mwmp::Main::get().getLocalPlayer()->requestTouchRecovery(target);
                            break;
                        }
                    }
                }
                if (mwmp::PlayerList::isDedicatedPlayer(target) || stats.isDeathAnimationFinished())
                    return;
            }
        }

        // If none of the effects need to apply, we can early-out
        bool found = false;
        for (const ESM::ENAMstruct& effect : effects.mList)
        {
            if (effect.mRange == range)
            {
                found = true;
                break;
            }
        }
        if (!found)
            return;

        const ESM::Spell* spell = MWBase::Environment::get().getWorld()->getStore().get<ESM::Spell>().search (mId);
        if (spell && targetIsActor && (spell->mData.mType == ESM::Spell::ST_Disease || spell->mData.mType == ESM::Spell::ST_Blight))
        {
            int requiredResistance = (spell->mData.mType == ESM::Spell::ST_Disease) ?
                ESM::MagicEffect::ResistCommonDisease
                : ESM::MagicEffect::ResistBlightDisease;
            float x = target.getClass().getCreatureStats(target).getMagicEffects().get(requiredResistance).getMagnitude();

            if (Misc::Rng::roll0to99() <= x)
            {
                // Fully resisted, show message
                if (target == getPlayer())
                    MWBase::Environment::get().getWindowManager()->messageBox("#{sMagicPCResisted}");
                return;
            }
        }

        ESM::EffectList reflectedEffects;
        std::vector<ActiveSpells::ActiveEffect> appliedLastingEffects;

        // HACK: cache target's magic effects here, and add any applied effects to it. Use the cached effects for determining resistance.
        // This is required for Weakness effects in a spell to apply to any subsequent effects in the spell.
        // Otherwise, they'd only apply after the whole spell was added.
        MagicEffects targetEffects;
        if (targetIsActor)
            targetEffects += target.getClass().getCreatureStats(target).getMagicEffects();

        bool castByPlayer = (!caster.isEmpty() && caster == getPlayer());

        ActiveSpells targetSpells;
        if (targetIsActor)
            targetSpells = target.getClass().getCreatureStats(target).getActiveSpells();

        bool canCastAnEffect = false;    // For bound equipment.If this remains false
                                         // throughout the iteration of this spell's 
                                         // effects, we display a "can't re-cast" message

        // Weapon poisons are delivered physically by the weapon. They still use
        // ordinary poison/magic resistances below, but Reflect and Spell
        // Absorption must not bounce or consume the alchemical coating.
        const bool usesReflectAndAbsorption = mSourceType != SourceType::Potion || target == caster;
        const int absorbChance = usesReflectAndAbsorption ? getAbsorbChance(caster, target) : 0;

        // Spell Absorption is tested per effect for compatibility, but the Magicka
        // reward is paid at most once per application. Previously an 8-effect
        // spell could restore its full cast cost eight times.
        bool absorptionRewardGranted = false;

        // Y085: instant health changes (including temporary Drain Health)
        // share burst; timed damage shares separate rate and lifetime budgets.
        float arenaRemainingSpellHealthDamage = 0.f;
        float arenaRemainingDotRate = 0.f;
        float arenaRemainingDotTotal = 0.f;
        // Y114: both PvE directions use the same level-aware budget, including
        // enchanted weapons, poisons and hostile creature spells. Do not apply
        // this rule to self-buffs or to player-versus-player damage.
        const bool arenaLevelScaledPveDamage = targetIsActor
            && ArenaCombatLimits::pveOpponents(target, caster);
        ArenaCombatLimits::PveSpellBudget arenaPveSpellBudget;
        if (targetIsActor)
        {
            const float maximumHealth = std::max(0.f,
                target.getClass().getCreatureStats(target).getHealth().getModified());
            arenaRemainingSpellHealthDamage = maximumHealth * arenaHealthDamageCapFraction(target, caster);
            if (arenaLevelScaledPveDamage)
            {
                const auto owner = ArenaCombatLimits::summonOwner(caster);
                const auto source = owner.isEmpty() ? caster : owner;
                arenaPveSpellBudget = ArenaCombatLimits::makePveSpellBudget(maximumHealth,
                    source.getClass().getCreatureStats(source).getLevel(),
                    target.getClass().getCreatureStats(target).getLevel());
            }
            const bool player = isArenaNetworkPlayer(target);
            const auto owner = ArenaCombatLimits::summonOwner(caster);
            const auto source = owner.isEmpty() ? caster : owner;
            const bool pvp = player && source != target && isArenaNetworkPlayer(source);
            arenaRemainingDotRate = maximumHealth * (player ? (pvp ? 0.08f : 0.12f) : 0.12f);
            arenaRemainingDotTotal = maximumHealth * (player ? (pvp ? 0.45f : 0.65f) : 1.f);
        }

        int currentEffectIndex = 0;
        for (std::vector<ESM::ENAMstruct>::const_iterator effectIt (effects.mList.begin());
             !target.isEmpty() && effectIt != effects.mList.end(); ++effectIt, ++currentEffectIndex)
        {
            if (effectIt->mRange != range)
                continue;

            const ESM::MagicEffect *magicEffect =
                MWBase::Environment::get().getWorld()->getStore().get<ESM::MagicEffect>().find (
                effectIt->mEffectID);

            const bool isHarmful = (magicEffect->mData.mFlags & ESM::MagicEffect::Harmful) != 0;

            // Apply friendly fire per effect rather than per spell, so mixed
            // spells may still heal or buff a protected player while their
            // harmful components (damage, drain, absorb, paralysis, etc.) are
            // discarded. This also covers touch, projectile, area, reflected,
            // enchanted-weapon and damage-over-time spell applications.
            if (isHarmful && !MechanicsHelper::isFriendlyFireAllowed(caster, target))
                continue;

            // U035: heavy on-strike effects (paralysis, silence, reflect, stat
            // drain) have to be earned. The first hits of a stack are absorbed
            // and only the last one lands, so a single lucky swing can no
            // longer take a fight away from the player it hits.
            //
            // This sits before onHit/reflect on purpose: a suppressed effect
            // did not happen at all, so it must not be reflected back at the
            // attacker and must not report a magic hit to the victim. The
            // weapon's own physical hit and its damage effects are untouched.
            if (mEnchantmentType == ESM::Enchantment::WhenStrikes
                && !caster.isEmpty() && target != caster
                && OnStrikeStacks::isHeavyEffect(effectIt->mEffectID))
            {
                const bool targetIsPlayer = target == getPlayer()
                    || mwmp::PlayerList::isDedicatedPlayer(target);
                bool gated = targetIsPlayer;
                if (!gated)
                {
                    try
                    {
                        gated = Settings::Manager::getBool("combat on strike stack pve", "Game");
                    }
                    catch (const std::exception&)
                    {
                        gated = false;
                    }
                }

                if (gated && !OnStrikeStacks::chargeAndTest(caster, target, effectIt->mEffectID, mStrikeToken))
                    continue;
            }

            // Re-casting a bound equipment effect has no effect if the spell is still active
            if (magicEffect->mData.mFlags & ESM::MagicEffect::NonRecastable && targetSpells.isSpellActive(mId))
            {
                if (effectIt == (effects.mList.end() - 1) && !canCastAnEffect && castByPlayer)
                    MWBase::Environment::get().getWindowManager()->messageBox("#{sMagicCannotRecast}");
                continue;
            }
            canCastAnEffect = true;

            // Try absorbing the effect. A reflected effect can still be
            // nullified by Spell Absorption, but it never grants Magicka: this
            // closes the Reflect -> Absorb infinite-Magicka loop (especially with
            // multi-effect/AoE spells and several reflecting targets).
            if (absorbChance && Misc::Rng::roll0to99() < absorbChance)
            {
                if (!reflected && !absorptionRewardGranted)
                {
                    absorbSpell(mId, caster, target);
                    absorptionRewardGranted = true;
                }
                continue;
            }

            if (!checkEffectTarget(effectIt->mEffectID, target, caster, castByPlayer))
                continue;

            // caster needs to be an actor for linked effects (e.g. Absorb)
            if (magicEffect->mData.mFlags & ESM::MagicEffect::CasterLinked
                    && (caster.isEmpty() || !caster.getClass().isActor()))
                continue;

            // Notify the target actor they've been hit
            if (target.getClass().isActor() && target != caster && !caster.isEmpty() && isHarmful)
                target.getClass().onHit(target, 0.0f, true, MWWorld::Ptr(), caster, osg::Vec3f(), true);

            // Reflect harmful effects. Only the actor being hit owns the
            // reflection chance. Reflected effects never reflect a second time.
            if (!reflected && usesReflectAndAbsorption
                && reflectEffect(*effectIt, magicEffect, target, caster, reflectedEffects))
                continue;

            // Now that reflected effects have been handled, don't unilaterally
            // process effects further for dedicated players and actors on this
            // client. Their authoritative effects arrive through SpellsActive.
            if (mwmp::PlayerList::isDedicatedPlayer(target) || mwmp::Main::get().getCellController()->isDedicatedActor(target))
                continue;
            /*
                End of tes3mp addition
            */

            // Try resisting.
            float magnitudeMult = getEffectMultiplier(effectIt->mEffectID, target, caster, spell, &targetEffects);

            // Start of EncoreMP resistance caps

            if (target == getPlayer())
            {
                int effectholder = 1;
                effectholder = effectIt->mEffectID;

                // Y085: never raise resisted damage, especially genuine immunity.
                const float magcap = 0.f;

                //sub system to add 10% of willpower to magic resist
                MWWorld::Ptr player = getPlayer();
                const MWMechanics::CreatureStats &playerStats = player.getClass().getCreatureStats(player);
                float playerWill = playerStats.getAttribute(ESM::Attribute::Willpower).getModified();
                float willMagicResist = 0.0f;

                if (playerWill > 50.0f)
                {
                    playerWill -= 50.0f;
                    willMagicResist = (playerWill / 500.0f);
                }

                //check for drain effects
                if (effectholder == 17 || effectholder == 18 || effectholder == 19 || effectholder == 20 || effectholder == 21)
                {
                    magnitudeMult -= willMagicResist;
                    if (magnitudeMult < magcap)
                    {
                        magnitudeMult = magcap;
                    }
                }

                //check for damage effects
                if (effectholder == 22 || effectholder == 23 || effectholder == 24 || effectholder == 25 || effectholder == 26)
                {
                    magnitudeMult -= willMagicResist;
                    if (magnitudeMult < magcap)
                    {
                        magnitudeMult = magcap;
                    }
                }

                //check for absorb effects
                if (effectholder == 85 || effectholder == 86 || effectholder == 87 || effectholder == 88 || effectholder == 89)
                {
                    magnitudeMult -= willMagicResist;
                    if (magnitudeMult < magcap)
                    {
                        magnitudeMult = magcap;
                    }
                }

                //check for weakness to elemental magicka poison effects
                if (effectholder == 28 || effectholder == 29 || effectholder == 30 || effectholder == 31 || effectholder == 35)
                {
                    magnitudeMult -= willMagicResist;
                    if (magnitudeMult < magcap)
                    {
                        magnitudeMult = magcap;
                    }
                }


                //check for other weakness effects normal weps common blight
                if (effectholder == 32 || effectholder == 33 || effectholder == 36)
                {
                    magnitudeMult -= willMagicResist;
                    if (magnitudeMult < magcap)
                    {
                        magnitudeMult = magcap;
                    }
                }

                //check for negative illusion and alteration effects
                if (effectholder == 7 || effectholder == 46 || effectholder == 47 || effectholder == 48)
                {
                    magnitudeMult -= willMagicResist;
                    if (magnitudeMult < magcap)
                    {
                        magnitudeMult = magcap;
                    }
                }

            }

            // End of EncoreMP resistance caps

            // Y113: repeated elemental weakness effects should improve a spell,
            // not multiply damage without limit (e.g. +300% on the same target).
            // Genuine 100% resistance still yields zero. Restrict this to
            // player-cast PvE damage: other spells and PvP rules are untouched.
            if (arenaLevelScaledPveDamage && arenaIsDirectDamageEffect(effectIt->mEffectID))
                magnitudeMult = std::clamp(magnitudeMult, 0.f, 2.f);

            // Y061: NPC level amplifies an existing Resist Paralysis source;
            // player level is intentionally not used for combat scaling.
            if (effectIt->mEffectID == ESM::MagicEffect::Paralyze
                && isArenaNpcLevelActor(target) && magnitudeMult > 0.f)
            {
                const float paralysisResistance = std::clamp(
                    targetEffects.get(ESM::MagicEffect::ResistParalysis).getMagnitude(), 0.f, 100.f);
                if (paralysisResistance > 0.f)
                {
                    const float extraResistance = paralysisResistance
                        * (arenaNpcLevelMultiplier(target) - 1.f) * 0.01f;
                    magnitudeMult = std::max(0.f, magnitudeMult - extraResistance);
                }
            }

            if (magnitudeMult == 0)
            {
                // Fully resisted, show message
                if (target == getPlayer())
                    MWBase::Environment::get().getWindowManager()->messageBox("#{sMagicPCResisted}");
                else if (castByPlayer)
                    MWBase::Environment::get().getWindowManager()->messageBox("#{sMagicTargetResisted}");
            }
            else
            {
                float magnitude = effectIt->mMagnMin + Misc::Rng::rollDice(effectIt->mMagnMax - effectIt->mMagnMin + 1);
                magnitude *= magnitudeMult;

                // EncoreMP determine if spellcaster is allied to the player

                bool casterIsPlayerAlly = false;

                if (!caster.isEmpty() && caster.getClass().isActor())
                {
                    MWMechanics::CreatureStats& statsCaster = caster.getClass().getCreatureStats(caster);
                    for (const auto& lpackage : statsCaster.getAiSequence())
                    {
                        if (!lpackage) continue;
                        if (lpackage && lpackage->followTargetThroughDoors())
                        {
                            const MWWorld::Ptr& master = lpackage->getTarget();
                            if (master.isEmpty()) continue;
                            bool masterIsPlayer = (master == MWMechanics::getPlayer()) || mwmp::PlayerList::isDedicatedPlayer(master);
                            if (!masterIsPlayer) continue;
                            casterIsPlayerAlly = true;
                        }
                    }
                }

                // end of ally determination

                // EncoreMP enchantment on strike scaling begin

                const MWWorld::Ptr player = MWMechanics::getPlayer();

                int effectholder = effectIt->mEffectID;

                // spell effects are 14, 15, 16 elements, 18 drain health, 23 damage health, 27 poison and 86 absorb health
                if (mEnchantmentType == ESM::Enchantment::WhenStrikes && target != player && (effectholder == 14 || effectholder == 15 || effectholder == 16 || effectholder == 18 || effectholder == 23 || effectholder == 27 || effectholder == 86))
                {
                    if (caster == player)
                    {
                        magnitude *= onstrikeDamageScale();
                    }
                    else if (casterIsPlayerAlly)
                    {
                        magnitude *= allyDamageDealt();
                    }

                }

                // EncoreMP enchantment on strike scaling end


                // EncoreMP castonce and whenused scaling begin

                if ((mEnchantmentType == ESM::Enchantment::CastOnce || mEnchantmentType == ESM::Enchantment::WhenUsed) && target != player && target != caster && (effectholder == 14 || effectholder == 15 || effectholder == 16 || effectholder == 18 || effectholder == 23 || effectholder == 27 || effectholder == 86))
                {
                    if (caster == player)
                    {
                        magnitude *= castenchantedDamagescale();
                    }
                    else if (casterIsPlayerAlly)
                    {
                        magnitude *= allyDamageDealt();
                    }
                }

                // EncoreMP castonce and whenused scaling end


                // EncoreMP general spellcasting start, for now it is using the same damage scale as enchanted items

                if (mSourceType == SourceType::Spell && target != player && target != caster && (effectholder == 14 || effectholder == 15 || effectholder == 16 || effectholder == 18 || effectholder == 23 || effectholder == 27 || effectholder == 86))
                {
                    if (caster == player)
                    {
                        // ArenaMP Y091: direct spells are not enchanted-item attacks.
                        // Applying castenchantedDamagescale() here made a real spell pass
                        // through an unrelated enchantment/difficulty reduction before the
                        // spell-specific balance rules below. Preserve enchantment scaling
                        // for actual enchanted items, but let spell magnitude and elemental
                        // weakness/resistance remain meaningful in PvE.
                        if (spellSchoolToSkill(magicEffect->mData.mSchool) == ESM::Skill::Destruction)
                            magnitude *= std::clamp(Settings::Manager::getFloat(
                                "destruction spell damage multiplier", "Game"), 0.f, 2.f);
                    }
                    else if (casterIsPlayerAlly)
                    {
                        magnitude *= allyDamageDealt();
                    }
                }

                // EncoreMP general spellcasting end



                // EncoreMP magic damage taken begin

                if (target == player && mCaster != player && !caster.isEmpty() && !reflected && caster.getClass().isActor() && (effectholder == 14 || effectholder == 15 || effectholder == 16 || effectholder == 18 || effectholder == 23 || effectholder == 27 || effectholder == 86))
                {
                    magnitude *= magicdamagetaken();
                }

                // EncoreMP magic damage taken end

                // Damage spells scale with school, willpower and low-stamina penalty.
                // NPC spell offense uses the common L1..L150 curve.
                if (mSourceType == SourceType::Spell && arenaIsDirectDamageEffect(effectholder)
                    && target != caster)
                {
                    if (isArenaNetworkPlayer(caster))
                    {
                        const int school = spellSchoolToSkill(magicEffect->mData.mSchool);
                        const float skill = caster.getClass().getSkill(caster, school);
                        const float willpower = caster.getClass().getCreatureStats(caster)
                            .getAttribute(ESM::Attribute::Willpower).getModified();
                        magnitude *= (.8f + .004f * ArenaCombatLimits::softStat(skill)
                            + .002f * ArenaCombatLimits::softStat(willpower))
                            * arenaPlayerStaminaSpellMultiplier(caster);
                    }
                    else if (isArenaNpcLevelActor(caster))
                        magnitude *= arenaNpcLevelMultiplier(caster);
                }

                // Magic and physical damage share the L1..L150 defense curve.
                if (arenaIsDirectDamageEffect(effectholder) && isArenaNpcLevelActor(target)
                    && target != caster && magnitude > 0.f)
                    magnitude *= arenaNpcIncomingDamageMultiplier(target);

                // Y075: direct NPC/creature magic uses the same level-scaled final
                // damage multiplier as physical attacks. Reflected player damage is excluded.
                if (arenaIsDirectDamageEffect(effectholder) && target == player
                    && isArenaNpcLevelActor(caster) && !reflected && magnitude > 0.f)
                    magnitude *= arenaNpcDamageToPlayerMultiplier(caster);

                // ArenaMP Y087: summon spell attacks use the same 0.80 final damage scale.
                // Only direct damage is scaled: utility/control effects retain their normal
                // duration and magnitude, so summon AI behaviour is not accidentally broken.
                if (arenaIsDirectDamageEffect(effectholder) && !reflected && magnitude > 0.f
                    && arenaIsSummonedCreature(caster))
                    magnitude *= 0.80f;

                // Drain Health retains native temporary-stat lifecycle and shares
                // burst magnitude, regardless of duration. Do not convert it to DoT.
                if (targetIsActor && arenaIsDirectDamageEffect(effectholder) && magnitude > 0.f)
                {
                    // ArenaMP Y114: unified 10..60% total-PvE-HP rule.
                    // Budget is shared across every damage effect of this cast;
                    // for enchanted/poisoned weapon hits it also shares the
                    // synchronous strike budget with the physical component.
                    const bool timedDamage = effectholder != ESM::MagicEffect::DrainHealth
                        && !(magicEffect->mData.mFlags & (ESM::MagicEffect::AppliedOnce | ESM::MagicEffect::NoDuration))
                        && effectIt->mDuration > 0;
                    if (arenaLevelScaledPveDamage)
                    {
                        const float duration = timedDamage ? static_cast<float>(effectIt->mDuration) : 1.f;
                        magnitude = ArenaCombatLimits::reservePveSpellDamage(
                            magnitude, duration, timedDamage, arenaPveSpellBudget);
                        const float projected = magnitude * duration;
                        magnitude = ArenaCombatLimits::reserveActiveImpactDamage(target, projected) / duration;
                    }
                    else if (timedDamage)
                    {
                        magnitude = MWMechanics::ArenaCombatLimits::reserveDot(magnitude,
                            static_cast<float>(effectIt->mDuration), arenaRemainingDotRate, arenaRemainingDotTotal);
                    }
                    else
                    {
                        const float durationFactor = arenaProjectedDirectDamageDuration(
                            effectholder, *effectIt, *magicEffect);
                        const float allowed = std::min(magnitude * durationFactor,
                            std::max(0.f, arenaRemainingSpellHealthDamage));
                        magnitude = allowed / durationFactor;
                        arenaRemainingSpellHealthDamage = std::max(0.f, arenaRemainingSpellHealthDamage - allowed);
                    }
                }


                if (!target.getClass().isActor())
                {
                    // non-actor objects have no list of active magic effects, so have to apply instantly
                    if (!applyInstantEffect(target, caster, EffectKey(*effectIt), magnitude))
                        continue;
                }
                else // target.getClass().isActor() == true
                {
                    ActiveSpells::ActiveEffect effect;
                    effect.mEffectId = effectIt->mEffectID;
                    effect.mArg = MWMechanics::EffectKey(*effectIt).mArg;
                    effect.mMagnitude = magnitude;
                    effect.mTimeLeft = 0.f;
                    effect.mEffectIndex = currentEffectIndex;

                    // Avoid applying absorb effects if the caster is the target
                    // We still need the spell to be added
                    if (caster == target
                        && effectIt->mEffectID >= ESM::MagicEffect::AbsorbAttribute
                        && effectIt->mEffectID <= ESM::MagicEffect::AbsorbSkill)
                    {
                        effect.mMagnitude = 0;
                    }

                    // Avoid applying harmful effects to the player in god mode
                    if (target == getPlayer() && MWBase::Environment::get().getWorld()->getGodModeState() && isHarmful)
                    {
                        effect.mMagnitude = 0;
                    }

                    bool effectAffectsHealth = isHarmful || effectIt->mEffectID == ESM::MagicEffect::RestoreHealth;
                    if (castByPlayer && target != caster && !target.getClass().getCreatureStats(target).isDead() && effectAffectsHealth)
                    {
                        // If player is attempting to cast a harmful spell on or is healing a living target, show the target's HP bar.
                        MWBase::Environment::get().getWindowManager()->setEnemy(target);
                    }

                    bool hasDuration = !(magicEffect->mData.mFlags & ESM::MagicEffect::NoDuration);
                    effect.mDuration = hasDuration ? static_cast<float>(effectIt->mDuration) : 1.f;

                    bool appliedOnce = magicEffect->mData.mFlags & ESM::MagicEffect::AppliedOnce;
                    if (!appliedOnce)
                        effect.mDuration = std::max(1.f, effect.mDuration);

                    if (effect.mDuration == 0)
                    {
                        // We still should add effect to list to allow GetSpellEffects to detect this spell
                        appliedLastingEffects.push_back(effect);

                        // duration 0 means apply full magnitude instantly
                        bool wasDead = target.getClass().getCreatureStats(target).isDead();
                        effectTick(target.getClass().getCreatureStats(target), target, EffectKey(*effectIt), effect.mMagnitude);
                        bool isDead = target.getClass().getCreatureStats(target).isDead();

                        /*
                            Start of tes3mp addition

                            If the target was a LocalPlayer or LocalActor who died, record the caster as the killer
                        */
                        if (!wasDead && isDead)
                        {
                            bool isSuicide = target == caster || caster.isEmpty();

                            if (target == MWMechanics::getPlayer())
                            {
                                mwmp::Main::get().getLocalPlayer()->killer = isSuicide ?
                                    MechanicsHelper::getTarget(target) : MechanicsHelper::getTarget(caster);
                            }
                            else if (mwmp::Main::get().getCellController()->isLocalActor(target))
                            {
                                mwmp::Main::get().getCellController()->getLocalActor(target)->killer = isSuicide ?
                                    MechanicsHelper::getTarget(target) : MechanicsHelper::getTarget(caster);
                            }
                        }
                        /*
                            End of tes3mp addition
                        */

                        if (!wasDead && isDead)
                            MWBase::Environment::get().getMechanicsManager()->actorKilled(target, caster);
                    }
                    else
                    {
                        effect.mTimeLeft = effect.mDuration;

                        targetEffects.add(MWMechanics::EffectKey(*effectIt), MWMechanics::EffectParam(effect.mMagnitude));

                        // add to list of active effects, to apply in next frame
                        appliedLastingEffects.push_back(effect);

                        // Unequip all items, if a spell with the ExtraSpell effect was casted
                        if (effectIt->mEffectID == ESM::MagicEffect::ExtraSpell && target.getClass().hasInventoryStore(target))
                        {
                            MWWorld::InventoryStore& store = target.getClass().getInventoryStore(target);
                            store.unequipAll(target);
                        }

                        // Command spells should have their effect, including taking the target out of combat, each time the spell successfully affects the target
                        if (((effectIt->mEffectID == ESM::MagicEffect::CommandHumanoid && target.getClass().isNpc())
                        || (effectIt->mEffectID == ESM::MagicEffect::CommandCreature && target.getTypeName() == typeid(ESM::Creature).name()))
                        && !caster.isEmpty() && caster.getClass().isActor() && target != getPlayer() && effect.mMagnitude >= target.getClass().getCreatureStats(target).getLevel())
                        {
                            MWMechanics::AiFollow package(caster, true);
                            target.getClass().getCreatureStats(target).getAiSequence().stack(package, target);
                        }

                        // For absorb effects, also apply the effect to the caster - but with a negative
                        // magnitude, since we're transferring stats from the target to the caster
                        if (effectIt->mEffectID >= ESM::MagicEffect::AbsorbAttribute && effectIt->mEffectID <= ESM::MagicEffect::AbsorbSkill)
                            absorbStat(*effectIt, effect, caster, target, reflected, mSourceName);
                    }
                }

                // Re-casting a summon effect will remove the creature from previous castings of that effect.
                if (isSummoningEffect(effectIt->mEffectID) && targetIsActor)
                {
                    CreatureStats& targetStats = target.getClass().getCreatureStats(target);
                    ESM::SummonKey key(effectIt->mEffectID, mId, currentEffectIndex);
                    auto findCreature = targetStats.getSummonedCreatureMap().find(key);
                    if (findCreature != targetStats.getSummonedCreatureMap().end())
                    {
                        /*
                            Start of tes3mp change (major)

                            Don't clean up placeholder summoned creatures still awaiting a spawn
                            packet from the server, because that would make the packet create permanent
                            spawns instead
                        */
                        if (findCreature->second != -1)
                        {
                            MWBase::Environment::get().getMechanicsManager()->cleanupSummonedCreature(target, findCreature->second);
                            targetStats.getSummonedCreatureMap().erase(findCreature);
                        }
                        /*
                            End of tes3mp change (major)
                        */
                    }
                }

                if (target.getClass().isActor() || magicEffect->mData.mFlags & ESM::MagicEffect::NoDuration)
                {
                    static const std::string schools[] = {
                        "alteration", "conjuration", "destruction", "illusion", "mysticism", "restoration"
                    };

                    MWBase::SoundManager *sndMgr = MWBase::Environment::get().getSoundManager();
                    if(!magicEffect->mHitSound.empty())
                        sndMgr->playSound3D(target, magicEffect->mHitSound, 1.0f, 1.0f);
                    else
                        sndMgr->playSound3D(target, schools[magicEffect->mData.mSchool]+" hit", 1.0f, 1.0f);

                    /*
                        Start of tes3mp addition

                        Send an ID_OBJECT_SOUND packet every time a sound is made here
                    */
                    mwmp::ObjectList* objectList = mwmp::Main::get().getNetworking()->getObjectList();
                    objectList->reset();
                    objectList->packetOrigin = mwmp::CLIENT_GAMEPLAY;
                    objectList->addObjectSound(target, magicEffect->mHitSound.empty() ? schools[magicEffect->mData.mSchool] + " hit" : magicEffect->mHitSound, 1.0f, 1.0f);
                    objectList->sendObjectSound();
                    /*
                        End of tes3mp addition
                    */

                    // Add VFX
                    const ESM::Static* castStatic;
                    if (!magicEffect->mHit.empty())
                        castStatic = MWBase::Environment::get().getWorld()->getStore().get<ESM::Static>().find (magicEffect->mHit);
                    else
                        castStatic = MWBase::Environment::get().getWorld()->getStore().get<ESM::Static>().find ("VFX_DefaultHit");

                    bool loop = (magicEffect->mData.mFlags & ESM::MagicEffect::ContinuousVfx) != 0;
                    // Note: in case of non actor, a free effect should be fine as well
                    MWRender::Animation* anim = MWBase::Environment::get().getWorld()->getAnimation(target);
                    if (anim && !castStatic->mModel.empty())
                        anim->addEffect("meshes\\" + castStatic->mModel, magicEffect->mIndex, loop, "", magicEffect->mParticle);
                }
            }
        }

        if (!exploded)
            MWBase::Environment::get().getWorld()->explodeSpell(mHitPosition, effects, caster, target, range, mId, mSourceName, mFromProjectile);

        if (!target.isEmpty())
        {
            if (!reflectedEffects.mList.empty())
                inflict(caster, target, reflectedEffects, range, true, exploded);

            if (!appliedLastingEffects.empty())
            {
                int casterActorId = -1;
                if (!caster.isEmpty() && caster.getClass().isActor())
                    casterActorId = caster.getClass().getCreatureStats(caster).getActorId();
                const bool alchemyDurationStack = mSourceType == SourceType::Potion || mSourceType == SourceType::Ingredient;
                target.getClass().getCreatureStats(target).getActiveSpells().addSpell(mId, mStack, appliedLastingEffects,
                        mSourceName, casterActorId, alchemyDurationStack);
            }
        }
    }

    bool CastSpell::applyInstantEffect(const MWWorld::Ptr &target, const MWWorld::Ptr &caster, const MWMechanics::EffectKey& effect, float magnitude)
    {
        short effectId = effect.mId;
        if (target.getClass().canLock(target))
        {
            if (effectId == ESM::MagicEffect::Lock)
            {
                const MWWorld::ESMStore& store = MWBase::Environment::get().getWorld()->getStore();
                const ESM::MagicEffect *magiceffect = store.get<ESM::MagicEffect>().find(effectId);
                MWRender::Animation* animation = MWBase::Environment::get().getWorld()->getAnimation(target);
                if (animation)
                    animation->addSpellCastGlow(magiceffect);
                if (target.getCellRef().getLockLevel() < magnitude) //If the door is not already locked to a higher value, lock it to spell magnitude
                {
                    if (caster == getPlayer())
                        MWBase::Environment::get().getWindowManager()->messageBox("#{sMagicLockSuccess}");

                    /*
                        Start of tes3mp change (major)

                        Disable unilateral locking on this client and expect the server's reply to our
                        packet to do it instead
                    */
                    //target.getCellRef().lock(static_cast<int>(magnitude));
                    /*
                        End of tes3mp change (major)
                    */

                    /*
                        Start of tes3mp addition

                        Send an ID_OBJECT_LOCK packet every time an object is locked here
                    */
                    mwmp::ObjectList *objectList = mwmp::Main::get().getNetworking()->getObjectList();
                    objectList->reset();
                    objectList->packetOrigin = mwmp::CLIENT_GAMEPLAY;
                    objectList->addObjectLock(target, static_cast<int>(magnitude));
                    objectList->sendObjectLock();
                    /*
                        End of tes3mp addition
                    */
                }
                return true;
            }
            else if (effectId == ESM::MagicEffect::Open)
            {
                if (!caster.isEmpty())
                {
                    MWBase::Environment::get().getMechanicsManager()->unlockAttempted(getPlayer(), target);
                    // Use the player instead of the caster for vanilla crime compatibility
                }
                const MWWorld::ESMStore& store = MWBase::Environment::get().getWorld()->getStore();
                const ESM::MagicEffect *magiceffect = store.get<ESM::MagicEffect>().find(effectId);
                MWRender::Animation* animation = MWBase::Environment::get().getWorld()->getAnimation(target);
                if (animation)
                    animation->addSpellCastGlow(magiceffect);
                if (target.getCellRef().getLockLevel() <= magnitude)
                {
                    if (target.getCellRef().getLockLevel() > 0)
                    {
                        MWBase::Environment::get().getSoundManager()->playSound3D(target, "Open Lock", 1.f, 1.f);

                        if (caster == getPlayer())
                            MWBase::Environment::get().getWindowManager()->messageBox("#{sMagicOpenSuccess}");
                    }

                    /*
                        Start of tes3mp change (major)

                        Disable unilateral locking on this client and expect the server's reply to our
                        packet to do it instead
                    */
                    //target.getCellRef().unlock();
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
                    objectList->addObjectLock(target, 0);
                    objectList->sendObjectLock();
                    /*
                        End of tes3mp addition
                    */
                }
                else
                {
                    MWBase::Environment::get().getSoundManager()->playSound3D(target, "Open Lock Fail", 1.f, 1.f);
                }

                return true;
            }
        }
        else if (target.getClass().isActor() && effectId == ESM::MagicEffect::Dispel)
        {
            target.getClass().getCreatureStats(target).getActiveSpells().purgeAll(magnitude, true);
            return true;
        }
        else if (target.getClass().isActor() && target == getPlayer())
        {
            MWRender::Animation* anim = MWBase::Environment::get().getWorld()->getAnimation(mCaster);
            bool teleportingEnabled = MWBase::Environment::get().getWorld()->isTeleportingEnabled();

            if (effectId == ESM::MagicEffect::DivineIntervention || effectId == ESM::MagicEffect::AlmsiviIntervention)
            {
                if (!teleportingEnabled)
                {
                    if (caster == getPlayer())
                        MWBase::Environment::get().getWindowManager()->messageBox("#{sTeleportDisabled}");
                    return true;
                }
                std::string marker = (effectId == ESM::MagicEffect::DivineIntervention) ? "divinemarker" : "templemarker";
                MWBase::Environment::get().getWorld()->teleportToClosestMarker(target, marker);
                anim->removeEffect(effectId);
                const ESM::Static* fx = MWBase::Environment::get().getWorld()->getStore().get<ESM::Static>()
                    .search("VFX_Summon_end");
                if (fx)
                    anim->addEffect("meshes\\" + fx->mModel, -1);
                return true;
            }
            else if (effectId == ESM::MagicEffect::Mark)
            {
                if (teleportingEnabled)
                {
                    MWBase::Environment::get().getWorld()->getPlayer().markPosition(
                        target.getCell(), target.getRefData().getPosition());
                }
                else if (caster == getPlayer())
                {
                    MWBase::Environment::get().getWindowManager()->messageBox("#{sTeleportDisabled}");
                }

                /*
                    Start of tes3mp addition

                    Send a PlayerMiscellaneous packet with the player's new mark location
                */
                if (teleportingEnabled)
                    mwmp::Main::get().getLocalPlayer()->sendMarkLocation(*target.getCell()->getCell(), target.getRefData().getPosition());
                /*
                    End of tes3mp addition
                */

                return true;
            }
            else if (effectId == ESM::MagicEffect::Recall)
            {
                if (!teleportingEnabled)
                {
                    if (caster == getPlayer())
                        MWBase::Environment::get().getWindowManager()->messageBox("#{sTeleportDisabled}");
                    return true;
                }

                // Enter the selection only after the Recall effect succeeds and
                // after the vanilla teleport-disabled check. Never overwrite Mark.
                auto* gui = mwmp::Main::get().getGUIController();
                if (gui && gui->requestGroupRecall())
                {
                    if (anim) anim->removeEffect(effectId);
                    return true;
                }

                MWWorld::CellStore* markedCell = nullptr;
                ESM::Position markedPosition;

                MWBase::Environment::get().getWorld()->getPlayer().getMarkedPosition(markedCell, markedPosition);
                if (markedCell)
                {
                    MWWorld::ActionTeleport action(markedCell->isExterior() ? "" : markedCell->getCell()->mName,
                                            markedPosition, false);
                    action.execute(target);
                    anim->removeEffect(effectId);
                }
                return true;
            }
        }
        return false;
    }


    bool CastSpell::cast(const std::string &id)
    {
        const MWWorld::ESMStore& store = MWBase::Environment::get().getWorld()->getStore();
        if (const auto spell = store.get<ESM::Spell>().search(id))
            return cast(spell);

        if (const auto potion = store.get<ESM::Potion>().search(id))
            return cast(potion);

        if (const auto ingredient = store.get<ESM::Ingredient>().search(id))
            return cast(ingredient);

        throw std::runtime_error("ID type cannot be casted");
    }

    bool CastSpell::cast(const MWWorld::Ptr &item, bool launchProjectile)
    {

        {
            const std::string &refId = item.getCellRef().getRefId();
            auto &store = MWBase::Environment::get().getWorld()->getStore();
            if (auto spell = store.get<ESM::Spell>().search(refId))
                return cast(spell);
        }

        mSourceType = SourceType::EnchantedItem;

        std::string enchantmentName = item.getClass().getEnchantment(item);

        if (enchantmentName.empty())
            throw std::runtime_error("can't cast an item without an enchantment");

        mSourceName = item.getClass().getName(item);
        mId = item.getCellRef().getRefId();

        const ESM::Enchantment* enchantment = MWBase::Environment::get().getWorld()->getStore().get<ESM::Enchantment>().find(enchantmentName);

        // EncoreMP 1 line addition
        mEnchantmentType = enchantment->mData.mType;    

        mStack = false;

        bool godmode = mCaster == MWMechanics::getPlayer() && MWBase::Environment::get().getWorld()->getGodModeState();
        bool isProjectile = false;
        if (item.getTypeName() == typeid(ESM::Weapon).name())
        {
            int type = item.get<ESM::Weapon>()->mBase->mData.mType;
            ESM::WeaponType::Class weapclass = MWMechanics::getWeaponType(type)->mWeaponClass;
            isProjectile = (weapclass == ESM::WeaponType::Thrown || weapclass == ESM::WeaponType::Ammo);
        }
        int type = enchantment->mData.mType;

        // Check if there's enough charge left
        if (!godmode && (type == ESM::Enchantment::WhenUsed || type == ESM::Enchantment::WhenStrikes))
        {
            int castCost = getEffectiveEnchantedItemUseCost(*enchantment, mCaster);

            if (item.getCellRef().getEnchantmentCharge() == -1)
                item.getCellRef().setEnchantmentCharge(static_cast<float>(enchantment->mData.mCharge));

            if (item.getCellRef().getEnchantmentCharge() < castCost)
            {
                if (mCaster == getPlayer())
                {
                    if (type == ESM::Enchantment::WhenStrikes)
                    {
                        return false;
                    }
                    MWBase::Environment::get().getWindowManager()->messageBox("#{sMagicInsufficientCharge}");

                    // Failure sound
                    int school = 0;
                    if (!enchantment->mEffects.mList.empty())
                    {
                        short effectId = enchantment->mEffects.mList.front().mEffectID;
                        const ESM::MagicEffect* magicEffect = MWBase::Environment::get().getWorld()->getStore().get<ESM::MagicEffect>().find(effectId);
                        school = magicEffect->mData.mSchool;
                    }

                    static const std::string schools[] = {
                        "alteration", "conjuration", "destruction", "illusion", "mysticism", "restoration"
                    };
                    MWBase::SoundManager *sndMgr = MWBase::Environment::get().getSoundManager();
                    sndMgr->playSound3D(mCaster, "Spell Failure " + schools[school], 1.0f, 1.0f);

                    /*
                        Start of tes3mp addition

                        Send an ID_OBJECT_SOUND packet every time a sound is made here
                    */
                    mwmp::ObjectList* objectList = mwmp::Main::get().getNetworking()->getObjectList();
                    objectList->reset();
                    objectList->packetOrigin = mwmp::CLIENT_GAMEPLAY;
                    objectList->addObjectSound(mCaster, "Spell Failure " + schools[school], 1.0f, 1.0f);
                    objectList->sendObjectSound();
                    /*
                        End of tes3mp addition
                    */
                }
                return false;
            }

            // Reduce charge
            item.getCellRef().setEnchantmentCharge(item.getCellRef().getEnchantmentCharge() - castCost);
        }

        if (type == ESM::Enchantment::WhenUsed)
        {
            if (mCaster == getPlayer())
                mCaster.getClass().skillUsageSucceeded (mCaster, ESM::Skill::Enchant, 1);
        }
        else if (type == ESM::Enchantment::CastOnce)
        {
            if (!godmode)
                item.getContainerStore()->remove(item, 1, mCaster);
        }
        else if (type == ESM::Enchantment::WhenStrikes)
        {
            if (mCaster == getPlayer())
                mCaster.getClass().skillUsageSucceeded (mCaster, ESM::Skill::Enchant, 3);
        }

        inflict(mCaster, mCaster, enchantment->mEffects, ESM::RT_Self);

        if (isProjectile || !mTarget.isEmpty())
            inflict(mTarget, mCaster, enchantment->mEffects, ESM::RT_Touch);

        if (launchProjectile)
            launchMagicBolt();
        else if (isProjectile || !mTarget.isEmpty())
            inflict(mTarget, mCaster, enchantment->mEffects, ESM::RT_Target);

        return true;
    }

    bool CastSpell::cast(const ESM::Potion* potion)
    {
        // EncoreMP, potions no longer stack with copies of themself

        mSourceType = SourceType::Potion;
        mSourceName = potion->mName;
        mId = potion->mId;
        mStack = false;

        inflict(mCaster, mCaster, potion->mEffects, ESM::RT_Self);

        return true;
    }

    bool CastSpell::cast(const ESM::Spell* spell)
    {
        mSourceType = SourceType::Spell;
        mSourceName = spell->mName;
        mId = spell->mId;
        mStack = false;

        const MWWorld::ESMStore& store = MWBase::Environment::get().getWorld()->getStore();

        int school = 0;

        bool godmode = mCaster == MWMechanics::getPlayer() && MWBase::Environment::get().getWorld()->getGodModeState();

        if (mCaster.getClass().isActor() && !mAlwaysSucceed && !mManualSpell)
        {
            school = getSpellSchool(spell, mCaster);

            CreatureStats& stats = mCaster.getClass().getCreatureStats(mCaster);

            if (!godmode)
            {
                // Reduce fatigue (note that in the vanilla game, both GMSTs are 0, and there's no fatigue loss)
                static const float fFatigueSpellBase = store.get<ESM::GameSetting>().find("fFatigueSpellBase")->mValue.getFloat();
                static const float fFatigueSpellMult = store.get<ESM::GameSetting>().find("fFatigueSpellMult")->mValue.getFloat();
                DynamicStat<float> fatigue = stats.getFatigue();
                const float normalizedEncumbrance = mCaster.getClass().getNormalizedEncumbrance(mCaster);

                float fatigueLoss = spell->mData.mCost * (fFatigueSpellBase + normalizedEncumbrance * fFatigueSpellMult);
                fatigue.setCurrent(fatigue.getCurrent() - fatigueLoss); 
                stats.setFatigue(fatigue);

                bool fail = false;

                /*
                    Start of tes3mp change (major)
                
                    Make spell casting fail based on the casting success rated determined
                    in MechanicsHelper::getSpellSuccess()
                */
                mwmp::Cast *localCast = NULL;
                mwmp::Cast *dedicatedCast = MechanicsHelper::getDedicatedCast(mCaster);

                if (dedicatedCast)
                    dedicatedCast->pressed = false;
                else
                {
                    localCast = MechanicsHelper::getLocalCast(mCaster);

                    /*
                        Start of AMP change (Y044)

                        getLocalCast() returns nullptr for a caster that is neither the
                        local player nor one of our local actors, which happens when an
                        actor is cast from during the frame its LocalActor is being
                        handed over on a cell change. Fall back to the ordinary success
                        roll instead of dereferencing a null pointer.
                    */
                    if (localCast)
                    {
                        localCast->success = MechanicsHelper::getSpellSuccess(mId, mCaster);
                        localCast->pressed = false;
                        localCast->shouldSend = true;
                    }
                    /*
                        End of AMP change (Y044)
                    */
                }

                // Check success
                if ((localCast && localCast->success == false) ||
                    (dedicatedCast && dedicatedCast->success == false))
                {
                    if (mCaster == getPlayer())
                        MWBase::Environment::get().getWindowManager()->messageBox("#{sMagicSkillFail}");
                    fail = true;
                }
                /*
                    End of tes3mp change (major)
                */

                if (fail)
                {
                    // Failure sound
                    static const std::string schools[] = {
                        "alteration", "conjuration", "destruction", "illusion", "mysticism", "restoration"
                    };

                    MWBase::SoundManager *sndMgr = MWBase::Environment::get().getSoundManager();
                    sndMgr->playSound3D(mCaster, "Spell Failure " + schools[school], 1.0f, 1.0f);

                    /*
                        Start of tes3mp addition

                        Send an ID_OBJECT_SOUND packet every time a sound is made here
                    */
                    mwmp::ObjectList* objectList = mwmp::Main::get().getNetworking()->getObjectList();
                    objectList->reset();
                    objectList->packetOrigin = mwmp::CLIENT_GAMEPLAY;
                    objectList->addObjectSound(mCaster, "Spell Failure " + schools[school], 1.0f, 1.0f);
                    objectList->sendObjectSound();
                    /*
                        End of tes3mp addition
                    */

                    return false;
                }
            }

            // A power can be used once per 24h
            if (spell->mData.mType == ESM::Spell::ST_Power)
                stats.getSpells().usePower(spell);
        }

        // EncoreMP, more XP from high costed spells

        float xpMult = 1.0f;
        float spellCost = spell->mData.mCost;

        if (spellCost > 5.0f)
        {
            xpMult = (spellCost * 0.0889f);
            xpMult += 0.556f;
        }

        if (!mManualSpell && mCaster == getPlayer() && spellIncreasesSkill(spell))
            mCaster.getClass().skillUsageSucceeded(mCaster, spellSchoolToSkill(school), 0, xpMult);

        // end of EncoreMP, more XP from high costed spells


        // A non-actor doesn't play its spell cast effects from a character controller, so play them here
        if (!mCaster.getClass().isActor())
            playSpellCastingEffects(spell->mEffects.mList);

        inflict(mCaster, mCaster, spell->mEffects, ESM::RT_Self);

        if (!mTarget.isEmpty())
            inflict(mTarget, mCaster, spell->mEffects, ESM::RT_Touch);

        launchMagicBolt();

        return true;
    }

    bool CastSpell::cast (const ESM::Ingredient* ingredient)
    {
        mSourceType = SourceType::Ingredient;
        mId = ingredient->mId;
        mStack = true;
        mSourceName = ingredient->mName;

        ESM::ENAMstruct effect;
        effect.mEffectID = ingredient->mData.mEffectID[0];
        effect.mSkill = ingredient->mData.mSkills[0];
        effect.mAttribute = ingredient->mData.mAttributes[0];
        effect.mRange = ESM::RT_Self;
        effect.mArea = 0;

        const MWWorld::ESMStore& store = MWBase::Environment::get().getWorld()->getStore();
        const auto magicEffect = store.get<ESM::MagicEffect>().find(effect.mEffectID);
        const MWMechanics::CreatureStats& creatureStats = mCaster.getClass().getCreatureStats(mCaster);

        float x = (mCaster.getClass().getSkill(mCaster, ESM::Skill::Alchemy) +
                    0.2f * creatureStats.getAttribute (ESM::Attribute::Intelligence).getModified()
                    + 0.1f * creatureStats.getAttribute (ESM::Attribute::Luck).getModified())
                    * creatureStats.getFatigueTerm();

        int roll = Misc::Rng::roll0to99();
        if (roll > x)
        {
            // "X has no effect on you"
            std::string message = store.get<ESM::GameSetting>().find("sNotifyMessage50")->mValue.getString();
            message = Misc::StringUtils::format(message, ingredient->mName);
            MWBase::Environment::get().getWindowManager()->messageBox(message);
            return false;
        }

        float magnitude = 0;
        float y = roll / std::min(x, 100.f);
        y *= 0.25f * x;
        if (magicEffect->mData.mFlags & ESM::MagicEffect::NoDuration)
            effect.mDuration = 1;
        else
            effect.mDuration = static_cast<int>(y);
        if (!(magicEffect->mData.mFlags & ESM::MagicEffect::NoMagnitude))
        {
            if (!(magicEffect->mData.mFlags & ESM::MagicEffect::NoDuration))
                magnitude = floor((0.05f * y) / (0.1f * magicEffect->mData.mBaseCost));
            else
                magnitude = floor(y / (0.1f * magicEffect->mData.mBaseCost));
            magnitude = std::max(1.f, magnitude);
        }
        else
            magnitude = 1;

        effect.mMagnMax = static_cast<int>(magnitude);
        effect.mMagnMin = static_cast<int>(magnitude);

        ESM::EffectList effects;
        effects.mList.push_back(effect);

        inflict(mCaster, mCaster, effects, ESM::RT_Self);

        return true;
    }

    void CastSpell::playSpellCastingEffects(const std::string &spellid, bool enchantment)
    {
        const MWWorld::ESMStore& store = MWBase::Environment::get().getWorld()->getStore();
        if (enchantment)
        {
            if (const auto spell = store.get<ESM::Enchantment>().search(spellid))
                playSpellCastingEffects(spell->mEffects.mList);
        }
        else
        {
            if (const auto spell = store.get<ESM::Spell>().search(spellid))
                playSpellCastingEffects(spell->mEffects.mList);
        }
    }

    void CastSpell::playSpellCastingEffects(const std::vector<ESM::ENAMstruct>& effects)
    {
        const MWWorld::ESMStore& store = MWBase::Environment::get().getWorld()->getStore();
        std::vector<std::string> addedEffects;
        for (const ESM::ENAMstruct& effectData : effects)
        {
            const auto effect = store.get<ESM::MagicEffect>().find(effectData.mEffectID);

            const ESM::Static* castStatic;

            if (!effect->mCasting.empty())
                castStatic = store.get<ESM::Static>().find (effect->mCasting);
            else
                castStatic = store.get<ESM::Static>().find ("VFX_DefaultCast");

            // check if the effect was already added
            if (std::find(addedEffects.begin(), addedEffects.end(), "meshes\\" + castStatic->mModel) != addedEffects.end())
                continue;

            MWRender::Animation* animation = MWBase::Environment::get().getWorld()->getAnimation(mCaster);
            if (animation)
            {
                animation->addEffect("meshes\\" + castStatic->mModel, effect->mIndex, false, "", effect->mParticle);
            }
            else
            {
                // If the caster has no animation, add the effect directly to the effectManager
                // We should scale it manually
                osg::Vec3f bounds (MWBase::Environment::get().getWorld()->getHalfExtents(mCaster) * 2.f / Constants::UnitsPerFoot);
                float scale = std::max({ bounds.x()/3.f, bounds.y()/3.f, bounds.z()/6.f });
                float meshScale = !mCaster.getClass().isActor() ? mCaster.getCellRef().getScale() : 1.0f;
                osg::Vec3f pos (mCaster.getRefData().getPosition().asVec3());
                MWBase::Environment::get().getWorld()->spawnEffect("meshes\\" + castStatic->mModel, effect->mParticle, pos, scale * meshScale);
            }

            if (animation && !mCaster.getClass().isActor())
                animation->addSpellCastGlow(effect);

            static const std::string schools[] = {
                "alteration", "conjuration", "destruction", "illusion", "mysticism", "restoration"
            };

            addedEffects.push_back("meshes\\" + castStatic->mModel);

            MWBase::SoundManager *sndMgr = MWBase::Environment::get().getSoundManager();
            if(!effect->mCastSound.empty())
                sndMgr->playSound3D(mCaster, effect->mCastSound, 1.0f, 1.0f);
            else
                sndMgr->playSound3D(mCaster, schools[effect->mData.mSchool]+" cast", 1.0f, 1.0f);
        }
    }
}
