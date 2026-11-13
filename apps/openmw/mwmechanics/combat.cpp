#include "../mwmechanics/arenacombatactors.hpp"
#include "combat.hpp"
#include "armorwearrules.hpp"
#include "activeguard.hpp"
#include "classarchetype.hpp"
#include "poison.hpp"

#include <algorithm>
#include <components/misc/rng.hpp>
#include <components/settings/settings.hpp>

#include <components/esm/loadmgef.hpp>
#include <components/esm/loadstat.hpp>

#include <components/sceneutil/positionattitudetransform.hpp>

/*
    Start of tes3mp addition

    Include additional headers for multiplayer purposes
*/
#include <components/openmw-mp/TimedLog.hpp>
#include "../mwmp/Main.hpp"
#include "../mwmp/LocalPlayer.hpp"
#include "../mwmp/PlayerList.hpp"
#include "../mwmp/CellController.hpp"
#include "../mwmp/MechanicsHelper.hpp"
/*
    End of tes3mp addition
*/

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwbase/windowmanager.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwworld/esmstore.hpp"

#include "../mwrender/animation.hpp"

#include "npcstats.hpp"
#include "movement.hpp"
#include "onstrikestacks.hpp"
#include "spellcasting.hpp"
#include "spellresistance.hpp"
#include "difficultyscaling.hpp"
#include "actorutil.hpp"
#include "pathfinding.hpp"
#include "xpleveling.hpp"

// added by EncoreMP

#include "weapontype.hpp"
#include "aipackage.hpp"

// added by EncoreMP

namespace
{

float signedAngleRadians (const osg::Vec3f& v1, const osg::Vec3f& v2, const osg::Vec3f& normal)
{
    return std::atan2((normal * (v1 ^ v2)), (v1 * v2));
}

bool arenaIsNetworkPlayer(const MWWorld::Ptr& actor)
{
    return !actor.isEmpty() && actor.getClass().isActor()
        && (actor == MWMechanics::getPlayer() || mwmp::PlayerList::isDedicatedPlayer(actor));
}

bool arenaIsNpcLevelActor(const MWWorld::Ptr& actor)
{
    return !actor.isEmpty() && actor.getClass().isActor() && !arenaIsNetworkPlayer(actor);
}

float arenaNpcLevelOffenseMultiplier(const MWWorld::Ptr& actor)
{
    if (!arenaIsNpcLevelActor(actor)) return 1.f;
    return MWMechanics::ArenaCombatLimits::offense(actor.getClass().getCreatureStats(actor).getLevel());
    }

float arenaPlayerStaminaDamageMultiplier(const MWWorld::Ptr& actor)
{
    if (!arenaIsNetworkPlayer(actor))
        return 1.f;

    const MWMechanics::CreatureStats& stats = actor.getClass().getCreatureStats(actor);
    const float maximum = stats.getFatigue().getModified();
    if (maximum <= 0.f)
        return 1.f;

    const float ratio = std::clamp(stats.getFatigue().getCurrent() / maximum, 0.f, 1.f);
    return MWMechanics::ArenaCombatLimits::stamina(ratio);
}

float arenaClampStandaloneHealthDamage(const MWWorld::Ptr& target, const MWWorld::Ptr& source, float damage)
{
    if (damage <= 0.f || target.isEmpty() || !target.getClass().isActor())
        return damage;
    const float maximumHealth = std::max(0.f,
        target.getClass().getCreatureStats(target).getHealth().getModified());
    if (maximumHealth <= 0.f)
        return damage;

    const float fraction = MWMechanics::ArenaCombatLimits::burstFraction(target, source);
    return MWMechanics::ArenaCombatLimits::reserveActiveImpactDamage(target,
        std::min(damage, maximumHealth * fraction));
}

}

namespace MWMechanics
{
    int getArmorWear(const MWWorld::Ptr& wearer, const MWWorld::Ptr& armor, float damage)
    {
        if (wearer.isEmpty() || armor.isEmpty() || !wearer.getClass().isNpc()
            || !armor.getClass().hasItemHealth(armor))
            return 0;
        const int skill = armor.getClass().getEquipmentSkill(armor);
        if (skill != ESM::Skill::LightArmor && skill != ESM::Skill::MediumArmor
            && skill != ESM::Skill::HeavyArmor)
            return 0;
        const float wear = ArmorWearRules::expectedWear(damage,
            armor.getClass().getItemMaxHealth(armor), wearer.getClass().getSkill(wearer, skill),
            wearer.getClass().getSkill(wearer, ESM::Skill::Armorer));
        return ArmorWearRules::roundWear(wear, Misc::Rng::rollProbability());
    }


