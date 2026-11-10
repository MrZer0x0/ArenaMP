#include "../mwmechanics/arenacombo.hpp"
#include <algorithm>
#include <cmath>
#include <exception>
#include <sstream>
#include <MyGUI_LanguageManager.h>
#include <components/misc/stringops.hpp>
#include <SDL_keyboard.h>
#include <SDL_scancode.h>
#include <components/esm/loadalch.hpp>
#include <components/esm/loadmgef.hpp>
#include <components/esm/esmwriter.hpp>
#include <components/esm/loadbook.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include <components/openmw-mp/Utils.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/journal.hpp"
#include "../mwbase/soundmanager.hpp"

#include "../mwclass/creature.hpp"
#include "../mwclass/npc.hpp"

#include "../mwdialogue/dialoguemanagerimp.hpp"
#include "../mwdialogue/journalimp.hpp"

#include "../mwgui/inventorywindow.hpp"
#include "../mwgui/tradewindow.hpp"
#include "CraftEconomic.hpp"
#include "../mwgui/windowmanagerimp.hpp"

#include "../mwinput/inputmanagerimp.hpp"

#include "../mwmechanics/activespells.hpp"
#include "../mwmechanics/aitravel.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/mechanicsmanagerimp.hpp"
#include "../mwmechanics/character.hpp"
#include "../mwmechanics/movement.hpp"
#include "../mwmechanics/spellcasting.hpp"
#include "../mwmechanics/spellutil.hpp"

#include "../mwscript/scriptmanagerimp.hpp"

#include "../mwstate/statemanagerimp.hpp"

#include "../mwworld/cellstore.hpp"
#include "../mwworld/customdata.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/manualref.hpp"
#include "../mwworld/player.hpp"
#include "../mwworld/worldimp.hpp"
#include "../mwworld/interactionanimation.hpp"

#include "../mwrender/animation.hpp"

#include "LocalPlayer.hpp"
#include "Main.hpp"
#include "PlayerPose.hpp"
#include "Networking.hpp"
#include "PlayerList.hpp"
#include "CellController.hpp"
#include "ScriptController.hpp"
#include "GUIController.hpp"
#include "MechanicsHelper.hpp"
#include "InteractionAnimationSync.hpp"


namespace
{
    void notifyDialogueInventoryChange(const std::string& name, int count, bool added)
    {
        auto* windowManager = MWBase::Environment::get().getWindowManager();
        if (count <= 0 || !windowManager->containsMode(MWGui::GM_Dialogue)
            || windowManager->containsMode(MWGui::GM_Barter))
            return;
        const char* tag = added
            ? (count > 1 ? "#{sNotifyMessage61}" : "#{sNotifyMessage60}")
            : (count > 1 ? "#{sNotifyMessage63}" : "#{sNotifyMessage62}");
        std::string text = MyGUI::LanguageManager::getInstance().replaceTags(tag);
        text = count > 1 ? Misc::StringUtils::format(text, count, name)
                         : Misc::StringUtils::format(text, name);
        windowManager->messageBox(text, MWGui::ShowInDialogueMode_Only);
    }

    constexpr float sNetworkPoseTransitionSeconds = 0.18f;
    // ArenaMP Y065: reliable ordered animation packets plus a compact heartbeat.
    // 2.5 s is fast enough to heal late AOI/cell-controller reconstruction while
    // remaining far below movement packet traffic. Identical refreshes are deduped.
    constexpr float sAnimationStateHeartbeatSeconds = 2.5f;
    int getPlayerPoseBlendMask(const MWWorld::Ptr& ptr, int requestedMask, const std::string& group)
    {
        int result = requestedMask;
        if (mwmp::isWeaponPose(group)) return result;
        if (!ptr.isEmpty() && ptr.getClass().hasInventoryStore(ptr))
        {
            MWWorld::InventoryStore& store = ptr.getClass().getInventoryStore(ptr);
            MWWorld::ContainerStoreIterator carried = store.getSlot(MWWorld::InventoryStore::Slot_CarriedLeft);
            if (carried != store.end()
                && (carried.getType() == MWWorld::ContainerStore::Type_Armor
                    || carried.getType() == MWWorld::ContainerStore::Type_Light))
            {
                result &= ~MWRender::Animation::BlendMask_LeftArm;
            }
        }
        return result;
    }

    bool playerPoseIsBlocked(const MWWorld::Ptr& ptr, bool jumping, const std::string& group = std::string())
    {
        if (ptr.isEmpty())
            return true;

        const MWMechanics::CreatureStats& stats = ptr.getClass().getCreatureStats(ptr);
        const MWMechanics::Movement& movement = ptr.getClass().getMovementSettings(ptr);
        const bool moving = std::abs(movement.mPosition[0]) > 0.05f
            || std::abs(movement.mPosition[1]) > 0.05f
            || std::abs(movement.mPosition[2]) > 0.05f;

        return jumping || moving || stats.isDead() || stats.getKnockedDown()
            || !ptr.getRefData().getAnimationState().mScriptedAnims.empty()
            || stats.getAiSequence().isInCombat()
            || stats.isParalyzed()
            || MWBase::Environment::get().getMechanicsManager()->isAttackingOrSpell(ptr)
            || (mwmp::isWeaponPose(group)
                ? (stats.getDrawState() != MWMechanics::DrawState_Weapon || !mwmp::weaponPoseMatches(ptr, group))
                : stats.getDrawState() != MWMechanics::DrawState_Nothing)
            || MWBase::Environment::get().getWorld()->isSwimming(ptr);
    }

    bool playPlayerPose(const MWWorld::Ptr& ptr, const std::string& group, int blendMask, float speed)
    {
        MWRender::Animation* animation = MWBase::Environment::get().getWorld()->getAnimation(ptr);
        if (!animation || group.empty() || blendMask == 0 || !animation->hasAnimation(group))
            return false;

        MWRender::Animation::AnimPriority priority(MWMechanics::Priority_Default);
        if (blendMask & MWRender::Animation::BlendMask_LowerBody)
            priority[MWRender::Animation::BoneGroup_LowerBody] = MWMechanics::Priority_Weapon;
        if (blendMask & MWRender::Animation::BlendMask_Torso)
            priority[MWRender::Animation::BoneGroup_Torso] = MWMechanics::Priority_Weapon;
        if (blendMask & MWRender::Animation::BlendMask_LeftArm)
            priority[MWRender::Animation::BoneGroup_LeftArm] = MWMechanics::Priority_Weapon;
        if (blendMask & MWRender::Animation::BlendMask_RightArm)
            priority[MWRender::Animation::BoneGroup_RightArm] = MWMechanics::Priority_Weapon;

        if (animation->isPlaying(group))
            animation->disable(group);
        animation->beginBoneTransition(blendMask, sNetworkPoseTransitionSeconds);
        animation->play(group, priority, blendMask, false, speed, "start", "stop", 0.f, 1000000, true);
        return animation->isPlaying(group);
    }

    void stopPlayerPose(const MWWorld::Ptr& ptr, const std::string& group)
    {
        if (group.empty())
            return;
        if (MWRender::Animation* animation = MWBase::Environment::get().getWorld()->getAnimation(ptr))
        {
            if (animation->getInfo(group))
            {
                animation->beginBoneTransition(MWRender::Animation::BlendMask_All,
                    sNetworkPoseTransitionSeconds);
                animation->disable(group);
            }
        }
    }

    const char* sPlayerPosePrefix = "arenamp_pose|";

    std::string encodePlayerPose(const std::string& group, int blendMask, float speed)
    {
        std::ostringstream stream;
        stream << sPlayerPosePrefix << blendMask << '|' << speed << '|' << group;
        return stream.str();
    }

    bool decodePlayerPose(const std::string& value, std::string& group, int& blendMask, float& speed)
    {
        const std::string prefix(sPlayerPosePrefix);
        if (value.compare(0, prefix.size(), prefix) != 0)
            return false;

        const size_t maskEnd = value.find('|', prefix.size());
        if (maskEnd == std::string::npos)
            return false;
        const size_t speedEnd = value.find('|', maskEnd + 1);
        if (speedEnd == std::string::npos)
            return false;

        try
        {
            blendMask = std::stoi(value.substr(prefix.size(), maskEnd - prefix.size()));
            speed = std::stof(value.substr(maskEnd + 1, speedEnd - maskEnd - 1));
        }
        catch (const std::exception&)
        {
            return false;
        }

        group = value.substr(speedEnd + 1);
        return true;
    }
}

using namespace mwmp;

std::map<std::string, int> storedItemRemovals;

LocalPlayer::LocalPlayer()
{
    deathTime = time(0);
    receivedCharacter = false;

    charGenState.currentStage = 0;
    charGenState.endStage = 1;
    charGenState.isFinished = false;

    ignorePosPacket = false;
    ignoreJailTeleportation = false;
    ignoreJailSkillIncreases = false;
    
    MWMechanics::ArenaCombo::local().reset();
    attack.shouldSend = false;
    attack.instant = false;
    attack.pressed = false;

    cast.shouldSend = false;
    cast.instant = false;
    cast.pressed = false;

    killer.isPlayer = false;
    killer.refId = "";
    killer.name = "";

    isChangingRegion = false;

    jailProgressText = "";
    jailEndText = "";

    isUsingBed = false;
    avoidSendingInventoryPackets = false;
    isReceivingQuickKeys = false;
    mRespawnWaitActive = false;
    mRespawnWaitRemaining = mRespawnWaitDuration = 0.f;
    isPlayingAnimation = false;
    diedSinceArrestAttempt = false;

    // Y044: no server profile has been received yet, so nothing may be sent upstream.
    mLoginSyncPending = false;
    mLoginSyncReceived = 0;
    mLoginSyncElapsed = 0.f;
    mDynamicStatsBaselineValid = false;

    mDeathRecoveryActive = false;
    mDeathRecoveryElapsed = 0.f;
    mDeathRecoveryDuration = 30.f;
    mDeathRecoveryInitialXp = 0.f;
    mDeathRecoveryRequestCooldown = 0.f;
    mDeathRecoveryUseQueued = false;
    mReviveLockoutRemaining = 0.f;

    mPersistentAnimationActive = false;
    mPersistentAnimationPlaying = false;
    mPersistentAnimationBlendMask = MWRender::Animation::BlendMask_All;
    mPersistentAnimationAppliedMask = 0;
    mPersistentAnimationSpeed = 1.f;
    mPersistentAnimationSyncTimer = 0.f;

    mWalkAnimationStyle.clear();
    mWalkAnimationSyncTimer = 0.f;

    mInteractionAnimationActive = false;
    mInteractionAnimationTime = 0.f;
    mInteractionAnimationSyncTimer = 0.f;
}


LocalPlayer::~LocalPlayer()
{

}

void LocalPlayer::updateLanguage()
{
    const MWBase::WindowManager* windowManager = MWBase::Environment::get().getWindowManager();
    const std::string clientLanguage = windowManager ? windowManager->getArenaLanguage() : "en";
    language = clientLanguage == "ru" ? "RU" : "EN";
}


Networking *LocalPlayer::getNetworking()
{
    return mwmp::Main::get().getNetworking();
}

MWWorld::Ptr LocalPlayer::getPlayerPtr()
{
    return MWBase::Environment::get().getWorld()->getPlayerPtr();
}

void LocalPlayer::update()
{
    const float frameDuration = MWBase::Environment::get().getFrameDuration();
    updateDeathRecovery(frameDuration);
    updateInteractionAnimation(frameDuration);
    updatePersistentAnimation(frameDuration);
    updateWalkAnimationSync(frameDuration);
    MWWorld::InteractionAnimation::update(frameDuration);

    static float updateTimer = 0;
    const float timeoutSec = 0.015;

    /*
        Start of AMP addition (Y044)
    */
    updateLoginSync(frameDuration);
    /*
        End of AMP addition (Y044)
    */

    if ((updateTimer += MWBase::Environment::get().getFrameDuration()) >= timeoutSec)
    {
        updateTimer = 0;
        updateCell();
        updatePosition();
        updateAnimFlags();
        updateAttackOrCast();

        /*
            Start of AMP change (Y044)

            Movement, animation and casting stay live during the login handshake so
            the session never feels frozen, but character state is not published
            until the server-owned profile has replaced the CharGen placeholder.
        */
        if (!mLoginSyncPending)
        {
            updateEquipment();
            updateStatsDynamic();
            updateAttributes();
            updateSkills();
            updateLevel();
        }
        /*
            End of AMP change (Y044)
        */

        updateBounty();
        updateReputation();
    }
}

/*
    Start of AMP addition (Y044)

    Login profile synchronisation gate.
*/
void LocalPlayer::beginLoginSync(bool force)
{
    // A character created in this session has no stored profile to wait for; its
    // state was already published by processCharGen().
    if (!force && charGenState.endStage > 1)
    {
        mLoginSyncPending = false;
        mDynamicStatsBaselineValid = false;
        return;
    }

    mLoginSyncPending = true;
    mLoginSyncReceived = 0;
    mLoginSyncElapsed = 0.f;
    mDynamicStatsBaselineValid = false;

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
        "Holding back character state packets until the server profile arrives");
}

void LocalPlayer::markLoginSyncReceived(int section)
{
    if (!mLoginSyncPending)
        return;

    mLoginSyncReceived |= section;

    if ((mLoginSyncReceived & LoginSync_All) == LoginSync_All)
    {
        mLoginSyncPending = false;
        adoptServerStateAsBaseline();

        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
            "Server profile fully applied, resuming character state packets");
    }
}

void LocalPlayer::updateLoginSync(float dt)
{
    if (!mLoginSyncPending)
        return;

    mLoginSyncElapsed += dt;

    // A server that never sends one of the sections must not lock the client out
    // of its own progression for the rest of the session.
    static const float sLoginSyncTimeout = 15.f;

    if (mLoginSyncElapsed >= sLoginSyncTimeout)
    {
        mLoginSyncPending = false;
        adoptServerStateAsBaseline();

        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
            "Timed out waiting for the full server profile (received mask %d), resuming character state packets",
            mLoginSyncReceived);
    }
}

// Copy whatever the player Ptr holds right now into our mirrors, so the first
// update after the gate opens does not report the freshly applied server values
// back as if they were local changes.
void LocalPlayer::adoptServerStateAsBaseline()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();

    if (ptrPlayer.isEmpty())
        return;

    const MWMechanics::NpcStats& ptrNpcStats = ptrPlayer.getClass().getNpcStats(ptrPlayer);
    MWMechanics::CreatureStats& ptrCreatureStats = ptrPlayer.getClass().getCreatureStats(ptrPlayer);

    mOldHealth = ptrCreatureStats.getHealth();
    mOldMagicka = ptrCreatureStats.getMagicka();
    mOldFatigue = ptrCreatureStats.getFatigue();
    mDynamicStatsBaselineValid = true;

    for (int i = 0; i < 3; ++i)
        ptrCreatureStats.getDynamic(i).writeState(creatureStats.mDynamic[i]);

    for (int i = 0; i < 8; ++i)
    {
        ptrNpcStats.getAttribute(i).writeState(creatureStats.mAttributes[i]);
        npcStats.mSkillIncrease[i] = ptrNpcStats.getSkillIncrease(i);
        npcStats.mXpAttributeProgress[i] = ptrNpcStats.getXpAttributeProgress(i);
    }

    for (int i = 0; i < 27; ++i)
        ptrNpcStats.getSkill(i).writeState(npcStats.mSkills[i]);

    creatureStats.mLevel = ptrNpcStats.getLevel();
    npcStats.mLevelProgress = ptrNpcStats.getLevelProgress();
    npcStats.mExperience = ptrNpcStats.getExperience();
    npcStats.mSkillPoints = ptrNpcStats.getSkillPoints();
    npcStats.mXpRewardKeys.assign(ptrNpcStats.getXpRewardKeys().begin(), ptrNpcStats.getXpRewardKeys().end());

    if (ptrPlayer.getClass().hasInventoryStore(ptrPlayer))
    {
        MWWorld::InventoryStore& invStore = ptrPlayer.getClass().getInventoryStore(ptrPlayer);

        for (int slot = 0; slot < MWWorld::InventoryStore::Slots; slot++)
        {
            auto& item = equipmentItems[slot];
            MWWorld::ContainerStoreIterator it = invStore.getSlot(slot);

            if (it != invStore.end())
            {
                item.refId = it->getCellRef().getRefId();
                item.count = it->getRefData().getCount();
                item.charge = it->getCellRef().getCharge();
                item.enchantmentCharge = it->getCellRef().getEnchantmentCharge();
                item.poisonId = it->getRefData().getPoisonId();
                item.poisonCharges = it->getRefData().getPoisonCharges();
            }
            else
            {
                item.refId = "";
                item.count = 0;
                item.charge = -1;
                item.enchantmentCharge = -1;
                item.poisonId.clear();
                item.poisonCharges = 0;
            }
        }
    }
}
/*
    End of AMP addition (Y044)
*/