    bool applyOnStrikeEnchantment(const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim, const MWWorld::Ptr& object, const osg::Vec3f& hitPosition, const bool fromProjectile)
    {
        std::string enchantmentName = !object.isEmpty() ? object.getClass().getEnchantment(object) : "";
        if (!enchantmentName.empty())
        {
            const ESM::Enchantment* enchantment = MWBase::Environment::get().getWorld()->getStore().get<ESM::Enchantment>().find(
                        enchantmentName);
            if (enchantment->mData.mType == ESM::Enchantment::WhenStrikes)
            {
                MWMechanics::CastSpell cast(attacker, victim, fromProjectile);
                cast.mHitPosition = hitPosition;
                // U035: one token per strike, so a multi-effect enchantment
                // cannot charge the same heavy stack more than once per blow.
                cast.mStrikeToken = MWMechanics::OnStrikeStacks::nextStrikeToken();
                cast.cast(object, false);
                return true;
            }
        }
        return false;
    }

    float applyClassArchetypeElementalHit(const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim,
        const MWWorld::Ptr& object, float physicalDamage, const osg::Vec3f& /*hitPosition*/)
    {
        if (attacker.isEmpty() || victim.isEmpty() || object.isEmpty() || physicalDamage <= 0.f)
            return 0.f;
        if (!attacker.getClass().isNpc())
            return 0.f;
        if (!victim.getClass().isActor() || object.getTypeName() != typeid(ESM::Weapon).name())
            return 0.f;
        if (!MechanicsHelper::isFriendlyFireAllowed(attacker, victim))
            return 0.f;
        if (victim == getPlayer() && MWBase::Environment::get().getWorld()->getGodModeState())
            return 0.f;

        int effectId = -1;
        float rawDamage = 0.f;
        if (!ClassArchetype::getWeaponElementalBonus(attacker, physicalDamage, effectId, rawDamage))
            return 0.f;

        CreatureStats& victimStats = victim.getClass().getCreatureStats(victim);
        float resistance = getEffectResistanceAttribute(static_cast<short>(effectId), &victimStats.getMagicEffects());
        const float multiplier = std::clamp(1.f - resistance / 100.f, 0.f, 2.f);
        float elementalDamage = std::max(0.f, rawDamage * multiplier);
        elementalDamage = arenaClampStandaloneHealthDamage(victim, attacker, elementalDamage);
        if (elementalDamage <= 0.f)
            return 0.f;

        DynamicStat<float> health = victimStats.getHealth();
        health.setCurrent(health.getCurrent() - elementalDamage);
        victimStats.setHealth(health);

        // Reuse the native magic-effect presentation, but do not create an
        // ActiveSpell: this is a deterministic weapon proc, not persistent state.
        const MWWorld::ESMStore& store = MWBase::Environment::get().getWorld()->getStore();
        const ESM::MagicEffect* effect = store.get<ESM::MagicEffect>().find(effectId);
        if (effect)
        {
            MWBase::SoundManager* sound = MWBase::Environment::get().getSoundManager();
            sound->playSound3D(victim, effect->mHitSound.empty() ? "destruction hit" : effect->mHitSound, 1.f, 1.f);

            const ESM::Static* hitStatic = nullptr;
            if (!effect->mHit.empty())
                hitStatic = store.get<ESM::Static>().search(effect->mHit);
            if (!hitStatic)
                hitStatic = store.get<ESM::Static>().search("VFX_DefaultHit");
            MWRender::Animation* animation = MWBase::Environment::get().getWorld()->getAnimation(victim);
            if (animation && hitStatic && !hitStatic->mModel.empty())
                animation->addEffect("meshes\\" + hitStatic->mModel, effect->mIndex, false, "", effect->mParticle);
        }

        return elementalDamage;
    }

    bool blockMeleeAttack(const MWWorld::Ptr &attacker, const MWWorld::Ptr &blocker, const MWWorld::Ptr &weapon, float damage, float attackStrength)
    {
        if (!blocker.getClass().hasInventoryStore(blocker))
            return false;

        MWMechanics::CreatureStats& blockerStats = blocker.getClass().getCreatureStats(blocker);

        if (blockerStats.getKnockedDown() // Used for both knockout or knockdown
                || blockerStats.getHitRecovery()
                || blockerStats.isParalyzed())
            return false;

        if (!MWBase::Environment::get().getMechanicsManager()->isReadyToBlock(blocker))
            return false;

        MWWorld::InventoryStore& inv = blocker.getClass().getInventoryStore(blocker);
        MWWorld::ContainerStoreIterator shield = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedLeft);
        if (shield == inv.end() || shield->getTypeName() != typeid(ESM::Armor).name())
            return false;

        if (!blocker.getRefData().getBaseNode())
            return false; // shouldn't happen

        float angleDegrees = osg::RadiansToDegrees(
                    signedAngleRadians (
                    (attacker.getRefData().getPosition().asVec3() - blocker.getRefData().getPosition().asVec3()),
                    blocker.getRefData().getBaseNode()->getAttitude() * osg::Vec3f(0,1,0),
                    osg::Vec3f(0,0,1)));

        const MWWorld::Store<ESM::GameSetting>& gmst = MWBase::Environment::get().getWorld()->getStore().get<ESM::GameSetting>();
        if (angleDegrees < gmst.find("fCombatBlockLeftAngle")->mValue.getFloat())
            return false;
        if (angleDegrees > gmst.find("fCombatBlockRightAngle")->mValue.getFloat())
            return false;

        MWMechanics::CreatureStats& attackerStats = attacker.getClass().getCreatureStats(attacker);

        float blockTerm = blocker.getClass().getSkill(blocker, ESM::Skill::Block) + 0.2f * blockerStats.getAttribute(ESM::Attribute::Agility).getModified()
            + 0.1f * blockerStats.getAttribute(ESM::Attribute::Luck).getModified();
        float enemySwing = attackStrength;
        float swingTerm = enemySwing * gmst.find("fSwingBlockMult")->mValue.getFloat() + gmst.find("fSwingBlockBase")->mValue.getFloat();

        float blockerTerm = blockTerm * swingTerm;
        if (blocker.getClass().getMovementSettings(blocker).mPosition[1] <= 0)
            blockerTerm *= gmst.find("fBlockStillBonus")->mValue.getFloat();
        blockerTerm *= blockerStats.getFatigueTerm();

        float attackerSkill = 0;
        if (weapon.isEmpty())
            attackerSkill = attacker.getClass().getSkill(attacker, ESM::Skill::HandToHand);
        else
            attackerSkill = attacker.getClass().getSkill(attacker, weapon.getClass().getEquipmentSkill(weapon));
        float attackerTerm = attackerSkill + 0.2f * attackerStats.getAttribute(ESM::Attribute::Agility).getModified()
                + 0.1f * attackerStats.getAttribute(ESM::Attribute::Luck).getModified();
        attackerTerm *= attackerStats.getFatigueTerm();

        int x = int(blockerTerm - attackerTerm);
        int iBlockMaxChance = gmst.find("iBlockMaxChance")->mValue.getInteger();
        int iBlockMinChance = gmst.find("iBlockMinChance")->mValue.getInteger();
        x = std::min(iBlockMaxChance, std::max(iBlockMinChance, x));

        /*
            Start of tes3mp change (major)

            Only calculate block chance for LocalPlayers and LocalActors; otherwise,
            get the block state from the relevant DedicatedPlayer or DedicatedActor
        */
        mwmp::Attack *localAttack = MechanicsHelper::getLocalAttack(attacker);

        if (localAttack)
        {
            localAttack->block = false;
        }

        mwmp::Attack *dedicatedAttack = MechanicsHelper::getDedicatedAttack(blocker);