float LocalPlayer::getDeathRecoveryRemainingSeconds() const
{
    return mDeathRecoveryActive ? std::max(0.f, mDeathRecoveryDuration - mDeathRecoveryElapsed) : 0.f;
}

bool LocalPlayer::isRestoreHealthPotion(const MWWorld::Ptr& item) const
{
    if (item.isEmpty() || item.getTypeName() != typeid(ESM::Potion).name())
        return false;
    const ESM::Potion* potion = item.get<ESM::Potion>()->mBase;
    if (!potion)
        return false;
    for (const ESM::ENAMstruct& effect : potion->mEffects.mList)
    {
        if (effect.mEffectID == ESM::MagicEffect::RestoreHealth
            && (effect.mMagnMin > 0 || effect.mMagnMax > 0))
            return true;
    }
    return false;
}

int LocalPlayer::getRestoreHealthPotionCount(std::string* firstRefId) const
{
    const MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
    if (player.isEmpty())
        return 0;
    int total = 0;
    MWWorld::InventoryStore& inv = player.getClass().getInventoryStore(player);
    for (MWWorld::ContainerStoreIterator it = inv.begin(); it != inv.end(); ++it)
    {
        if (!isRestoreHealthPotion(*it))
            continue;
        if (firstRefId && firstRefId->empty())
            *firstRefId = it->getCellRef().getRefId();
        total += std::max(0, it->getRefData().getCount());
    }
    return total;
}

int LocalPlayer::getRequiredDeathRecoveryPotionCount(int level) const
{
    // Keep the client presentation in lock-step with the default server policy.
    // The server remains authoritative and validates the target's saved level.
    const int safeLevel = std::max(1, level);
    return 1 + safeLevel / 5;
}

int LocalPlayer::getSelfDeathRecoveryPotionRequirement() const
{
    const MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
    if (player.isEmpty())
        return 1;
    return getRequiredDeathRecoveryPotionCount(
        player.getClass().getCreatureStats(player).getLevel());
}

bool LocalPlayer::getRecoverableAllyName(std::string& name, int* level) const
{
    name.clear();
    MWWorld::Ptr faced = MWBase::Environment::get().getWorld()->getFacedObject();
    if (faced.isEmpty() || !PlayerList::isDedicatedPlayer(faced))
        return false;
    DedicatedPlayer* target = PlayerList::getPlayer(faced);
    const MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
    // Y073: any living player can help; the server validates range and state.
    if (!target || player.isEmpty() || player.getClass().getCreatureStats(player).isDead())
        return false;
    if (!faced.getClass().getCreatureStats(faced).isDead())
        return false;
    name = target->npc.mName;
    if (level)
        *level = std::max(1, faced.getClass().getCreatureStats(faced).getLevel());
    return !name.empty();
}

void LocalPlayer::applyDeathRecoveryState(const std::string& state, float seconds)
{
    seconds = std::max(0.f, seconds);

    if (state == "DENY")
    {
        // Died inside the lockout: the server did not open a window. Close any
        // panel that a local optimistic beginDeathRecovery() already opened.
        mReviveLockoutRemaining = seconds;
        mDeathRecoveryActive = false;
        mDeathRecoveryElapsed = 0.f;
        mDeathRecoveryRequestCooldown = 0.f;
        mDeathRecoveryUseQueued = false;
    }
    else if (state == "LOCK")
    {
        // A recovery just succeeded - start the local countdown.
        mReviveLockoutRemaining = seconds;
    }
    else if (state == "OPEN")
    {
        // The server accepted this death as recoverable, so no lockout can be
        // pending. Normally the window is already open from the optimistic
        // local call in die(); this only matters when the local countdown was
        // ahead of the server's and suppressed it.
        mReviveLockoutRemaining = 0.f;
        if (!mDeathRecoveryActive)
            beginDeathRecovery();
        // After beginDeathRecovery(), which resets the duration to its default.
        if (seconds > 0.f)
            mDeathRecoveryDuration = seconds;
    }
}

void LocalPlayer::sendDeathRecoveryControl(const std::string& payload)
{
    chatMessage = "@@AMP_REVIVE@@" + payload;
    getNetworking()->getPlayerPacket(ID_CHAT_MESSAGE)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_CHAT_MESSAGE)->Send();
}

void LocalPlayer::requestTouchRecovery(const MWWorld::Ptr& targetPtr)
{
    if (targetPtr.isEmpty() || !PlayerList::isDedicatedPlayer(targetPtr)
        || !targetPtr.getClass().getCreatureStats(targetPtr).isDead())
        return;
    DedicatedPlayer* target = PlayerList::getPlayer(targetPtr);
    const MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
    if (!target || player.isEmpty() || player.getClass().getCreatureStats(player).isDead()
        || mDeathRecoveryRequestCooldown > 0.f)
        return;
    sendDeathRecoveryControl("TOUCH\t" + target->npc.mName);
    mDeathRecoveryRequestCooldown = 0.75f;
}

void LocalPlayer::notifyDeathRecoveryUsePressed()
{
    // KeyboardManager and the Activate action listener forward press edges from
    // Android touch, keyboard, mouse and controller. A press and release may
    // arrive within a single frame, so consume the queued event in update().
    mDeathRecoveryUseQueued = true;
}

void LocalPlayer::applyRespawnWait(float seconds)
{
    if (!std::isfinite(seconds) || seconds < 0.f) return;
    mRespawnWaitActive = true;
    mRespawnWaitRemaining = mRespawnWaitDuration = seconds;
}

void LocalPlayer::applyBountyDeathState(float remaining, float total)
{
    if (!std::isfinite(remaining) || !std::isfinite(total) || remaining < 0.f || total < 0.f)
        return;
    mBountyDeathActive = total > 0.f;
    mBountyDeathDuration = total;
    mBountyDeathRemaining = std::clamp(remaining, 0.f, total);
    mDeathRecoveryActive = false;
    mDeathRecoveryUseQueued = false;
    if (mBountyDeathActive)
    {
        auto* wm = MWBase::Environment::get().getWindowManager();
        wm->removeGuiMode(MWGui::GM_Barter);
        wm->removeGuiMode(MWGui::GM_Container);
        wm->removeGuiMode(MWGui::GM_Inventory);
    }
}

void LocalPlayer::beginDeathRecovery()
{
    if (mBountyDeathActive) return;
    // U035: while the revive lockout is running there is no incapacitated
    // state at all. Opening the panel and then refusing every request would
    // leave the player pressing Take at a body that cannot be saved, and send
    // party members running to a rescue the server will decline.
    if (mReviveLockoutRemaining > 0.f)
        return;

    const MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
    mDeathRecoveryActive = true;
    // Close existing item/skill windows too; resurrection clears the recovery flag.
    auto* windowManager = MWBase::Environment::get().getWindowManager();
    windowManager->removeGuiMode(MWGui::GM_Barter);
    windowManager->removeGuiMode(MWGui::GM_Container);
    windowManager->removeGuiMode(MWGui::GM_Inventory);
    mDeathRecoveryElapsed = 0.f;
    mDeathRecoveryDuration = 30.f;
    mDeathRecoveryRequestCooldown = 0.f;
    mDeathRecoveryUseQueued = false;
    mDeathRecoveryInitialXp = player.isEmpty() ? 0.f
        : std::max(0.f, player.getClass().getNpcStats(player).getExperience());
}

void LocalPlayer::updateDeathRecovery(float dt)
{
    dt = std::max(0.f, dt);
    if (mRespawnWaitActive) mRespawnWaitRemaining = std::max(0.f, mRespawnWaitRemaining - dt);
    if (mBountyDeathActive)
    {
        mBountyDeathRemaining = std::max(0.f, mBountyDeathRemaining - dt);
        mDeathRecoveryUseQueued = false;
        return; // Only a server CLEAR ends the sentence, never the local zero.
    }
    if (mDeathRecoveryActive)
        mDeathRecoveryElapsed = std::min(mDeathRecoveryDuration, mDeathRecoveryElapsed + dt);
    mDeathRecoveryRequestCooldown = std::max(0.f, mDeathRecoveryRequestCooldown - dt);
    mReviveLockoutRemaining = std::max(0.f, mReviveLockoutRemaining - dt);

    if (mRespawnWaitActive && !mDeathRecoveryActive) {
        mDeathRecoveryUseQueued = false;
        return;
    }

    // Use the actual Activate/Take press edge, not SDL_SCANCODE_E. The latter
    // incorrectly ignores rebinding, mouse and controller input, and Android
    // touch taps may be shorter than a frame. Both input paths coalesce here.
    const bool usePressed = mDeathRecoveryUseQueued;
    mDeathRecoveryUseQueued = false;
    if (!usePressed || mDeathRecoveryRequestCooldown > 0.f)
        return;

    // Both the own and the ally request are refused server-side during the
    // lockout; do not spend a packet on them.
    if (mReviveLockoutRemaining > 0.f)
        return;

    GUIController* gui = Main::get().getGUIController();
    MWBase::WindowManager* wm = MWBase::Environment::get().getWindowManager();
    if ((gui && gui->getChatEditState()) || (wm && wm->isGuiMode()))
        return;

    std::string potionRefId;
    const int potionCount = getRestoreHealthPotionCount(&potionRefId);
    if (potionRefId.empty())
        return;

    if (mDeathRecoveryActive)
    {
        if (potionCount < getSelfDeathRecoveryPotionRequirement())
            return;
        sendDeathRecoveryControl("SELF_POTION\t" + potionRefId);
        mDeathRecoveryRequestCooldown = 0.75f;
        return;
    }

    std::string allyName;
    int allyLevel = 1;
    if (getRecoverableAllyName(allyName, &allyLevel))
    {
        if (potionCount < getRequiredDeathRecoveryPotionCount(allyLevel))
            return;
        sendDeathRecoveryControl("ALLY_POTION\t" + allyName + "\t" + potionRefId);
        mDeathRecoveryRequestCooldown = 0.75f;
    }
}

bool LocalPlayer::processCharGen()
{
    MWBase::WindowManager *windowManager = MWBase::Environment::get().getWindowManager();

    // If we haven't finished CharGen and we're in a menu, it must be
    // one of the CharGen menus, so go no further until it's closed
    if (windowManager->isGuiMode() && !charGenState.isFinished)
    {
        return false;
    }

    // If the current stage of CharGen is not the last one,
    // move to the next one
    else if (charGenState.currentStage < charGenState.endStage)
    {
        switch (charGenState.currentStage)
        {
        case 0:
            windowManager->pushGuiMode(MWGui::GM_Name);
            break;
        case 1:
            windowManager->pushGuiMode(MWGui::GM_Race);
            break;
        case 2:
            windowManager->pushGuiMode(MWGui::GM_Class);
            break;
        case 3:
            windowManager->pushGuiMode(MWGui::GM_Birth);
            break;
        default:
            windowManager->pushGuiMode(MWGui::GM_Review);
            break;
        }
        getNetworking()->getPlayerPacket(ID_PLAYER_CHARGEN)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_CHARGEN)->Send();

        return false;
    }

    // If we've reached the last stage of CharGen, send the
    // corresponding packets and mark CharGen as finished
    else if (!charGenState.isFinished)
    {
        MWBase::World *world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr ptrPlayer = world->getPlayerPtr();
        npc = *ptrPlayer.get<ESM::NPC>()->mBase;
        birthsign = world->getPlayer().getBirthSign();

        updateLanguage();
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending ID_PLAYER_BASEINFO to server with my CharGen info (language %s)", language.c_str());
        getNetworking()->getPlayerPacket(ID_PLAYER_BASEINFO)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_BASEINFO)->Send();

        // Send stats packets if this is the 2nd round of CharGen that
        // only happens for new characters. The modern CharGen also allows a
        // height/scale choice; synchronize it before ID_PLAYER_CHARGEN so the
        // server's presence gate can reveal the player with the final scale.
        if (charGenState.endStage != 1)
        {
            scale = ptrPlayer.getCellRef().getScale();
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
                "Sending ID_PLAYER_SHAPESHIFT with CharGen scale %.3f", scale);
            getNetworking()->getPlayerPacket(ID_PLAYER_SHAPESHIFT)->setPlayer(this);
            getNetworking()->getPlayerPacket(ID_PLAYER_SHAPESHIFT)->Send();

            updateStatsDynamic(true);
            updateAttributes(true);
            updateSkills(true);
            updateLevel(true);
            sendClass();
            sendSpellbook();
            getNetworking()->getPlayerPacket(ID_PLAYER_CHARGEN)->setPlayer(this);
            getNetworking()->getPlayerPacket(ID_PLAYER_CHARGEN)->Send();
        }

        // Mark character generation as finished until overridden by a new ID_PLAYER_CHARGEN packet
        charGenState.isFinished = true;
    }

    return true;
}

bool LocalPlayer::isLoggedIn()
{
    if (charGenState.isFinished && (charGenState.endStage > 1 || receivedCharacter))
        return true;

    return false;
}

void LocalPlayer::updateStatsDynamic(bool forceUpdate)
{
    if (statsDynamicIndexChanges.size() > 0)
        statsDynamicIndexChanges.clear();

    MWWorld::Ptr ptrPlayer = getPlayerPtr();

    MWMechanics::CreatureStats *ptrCreatureStats = &ptrPlayer.getClass().getCreatureStats(ptrPlayer);
    MWMechanics::DynamicStat<float> health(ptrCreatureStats->getHealth());
    MWMechanics::DynamicStat<float> magicka(ptrCreatureStats->getMagicka());
    MWMechanics::DynamicStat<float> fatigue(ptrCreatureStats->getFatigue());

    /*
        Start of AMP change (Y044)

        These were function-local statics, so they kept the values of the previous
        session after a relog inside the same process and were compared against a
        character they no longer belonged to.
    */
    if (!mDynamicStatsBaselineValid)
    {
        mOldHealth = ptrCreatureStats->getHealth();
        mOldMagicka = ptrCreatureStats->getMagicka();
        mOldFatigue = ptrCreatureStats->getFatigue();
        mDynamicStatsBaselineValid = true;
    }

    MWMechanics::DynamicStat<float>& oldHealth = mOldHealth;
    MWMechanics::DynamicStat<float>& oldMagicka = mOldMagicka;
    MWMechanics::DynamicStat<float>& oldFatigue = mOldFatigue;
    /*
        End of AMP change (Y044)
    */


    // Update stats when they become 0 or they have changed enough
    auto needUpdate = [](MWMechanics::DynamicStat<float> &oldVal, MWMechanics::DynamicStat<float> &newVal, int limit) {
        return oldVal != newVal && (newVal.getCurrent() == 0 || oldVal.getCurrent() == 0
                                    || abs(oldVal.getCurrent() - newVal.getCurrent()) >= limit);
    };

    if (forceUpdate || needUpdate(oldHealth, health, 2))
        statsDynamicIndexChanges.push_back(0);

    if (forceUpdate || needUpdate(oldMagicka, magicka, 4))
        statsDynamicIndexChanges.push_back(1);

    if (forceUpdate || needUpdate(oldFatigue, fatigue, 4))
        statsDynamicIndexChanges.push_back(2);

    if (forceUpdate || statsDynamicIndexChanges.size() > 0)
    {
        oldHealth = health;
        oldMagicka = magicka;
        oldFatigue = fatigue;

        health.writeState(creatureStats.mDynamic[0]);
        magicka.writeState(creatureStats.mDynamic[1]);
        fatigue.writeState(creatureStats.mDynamic[2]);

        creatureStats.mDead = ptrCreatureStats->isDead();

        exchangeFullInfo = false;
        getNetworking()->getPlayerPacket(ID_PLAYER_STATS_DYNAMIC)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_STATS_DYNAMIC)->Send();
    }
}

void LocalPlayer::updateAttributes(bool forceUpdate)
{
    // Only send attributes if we are not a werewolf, or they will be
    // overwritten by the werewolf ones
    if (isWerewolf) return;

    if (attributeIndexChanges.size() > 0)
        attributeIndexChanges.clear();

    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    const MWMechanics::NpcStats &ptrNpcStats = ptrPlayer.getClass().getNpcStats(ptrPlayer);

    for (int i = 0; i < 8; ++i)
    {
        if (ptrNpcStats.getAttribute(i).getBase() != creatureStats.mAttributes[i].mBase ||
            ptrNpcStats.getAttribute(i).getModifier() != creatureStats.mAttributes[i].mMod ||
            ptrNpcStats.getAttribute(i).getDamage() != creatureStats.mAttributes[i].mDamage ||
            ptrNpcStats.getSkillIncrease(i) != npcStats.mSkillIncrease[i] ||
            forceUpdate)
        {
            attributeIndexChanges.push_back(i);
            ptrNpcStats.getAttribute(i).writeState(creatureStats.mAttributes[i]);
            npcStats.mSkillIncrease[i] = ptrNpcStats.getSkillIncrease(i);
        }
    }

    if (attributeIndexChanges.size() > 0)
    {
        exchangeFullInfo = false;
        getNetworking()->getPlayerPacket(ID_PLAYER_ATTRIBUTE)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_ATTRIBUTE)->Send();
    }
}

void LocalPlayer::updateSkills(bool forceUpdate)
{
    // Only send skills if we are not a werewolf, or they will be
    // overwritten by the werewolf ones
    if (isWerewolf) return;

    if (skillIndexChanges.size() > 0)
        skillIndexChanges.clear();

    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    const MWMechanics::NpcStats &ptrNpcStats = ptrPlayer.getClass().getNpcStats(ptrPlayer);

    for (int i = 0; i < 27; ++i)
    {
        // Update a skill if its base value has changed at all or its progress has changed enough
        if (ptrNpcStats.getSkill(i).getBase() != npcStats.mSkills[i].mBase ||
            ptrNpcStats.getSkill(i).getModifier() != npcStats.mSkills[i].mMod ||
            ptrNpcStats.getSkill(i).getDamage() != npcStats.mSkills[i].mDamage ||
            abs(ptrNpcStats.getSkill(i).getProgress() - npcStats.mSkills[i].mProgress) > 0.75 ||
            forceUpdate)
        {
            skillIndexChanges.push_back(i);
            ptrNpcStats.getSkill(i).writeState(npcStats.mSkills[i]);
        }
    }

    if (skillIndexChanges.size() > 0)
    {
        exchangeFullInfo = false;
        getNetworking()->getPlayerPacket(ID_PLAYER_SKILL)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_SKILL)->Send();
    }
}

void LocalPlayer::updateLevel(bool forceUpdate)
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    const MWMechanics::NpcStats &ptrNpcStats = ptrPlayer.getClass().getNpcStats(ptrPlayer);

    bool xpChanged = std::fabs(ptrNpcStats.getExperience() - npcStats.mExperience) > 0.001f
        || ptrNpcStats.getSkillPoints() != npcStats.mSkillPoints;

    for (int i = 0; i < 8 && !xpChanged; ++i)
        xpChanged = std::fabs(ptrNpcStats.getXpAttributeProgress(i) - npcStats.mXpAttributeProgress[i]) > 0.001f;

    std::vector<std::string> rewardKeys(ptrNpcStats.getXpRewardKeys().begin(), ptrNpcStats.getXpRewardKeys().end());
    const bool rewardKeysChanged = rewardKeys != npcStats.mXpRewardKeys;
    if (rewardKeysChanged)
        xpChanged = true;

    if (ptrNpcStats.getLevel() != creatureStats.mLevel ||
        ptrNpcStats.getLevelProgress() != npcStats.mLevelProgress || xpChanged ||
        forceUpdate)
    {
        creatureStats.mLevel = ptrNpcStats.getLevel();
        npcStats.mLevelProgress = ptrNpcStats.getLevelProgress();
        npcStats.mXpVersion = 1;
        npcStats.mExperience = ptrNpcStats.getExperience();
        npcStats.mSkillPoints = ptrNpcStats.getSkillPoints();
        for (int i = 0; i < 8; ++i)
            npcStats.mXpAttributeProgress[i] = ptrNpcStats.getXpAttributeProgress(i);
        npcStats.mXpRewardKeys = rewardKeys;
        xpRewardKeysChanged = rewardKeysChanged;

        getNetworking()->getPlayerPacket(ID_PLAYER_LEVEL)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_LEVEL)->Send();
        xpRewardKeysChanged = false;
    }
}

void LocalPlayer::updateBounty(bool forceUpdate)
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    const MWMechanics::NpcStats &ptrNpcStats = ptrPlayer.getClass().getNpcStats(ptrPlayer);

    if (ptrNpcStats.getBounty() != npcStats.mBounty || forceUpdate)
    {
        npcStats.mBounty = ptrNpcStats.getBounty();
        getNetworking()->getPlayerPacket(ID_PLAYER_BOUNTY)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_BOUNTY)->Send();
    }
}

void LocalPlayer::updateReputation(bool forceUpdate)
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    const MWMechanics::NpcStats &ptrNpcStats = ptrPlayer.getClass().getNpcStats(ptrPlayer);

    if (ptrNpcStats.getReputation() != npcStats.mReputation || forceUpdate)
    {
        npcStats.mReputation = ptrNpcStats.getReputation();
        getNetworking()->getPlayerPacket(ID_PLAYER_REPUTATION)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_REPUTATION)->Send();
    }
}

void LocalPlayer::updatePosition(bool forceUpdate)
{
    MWBase::World *world = MWBase::Environment::get().getWorld();
    MWWorld::Ptr ptrPlayer = world->getPlayerPtr();

    static bool posWasChanged = false;
    static bool isJumping = false;
    static bool sentJumpEnd = true;
    static float oldRot[2] = {0};
    static float idleHeartbeatTimer = 0.f;

    position = ptrPlayer.getRefData().getPosition();

    bool posIsChanging = (direction.pos[0] != 0 || direction.pos[1] != 0 ||
        direction.rot[0] != 0 || direction.rot[1] != 0 || direction.rot[2] != 0);

    // Animations can change a player's position without actually creating directional movement,
    // so update positions accordingly
    if (!posIsChanging && isPlayingAnimation)
    {
        if (MWBase::Environment::get().getMechanicsManager()->checkAnimationPlaying(ptrPlayer, animation.groupname))
            posIsChanging = true;
        else
            isPlayingAnimation = false;
    }

    if (!posIsChanging)
        idleHeartbeatTimer += MWBase::Environment::get().getFrameDuration();
    else
        idleHeartbeatTimer = 0.f;

    // Position uses an unreliable sequenced transport in ArenaMP. Keep a small
    // idle heartbeat so a lost movement->stop sample cannot leave another client
    // walking forever. Two packets per second while idle is enough and remains
    // far cheaper than reliable per-frame movement.
    const bool sendIdleHeartbeat = !posIsChanging && idleHeartbeatTimer >= 0.5f;

    if (forceUpdate || posIsChanging || posWasChanged || sendIdleHeartbeat)
    {
        oldRot[0] = position.rot[0];
        oldRot[1] = position.rot[2];

        posWasChanged = posIsChanging;
        idleHeartbeatTimer = 0.f;

        if (!isJumping && !world->isOnGround(ptrPlayer) && !world->isFlying(ptrPlayer))
            isJumping = true;

        getNetworking()->getPlayerPacket(ID_PLAYER_POSITION)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_POSITION)->Send();
    }
    else if (isJumping && world->isOnGround(ptrPlayer))
    {
        isJumping = false;
        sentJumpEnd = false;
    }
    // Packet with jump end position has to be sent one tick after above check
    else if (!sentJumpEnd)
    {
        sentJumpEnd = true;
        position = ptrPlayer.getRefData().getPosition();
        getNetworking()->getPlayerPacket(ID_PLAYER_POSITION)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_POSITION)->Send();
    }
}

void LocalPlayer::updateCell(bool forceUpdate)
{
    const ESM::Cell *ptrCell = MWBase::Environment::get().getWorld()->getPlayerPtr().getCell()->getCell();

    // If the LocalPlayer's Ptr cell is different from the LocalPlayer's packet cell, proceed
    if (forceUpdate || !Main::get().getCellController()->isSameCell(*ptrCell, cell))
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending ID_PLAYER_CELL_CHANGE about LocalPlayer to server");

        LOG_APPEND(TimedLog::LOG_INFO, "- Moved from %s to %s", cell.getShortDescription().c_str(),
                   ptrCell->getShortDescription().c_str());

        // Do not leave the last fraction of a second of script locals queued against the
        // old cell. The packet still carries the old cell here, which is exactly what the
        // server needs in order to validate its authority and store it correctly.
        ScriptController::flushQueuedLocalChanges(0.0f, true);

        if (!Misc::StringUtils::ciEqual(cell.mRegion, ptrCell->mRegion))
        {
            LOG_APPEND(TimedLog::LOG_INFO, "- Changed region from %s to %s",
                cell.mRegion.empty() ? "none" : cell.mRegion.c_str(),
                ptrCell->mRegion.empty() ? "none" : ptrCell->mRegion.c_str());

            isChangingRegion = true;
        }

        MWMechanics::ArenaCombo::local().reset();
        cell = *ptrCell;
        previousCellPosition = position;

        // Make sure the position is updated before a cell packet is sent, or else
        // cell change events in server scripts will have the wrong player position
        updatePosition(true);

        getNetworking()->getPlayerPacket(ID_PLAYER_CELL_CHANGE)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_CELL_CHANGE)->Send();

        // X022: CellChange and AnimFlags share RELIABLE_ORDERED/CHANNEL_PLAYER.
        // Send a fresh animation snapshot immediately afterwards so observers do
        // not keep a pre-door jump/run state until an unrelated flag changes.
        updateAnimFlags(true);

        isChangingRegion = false;

        // If this is an interior cell, are there any other players in it? If so,
        // enable their markers
        if (!ptrCell->isExterior())
        {
            mwmp::PlayerList::enableMarkers(*ptrCell);
        }
    }
}

void LocalPlayer::updateEquipment(bool forceUpdate)
{
    if (equipmentIndexChanges.size() > 0)
        equipmentIndexChanges.clear();

    MWWorld::Ptr ptrPlayer = getPlayerPtr();

    MWWorld::InventoryStore &invStore = ptrPlayer.getClass().getInventoryStore(ptrPlayer);
    for (int slot = 0; slot < MWWorld::InventoryStore::Slots; slot++)
    {
        auto &item = equipmentItems[slot];
        MWWorld::ContainerStoreIterator it = invStore.getSlot(slot);

        if (it != invStore.end())
        {
            MWWorld::CellRef &cellRef = it->getCellRef();

            if (Misc::StringUtils::ciEqual(cellRef.getRefId(), item.refId) == false ||
                cellRef.getCharge() != item.charge ||
                Utils::compareFloats(cellRef.getEnchantmentCharge(), item.enchantmentCharge, 1.0f) == false ||
                it->getRefData().getCount() != item.count ||
                it->getRefData().getPoisonId() != item.poisonId ||
                it->getRefData().getPoisonCharges() != item.poisonCharges ||
                forceUpdate)
            {
                equipmentIndexChanges.push_back(slot);

                item.refId = it->getCellRef().getRefId();
                item.count = it->getRefData().getCount();
                item.charge = it->getCellRef().getCharge();
                item.enchantmentCharge = it->getCellRef().getEnchantmentCharge();
                item.poisonId = it->getRefData().getPoisonId();
                item.poisonCharges = it->getRefData().getPoisonCharges();
            }
        }
        else if (!item.refId.empty())
        {
            equipmentIndexChanges.push_back(slot);
            item.refId = "";
            item.count = 0;
            item.charge = -1;
            item.enchantmentCharge = -1;
            item.poisonId.clear();
            item.poisonCharges = 0;
        }
    }

    if (equipmentIndexChanges.size() > 0)
    {
        exchangeFullInfo = false;
        getNetworking()->getPlayerPacket(ID_PLAYER_EQUIPMENT)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_EQUIPMENT)->Send();
    }
}

void LocalPlayer::updateInventory(bool forceUpdate)
{
    static bool invChanged = false;

    if (forceUpdate)
        invChanged = true;

    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWWorld::InventoryStore &ptrInventory = ptrPlayer.getClass().getInventoryStore(ptrPlayer);
    mwmp::Item item;

    auto setItem = [](Item &item, const MWWorld::Ptr &iter) {
        item.refId = iter.getCellRef().getRefId();
        if (item.refId.find("$dynamic") != std::string::npos)
            return true;
        item.count = iter.getRefData().getCount();
        item.charge = iter.getCellRef().getCharge();
        item.enchantmentCharge = iter.getCellRef().getEnchantmentCharge();
        item.soul = iter.getCellRef().getSoul();
        item.poisonId = iter.getRefData().getPoisonId();
        item.poisonCharges = iter.getRefData().getPoisonCharges();

        return false;
    };

    if (!invChanged)
    {
        for (const auto &itemOld : inventoryChanges.items)
        {
            auto result = ptrInventory.begin();
            for (; result != ptrInventory.end(); ++result)
            {
                if(setItem(item, *result))
                    continue;

                if (item == itemOld)
                    break;
            }
            if (result == ptrInventory.end())
            {
                invChanged = true;
                break;
            }
        }
    }

    if (!invChanged)
    {
        for (const auto &iter : ptrInventory)
        {
            if(setItem(item, iter))
                continue;

            auto items = inventoryChanges.items;

            if (find(items.begin(), items.end(), item) == items.end())
            {
                invChanged = true;
                break;
            }
        }
    }

    if (!invChanged)
        return;

    invChanged = false;

    sendInventory();
}