        if ((dedicatedAttack && dedicatedAttack->block == true) ||
            Misc::Rng::roll0to99() < x)
        {
            if (localAttack)
            {
                localAttack->block = true;
            }
        /*
            End of tes3mp change (major)
        */

            // Reduce shield durability by incoming damage
            int shieldhealth = shield->getClass().getItemHealth(*shield);

            // Y110: passive shield blocks use the same wear rules as body armour.
            const int wear = getArmorWear(blocker, *shield, damage);
            shieldhealth -= std::min(shieldhealth, wear);
            shield->getCellRef().setCharge(shieldhealth);
            if (shieldhealth == 0)
                inv.unequipItem(*shield, blocker);


            // Reduce blocker fatigue
            const float fFatigueBlockBase = gmst.find("fFatigueBlockBase")->mValue.getFloat();
            const float fFatigueBlockMult = gmst.find("fFatigueBlockMult")->mValue.getFloat();
            const float fWeaponFatigueBlockMult = gmst.find("fWeaponFatigueBlockMult")->mValue.getFloat();
            MWMechanics::DynamicStat<float> fatigue = blockerStats.getFatigue();
            float normalizedEncumbrance = blocker.getClass().getNormalizedEncumbrance(blocker);
            normalizedEncumbrance = std::min(1.f, normalizedEncumbrance);
            float fatigueLoss = fFatigueBlockBase + normalizedEncumbrance * fFatigueBlockMult;
            if (!weapon.isEmpty())
                fatigueLoss += weapon.getClass().getWeight(weapon) * attackStrength * fWeaponFatigueBlockMult;
            fatigue.setCurrent(fatigue.getCurrent() - fatigueLoss);
            blockerStats.setFatigue(fatigue);

            blockerStats.setBlock(true);

            if (blocker == getPlayer())
                blocker.getClass().skillUsageSucceeded(blocker, ESM::Skill::Block, 0);

            return true;
        }
        return false;
    }

    bool isNormalWeapon(const MWWorld::Ptr &weapon)
    {
        if (weapon.isEmpty())
            return false;

        const int flags = weapon.get<ESM::Weapon>()->mBase->mData.mFlags;
        bool isSilver = flags & ESM::Weapon::Silver;
        bool isMagical = flags & ESM::Weapon::Magical;
        bool isEnchanted = !weapon.getClass().getEnchantment(weapon).empty();

        return !isSilver && !isMagical && (!isEnchanted || !Settings::Manager::getBool("enchanted weapons are magical", "Game"));
    }

    void resistNormalWeapon(const MWWorld::Ptr &actor, const MWWorld::Ptr& attacker, const MWWorld::Ptr &weapon, float &damage)
    {
        if (damage == 0 || weapon.isEmpty() || !isNormalWeapon(weapon))
            return;

        const MWMechanics::MagicEffects& effects = actor.getClass().getCreatureStats(actor).getMagicEffects();
        const float resistance = effects.get(ESM::MagicEffect::ResistNormalWeapons).getMagnitude() / 100.f;
        const float weakness = effects.get(ESM::MagicEffect::WeaknessToNormalWeapons).getMagnitude() / 100.f;

        damage *= 1.f - std::min(1.f, resistance-weakness);

        if (damage == 0 && attacker == getPlayer())
            MWBase::Environment::get().getWindowManager()->messageBox("#{sMagicTargetResistsWeapons}");
    }

    void applyWerewolfDamageMult(const MWWorld::Ptr &actor, const MWWorld::Ptr &weapon, float &damage)
    {
        if (damage == 0 || weapon.isEmpty() || !actor.getClass().isNpc())
            return;

        const int flags = weapon.get<ESM::Weapon>()->mBase->mData.mFlags;
        bool isSilver = flags & ESM::Weapon::Silver;

        if (isSilver && actor.getClass().getNpcStats(actor).isWerewolf())
        {
            const MWWorld::ESMStore& store = MWBase::Environment::get().getWorld()->getStore();
            damage *= store.get<ESM::GameSetting>().find("fWereWolfSilverWeaponDamageMult")->mValue.getFloat();
        }
    }

    void projectileHit(const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim, MWWorld::Ptr weapon, const MWWorld::Ptr& projectile,
                       const osg::Vec3f& hitPosition, float attackStrength)
    {
        /*
            Start of tes3mp addition

            Ignore projectiles fired by DedicatedPlayers and DedicatedActors

            If fired by LocalPlayers and LocalActors, get the associated LocalAttack and set its type
            to RANGED while also marking it as a hit
        */
        if (mwmp::PlayerList::isDedicatedPlayer(attacker) || mwmp::Main::get().getCellController()->isDedicatedActor(attacker))
            return;

        mwmp::Attack *localAttack = MechanicsHelper::getLocalAttack(attacker);

        if (localAttack)
        {
            localAttack->guardResult = 0;
            localAttack->guardFatigue = localAttack->guardWear = 0.f;
            localAttack->guardItem.clear();
            localAttack->block = false;
            localAttack->type = mwmp::Attack::RANGED;
            localAttack->isHit = true;
        }
        /*
            End of tes3mp addition
        */

        MWBase::World *world = MWBase::Environment::get().getWorld();
        const MWWorld::Store<ESM::GameSetting> &gmst = world->getStore().get<ESM::GameSetting>();

        bool validVictim = !victim.isEmpty() && victim.getClass().isActor();

        float damage = 0.f;
        if (validVictim)
        {
            if (attacker == getPlayer())
                MWBase::Environment::get().getWindowManager()->setEnemy(victim);

            int weaponSkill = ESM::Skill::Marksman;
            if (!weapon.isEmpty())
                weaponSkill = weapon.getClass().getEquipmentSkill(weapon);

            int skillValue = attacker.getClass().getSkill(attacker, weapon.getClass().getEquipmentSkill(weapon));

            /*
                Start of tes3mp addition

                Mark this as a successful attack for the associated LocalAttack unless proven otherwise
            */
            if (localAttack)
                localAttack->success = true;
            /*
                End of tes3mp addition
            */

            // start of EncoreMP ranged attack accuracy changes

            float hitchanceholder = 0.f;
            hitchanceholder = getHitChance(attacker, victim, skillValue);

            if (attacker == getPlayer())
            {
                hitchanceholder += 20;
            }

            // CB1: Light/Sanctuary dodge also works against arrows and bolts.
            if (Misc::Rng::roll0to99() >= hitchanceholder || ActiveGuard::evaded(attacker, victim))
            {
                /*
                    Start of tes3mp addition

                    Mark this as a failed LocalAttack now that the hit roll has failed
                */
                if (localAttack)
                    localAttack->success = false;
                /*
                    End of tes3mp addition
                */

                victim.getClass().onHit(victim, damage, false, projectile, attacker, osg::Vec3f(), false);
                MWMechanics::reduceWeaponCondition(damage, false, weapon, attacker);
                return;
            }

            // end of EncoreMP ranged attack accuracy changes

            const unsigned char* attack = weapon.get<ESM::Weapon>()->mBase->mData.mChop;
            damage = attack[0] + ((attack[1] - attack[0]) * attackStrength); // Bow/crossbow damage

            // Arrow/bolt damage
            // NB in case of thrown weapons, we are applying the damage twice since projectile == weapon
            attack = projectile.get<ESM::Weapon>()->mBase->mData.mChop;
            damage += attack[0] + ((attack[1] - attack[0]) * attackStrength);

            adjustWeaponDamage(damage, weapon, attacker);
            if (weapon == projectile || Settings::Manager::getBool("only appropriate ammunition bypasses resistance", "Game") || isNormalWeapon(weapon))
                resistNormalWeapon(victim, attacker, projectile, damage);
            applyWerewolfDamageMult(victim, projectile, damage);

            if (attacker == getPlayer())
                attacker.getClass().skillUsageSucceeded(attacker, weaponSkill, 0);

            const MWMechanics::AiSequence& sequence = victim.getClass().getCreatureStats(victim).getAiSequence();
            bool unaware = attacker == getPlayer() && !sequence.isInCombat()
                && !MWBase::Environment::get().getMechanicsManager()->awarenessCheck(attacker, victim);
            bool knockedDown = victim.getClass().getCreatureStats(victim).getKnockedDown();
            if (knockedDown || unaware)
            {
                damage *= gmst.find("fCombatKODamageMult")->mValue.getFloat();
                if (!knockedDown)
                    MWBase::Environment::get().getSoundManager()->playSound3D(victim, "critical damage", 1.0f, 1.0f);
            }

            if (unaware && damage > 0.f)
                XPLeveling::awardCriticalHit(attacker, victim);
        }

        reduceWeaponCondition(damage, validVictim, weapon, attacker);

        const bool guarded = validVictim && ActiveGuard::resolve(attacker, victim, weapon, damage, true, localAttack);
        const bool blocked = guarded && damage <= 0.f;

        // Y114: bow, arrow enchantment and poison share a single impact cap.
        MWMechanics::ArenaCombatLimits::ScopedImpactBudget arenaImpact(victim, attacker);

        // Apply "On hit" effect of the projectile
        bool appliedEnchantment = !blocked && applyOnStrikeEnchantment(attacker, victim, projectile, hitPosition, true);

        /*
            Start of tes3mp change (minor)

            Track whether the strike enchantment is successful for attacks by the
            LocalPlayer or LocalActors for their projectile
        */
        if (localAttack)
            localAttack->applyAmmoEnchantment = appliedEnchantment;
        /*
            End of tes3mp change (minor)
        */

        // Poison on ranged attacks belongs to the bow/crossbow or thrown weapon.
        // This function already rejects DedicatedPlayer/DedicatedActor attackers,
        // so the charge is consumed only by the authoritative firing client.
        if (validVictim && !blocked
            && !(victim == getPlayer() && MWBase::Environment::get().getWorld()->getGodModeState()))
            applyWeaponPoison(attacker, victim, weapon, hitPosition, true);

        if (validVictim)
        {

            // start of EncoreMP arrow recovery changes

            int getmarksman = attacker.getClass().getSkill(attacker, weapon.getClass().getEquipmentSkill(weapon));
            
            // Non-enchanted arrows shot at enemies have a chance to turn up in their inventory
            // EncoreMP makes it so that enchanted arrows can too

            if (victim != getPlayer() && !appliedEnchantment)
            {
                float fProjectileThrownStoreChance = gmst.find("fProjectileThrownStoreChance")->mValue.getFloat();

                if (attacker == getPlayer())
                {
                    float skillprojectilemod = 0.f;
                    float getmarksmanfloat = getmarksman;
                    skillprojectilemod = std::max(1.0f, getmarksmanfloat / 25.0f);
                    fProjectileThrownStoreChance *= skillprojectilemod;
                }

                if (Misc::Rng::rollProbability() < fProjectileThrownStoreChance / 100.f)
                    victim.getClass().getContainerStore(victim).add(projectile, 1, victim);
            }

            if (victim != getPlayer() && appliedEnchantment)
            {
                float fProjectileThrownStoreChance = gmst.find("fProjectileThrownStoreChance")->mValue.getFloat();

                if (attacker == getPlayer())
                {
                    float skillprojectilemod = 0.f;
                    float getmarksmanfloat = getmarksman;
                    if (getmarksmanfloat > 99)
                    {
                        getmarksmanfloat = 100;
                    }
                    if (getmarksmanfloat < 51)
                    {
                        fProjectileThrownStoreChance = 0.f;
                    }
                    else
                    {
                        getmarksmanfloat -= 50;
                        skillprojectilemod = std::max(0.05f, getmarksmanfloat / 25.0f);
                    }
                    fProjectileThrownStoreChance *= skillprojectilemod;
                }

                if (Misc::Rng::rollProbability() < fProjectileThrownStoreChance / 100.f)
                    victim.getClass().getContainerStore(victim).add(projectile, 1, victim);
            }

            /// end of EncoreMP arrow recovery changes

            victim.getClass().onHit(victim, damage, true, projectile, attacker, hitPosition, true);
        }
        /*
            Start of tes3mp addition

            If this is a local attack that had no victim, send a packet for it here
        */
        else if (localAttack)
        {
            localAttack->hitPosition = MechanicsHelper::getPositionFromVector(hitPosition);
            localAttack->shouldSend = true;
        }
        /*
            End of tes3mp addition
        */
    }

    float getHitChance(const MWWorld::Ptr &attacker, const MWWorld::Ptr &victim, int skillValue)
    {
        // Called only after a melee sweep/projectile has contacted the target.
        // Avoid a second invisible dice roll; active guard and positioning defend.
        (void)attacker; (void)victim; (void)skillValue;
        return 100.f;
    }

    void applyElementalShields(const MWWorld::Ptr &attacker, const MWWorld::Ptr &victim)
    {
        // Don't let elemental shields harm the player in god mode.
        bool godmode = attacker == getPlayer() && MWBase::Environment::get().getWorld()->getGodModeState();
        if (godmode)
            return;
        for (int i=0; i<3; ++i)
        {
            float magnitude = victim.getClass().getCreatureStats(victim).getMagicEffects().get(ESM::MagicEffect::FireShield+i).getMagnitude();

            if (!magnitude)
                continue;

            CreatureStats& attackerStats = attacker.getClass().getCreatureStats(attacker);
            float saveTerm = attacker.getClass().getSkill(attacker, ESM::Skill::Destruction)
                    + 0.2f * attackerStats.getAttribute(ESM::Attribute::Willpower).getModified()
                    + 0.1f * attackerStats.getAttribute(ESM::Attribute::Luck).getModified();

            float fatigueMax = attackerStats.getFatigue().getModified();
            float fatigueCurrent = attackerStats.getFatigue().getCurrent();

            float normalisedFatigue = floor(fatigueMax)==0 ? 1 : std::max (0.0f, (fatigueCurrent/fatigueMax));

            saveTerm *= 1.25f * normalisedFatigue;

            float x = std::max(0.f, saveTerm - Misc::Rng::roll0to99());

            int element = ESM::MagicEffect::FireDamage;
            if (i == 1)
                element = ESM::MagicEffect::ShockDamage;
            if (i == 2)
                element = ESM::MagicEffect::FrostDamage;

            float elementResistance = MWMechanics::getEffectResistanceAttribute(element, &attackerStats.getMagicEffects());

            x = std::min(100.f, x + elementResistance);

            static const float fElementalShieldMult = MWBase::Environment::get().getWorld()->getStore().get<ESM::GameSetting>().find("fElementalShieldMult")->mValue.getFloat();
            x = fElementalShieldMult * magnitude * (1.f - 0.01f * x);

            // determine if the source of the Elemental shield is a creature allied with the player, and set Boolean to true if so
            bool sourceIsPlayerAlly = false;

            if (!victim.isEmpty() && victim.getClass().isActor())
            {
                MWMechanics::CreatureStats& statsSource = victim.getClass().getCreatureStats(victim);
                for (const auto& package : statsSource.getAiSequence())
                {
                    if (!package) continue;
                    if (package && package->followTargetThroughDoors())
                    {
                        const MWWorld::Ptr& master = package->getTarget();
                        if (master.isEmpty()) continue;
                        bool masterIsPlayer = (master == MWMechanics::getPlayer()) || mwmp::PlayerList::isDedicatedPlayer(master);
                        if (!masterIsPlayer) continue;
                        sourceIsPlayerAlly = true;
                    }
                }
            }


            MWWorld::Ptr player = MWMechanics::getPlayer();

            if (attacker == player)
            {
                x *= magicdamagetaken();
            }
            else if ((victim == player) || (sourceIsPlayerAlly == true))
            {
                x *= castenchantedDamagescale();
            }


            x = arenaClampStandaloneHealthDamage(attacker, victim, x);
            MWMechanics::DynamicStat<float> health = attackerStats.getHealth();
            health.setCurrent(health.getCurrent() - x);
            attackerStats.setHealth(health);

            MWBase::Environment::get().getSoundManager()->playSound3D(attacker, "Health Damage", 1.0f, 1.0f);
        }
    }

    void reduceWeaponCondition(float damage, bool hit, MWWorld::Ptr &weapon, const MWWorld::Ptr &attacker)
    {
        if (weapon.isEmpty())
            return;

        if (!hit)
            damage = 0.f;

        const bool weaphashealth = weapon.getClass().hasItemHealth(weapon);
        if(weaphashealth)
        {
            int weaphealth = weapon.getClass().getItemHealth(weapon);

            bool godmode = attacker == MWMechanics::getPlayer() && MWBase::Environment::get().getWorld()->getGodModeState();

            // weapon condition does not degrade when godmode is on
            if (!godmode)
            {
                const float fWeaponDamageMult = MWBase::Environment::get().getWorld()->getStore().get<ESM::GameSetting>().find("fWeaponDamageMult")->mValue.getFloat();
                float x = std::max(1.f, fWeaponDamageMult * damage);

                MWWorld::Ptr player = MWMechanics::getPlayer();
                if (attacker == player)
                {
                    /// get and clamp armorer skill
                    float armorerSkill = player.getClass().getSkill(player, ESM::Skill::Armorer);
                    armorerSkill = std::min(100.0f, armorerSkill);
                    armorerSkill = std::max(1.0f, armorerSkill);

                    float armorerx = 1.0f - (armorerSkill * 0.0075f);

                    x *= weapondamagetaken();

                    x = (armorerx * x);

                    x = std::max(1.0f, x);

                }

                weaphealth -= std::min(int(x), weaphealth);
                weapon.getCellRef().setCharge(weaphealth);
            }

            // Weapon broken? unequip it
            if (weaphealth == 0)
                weapon = *attacker.getClass().getInventoryStore(attacker).unequipItem(weapon, attacker);
        }
    }

    void adjustWeaponDamage(float &damage, const MWWorld::Ptr &weapon, const MWWorld::Ptr& attacker)
    {
        if (weapon.isEmpty()) return;
        if (weapon.getClass().hasItemHealth(weapon))
            damage *= weapon.getClass().getItemNormalizedHealth(weapon);
        const auto& stats = attacker.getClass().getCreatureStats(attacker);
        const int skillId = weapon.getClass().getEquipmentSkill(weapon);
        const float skill = skillId >= 0 ? attacker.getClass().getSkill(attacker, skillId) : 30.f;
        const float strength = stats.getAttribute(ESM::Attribute::Strength).getModified();
        const float agility = stats.getAttribute(ESM::Attribute::Agility).getModified();
        float attribute = strength;
        if (skillId == ESM::Skill::ShortBlade || skillId == ESM::Skill::Marksman)
            attribute = agility * .75f + strength * .25f;
        else if (skillId == ESM::Skill::LongBlade || skillId == ESM::Skill::Spear)
            attribute = (strength + agility) * .5f;
        damage *= ArenaCombatLimits::weaponScaling(skill, attribute);
        if (attacker.getClass().isNpc())
            damage *= ClassArchetype::getWeaponDamageMultiplier(attacker);
        damage *= arenaPlayerStaminaDamageMultiplier(attacker);
        damage *= arenaNpcLevelOffenseMultiplier(attacker);
    }

    void getHandToHandDamage(const MWWorld::Ptr &attacker, const MWWorld::Ptr &victim, float &damage, bool &healthdmg, float attackStrength)
    {
        const auto& stats = attacker.getClass().getCreatureStats(attacker);
        const float skill = attacker.getClass().getSkill(attacker, ESM::Skill::HandToHand);
        const float attribute = (stats.getAttribute(ESM::Attribute::Strength).getModified()
            + stats.getAttribute(ESM::Attribute::Agility).getModified()) * .5f;
        healthdmg = true;
        damage = 10.f * (1.f + 1.2f * std::clamp(attackStrength, 0.f, 1.f))
            * ArenaCombatLimits::weaponScaling(skill, attribute);
        const bool werewolf = attacker.getClass().isNpc()
            && attacker.getClass().getNpcStats(attacker).isWerewolf();
        if (werewolf)
            damage *= MWBase::Environment::get().getWorld()->getGlobalFloat("werewolfclawmult");
        damage *= arenaPlayerStaminaDamageMultiplier(attacker);
        damage *= arenaNpcLevelOffenseMultiplier(attacker);
        if (!werewolf)
            MWBase::Environment::get().getSoundManager()->playSound3D(victim, "Hand To Hand Hit", 1.f, 1.f);
    }

    void applyFatigueLoss(const MWWorld::Ptr &attacker, const MWWorld::Ptr &weapon, float attackStrength)
    {
        CreatureStats& stats = attacker.getClass().getCreatureStats(attacker);
        MWMechanics::DynamicStat<float> fatigue = stats.getFatigue();
        const float normalizedEncumbrance = attacker.getClass().getNormalizedEncumbrance(attacker);

        bool godmode = attacker == MWMechanics::getPlayer() && MWBase::Environment::get().getWorld()->getGodModeState();

        if (!godmode)
        {
            float fatigueLoss = ArenaCombatLimits::attackCost(
                weapon.isEmpty() ? 0.f : weapon.getClass().getWeight(weapon),
                attackStrength, normalizedEncumbrance);
            if (attacker == MWMechanics::getPlayer())
                fatigueLoss *= ArenaCombatLimits::playerAttackCostMultiplier(
                    stats.getAttribute(ESM::Attribute::Endurance).getModified());
            fatigue.setCurrent(std::max(0.f, fatigue.getCurrent() - fatigueLoss));
            stats.setFatigue(fatigue);
        }
    }

    float getFightDistanceBias(const MWWorld::Ptr& actor1, const MWWorld::Ptr& actor2)
    {
        osg::Vec3f pos1 (actor1.getRefData().getPosition().asVec3());
        osg::Vec3f pos2 (actor2.getRefData().getPosition().asVec3());

        float d = getAggroDistance(actor1, pos1, pos2);

        static const int iFightDistanceBase = MWBase::Environment::get().getWorld()->getStore().get<ESM::GameSetting>().find(
                    "iFightDistanceBase")->mValue.getInteger();
        static const float fFightDistanceMultiplier = MWBase::Environment::get().getWorld()->getStore().get<ESM::GameSetting>().find(
                    "fFightDistanceMultiplier")->mValue.getFloat();

        return (iFightDistanceBase - fFightDistanceMultiplier * d);
    }

    bool isTargetMagicallyHidden(const MWWorld::Ptr& target)
    {
        const MagicEffects& magicEffects = target.getClass().getCreatureStats(target).getMagicEffects();
        return (magicEffects.get(ESM::MagicEffect::Invisibility).getMagnitude() > 0)
            || (magicEffects.get(ESM::MagicEffect::Chameleon).getMagnitude() > 75);
    }

    float getAggroDistance(const MWWorld::Ptr& actor, const osg::Vec3f& lhs, const osg::Vec3f& rhs)
    {
        if (canActorMoveByZAxis(actor))
            return distanceIgnoreZ(lhs, rhs);
        return distance(lhs, rhs);
    }
}