void LocalPlayer::updateAttackOrCast()
{
    if (attack.shouldSend)
    {
        // Arena Y027: this is the one MP-safe point at which the outgoing hit is
        // guaranteed to belong to the LocalPlayer and already carries the finalized
        // post-resistance/post-armour damage written by Npc/Creature::onHit. Showing
        // feedback here avoids actor-authority ambiguity and prevents remote echoes
        // from producing duplicate floating numbers.
        if (attack.isHit && attack.success && std::isfinite(attack.damage) && attack.damage > 0.f)
            MWBase::Environment::get().getWindowManager()->hudDamageNumber(
                attack.comboFinisher ? attack.comboAppliedDamage : attack.damage, attack.comboFinisher);

        getNetworking()->getPlayerPacket(ID_PLAYER_ATTACK)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_ATTACK)->Send();

        if (attack.type == mwmp::Attack::RANGED)
            MWMechanics::ArenaCombo::local().reset();
        attack.comboFinisher = false;
        attack.comboAppliedDamage = 0.f;
        attack.shouldSend = false;
    }
    else if (cast.shouldSend)
    {
        MWMechanics::ArenaCombo::local().reset();
        getNetworking()->getPlayerPacket(ID_PLAYER_CAST)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_CAST)->Send();

        cast.shouldSend = false;
        cast.hasProjectile = false;
    }
}

void LocalPlayer::updateAnimFlags(bool forceUpdate)
{
    MWBase::World *world = MWBase::Environment::get().getWorld();
    MWWorld::Ptr ptrPlayer = world->getPlayerPtr();

    MWMechanics::NpcStats ptrNpcStats = ptrPlayer.getClass().getNpcStats(ptrPlayer);
    using namespace MWMechanics;

    static bool wasRunning = ptrNpcStats.getMovementFlag(CreatureStats::Flag_Run);
    static bool wasSneaking = ptrNpcStats.getMovementFlag(CreatureStats::Flag_Sneak)
        || ptrNpcStats.getMovementFlag(CreatureStats::Flag_ForceSneak);
    static bool wasForceJumping = ptrNpcStats.getMovementFlag(CreatureStats::Flag_ForceJump);
    static bool wasForceMoveJumping = ptrNpcStats.getMovementFlag(CreatureStats::Flag_ForceMoveJump);

    const bool guarding = ptrNpcStats.getMovementFlag(CreatureStats::Flag_ActiveGuard);
    bool isRunning = ptrNpcStats.getMovementFlag(CreatureStats::Flag_Run);
    bool isSneaking = ptrNpcStats.getMovementFlag(CreatureStats::Flag_Sneak)
        || ptrNpcStats.getMovementFlag(CreatureStats::Flag_ForceSneak);
    bool isForceJumping = ptrNpcStats.getMovementFlag(CreatureStats::Flag_ForceJump);
    bool isForceMoveJumping = ptrNpcStats.getMovementFlag(CreatureStats::Flag_ForceMoveJump);
    
    isFlying = world->isFlying(ptrPlayer);
    isJumping = !world->isOnGround(ptrPlayer) && !isFlying;

    // We need to send a new packet at the end of jumping, flying and TCL-ing too,
    // so keep track of what we were doing last frame
    static bool wasJumping = false;
    static bool wasFlying = false;
    static bool hadTcl = false;

    drawState = ptrPlayer.getClass().getNpcStats(ptrPlayer).getDrawState();
    static char lastDrawState = ptrPlayer.getClass().getNpcStats(ptrPlayer).getDrawState();

    if (((movementFlags & CreatureStats::Flag_ActiveGuard) != 0) != guarding || wasRunning != isRunning ||
        wasSneaking != isSneaking || wasForceJumping != isForceJumping ||
        wasForceMoveJumping != isForceMoveJumping || lastDrawState != drawState ||
        wasJumping || isJumping || wasFlying != isFlying || hadTcl != hasTcl ||
        forceUpdate)
    {
        wasSneaking = isSneaking;
        wasRunning = isRunning;
        wasForceJumping = isForceJumping;
        wasForceMoveJumping = isForceMoveJumping;
        lastDrawState = drawState;
        
        wasJumping = isJumping;
        wasFlying = isFlying;
        hadTcl = hasTcl;

        movementFlags = 0;

#define __SETFLAG(flag, value) (value) ? (movementFlags | flag) : (movementFlags & ~flag)

        movementFlags = __SETFLAG(CreatureStats::Flag_ActiveGuard, guarding);
        movementFlags = __SETFLAG(CreatureStats::Flag_Sneak, isSneaking);
        movementFlags = __SETFLAG(CreatureStats::Flag_Run, isRunning);
        // Arena Y013: do not overwrite ForceJump with the second assignment.
        // A current jump or explicit force-jump input must keep the same bit set.
        movementFlags = __SETFLAG(CreatureStats::Flag_ForceJump, isForceJumping || isJumping);
        movementFlags = __SETFLAG(CreatureStats::Flag_ForceMoveJump, isForceMoveJumping);

#undef __SETFLAG

        if (isJumping)
            updatePosition(true); // fix position after jump;

        getNetworking()->getPlayerPacket(ID_PLAYER_ANIM_FLAGS)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_ANIM_FLAGS)->Send();
    }
}

void LocalPlayer::addItems()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    const MWWorld::ESMStore &esmStore = MWBase::Environment::get().getWorld()->getStore();
    MWWorld::ContainerStore &ptrStore = ptrPlayer.getClass().getContainerStore(ptrPlayer);

    for (const auto &item : inventoryChanges.items)
    {
        // Skip bound items
        if (MWBase::Environment::get().getMechanicsManager()->isBoundItem(item.refId))
            continue;

        try
        {
            MWWorld::ManualRef itemRef(esmStore, item.refId, item.count);
            MWWorld::Ptr itemPtr = itemRef.getPtr();

            if (item.charge != -1)
                itemPtr.getCellRef().setCharge(item.charge);

            if (item.enchantmentCharge != -1)
                itemPtr.getCellRef().setEnchantmentCharge(item.enchantmentCharge);

            if (!item.soul.empty())
                itemPtr.getCellRef().setSoul(item.soul);
            itemPtr.getRefData().setPoison(item.poisonId, item.poisonCharges);

            LOG_APPEND(TimedLog::LOG_INFO, "- Adding inventory item %s with count %i", item.refId.c_str(), item.count);

            const int previousCount = ptrStore.count(item.refId);
            ptrStore.add(itemPtr, item.count, ptrPlayer);
            // Server quest giveItem/gold actions bypass dialogue script opcodes.
            // A full login/resync SET also calls addItems; it is not a reward.
            if (inventoryChanges.action == InventoryChanges::ADD)
                notifyDialogueInventoryChange(itemPtr.getClass().getName(itemPtr),
                    ptrStore.count(item.refId) - previousCount, true);
        }
        catch (std::exception&)
        {
            LOG_APPEND(TimedLog::LOG_INFO, "- Ignored addition of invalid inventory item %s", item.refId.c_str());
        }
    }

    updateInventoryWindow();
}

void LocalPlayer::addSpells()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWMechanics::Spells &ptrSpells = ptrPlayer.getClass().getCreatureStats(ptrPlayer).getSpells();

    for (const auto &spell : spellbookChanges.spells)
        // Only add spells that are ensured to exist
        if (MWBase::Environment::get().getWorld()->getStore().get<ESM::Spell>().search(spell.mId))
            ptrSpells.add(spell.mId);
        else
            LOG_APPEND(TimedLog::LOG_INFO, "- Ignored addition of invalid spell %s", spell.mId.c_str());
}

void LocalPlayer::addSpellsActive()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWMechanics::ActiveSpells& activeSpells = ptrPlayer.getClass().getCreatureStats(ptrPlayer).getActiveSpells();

    for (const auto& activeSpell : spellsActiveChanges.activeSpells)
    {
        MWWorld::TimeStamp timestamp = MWWorld::TimeStamp(activeSpell.timestampHour, activeSpell.timestampDay);
        int casterActorId = MechanicsHelper::getActorId(activeSpell.caster);

        // Don't do a check for a spell's existence, because active effects from potions need to be applied here too
        activeSpells.addSpell(activeSpell.id, activeSpell.isStackingSpell, activeSpell.params.mEffects, activeSpell.params.mDisplayName, casterActorId, timestamp, false);
    }
}

void LocalPlayer::addJournalItems()
{
    for (const auto &journalItem : journalChanges)
    {
        MWWorld::Ptr ptrFound;

        if (journalItem.type == JournalItem::ENTRY)
        {
            LOG_APPEND(TimedLog::LOG_VERBOSE, "- type: ENTRY, quest: %s, index: %i, actorRefId: %s",
                journalItem.quest.c_str(), journalItem.index, journalItem.actorRefId.c_str());

            ptrFound = MWBase::Environment::get().getWorld()->searchPtr(journalItem.actorRefId, false);

            if (!ptrFound)
                ptrFound = getPlayerPtr();
        }
        else
        {
            LOG_APPEND(TimedLog::LOG_VERBOSE, "- type: INDEX, quest: %s, index: %i",
                journalItem.quest.c_str(), journalItem.index);
        }

        try
        {
            if (journalItem.type == JournalItem::ENTRY)
            {
                if (journalItem.hasTimestamp)
                {
                    MWBase::Environment::get().getJournal()->addEntry(journalItem.quest, journalItem.index, ptrFound,
                        journalItem.timestamp.daysPassed, journalItem.timestamp.month, journalItem.timestamp.day);
                }
                else
                {
                    MWBase::Environment::get().getJournal()->addEntry(journalItem.quest, journalItem.index, ptrFound);
                }
            }
            else
                MWBase::Environment::get().getJournal()->setJournalIndex(journalItem.quest, journalItem.index);
        }
        catch (std::exception&)
        {
            LOG_APPEND(TimedLog::LOG_INFO, "- Ignored addition of invalid journal quest %s", journalItem.quest.c_str());
        }
    }
}

void LocalPlayer::addTopics()
{
    auto &env = MWBase::Environment::get();
    for (const auto &topic : topicChanges)
    {
        std::string topicId = topic.topicId;

        // If we're using a translated version of Morrowind, translate this topic from English into our language
        if (env.getWindowManager()->getTranslationDataStorage().hasTranslation())
            topicId = env.getWindowManager()->getTranslationDataStorage().getLocalizedTopicId(topicId);

        env.getDialogueManager()->addTopic(topicId);

        // C24: topicChanges is the persistent server-side list of known topics.
        // DialogueManager restoration alone is not enough for the journal: its
        // hyperlink index is built from Journal::topicBegin()/topicEnd(). Recreate
        // an empty history node so links remain clickable after reconnect.
        if (MWDialogue::Journal* journal = dynamic_cast<MWDialogue::Journal*>(env.getJournal()))
            journal->ensureKnownTopic(topicId);

        if (env.getWindowManager()->containsMode(MWGui::GM_Dialogue))
            env.getDialogueManager()->updateActorKnownTopics();
    }
}

void LocalPlayer::removeItems()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWWorld::ContainerStore &ptrStore = ptrPlayer.getClass().getContainerStore(ptrPlayer);

    for (const auto &item : inventoryChanges.items)
    {
        std::string itemName = item.refId;
        for (const auto entry : ptrStore)
            if (Misc::StringUtils::ciEqual(entry.getCellRef().getRefId(), item.refId))
            {
                itemName = entry.getClass().getName(entry);
                break;
            }
        const int removed = ptrStore.remove(item.refId, item.count, ptrPlayer);
        notifyDialogueInventoryChange(itemName, removed, false);

        LOG_APPEND(TimedLog::LOG_INFO, "- Removing inventory item %s with count %i", item.refId.c_str(), item.count);
    }
}

void LocalPlayer::removeSpells()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWMechanics::Spells &ptrSpells = ptrPlayer.getClass().getCreatureStats(ptrPlayer).getSpells();

    MWBase::WindowManager *wm = MWBase::Environment::get().getWindowManager();
    for (const auto &spell : spellbookChanges.spells)
    {
        ptrSpells.remove(spell.mId);
        if (spell.mId == wm->getSelectedSpell())
            wm->unsetSelectedSpell();
    }
}

void LocalPlayer::removeSpellsActive()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWMechanics::ActiveSpells& activeSpells = ptrPlayer.getClass().getCreatureStats(ptrPlayer).getActiveSpells();
 
    for (const auto& activeSpell : spellsActiveChanges.activeSpells)
    {
        LOG_APPEND(TimedLog::LOG_INFO, "- removing %sstacking active spell %s", activeSpell.isStackingSpell ? "" : "non-", activeSpell.id.c_str());

        // Remove stacking spells based on their timestamps
        if (activeSpell.isStackingSpell)
        {
            MWWorld::TimeStamp timestamp = MWWorld::TimeStamp(activeSpell.timestampHour, activeSpell.timestampDay);
            bool foundSpell = activeSpells.removeSpellByTimestamp(activeSpell.id, timestamp);

            if (!foundSpell)
            {
                LOG_APPEND(TimedLog::LOG_INFO, "-- spell with this ID and timestamp could not be found!");
            }
        }
        else
        {
            activeSpells.removeEffects(activeSpell.id);
        }
    }
}

void LocalPlayer::die()
{
    if (!mDeathRecoveryActive)
        beginDeathRecovery();
    creatureStats.mDead = true;

    MWWorld::Ptr playerPtr = MWBase::Environment::get().getWorld()->getPlayerPtr();
    MWMechanics::DynamicStat<float> health = playerPtr.getClass().getCreatureStats(playerPtr).getHealth();
    health.setCurrent(0);
    playerPtr.getClass().getCreatureStats(playerPtr).setHealth(health);

    Main::get().getNetworking()->getPlayerPacket(ID_PLAYER_DEATH)->setPlayer(this);
    Main::get().getNetworking()->getPlayerPacket(ID_PLAYER_DEATH)->Send();
}

void LocalPlayer::resurrect()
{
    mRespawnWaitActive = false;
    mRespawnWaitRemaining = mRespawnWaitDuration = 0.f;
    mDeathRecoveryActive = false;
    mDeathRecoveryElapsed = 0.f;
    mDeathRecoveryRequestCooldown = 0.f;
    mDeathRecoveryUseQueued = false;
    creatureStats.mDead = false;

    MWWorld::Ptr ptrPlayer = getPlayerPtr();

    if (resurrectType == mwmp::RESURRECT_TYPE::IMPERIAL_SHRINE)
        MWBase::Environment::get().getWorld()->teleportToClosestMarker(ptrPlayer, "divinemarker");
    else if (resurrectType == mwmp::RESURRECT_TYPE::TRIBUNAL_TEMPLE)
        MWBase::Environment::get().getWorld()->teleportToClosestMarker(ptrPlayer, "templemarker");

    MWBase::Environment::get().getMechanicsManager()->resurrect(ptrPlayer);

    // The player could have died from a hand-to-hand attack, so reset their fatigue
    // as well
    if (creatureStats.mDynamic[2].mMod < 1)
        creatureStats.mDynamic[2].mMod = 1;

    creatureStats.mDynamic[2].mCurrent = creatureStats.mDynamic[2].mMod;
    MWMechanics::DynamicStat<float> fatigue;
    fatigue.readState(creatureStats.mDynamic[2]);
    ptrPlayer.getClass().getCreatureStats(ptrPlayer).setFatigue(fatigue);

    // If this player had a weapon or spell readied when dying, they will still have it
    // readied but be unable to use it unless we clear it here
    ptrPlayer.getClass().getNpcStats(ptrPlayer).setDrawState(MWMechanics::DrawState_Nothing);

    // Record that the player has died since the last attempt was made to arrest them,
    // used to make guards lenient enough to attempt an arrest again
    diedSinceArrestAttempt = true;

    deathTime = time(0);

    LOG_APPEND(TimedLog::LOG_INFO, "- diedSinceArrestAttempt is now true");

    // Record that we are no longer a known werewolf, to avoid being attacked infinitely
    MWBase::Environment::get().getWorld()->setGlobalInt("pcknownwerewolf", 0);

    // Ensure we unequip any items with constant effects that can put us into an infinite
    // death loop
    static const int damageEffects[5] = { ESM::MagicEffect::DrainHealth, ESM::MagicEffect::FireDamage,
        ESM::MagicEffect::FrostDamage, ESM::MagicEffect::ShockDamage, ESM::MagicEffect::SunDamage };

    for (const auto &damageEffect : damageEffects)
        MechanicsHelper::unequipItemsByEffect(ptrPlayer, ESM::Enchantment::ConstantEffect, damageEffect);

    Main::get().getNetworking()->getPlayerPacket(ID_PLAYER_RESURRECT)->setPlayer(this);
    Main::get().getNetworking()->getPlayerPacket(ID_PLAYER_RESURRECT)->Send();

    updateStatsDynamic(true);
}

void LocalPlayer::closeInventoryWindows()
{
    if (MWBase::Environment::get().getWindowManager()->containsMode(MWGui::GM_Container) ||
        MWBase::Environment::get().getWindowManager()->containsMode(MWGui::GM_Inventory))
        MWBase::Environment::get().getWindowManager()->popGuiMode();

    MWBase::Environment::get().getWindowManager()->finishDragDrop();
}

void LocalPlayer::updateInventoryWindow()
{
    MWBase::Environment::get().getWindowManager()->getInventoryWindow()->updateItemView();
}

void LocalPlayer::setCharacter()
{
    receivedCharacter = true;

    MWBase::World *world = MWBase::Environment::get().getWorld();

    // Ignore invalid races
    if (world->getStore().get<ESM::Race>().search(npc.mRace) != 0)
    {
        MWBase::Environment::get().getWorld()->getPlayer().setBirthSign(birthsign);

        if (resetStats)
        {
            MWBase::Environment::get().getMechanicsManager()->setPlayerRace(npc.mRace, npc.isMale(), npc.mHead, npc.mHair);
            setEquipment();
        }
        else
        {
            ESM::NPC player = *world->getPlayerPtr().get<ESM::NPC>()->mBase;

            player.mRace = npc.mRace;
            player.mHead = npc.mHead;
            player.mHair = npc.mHair;
            player.mModel = npc.mModel;
            player.setIsMale(npc.isMale());
            world->createRecord(player);

            MWBase::Environment::get().getMechanicsManager()->playerLoaded();

            // This is needed to update the player's model instantly if they're in 3rd person
            world->reattachPlayerCamera();
        }

        MWBase::Environment::get().getWindowManager()->getInventoryWindow()->rebuildAvatar();
    }
    else
    {
        LOG_APPEND(TimedLog::LOG_INFO, "- Character update was ignored due to invalid race %s", npc.mRace.c_str());
    }
}

void LocalPlayer::setDynamicStats()
{
    MWBase::World *world = MWBase::Environment::get().getWorld();
    MWWorld::Ptr ptrPlayer = world->getPlayerPtr();

    MWMechanics::CreatureStats *ptrCreatureStats = &ptrPlayer.getClass().getCreatureStats(ptrPlayer);
    MWMechanics::DynamicStat<float> dynamicStat;

    for (int i = 0; i < 3; ++i)
    {
        dynamicStat = ptrCreatureStats->getDynamic(i);
        dynamicStat.setBase(creatureStats.mDynamic[i].mBase);
        dynamicStat.setCurrent(creatureStats.mDynamic[i].mCurrent);
        ptrCreatureStats->setDynamic(i, dynamicStat);
    }

    /*
        Start of AMP change (Y056)

        The stored snapshot is not authoritative for stats the engine derives. Correct
        them here, then let Y044's baseline logic run so it takes the corrected numbers,
        then publish anything that had to be fixed so the bad values stop being handed
        back out on the next login.
    */
    const bool corrected = applyEngineDerivedDynamicStats();

    // Y044: an authoritative profile section has arrived.
    markLoginSyncReceived(LoginSync_StatsDynamic);

    if (corrected)
        updateStatsDynamic(true);
    /*
        End of AMP change (Y056)
    */
}

/*
    Start of AMP addition (Y056)
*/
namespace
{
    // Character creation hands out the race's attribute values for the player's sex,
    // plus 10 for each of the class's two favoured attributes. Those are the exact
    // numbers maximum health started from, and unlike the per-level history they can be
    // recovered at any time from records the client already has loaded.
    bool getCharacterCreationAttributes(const MWWorld::Ptr &ptrPlayer, float &strength, float &endurance)
    {
        const ESM::NPC *record = ptrPlayer.get<ESM::NPC>()->mBase;

        if (record == nullptr)
            return false;

        const MWWorld::ESMStore &store = MWBase::Environment::get().getWorld()->getStore();
        const ESM::Race *race = store.get<ESM::Race>().search(record->mRace);

        if (race == nullptr)
            return false;

        const bool male = record->isMale();

        strength = static_cast<float>(race->mData.mAttributeValues[ESM::Attribute::Strength].getValue(male));
        endurance = static_cast<float>(race->mData.mAttributeValues[ESM::Attribute::Endurance].getValue(male));

        const ESM::Class *characterClass = store.get<ESM::Class>().search(record->mClass);

        if (characterClass != nullptr)
        {
            for (int i = 0; i < 2; ++i)
            {
                if (characterClass->mData.mAttribute[i] == ESM::Attribute::Strength)
                    strength += 10.f;
                else if (characterClass->mData.mAttribute[i] == ESM::Attribute::Endurance)
                    endurance += 10.f;
            }
        }

        return true;
    }
}

bool LocalPlayer::applyEngineDerivedDynamicStats()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();

    if (ptrPlayer.isEmpty() || !ptrPlayer.getClass().isNpc())
        return false;

    MWMechanics::NpcStats *ptrNpcStats = &ptrPlayer.getClass().getNpcStats(ptrPlayer);

    const float oldFatigueBase = ptrNpcStats->getFatigue().getBase();

    // Maximum fatigue is Strength + Willpower + Agility + Endurance, always.
    ptrNpcStats->recalcFatigueBase();

    const bool fatigueChanged = ptrNpcStats->getFatigue().getBase() != oldFatigueBase;

    if (fatigueChanged)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
            "Stored base fatigue %.1f did not match the derived value %.1f; using the derived one",
            oldFatigueBase, ptrNpcStats->getFatigue().getBase());
    }

    // Maximum magicka is Intelligence times the magicka multiplier, always. Ask the next
    // mechanics update to redo it rather than duplicating the GMST lookup here; that also
    // lets any Fortify Maximum Magicka effect land first.
    ptrNpcStats->setNeedRecalcDynamicStats(true);

    const bool healthRepaired = repairBaseHealth(ptrPlayer);

    return fatigueChanged || healthRepaired;
}

// Maximum health cannot be read back: it accumulates across level-ups, and every
// increase used the Endurance the character had at that moment, which is recorded
// nowhere. It can, however, be reconstructed retrospectively.
//
// Two figures are built from the character's own history:
//
//   floor       - what the character would have if Endurance had never risen above the
//                 value character creation handed out. Endurance only ever grows, so
//                 nothing legitimate can sit below this. It is the corruption test.
//
//   retrospect  - the same walk over the character's levels, but with Endurance rising
//                 in even steps from the starting racial figure to the one the character
//                 has now. That is the repair target, so both Endurance and level take
//                 part rather than a single flat multiplier.
//
// For L level-ups with Endurance going E0 -> E1 in even steps, the level-ups contribute
// fLevelUpHealthEndMult * (L * E0 + (E1 - E0) * (L + 1) / 2).
//
// That accrual is then scaled by sHealthRebuildGenerosity. Even growth is the most
// pessimistic assumption that still fits the character's history - most characters put
// points into Endurance early, where they compound over more levels - so the plain figure
// tends to land short of what was really lost. The multiplier applies to the rebuilt
// accrual only, never to the character creation base, which is known exactly.
bool LocalPlayer::repairBaseHealth(const MWWorld::Ptr &ptrPlayer)
{
    // How much of the unrecoverable level-up history to hand back. 1.0 is the strict
    // even-growth reconstruction; anything above it compensates for Endurance usually
    // having been raised earlier than evenly.
    static const float sHealthRebuildGenerosity = 1.4f;

    MWMechanics::NpcStats *ptrNpcStats = &ptrPlayer.getClass().getNpcStats(ptrPlayer);

    // Werewolf form carries its own base health, stored separately by the server and
    // unrelated to what character creation handed the human shape. Nothing here applies
    // to it, and its Endurance would badly distort the rebuild.
    if (ptrNpcStats->isWerewolf())
        return false;

    float chargenStrength = 0.f;
    float chargenEndurance = 0.f;

    if (!getCharacterCreationAttributes(ptrPlayer, chargenStrength, chargenEndurance))
        return false;

    const float chargenHealth = std::floor(0.5f * (chargenStrength + chargenEndurance));

    if (chargenHealth <= 0.f)
        return false;

    const MWWorld::Store<ESM::GameSetting> &gmst =
        MWBase::Environment::get().getWorld()->getStore().get<ESM::GameSetting>();

    const float perLevel = gmst.find("fLevelUpHealthEndMult")->mValue.getFloat();
    const float levelUps = static_cast<float>(std::max(0, ptrNpcStats->getLevel() - 1));

    // A permanently drained Endurance can sit below the starting one, so never walk the
    // history upwards past what the character can currently show for itself.
    const float currentEndurance = ptrNpcStats->getAttribute(ESM::Attribute::Endurance).getBase();
    const float startEndurance = std::min(chargenEndurance, currentEndurance);
    const float endEndurance = std::max(startEndurance, currentEndurance);

    const float floorHealth = chargenHealth + perLevel * levelUps * startEndurance;

    const float retrospectAccrual = perLevel *
        (levelUps * startEndurance + (endEndurance - startEndurance) * (levelUps + 1.f) * 0.5f);

    const float retrospectHealth = chargenHealth + retrospectAccrual * sHealthRebuildGenerosity;

    MWMechanics::DynamicStat<float> health(ptrNpcStats->getHealth());

    if (health.getBase() >= floorHealth)
        return false;

    float ratio = 1.f;
    if (health.getBase() > 0.f)
        ratio = std::min(1.f, std::max(0.f, health.getCurrent() / health.getBase()));

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
        "Stored base health %.1f is below the floor %.1f at level %d "
        "(character creation gave %.0f, Endurance %.0f -> %.0f, accrual %.1f x %.2f); "
        "rebuilding retrospectively to %.1f",
        health.getBase(), floorHealth, ptrNpcStats->getLevel(), chargenHealth,
        startEndurance, endEndurance, retrospectAccrual, sHealthRebuildGenerosity,
        retrospectHealth);

    health.setBase(retrospectHealth);
    health.setCurrent(std::max(1.f, retrospectHealth * ratio));
    ptrNpcStats->setHealth(health);

    return true;
}
/*
    End of AMP addition (Y056)
*/

void LocalPlayer::setAttributes()
{
    // Y044: an authoritative profile section has arrived.
    markLoginSyncReceived(LoginSync_Attributes);

    MWWorld::Ptr ptrPlayer = getPlayerPtr();

    MWMechanics::NpcStats *ptrNpcStats = &ptrPlayer.getClass().getNpcStats(ptrPlayer);
    MWMechanics::AttributeValue attributeValue;

    for (int attributeIndex = 0; attributeIndex < 8; ++attributeIndex)
    {
        // If the server wants to clear our attribute's non-zero modifier, we need to remove
        // the spell effect causing it, to avoid an infinite loop where the effect keeps resetting
        // the modifier
        if (creatureStats.mAttributes[attributeIndex].mMod == 0 && ptrNpcStats->getAttribute(attributeIndex).getModifier() > 0)
        {
            ptrNpcStats->getActiveSpells().purgeEffectByArg(ESM::MagicEffect::FortifyAttribute, attributeIndex);
            MWBase::Environment::get().getMechanicsManager()->updateMagicEffects(ptrPlayer);

            // Is the modifier for this attribute still higher than 0? If so, unequip items that
            // fortify the attribute
            if (ptrNpcStats->getAttribute(attributeIndex).getModifier() > 0)
            {
                MechanicsHelper::unequipItemsByEffect(ptrPlayer, ESM::Enchantment::ConstantEffect, ESM::MagicEffect::FortifyAttribute, attributeIndex, -1);
                mwmp::Main::get().getGUIController()->refreshGuiMode(MWGui::GM_Inventory);
            }
        }

        attributeValue.readState(creatureStats.mAttributes[attributeIndex]);
        ptrNpcStats->setAttribute(attributeIndex, attributeValue);

        ptrNpcStats->setSkillIncrease(attributeIndex, npcStats.mSkillIncrease[attributeIndex]);
    }
}

void LocalPlayer::setSkills()
{
    // Y044: an authoritative profile section has arrived.
    markLoginSyncReceived(LoginSync_Skills);

    MWWorld::Ptr ptrPlayer = getPlayerPtr();

    MWMechanics::NpcStats *ptrNpcStats = &ptrPlayer.getClass().getNpcStats(ptrPlayer);
    MWMechanics::SkillValue skillValue;

    for (int skillIndex = 0; skillIndex < 27; ++skillIndex)
    {
        // If the server wants to clear our skill's non-zero modifier, we need to remove
        // the spell effect causing it, to avoid an infinite loop where the effect keeps resetting
        // the modifier
        if (npcStats.mSkills[skillIndex].mMod == 0 && ptrNpcStats->getSkill(skillIndex).getModifier() > 0)
        {
            ptrNpcStats->getActiveSpells().purgeEffectByArg(ESM::MagicEffect::FortifySkill, skillIndex);
            MWBase::Environment::get().getMechanicsManager()->updateMagicEffects(ptrPlayer);

            // Is the modifier for this skill still higher than 0? If so, unequip items that
            // fortify the skill
            if (ptrNpcStats->getSkill(skillIndex).getModifier() > 0)
            {
                MechanicsHelper::unequipItemsByEffect(ptrPlayer, ESM::Enchantment::ConstantEffect, ESM::MagicEffect::FortifySkill, -1, skillIndex);
                mwmp::Main::get().getGUIController()->refreshGuiMode(MWGui::GM_Inventory);
            }
        }

        skillValue.readState(npcStats.mSkills[skillIndex]);
        ptrNpcStats->setSkill(skillIndex, skillValue);
    }
}

void LocalPlayer::setLevel()
{
    // Y044: an authoritative profile section has arrived.
    markLoginSyncReceived(LoginSync_Level);

    MWBase::World *world = MWBase::Environment::get().getWorld();
    MWWorld::Ptr ptrPlayer = world->getPlayerPtr();

    MWMechanics::NpcStats *ptrNpcStats = &ptrPlayer.getClass().getNpcStats(ptrPlayer);
    ptrNpcStats->setLevel(creatureStats.mLevel);
    ptrNpcStats->setLevelProgress(npcStats.mLevelProgress);
    ptrNpcStats->setExperience(npcStats.mExperience);
    ptrNpcStats->setSkillPoints(npcStats.mSkillPoints);
    for (int i = 0; i < 8; ++i)
        ptrNpcStats->setXpAttributeProgress(i, npcStats.mXpAttributeProgress[i]);
    if (xpRewardKeysChanged)
        ptrNpcStats->setXpRewardKeys(npcStats.mXpRewardKeys);
    xpRewardKeysChanged = false;
}

void LocalPlayer::setBounty()
{
    MWBase::World *world = MWBase::Environment::get().getWorld();
    MWWorld::Ptr ptrPlayer = world->getPlayerPtr();

    MWMechanics::NpcStats *ptrNpcStats = &ptrPlayer.getClass().getNpcStats(ptrPlayer);
    ptrNpcStats->setBounty(npcStats.mBounty);
}

void LocalPlayer::setReputation()
{
    MWBase::World *world = MWBase::Environment::get().getWorld();
    MWWorld::Ptr ptrPlayer = world->getPlayerPtr();

    MWMechanics::NpcStats *ptrNpcStats = &ptrPlayer.getClass().getNpcStats(ptrPlayer);
    ptrNpcStats->setReputation(npcStats.mReputation);
}

void LocalPlayer::setPosition()
{
    MWBase::World *world = MWBase::Environment::get().getWorld();
    MWWorld::Ptr ptrPlayer = world->getPlayerPtr();

    // If we're ignoring this position packet because of an invalid cell change,
    // don't make the next one get ignored as well
    if (ignorePosPacket)
        ignorePosPacket = false;
    else
    {
        world->getPlayer().setTeleported(true);

        world->moveObject(ptrPlayer, position.pos[0], position.pos[1], position.pos[2]);
        world->rotateObject(ptrPlayer, position.rot[0], position.rot[1], position.rot[2]);
        world->setInertialForce(ptrPlayer, osg::Vec3f(0.f, 0.f, 0.f));
    }

    updatePosition(true);

    // Make sure we update our draw state, or we'll end up with the wrong one
    updateAnimFlags(true);
}

void LocalPlayer::setMomentum()
{
    MWBase::World *world = MWBase::Environment::get().getWorld();
    MWWorld::Ptr ptrPlayer = world->getPlayerPtr();
    world->setInertialForce(ptrPlayer, momentum.asVec3());
}

void LocalPlayer::setCell()
{
    MWBase::World *world = MWBase::Environment::get().getWorld();
    MWWorld::Ptr ptrPlayer = world->getPlayerPtr();
    ESM::Position pos;

    // To avoid crashes, close container windows this player may be in
    closeInventoryWindows();

    world->getPlayer().setTeleported(true);

    int x = cell.mData.mX;
    int y = cell.mData.mY;

    if (cell.isExterior())
    {
        world->indexToPosition(x, y, pos.pos[0], pos.pos[1], true);
        pos.pos[2] = 0;

        pos.rot[0] = pos.rot[1] = pos.rot[2] = 0;

        world->changeToExteriorCell(pos, true);
        world->fixPosition();
    }
    else if (world->findExteriorPosition(cell.mName, pos))
    {
        world->changeToExteriorCell(pos, true);
        world->fixPosition();
    }
    else
    {
        try
        {
            world->findInteriorPosition(cell.mName, pos);
            world->changeToInteriorCell(cell.mName, pos, true);
        }
        // If we've been sent to an invalid interior, ignore the incoming
        // packet about our position in that cell
        catch (std::exception&)
        {
            LOG_APPEND(TimedLog::LOG_INFO, "%s", "- Cell doesn't exist on this client");
            ignorePosPacket = true;
        }
    }

    updateCell(true);
}

void LocalPlayer::setClass()
{
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received ID_PLAYER_CLASS from server");

    if (charClass.mId.empty()) // custom class
    {
        charClass.mData.mIsPlayable = 0x1;
        MWBase::Environment::get().getMechanicsManager()->setPlayerClass(charClass);
    }
    else
    {
        const ESM::Class *existingCharClass = MWBase::Environment::get().getWorld()->getStore().get<ESM::Class>().search(charClass.mId);

        if (existingCharClass)
        {
            MWBase::Environment::get().getMechanicsManager()->setPlayerClass(charClass.mId);
        }
        else
            LOG_APPEND(TimedLog::LOG_INFO, "- Ignored invalid default class %s", charClass.mId.c_str());
    }
}

void LocalPlayer::setEquipment()
{
    // Y044: an authoritative profile section has arrived.
    markLoginSyncReceived(LoginSync_Equipment);

    MWWorld::Ptr ptrPlayer = getPlayerPtr();

    MWWorld::InventoryStore &ptrInventory = ptrPlayer.getClass().getInventoryStore(ptrPlayer);

    for (int slot = 0; slot < MWWorld::InventoryStore::Slots; slot++)
    {
        mwmp::Item &currentItem = equipmentItems[slot];

        if (!currentItem.refId.empty())
        {
            auto it = find_if(ptrInventory.begin(), ptrInventory.end(), [&currentItem](const MWWorld::Ptr &itemPtr) {
                const bool chargeMatches = !itemPtr.getClass().hasItemHealth(itemPtr) || (currentItem.charge < 0
                    ? (itemPtr.getCellRef().getCharge() < 0
                        || itemPtr.getClass().getItemHealth(itemPtr) == itemPtr.getClass().getItemMaxHealth(itemPtr))
                    : itemPtr.getClass().getItemHealth(itemPtr) == currentItem.charge);
                return Misc::StringUtils::ciEqual(itemPtr.getCellRef().getRefId(), currentItem.refId)
                    && chargeMatches
                    && (currentItem.enchantmentCharge < 0
                        ? itemPtr.getCellRef().getEnchantmentCharge() < 0
                        : std::abs(itemPtr.getCellRef().getEnchantmentCharge() - currentItem.enchantmentCharge) < 0.01f)
                    && itemPtr.getRefData().getPoisonId() == currentItem.poisonId
                    && itemPtr.getRefData().getPoisonCharges() == currentItem.poisonCharges;
            });

            // If the item is not in our inventory, add it as long as it's not a bound item
            if (it == ptrInventory.end())
            {
                if (!MWBase::Environment::get().getMechanicsManager()->isBoundItem(currentItem.refId))
                {
                    try
                    {
                        // A stale equipment packet must not manufacture a worn copy
                        // when another state of this refId is already present.
                        const auto existing = ptrInventory.search(currentItem.refId);
                        if (!existing.isEmpty()) continue;
                        MWWorld::ManualRef ref(MWBase::Environment::get().getWorld()->getStore(),
                            currentItem.refId, currentItem.count);
                        auto restored = ref.getPtr();
                        restored.getCellRef().setCharge(currentItem.charge);
                        restored.getCellRef().setEnchantmentCharge(currentItem.enchantmentCharge);
                        restored.getRefData().setPoison(currentItem.poisonId, currentItem.poisonCharges);
                        auto addIter = ptrInventory.ContainerStore::add(restored, currentItem.count, ptrPlayer);

                        ptrInventory.equip(slot, addIter, ptrPlayer);
                    }
                    catch (std::exception&)
                    {
                        LOG_APPEND(TimedLog::LOG_INFO, "- Ignored addition of invalid equipment item %s", currentItem.refId.c_str());
                    }
                }
            }
            else
            {
                // Don't try to equip an item that is already equipped
                if (ptrInventory.getSlot(slot) != it)
                    ptrInventory.equip(slot, it, ptrPlayer);
            }
        }
        else
            ptrInventory.unequipSlot(slot, ptrPlayer);
    }

    MWBase::Environment::get().getWindowManager()->getInventoryWindow()->updatePlayer();
}

void LocalPlayer::setInventory()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWWorld::ContainerStore &ptrStore = ptrPlayer.getClass().getContainerStore(ptrPlayer);

    // Ensure no item is being drag and dropped
    MWBase::Environment::get().getWindowManager()->finishDragDrop();

    // Y107: a full SET invalidates item Ptrs held by an open crafting/barter UI.
    auto* wm=MWBase::Environment::get().getWindowManager();
    if (!CraftEconomic::get().busy())wm->removeGuiMode(MWGui::GM_Enchanting);
    const bool rebuildBarter=wm->getTradeWindow()->prepareInventoryReplace(ptrPlayer);
    // Clear items in inventory
    ptrStore.clear();

    // Proceed by adding items
    addItems();
    wm->getTradeWindow()->finishInventoryReplace(rebuildBarter);

    // Don't automatically setEquipment() here, or the player could end
    // up getting a new set of their starting clothes, or other items
    // supposed to no longer exist
    //
    // Instead, expect server scripts to do that manually
}

void LocalPlayer::setSpellbook()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWMechanics::Spells &ptrSpells = ptrPlayer.getClass().getCreatureStats(ptrPlayer).getSpells();

    // Clear spells in spellbook, while ignoring abilities, powers, etc.
    while (true)
    {
        MWMechanics::Spells::TIterator iter = ptrSpells.begin();
        for (; iter != ptrSpells.end(); iter++)
        {
            const ESM::Spell *spell = iter->first;
            if (spell->mData.mType == ESM::Spell::ST_Spell)
            {
                ptrSpells.remove(spell->mId);
                break;
            }
        }
        if (iter == ptrSpells.end())
            break;
    }

    // Proceed by adding spells
    addSpells();
}

void LocalPlayer::setSpellsActive()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWMechanics::ActiveSpells& activeSpells = ptrPlayer.getClass().getCreatureStats(ptrPlayer).getActiveSpells();
    activeSpells.clear();

    // Proceed by adding spells active
    addSpellsActive();
}

void LocalPlayer::setCooldowns()
{
    MWBase::World* world = MWBase::Environment::get().getWorld();
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWMechanics::Spells& ptrSpells = ptrPlayer.getClass().getCreatureStats(ptrPlayer).getSpells();

    for (const auto& cooldown : cooldownChanges)
    {
        if (world->getStore().get<ESM::Spell>().search(cooldown.id))
        {
            const ESM::Spell* spell = world->getStore().get<ESM::Spell>().search(cooldown.id);

            ptrSpells.setPowerUseTimestamp(spell, cooldown.startTimestampDay, cooldown.startTimestampHour);
        }
    }
}

void LocalPlayer::setQuickKeys()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received ID_PLAYER_QUICKKEYS from server");

    for (const auto &quickKey : quickKeyChanges)
    {
        LOG_APPEND(TimedLog::LOG_INFO, "- slot: %i, type: %i, itemId: %s", quickKey.slot, quickKey.type, quickKey.itemId.c_str());

        if (quickKey.type == QuickKey::ITEM || quickKey.type == QuickKey::ITEM_MAGIC)
        {
            MWWorld::InventoryStore &ptrInventory = ptrPlayer.getClass().getInventoryStore(ptrPlayer);

            auto it = find_if(ptrInventory.begin(), ptrInventory.end(), [&quickKey](const MWWorld::Ptr &inventoryItem) {
                return Misc::StringUtils::ciEqual(inventoryItem.getCellRef().getRefId(), quickKey.itemId);
            });

            if (it != ptrInventory.end())
                MWBase::Environment::get().getWindowManager()->setQuickKey(quickKey.slot, quickKey.type, (*it));
        }
        else if (quickKey.type == QuickKey::MAGIC)
        {
            MWMechanics::Spells &ptrSpells = ptrPlayer.getClass().getCreatureStats(ptrPlayer).getSpells();
            bool hasSpell = false;

            MWMechanics::Spells::TIterator iter = ptrSpells.begin();
            for (; iter != ptrSpells.end(); iter++)
            {
                const ESM::Spell *spell = iter->first;
                if (Misc::StringUtils::ciEqual(spell->mId, quickKey.itemId))
                {
                    hasSpell = true;
                    break;
                }
            }

            if (hasSpell)
                MWBase::Environment::get().getWindowManager()->setQuickKey(quickKey.slot, quickKey.type, 0, quickKey.itemId);
        }
        else
            MWBase::Environment::get().getWindowManager()->setQuickKey(quickKey.slot, quickKey.type, 0);
    }
}

void LocalPlayer::setFactions()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWMechanics::NpcStats &ptrNpcStats = ptrPlayer.getClass().getNpcStats(ptrPlayer);

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received ID_PLAYER_FACTION from server - action: %i", factionChanges.action);

    for (const auto &faction : factionChanges.factions)
    {
        LOG_APPEND(TimedLog::LOG_VERBOSE, " - processing faction: %s", faction.factionId.c_str());
        const ESM::Faction *esmFaction = MWBase::Environment::get().getWorld()->getStore().get<ESM::Faction>().search(faction.factionId);

        if (!esmFaction)
        {
            LOG_APPEND(TimedLog::LOG_INFO, "- Ignored invalid faction %s", faction.factionId.c_str());
            continue;
        }

        if (factionChanges.action == mwmp::FactionChanges::RANK)
        {

            if (!ptrNpcStats.isInFaction(faction.factionId))
            {
                // If the player isn't in this faction, make them join it
                ptrNpcStats.joinFaction(faction.factionId);
                LOG_APPEND(TimedLog::LOG_VERBOSE, "\t>JOINED FACTION: %s on rank change to: %d.",
                    faction.factionId.c_str(), faction.rank);
            }

            // While the faction rank is different in the packet than in the NpcStats,
            // adjust the NpcStats accordingly
            while (faction.rank != ptrNpcStats.getFactionRanks().at(faction.factionId))
            {
                if (faction.rank > ptrNpcStats.getFactionRanks().at(faction.factionId))
                    ptrNpcStats.raiseRank(faction.factionId);
                else
                    ptrNpcStats.lowerRank(faction.factionId);
            }
        }
        else if (factionChanges.action == mwmp::FactionChanges::EXPULSION)
        {
            // If the expelled state is different in the packet than in the NpcStats,
            // adjust the NpcStats accordingly
            if (faction.isExpelled != ptrNpcStats.getExpelled(faction.factionId))
            {
                if (faction.isExpelled)
                    ptrNpcStats.expell(faction.factionId);
                else
                    ptrNpcStats.clearExpelled(faction.factionId);
            }
        }

        else if (factionChanges.action == mwmp::FactionChanges::REPUTATION)
            ptrNpcStats.setFactionReputation(faction.factionId, faction.reputation);
    }
}

void LocalPlayer::setBooks()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWMechanics::NpcStats &ptrNpcStats = ptrPlayer.getClass().getNpcStats(ptrPlayer);
    const MWWorld::Store<ESM::Book>& books =
        MWBase::Environment::get().getWorld()->getStore().get<ESM::Book>();

    for (const auto &book : bookChanges)
    {
        // Bookworm migration: older XP reward keys stored a lowercase book id.
        // Resolve it through the ESM store and keep the canonical record id in
        // NpcStats so hasBeenUsed() also works for mixed-case TR/Tamriel_Data ids.
        const ESM::Book* record = books.search(book.bookId);
        ptrNpcStats.flagAsUsed(record ? record->mId : book.bookId);
    }
}

void LocalPlayer::setShapeshift()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();

    // Match the X022 character-creation and server limits for old profiles too.
    scale = std::max(0.85f, std::min(1.08f, scale));
    MWBase::Environment::get().getWorld()->scaleObject(ptrPlayer, scale);
    MWBase::Environment::get().getMechanicsManager()->setWerewolf(ptrPlayer, isWerewolf);
}

void LocalPlayer::setMarkLocation()
{
    MWWorld::CellStore *ptrCellStore = Main::get().getCellController()->getCellStore(markCell);

    if (ptrCellStore)
        MWBase::Environment::get().getWorld()->getPlayer().markPosition(ptrCellStore, markPosition);
}

void LocalPlayer::setSelectedSpell()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();

    MWMechanics::CreatureStats& stats = ptrPlayer.getClass().getCreatureStats(ptrPlayer);
    MWMechanics::Spells& spells = stats.getSpells();

    if (!spells.hasSpell(selectedSpellId))
        return;
 
    MWBase::Environment::get().getWindowManager()->setSelectedSpell(selectedSpellId,
        int(MWMechanics::getSpellSuccessChance(selectedSpellId, ptrPlayer)));
}

void LocalPlayer::sendDeath(char newDeathState)
{
    if (newDeathState != 0 && !mDeathRecoveryActive)
        beginDeathRecovery();

    if (MechanicsHelper::isEmptyTarget(killer))
        killer = MechanicsHelper::getTarget(getPlayerPtr());

    deathState = newDeathState;

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending ID_PLAYER_DEATH about myself to server\n- deathState: %d", deathState);
    getNetworking()->getPlayerPacket(ID_PLAYER_DEATH)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_DEATH)->Send();

    MechanicsHelper::clearTarget(killer);
}

void LocalPlayer::sendClass()
{
    MWBase::World *world = MWBase::Environment::get().getWorld();
    const ESM::NPC *npcBase = world->getPlayerPtr().get<ESM::NPC>()->mBase;
    const ESM::Class *esmClass = world->getStore().get<ESM::Class>().find(npcBase->mClass);

    if (npcBase->mClass.find("$dynamic") != std::string::npos) // custom class
    {
        charClass.mId = "";
        charClass.mName = esmClass->mName;
        charClass.mDescription = esmClass->mDescription;
        charClass.mData = esmClass->mData;
    }
    else
        charClass.mId = esmClass->mId;

    getNetworking()->getPlayerPacket(ID_PLAYER_CHARCLASS)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_CHARCLASS)->Send();
}

void LocalPlayer::sendInventory()
{
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending entire inventory to server");

    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWWorld::InventoryStore &ptrInventory = ptrPlayer.getClass().getInventoryStore(ptrPlayer);
    mwmp::Item item;

    inventoryChanges.items.clear();

    for (const auto &iter : ptrInventory)
    {
        item.refId = iter.getCellRef().getRefId();

        // Skip any items that somehow have clientside-only dynamic IDs
        if (item.refId.find("$dynamic") != std::string::npos)
            continue;

        // Skip bound items
        if (MWBase::Environment::get().getMechanicsManager()->isBoundItem(item.refId))
            continue;

        item.count = iter.getRefData().getCount();
        item.charge = iter.getCellRef().getCharge();
        item.enchantmentCharge = iter.getCellRef().getEnchantmentCharge();
        item.soul = iter.getCellRef().getSoul();
        item.poisonId = iter.getRefData().getPoisonId();
        item.poisonCharges = iter.getRefData().getPoisonCharges();

        inventoryChanges.items.push_back(item);
    }

    inventoryChanges.action = InventoryChanges::SET;
    getNetworking()->getPlayerPacket(ID_PLAYER_INVENTORY)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_INVENTORY)->Send();
}

void LocalPlayer::sendItemChange(const mwmp::Item& item, unsigned int action)
{
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending item change for %s with action %i, count %i",
        item.refId.c_str(), action, item.count);

    inventoryChanges.items.clear();
    inventoryChanges.items.push_back(item);
    inventoryChanges.action = action;

    getNetworking()->getPlayerPacket(ID_PLAYER_INVENTORY)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_INVENTORY)->Send();
}

void LocalPlayer::sendItemChange(const MWWorld::Ptr& itemPtr, int count, unsigned int action)
{
    mwmp::Item item = MechanicsHelper::getItem(itemPtr, count);
    sendItemChange(item, action);
}

void LocalPlayer::sendItemChange(const std::string& refId, int count, unsigned int action)
{
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending item change for %s with action %i, count %i",
        refId.c_str(), action, count);

    inventoryChanges.items.clear();
    
    mwmp::Item item;
    item.refId = refId;
    item.count = count;
    item.charge = -1;
    item.enchantmentCharge = -1;
    item.soul = "";

    inventoryChanges.items.push_back(item);

    inventoryChanges.action = action;
    getNetworking()->getPlayerPacket(ID_PLAYER_INVENTORY)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_INVENTORY)->Send();
}

void LocalPlayer::sendStoredItemRemovals()
{
    inventoryChanges.items.clear();

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending stored item removals for LocalPlayer:");

    for (auto storedItemRemoval : storedItemRemovals)
    {
        mwmp::Item item;
        item.refId = storedItemRemoval.first;
        item.count = storedItemRemoval.second;
        item.charge = -1;
        item.enchantmentCharge = -1;
        item.soul = "";
        inventoryChanges.items.push_back(item);

        LOG_APPEND(TimedLog::LOG_INFO, "- %s with count %i", item.refId.c_str(), item.count);
    }

    inventoryChanges.action = mwmp::InventoryChanges::ACTION_TYPE::REMOVE;
    getNetworking()->getPlayerPacket(ID_PLAYER_INVENTORY)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_INVENTORY)->Send();

    storedItemRemovals.clear();
}

void LocalPlayer::sendSpellbook()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWMechanics::Spells &ptrSpells = ptrPlayer.getClass().getCreatureStats(ptrPlayer).getSpells();

    spellbookChanges.spells.clear();

    // Send spells in spellbook, while ignoring abilities, powers, etc.
    for (const auto &spell : ptrSpells)
    {
        if (spell.first->mData.mType == ESM::Spell::ST_Spell)
            spellbookChanges.spells.push_back(*spell.first);
    }

    spellbookChanges.action = SpellbookChanges::SET;
    getNetworking()->getPlayerPacket(ID_PLAYER_SPELLBOOK)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_SPELLBOOK)->Send();
}

void LocalPlayer::sendSpellChange(std::string id, unsigned int action)
{
    // Skip any bugged spells that somehow have clientside-only dynamic IDs
    if (id.find("$dynamic") != std::string::npos)
        return;

    spellbookChanges.spells.clear();

    ESM::Spell spell;
    spell.mId = id;
    spellbookChanges.spells.push_back(spell);

    spellbookChanges.action = action;
    getNetworking()->getPlayerPacket(ID_PLAYER_SPELLBOOK)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_SPELLBOOK)->Send();
}

void LocalPlayer::sendSpellsActive()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWMechanics::ActiveSpells& activeSpells = ptrPlayer.getClass().getCreatureStats(ptrPlayer).getActiveSpells();

    spellsActiveChanges.activeSpells.clear();

    // Send spells in spellbook, while ignoring abilities, powers, etc.
    for (const auto& ptrSpell : activeSpells)
    {
        mwmp::ActiveSpell packetSpell;
        packetSpell.id = ptrSpell.first;
        packetSpell.params.mDisplayName = ptrSpell.second.mDisplayName;
        packetSpell.params.mEffects = ptrSpell.second.mEffects;
        spellsActiveChanges.activeSpells.push_back(packetSpell);
    }

    spellsActiveChanges.action = mwmp::SpellsActiveChanges::SET;
    getNetworking()->getPlayerPacket(ID_PLAYER_SPELLS_ACTIVE)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_SPELLS_ACTIVE)->Send();
}

void LocalPlayer::sendSpellsActiveAddition(const std::string id, bool isStackingSpell, const MWMechanics::ActiveSpells::ActiveSpellParams& params)
{
    // Skip any bugged spells that somehow have clientside-only dynamic IDs
    if (id.find("$dynamic") != std::string::npos)
        return;

    spellsActiveChanges.activeSpells.clear();


    MWWorld::Ptr caster = MWBase::Environment::get().getWorld()->searchPtrViaActorId(params.mCasterActorId);

    mwmp::ActiveSpell spell;
    spell.id = id;
    spell.isStackingSpell = isStackingSpell;
    spell.caster = MechanicsHelper::getTarget(caster);
    spell.timestampDay = params.mTimeStamp.getDay();
    spell.timestampHour = params.mTimeStamp.getHour();
    spell.params.mEffects = params.mEffects;
    spell.params.mDisplayName = params.mDisplayName;
    spellsActiveChanges.activeSpells.push_back(spell);

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending active spell addition with stacking %s, timestamp %i %f",
        spell.isStackingSpell ? "true" : "false", spell.timestampDay, spell.timestampHour);

    spellsActiveChanges.action = mwmp::SpellsActiveChanges::ADD;
    getNetworking()->getPlayerPacket(ID_PLAYER_SPELLS_ACTIVE)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_SPELLS_ACTIVE)->Send();
}

void LocalPlayer::sendSpellsActiveRemoval(const std::string id, bool isStackingSpell, MWWorld::TimeStamp timestamp)
{
    // Skip any bugged spells that somehow have clientside-only dynamic IDs
    if (id.find("$dynamic") != std::string::npos)
        return;

    spellsActiveChanges.activeSpells.clear();

    mwmp::ActiveSpell spell;
    spell.id = id;
    spell.isStackingSpell = isStackingSpell;
    spell.timestampDay = timestamp.getDay();
    spell.timestampHour = timestamp.getHour();
    spellsActiveChanges.activeSpells.push_back(spell);

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending active spell removal with stacking %s, timestamp %i %f",
        spell.isStackingSpell ? "true" : "false", spell.timestampDay, spell.timestampHour);

    spellsActiveChanges.action = mwmp::SpellsActiveChanges::REMOVE;
    getNetworking()->getPlayerPacket(ID_PLAYER_SPELLS_ACTIVE)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_SPELLS_ACTIVE)->Send();
}

void LocalPlayer::sendCooldownChange(std::string id, int startTimestampDay, float startTimestampHour)
{
    // Skip any bugged spells that somehow have clientside-only dynamic IDs
    if (id.find("$dynamic") != std::string::npos)
        return;

    cooldownChanges.clear();

    SpellCooldown spellCooldown;
    spellCooldown.id = id;
    spellCooldown.startTimestampDay = startTimestampDay;
    spellCooldown.startTimestampHour = startTimestampHour;

    cooldownChanges.push_back(spellCooldown);
;
    getNetworking()->getPlayerPacket(ID_PLAYER_COOLDOWNS)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_COOLDOWNS)->Send();
}

void LocalPlayer::sendQuickKey(unsigned short slot, int type, const std::string& itemId)
{
    quickKeyChanges.clear();

    mwmp::QuickKey quickKey;
    quickKey.slot = slot;
    quickKey.type = type;
    quickKey.itemId = itemId;

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending ID_PLAYER_QUICKKEYS", itemId.c_str());
    LOG_APPEND(TimedLog::LOG_INFO, "- slot: %i, type: %i, itemId: %s", quickKey.slot, quickKey.type, quickKey.itemId.c_str());

    quickKeyChanges.push_back(quickKey);

    getNetworking()->getPlayerPacket(ID_PLAYER_QUICKKEYS)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_QUICKKEYS)->Send();
}

void LocalPlayer::sendJournalEntry(const std::string& quest, int index, const MWWorld::Ptr& actor)
{
    journalChanges.clear();

    mwmp::JournalItem journalItem;
    journalItem.type = JournalItem::ENTRY;
    journalItem.quest = quest;
    journalItem.index = index;
    journalItem.actorRefId = actor.getCellRef().getRefId();
    journalItem.hasTimestamp = false;

    journalChanges.push_back(journalItem);

    getNetworking()->getPlayerPacket(ID_PLAYER_JOURNAL)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_JOURNAL)->Send();
}

void LocalPlayer::sendJournalIndex(const std::string& quest, int index)
{
    journalChanges.clear();

    mwmp::JournalItem journalItem;
    journalItem.type = JournalItem::INDEX;
    journalItem.quest = quest;
    journalItem.index = index;

    journalChanges.push_back(journalItem);

    getNetworking()->getPlayerPacket(ID_PLAYER_JOURNAL)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_JOURNAL)->Send();
}

void LocalPlayer::sendFactionRank(const std::string& factionId, int rank)
{
    factionChanges.factions.clear();
    factionChanges.action = FactionChanges::RANK;

    mwmp::Faction faction;
    faction.factionId = factionId;
    faction.rank = rank;

    factionChanges.factions.push_back(faction);

    getNetworking()->getPlayerPacket(ID_PLAYER_FACTION)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_FACTION)->Send();
}

void LocalPlayer::sendFactionExpulsionState(const std::string& factionId, bool isExpelled)
{
    factionChanges.factions.clear();
    factionChanges.action = FactionChanges::EXPULSION;

    mwmp::Faction faction;
    faction.factionId = factionId;
    faction.isExpelled = isExpelled;

    factionChanges.factions.push_back(faction);

    getNetworking()->getPlayerPacket(ID_PLAYER_FACTION)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_FACTION)->Send();
}

void LocalPlayer::sendFactionReputation(const std::string& factionId, int reputation)
{
    factionChanges.factions.clear();
    factionChanges.action = FactionChanges::REPUTATION;

    mwmp::Faction faction;
    faction.factionId = factionId;
    faction.reputation = reputation;

    factionChanges.factions.push_back(faction);

    getNetworking()->getPlayerPacket(ID_PLAYER_FACTION)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_FACTION)->Send();
}

void LocalPlayer::sendTopic(const std::string& topicId)
{
    topicChanges.clear();

    mwmp::Topic topic;

    // For translated versions of the game, make sure we translate the topic back into English first
    if (MWBase::Environment::get().getWindowManager()->getTranslationDataStorage().hasTranslation())
        topic.topicId = MWBase::Environment::get().getWindowManager()->getTranslationDataStorage().topicID(topicId);
    else
        topic.topicId = topicId;

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending ID_PLAYER_TOPIC with topic %s", topic.topicId.c_str());

    topicChanges.push_back(topic);

    getNetworking()->getPlayerPacket(ID_PLAYER_TOPIC)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_TOPIC)->Send();
}

void LocalPlayer::sendBook(const std::string& bookId)
{
    bookChanges.clear();

    mwmp::Book book;
    book.bookId = bookId;

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending ID_PLAYER_BOOK with book %s", book.bookId.c_str());

    bookChanges.push_back(book);

    getNetworking()->getPlayerPacket(ID_PLAYER_BOOK)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_BOOK)->Send();
}

void LocalPlayer::sendWerewolfState(bool werewolfState)
{
    isWerewolf = werewolfState;

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending ID_PLAYER_SHAPESHIFT with isWerewolf of %s", isWerewolf ? "true" : "false");

    getNetworking()->getPlayerPacket(ID_PLAYER_SHAPESHIFT)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_SHAPESHIFT)->Send();
}

void LocalPlayer::sendMarkLocation(const ESM::Cell& newMarkCell, const ESM::Position& newMarkPosition)
{
    miscellaneousChangeType = mwmp::MISCELLANEOUS_CHANGE_TYPE::MARK_LOCATION;
    markCell = newMarkCell;
    markPosition = newMarkPosition;

    getNetworking()->getPlayerPacket(ID_PLAYER_MISCELLANEOUS)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_MISCELLANEOUS)->Send();
}

void LocalPlayer::sendSelectedSpell(const std::string& newSelectedSpellId)
{
    miscellaneousChangeType = mwmp::MISCELLANEOUS_CHANGE_TYPE::SELECTED_SPELL;
    selectedSpellId = newSelectedSpellId;

    getNetworking()->getPlayerPacket(ID_PLAYER_MISCELLANEOUS)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_MISCELLANEOUS)->Send();
}

void LocalPlayer::sendItemUse(const MWWorld::Ptr& itemPtr, bool itemMagicState, char currentDrawState)
{
    usedItem.refId = itemPtr.getCellRef().getRefId();
    usedItem.count = itemPtr.getRefData().getCount();
    usedItem.charge = itemPtr.getCellRef().getCharge();
    usedItem.enchantmentCharge = itemPtr.getCellRef().getEnchantmentCharge();
    usedItem.soul = itemPtr.getCellRef().getSoul();
    usedItem.poisonId = itemPtr.getRefData().getPoisonId();
    usedItem.poisonCharges = itemPtr.getRefData().getPoisonCharges();

    usingItemMagic = itemMagicState;
    itemUseDrawState = currentDrawState;

    getNetworking()->getPlayerPacket(ID_PLAYER_ITEM_USE)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_ITEM_USE)->Send();
}

void LocalPlayer::sendCellStates()
{
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending ID_PLAYER_CELL_STATE to server");
    getNetworking()->getPlayerPacket(ID_PLAYER_CELL_STATE)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_CELL_STATE)->Send();
}

void LocalPlayer::clearCellStates()
{
    cellStateChanges.clear();
}

void LocalPlayer::clearCurrentContainer()
{
    currentContainer.refId = "";
    currentContainer.refNum = 0;
    currentContainer.mpNum = 0;
}

void LocalPlayer::storeCellState(const ESM::Cell& storedCell, int stateType)
{
    std::vector<CellState>::iterator iter;

    for (iter = cellStateChanges.begin(); iter != cellStateChanges.end(); )
    {
        // If there's already a cell state recorded for this particular cell,
        // remove it
        if (storedCell.getShortDescription() == (*iter).cell.getShortDescription())
            iter = cellStateChanges.erase(iter);
        else
            ++iter;
    }

    CellState cellState;
    cellState.cell = storedCell;
    cellState.type = stateType;

    cellStateChanges.push_back(cellState);
}

void LocalPlayer::storeCurrentContainer(const MWWorld::Ptr &container)
{
    currentContainer.refId = container.getCellRef().getRefId();
    currentContainer.refNum = container.getCellRef().getRefNum().mIndex;
    currentContainer.mpNum = container.getCellRef().getMpNum();
}

void LocalPlayer::storeItemRemoval(const std::string& refId, int count)
{
    storedItemRemovals[refId] = storedItemRemovals[refId] + count;
}

void LocalPlayer::storeLastEnchantmentQuantity(unsigned int quantity)
{
    lastEnchantmentQuantity = quantity;
}

void LocalPlayer::sendConsumableAnimation(const std::string& refId)
{
    if (!isLoggedIn())
        return;
    const std::string encoded = encodeConsumableAnimation(refId);
    if (encoded.empty())
        return;
    animation.groupname = encoded;
    animation.mode = 0;
    animation.count = 1;
    animation.persist = false;
    getNetworking()->getPlayerPacket(ID_PLAYER_ANIM_PLAY)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_ANIM_PLAY)->Send();
}

void LocalPlayer::playAnimation()
{
    std::string consumableRefId;
    if (decodeConsumableAnimation(animation.groupname, consumableRefId))
    {
        mwmp::playConsumableAnimation(getPlayerPtr(), consumableRefId);
        return;
    }

    InteractionAnimationData interactionData;
    if (decodeInteractionAnimation(animation.groupname, interactionData))
    {
        if (interactionData.stop)
        {
            cancelInteractionAnimation(false);
            return;
        }

        // Y065: a heartbeat must refresh state, never visibly restart the same
        // clip. This also keeps held reading props alive after AOI changes.
        if (mInteractionAnimationActive
            && sameInteractionAnimation(mInteractionAnimation, interactionData))
        {
            ensureInteractionAnimationProp(getPlayerPtr(), mInteractionAnimation);
            mInteractionAnimationTime = std::max(mInteractionAnimationTime, interactionData.duration);
            return;
        }

        if (mPersistentAnimationPlaying)
            stopPlayerPose(getPlayerPtr(), mPersistentAnimationGroup);
        mPersistentAnimationPlaying = false;
        mPersistentAnimationAppliedMask = 0;
        cancelInteractionAnimation(false);
        mInteractionAnimation = interactionData;
        mInteractionAnimationActive = mwmp::playInteractionAnimation(getPlayerPtr(), mInteractionAnimation);
        mInteractionAnimationTime = mInteractionAnimationActive ? interactionData.duration : 0.f;
        return;
    }

    std::string walkStyle;
    if (decodeWalkAnimationStyle(animation.groupname, walkStyle))
    {
        // Y065: periodic walk-style refreshes are state heartbeats, not replay
        // commands. Avoid resetting locomotion when the value did not change.
        if (walkStyle == mWalkAnimationStyle)
            return;
        mWalkAnimationStyle = walkStyle;
        mwmp::setWalkAnimationStyle(getPlayerPtr(), mWalkAnimationStyle);
        return;
    }

    std::string poseGroup;
    int poseMask = 0;
    float poseSpeed = 1.f;
    if (decodePlayerPose(animation.groupname, poseGroup, poseMask, poseSpeed))
    {
        const bool samePose = mPersistentAnimationActive == !poseGroup.empty()
            && mPersistentAnimationGroup == poseGroup
            && mPersistentAnimationBlendMask == poseMask
            && std::abs(mPersistentAnimationSpeed - poseSpeed) < 0.001f;
        if (samePose)
        {
            // Renderer/controller recreation can stop a pose even though network
            // state is unchanged; updatePersistentAnimation() heals that without
            // rewinding a pose that is still playing.
            updatePersistentAnimation(0.f);
            return;
        }

        stopPlayerPose(getPlayerPtr(), mPersistentAnimationGroup);
        mPersistentAnimationGroup = poseGroup;
        mPersistentAnimationBlendMask = poseMask;
        mPersistentAnimationSpeed = poseSpeed;
        mPersistentAnimationActive = !poseGroup.empty();
        mPersistentAnimationPlaying = false;
        mPersistentAnimationAppliedMask = 0;
        updatePersistentAnimation(0.f);
        return;
    }

    MWBase::Environment::get().getMechanicsManager()->playAnimationGroup(getPlayerPtr(),
        animation.groupname, animation.mode, animation.count, animation.persist);

    isPlayingAnimation = true;
}

void LocalPlayer::sendPersistentAnimationState()
{
    if (!isLoggedIn())
        return;

    animation.groupname = encodePlayerPose(
        mPersistentAnimationActive ? mPersistentAnimationGroup : std::string(),
        mPersistentAnimationActive ? mPersistentAnimationBlendMask : 0,
        mPersistentAnimationSpeed);
    animation.mode = 0;
    animation.count = 1;
    animation.persist = false;

    getNetworking()->getPlayerPacket(ID_PLAYER_ANIM_PLAY)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_ANIM_PLAY)->Send();
}

void LocalPlayer::setPersistentAnimation(const std::string& group, int blendMask, float speed)
{
    stopPlayerPose(getPlayerPtr(), mPersistentAnimationGroup);
    mPersistentAnimationGroup = group;
    mPersistentAnimationBlendMask = blendMask;
    mPersistentAnimationSpeed = speed;
    mPersistentAnimationActive = !group.empty();
    mPersistentAnimationPlaying = false;
    mPersistentAnimationAppliedMask = 0;
    mPersistentAnimationSyncTimer = sAnimationStateHeartbeatSeconds;
    updatePersistentAnimation(0.f);
    sendPersistentAnimationState();
}

void LocalPlayer::clearPersistentAnimation()
{
    stopPlayerPose(getPlayerPtr(), mPersistentAnimationGroup);
    mPersistentAnimationActive = false;
    mPersistentAnimationPlaying = false;
    mPersistentAnimationAppliedMask = 0;
    mPersistentAnimationGroup.clear();
    mPersistentAnimationSyncTimer = 0.f;
    sendPersistentAnimationState();
}

void LocalPlayer::sendWalkAnimationState()
{
    if (!isLoggedIn())
        return;

    animation.groupname = encodeWalkAnimationStyle(mWalkAnimationStyle);
    animation.mode = 0;
    animation.count = 1;
    animation.persist = false;

    getNetworking()->getPlayerPacket(ID_PLAYER_ANIM_PLAY)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_ANIM_PLAY)->Send();
}

void LocalPlayer::setWalkAnimationStyle(const std::string& group)
{
    if (!isValidWalkAnimationStyle(group))
        return;

    mWalkAnimationStyle = group;
    mwmp::setWalkAnimationStyle(getPlayerPtr(), mWalkAnimationStyle);
    mWalkAnimationSyncTimer = sAnimationStateHeartbeatSeconds;
    sendWalkAnimationState();
}

void LocalPlayer::updateWalkAnimationSync(float dt)
{
    if (!isLoggedIn())
        return;

    mWalkAnimationSyncTimer -= std::max(0.f, dt);
    if (mWalkAnimationSyncTimer <= 0.f)
    {
        sendWalkAnimationState();
        mWalkAnimationSyncTimer = sAnimationStateHeartbeatSeconds;
    }
}

void LocalPlayer::updatePersistentAnimation(float dt)
{
    // Y073: leaving third person unequips the pose, including its network state.
    // Do this before the interaction early-out so an overlay cannot defer it.
    if (mPersistentAnimationActive && MWBase::Environment::get().getWorld()->isFirstPerson())
    {
        clearPersistentAnimation();
        return;
    }
    if (mInteractionAnimationActive)
    {
        if (mPersistentAnimationPlaying)
            stopPlayerPose(getPlayerPtr(), mPersistentAnimationGroup);
        mPersistentAnimationPlaying = false;
        mPersistentAnimationAppliedMask = 0;
        return;
    }

    if (!mPersistentAnimationActive)
        return;

    mPersistentAnimationSyncTimer -= dt;
    if (mPersistentAnimationSyncTimer <= 0.f)
    {
        sendPersistentAnimationState();
        mPersistentAnimationSyncTimer = sAnimationStateHeartbeatSeconds;
    }

    const MWWorld::Ptr ptr = getPlayerPtr();
    MWRender::Animation* renderer = MWBase::Environment::get().getWorld()->getAnimation(ptr);
    if (!renderer)
        return;

    if (playerPoseIsBlocked(ptr, isJumping, mPersistentAnimationGroup))
    {
        if (mPersistentAnimationPlaying || renderer->isPlaying(mPersistentAnimationGroup))
        {
            const int previousMask = mPersistentAnimationAppliedMask != 0
                ? mPersistentAnimationAppliedMask : mPersistentAnimationBlendMask;
            renderer->beginBoneTransition(previousMask, sNetworkPoseTransitionSeconds);
            renderer->disable(mPersistentAnimationGroup);
        }
        mPersistentAnimationPlaying = false;
        mPersistentAnimationAppliedMask = 0;
        return;
    }

    const int effectiveMask = getPlayerPoseBlendMask(ptr, mPersistentAnimationBlendMask, mPersistentAnimationGroup);
    if (effectiveMask == 0)
    {
        if (renderer->isPlaying(mPersistentAnimationGroup))
        {
            const int previousMask = mPersistentAnimationAppliedMask != 0
                ? mPersistentAnimationAppliedMask : mPersistentAnimationBlendMask;
            renderer->beginBoneTransition(previousMask, sNetworkPoseTransitionSeconds);
            renderer->disable(mPersistentAnimationGroup);
        }
        mPersistentAnimationPlaying = false;
        mPersistentAnimationAppliedMask = 0;
        return;
    }

    if (effectiveMask != mPersistentAnimationAppliedMask
        || !mPersistentAnimationPlaying || !renderer->isPlaying(mPersistentAnimationGroup))
    {
        if (renderer->isPlaying(mPersistentAnimationGroup))
        {
            const int previousMask = mPersistentAnimationAppliedMask != 0
                ? mPersistentAnimationAppliedMask : mPersistentAnimationBlendMask;
            renderer->beginBoneTransition(previousMask, sNetworkPoseTransitionSeconds);
            renderer->disable(mPersistentAnimationGroup);
        }
        mPersistentAnimationPlaying = playPlayerPose(ptr, mPersistentAnimationGroup,
            effectiveMask, mPersistentAnimationSpeed);
        mPersistentAnimationAppliedMask = mPersistentAnimationPlaying ? effectiveMask : 0;
    }
}

bool LocalPlayer::playInteractionAnimation(const std::string& group, int blendMask, float speed,
    int loops, float duration, int prop, const std::string& propModel, bool menuAnimation)
{
    if (mInteractionAnimationActive)
        return false;

    if (mPersistentAnimationPlaying)
    {
        stopPlayerPose(getPlayerPtr(), mPersistentAnimationGroup);
        mPersistentAnimationPlaying = false;
        mPersistentAnimationAppliedMask = 0;
    }

    InteractionAnimationData data;
    data.group = group;
    data.blendMask = blendMask;
    data.speed = speed;
    data.loops = loops;
    data.duration = duration;
    data.prop = prop;
    data.propModel = propModel;

    stopInteractionAnimation(getPlayerPtr(), mInteractionAnimation);
    if (!mwmp::playInteractionAnimation(getPlayerPtr(), data))
        return false;

    mInteractionAnimation = data;
    mInteractionAnimationActive = true;
    mInteractionAnimationTime = duration;
    mMenuInteractionAnimation = menuAnimation;
    mInteractionAnimationSyncTimer = (!data.propModel.empty() || data.prop > 0) ? sAnimationStateHeartbeatSeconds : 0.f;

    if (isLoggedIn())
    {
        animation.groupname = encodeInteractionAnimation(data);
        animation.mode = 0;
        animation.count = 1;
        animation.persist = false;
        getNetworking()->getPlayerPacket(ID_PLAYER_ANIM_PLAY)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_ANIM_PLAY)->Send();
    }
    return true;
}

bool LocalPlayer::isInteractionAnimationPlaying() const
{
    return mInteractionAnimationActive;
}

void LocalPlayer::cancelInteractionAnimation(bool sendToServer)
{
    mMenuInteractionAnimation = false;
    if (!mInteractionAnimationActive)
        return;

    InteractionAnimationData stopped = mInteractionAnimation;
    stopInteractionAnimation(getPlayerPtr(), mInteractionAnimation);
    mInteractionAnimation = InteractionAnimationData();
    mInteractionAnimationActive = false;
    mInteractionAnimationTime = 0.f;
    mInteractionAnimationSyncTimer = 0.f;

    if (sendToServer && isLoggedIn())
    {
        stopped.stop = true;
        animation.groupname = encodeInteractionAnimation(stopped);
        animation.mode = 0;
        animation.count = 1;
        animation.persist = false;
        getNetworking()->getPlayerPacket(ID_PLAYER_ANIM_PLAY)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_ANIM_PLAY)->Send();
    }
}

void LocalPlayer::updateInteractionAnimation(float dt)
{
    if (!mInteractionAnimationActive)
        return;

    if ((mMenuInteractionAnimation && MWBase::Environment::get().getWorld()->isFirstPerson())
        || playerPoseIsBlocked(getPlayerPtr(), isJumping))
    {
        cancelInteractionAnimation(true);
        return;
    }

    ensureInteractionAnimationProp(getPlayerPtr(), mInteractionAnimation);

    const float elapsed = std::max(0.f, dt);
    mInteractionAnimationTime -= elapsed;

    // Refresh held reading props for players that entered the loaded-cell AOI
    // after the book or scroll was opened. Dedicated clients recognize an
    // identical refresh and do not restart the animation.
    if (mInteractionAnimationSyncTimer > 0.f)
    {
        mInteractionAnimationSyncTimer -= elapsed;
        if (mInteractionAnimationSyncTimer <= 0.f && isLoggedIn())
        {
            animation.groupname = encodeInteractionAnimation(mInteractionAnimation);
            animation.mode = 0;
            animation.count = 1;
            animation.persist = false;
            getNetworking()->getPlayerPacket(ID_PLAYER_ANIM_PLAY)->setPlayer(this);
            getNetworking()->getPlayerPacket(ID_PLAYER_ANIM_PLAY)->Send();
            mInteractionAnimationSyncTimer = sAnimationStateHeartbeatSeconds;
        }
    }

    if (mInteractionAnimationTime <= 0.f)
        cancelInteractionAnimation(true);
}

void LocalPlayer::playSpeech()
{
    MWBase::Environment::get().getSoundManager()->say(getPlayerPtr(), sound);

    MWBase::WindowManager *winMgr = MWBase::Environment::get().getWindowManager();
    if (winMgr->getSubtitlesEnabled()
        && !winMgr->containsMode(MWGui::GM_Dialogue))
        winMgr->messageBox(MWBase::Environment::get().getDialogueManager()->getVoiceCaption(sound), MWGui::ShowInDialogueMode_Never);
}
