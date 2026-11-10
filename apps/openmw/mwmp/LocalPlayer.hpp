#ifndef OPENMW_LOCALPLAYER_HPP
#define OPENMW_LOCALPLAYER_HPP

#include <cstdint>
#include <string>

#include <components/openmw-mp/Base/BasePlayer.hpp>
#include "../mwmechanics/activespells.hpp"
#include "../mwmechanics/stat.hpp"
#include "../mwworld/ptr.hpp"
#include "../mwworld/timestamp.hpp"
#include "InteractionAnimationSync.hpp"
#include <RakNetTypes.h>

namespace mwmp
{
    class Networking;
    class LocalPlayer : public BasePlayer
    {
    public:

        LocalPlayer();
        virtual ~LocalPlayer();

        time_t deathTime;
        bool receivedCharacter;

        bool isUsingBed;
        bool avoidSendingInventoryPackets;
        // Y009: incremented after an authoritative server InventoryChanges::SET.
        // HUD pickup notifications use it to reseed exactly instead of guessing.
        std::uint64_t hudInventorySetGeneration = 0;
        bool isReceivingQuickKeys;
        bool isPlayingAnimation;
        bool diedSinceArrestAttempt;
        unsigned int lastEnchantmentQuantity;

        void update();

        // Y039: local presentation/input for the server-authoritative death recovery window.
        bool isDeathRecoveryActive() const { return mDeathRecoveryActive || mBountyDeathActive || mRespawnWaitActive; }
        bool isBountyDeathActive() const { return mBountyDeathActive; }
        float getBountyDeathRemaining() const { return mBountyDeathRemaining; }
        float getBountyDeathDuration() const { return mBountyDeathDuration; }
        void applyRespawnWait(float seconds);
        bool isRespawnWaitVisible() const { return mRespawnWaitActive
            && !mDeathRecoveryActive; }
        float getRespawnWaitRemaining() const { return mRespawnWaitRemaining; }
        float getRespawnWaitDuration() const { return mRespawnWaitDuration; }
        void applyBountyDeathState(float remaining, float total);
        float getDeathRecoveryRemainingSeconds() const;
        float getDeathRecoveryDurationSeconds() const { return mDeathRecoveryDuration; }
        float getDeathRecoveryInitialXp() const { return mDeathRecoveryInitialXp; }
        int getRestoreHealthPotionCount(std::string* firstRefId = nullptr) const;
        int getRequiredDeathRecoveryPotionCount(int level) const;
        int getSelfDeathRecoveryPotionRequirement() const;
        bool getRecoverableAllyName(std::string& name, int* level = nullptr) const;
        void requestTouchRecovery(const MWWorld::Ptr& target);
        // U035f: SDL/Android synthetic key events may be shorter than one frame, so
        // revive input also has an event-driven edge in addition to keyboard polling.
        void notifyDeathRecoveryUsePressed();

        // U035: getting up costs a lockout. The server owns it; this is the
        // local mirror used to suppress the panel and show the countdown, so a
        // patched client can lie to its own HUD and gain nothing.
        // state: "OPEN" | "DENY" | "LOCK".
        void applyDeathRecoveryState(const std::string& state, float seconds);
        float getReviveLockoutRemaining() const { return mReviveLockoutRemaining; }

        bool processCharGen();
        bool isLoggedIn();
        void updateLanguage();

        /*
            Start of AMP addition (Y044)

            Between ID_LOADED and the arrival of the server-owned profile, the local
            player Ptr still holds the temporary CharGen state: level 1, class base
            skills and the starting shirt/pants/shoes. Sending that state would make
            the server persist it over the profile it has just loaded, which is how
            skills, attributes, base health/magicka/fatigue and worn equipment were
            being lost on login. Hold those packets back until the profile lands.
        */
        enum LoginSyncSection
        {
            LoginSync_StatsDynamic = 1 << 0,
            LoginSync_Attributes   = 1 << 1,
            LoginSync_Skills       = 1 << 2,
            LoginSync_Level        = 1 << 3,
            LoginSync_Equipment    = 1 << 4,
            LoginSync_All          = (1 << 5) - 1
        };

        void beginLoginSync(bool force = false);
        void markLoginSyncReceived(int section);
        bool isLoginSyncPending() const { return mLoginSyncPending; }
        /*
            End of AMP addition (Y044)
        */

        void updateStatsDynamic(bool forceUpdate = false);
        void updateAttributes(bool forceUpdate = false);
        void updateSkills(bool forceUpdate = false);
        void updateLevel(bool forceUpdate = false);
        void updateBounty(bool forceUpdate = false);
        void updateReputation(bool forceUpdate = false);
        void updatePosition(bool forceUpdate = false);
        void updateCell(bool forceUpdate = false);
        void updateEquipment(bool forceUpdate = false);
        void updateInventory(bool forceUpdate = false);
        void updateAttackOrCast();
        void updateAnimFlags(bool forceUpdate = false);

        void addItems();
        void addSpells();
        void addSpellsActive();
        void addJournalItems();
        void addTopics();

        void removeItems();
        void removeSpells();
        void removeSpellsActive();

        void die();
        void resurrect();

        void closeInventoryWindows();
        void updateInventoryWindow();

        void setCharacter();
        void setDynamicStats();

        /*
            Start of AMP addition (Y056)

            Re-derive the dynamic stats the engine owns after a server profile has been
            applied, and repair a base health value the profile got wrong. Returns true
            when anything had to be corrected, so the caller can publish the fixed values
            back to the server.
        */
        bool applyEngineDerivedDynamicStats();
        bool repairBaseHealth(const MWWorld::Ptr &ptrPlayer);
        /*
            End of AMP addition (Y056)
        */
        void setAttributes();
        void setSkills();
        void setLevel();
        void setBounty();
        void setReputation();
        void setPosition();
        void setMomentum();
        void setCell();
        void setClass();
        void setEquipment();
        void setInventory();
        void setSpellbook();
        void setSpellsActive();
        void setCooldowns();
        void setQuickKeys();
        void setFactions();
        void setBooks();
        void setShapeshift();
        void setMarkLocation();
        void setSelectedSpell();

        void sendDeath(char newDeathState);
        void sendClass();
        void sendInventory();
        void sendItemChange(const mwmp::Item& item, unsigned int action);
        void sendItemChange(const MWWorld::Ptr& itemPtr, int count, unsigned int action);
        void sendItemChange(const std::string& refId, int count, unsigned int action);
        void sendStoredItemRemovals();
        void sendSpellbook();
        void sendSpellChange(std::string id, unsigned int action);
        void sendSpellsActive();
        void sendSpellsActiveAddition(const std::string id, bool isStackingSpell, const MWMechanics::ActiveSpells::ActiveSpellParams& params);
        void sendSpellsActiveRemoval(const std::string id, bool isStackingSpell, MWWorld::TimeStamp timestamp);
        void sendCooldownChange(std::string id, int startTimestampDay, float startTimestampHour);
        void sendQuickKey(unsigned short slot, int type, const std::string& itemId = "");
        void sendJournalEntry(const std::string& quest, int index, const MWWorld::Ptr& actor);
        void sendJournalIndex(const std::string& quest, int index);
        void sendFactionRank(const std::string& factionId, int rank);
        void sendFactionExpulsionState(const std::string& factionId, bool isExpelled);
        void sendFactionReputation(const std::string& factionId, int reputation);
        void sendTopic(const std::string& topic);
        void sendBook(const std::string& bookId);
        void sendWerewolfState(bool isWerewolf);
        void sendMarkLocation(const ESM::Cell& newMarkCell, const ESM::Position& newMarkPosition);
        void sendSelectedSpell(const std::string& newSelectedSpellId);
        void sendItemUse(const MWWorld::Ptr& itemPtr, bool usingItemMagic = false, char currentDrawState = 0);
        void sendConsumableAnimation(const std::string& refId);
        void sendCellStates();

        void clearCellStates();
        void clearCurrentContainer();

        void storeCellState(const ESM::Cell& cell, int stateType);
        void storeCurrentContainer(const MWWorld::Ptr& container);
        void storeItemRemoval(const std::string& refId, int count);
        void storeLastEnchantmentQuantity(unsigned int quantity);

        void playAnimation();
        void playSpeech();

        void setPersistentAnimation(const std::string& group, int blendMask, float speed);
        void clearPersistentAnimation();
        void updatePersistentAnimation(float dt);
        void setWalkAnimationStyle(const std::string& group);
        bool playInteractionAnimation(const std::string& group, int blendMask, float speed,
            int loops, float duration, int prop, const std::string& propModel = std::string(), bool menuAnimation = false);
        bool isInteractionAnimationPlaying() const;
        void cancelInteractionAnimation(bool sendToServer);

        MWWorld::Ptr getPlayerPtr();

    private:
        Networking *getNetworking();
        void sendPersistentAnimationState();
        void sendWalkAnimationState();
        void updateWalkAnimationSync(float dt);
        void updateInteractionAnimation(float dt);
        void beginDeathRecovery();
        void updateDeathRecovery(float dt);
        void sendDeathRecoveryControl(const std::string& payload);
        bool isRestoreHealthPotion(const MWWorld::Ptr& item) const;

        /*
            Start of AMP addition (Y044)
        */
        void updateLoginSync(float dt);
        void adoptServerStateAsBaseline();

        bool mLoginSyncPending;
        int mLoginSyncReceived;
        float mLoginSyncElapsed;

        // These used to be function-local statics inside updateStatsDynamic(), so
        // they survived a disconnect and were compared against the stats of the
        // previous session after a relog within the same process.
        bool mDynamicStatsBaselineValid;
        MWMechanics::DynamicStat<float> mOldHealth;
        MWMechanics::DynamicStat<float> mOldMagicka;
        MWMechanics::DynamicStat<float> mOldFatigue;
        /*
            End of AMP addition (Y044)
        */

        bool mRespawnWaitActive = false;
        float mRespawnWaitRemaining = 0.f, mRespawnWaitDuration = 0.f;
        bool mBountyDeathActive = false;
        float mBountyDeathRemaining = 0.f;
        float mBountyDeathDuration = 0.f;
        bool mDeathRecoveryActive;
        float mDeathRecoveryElapsed;
        float mDeathRecoveryDuration;
        float mDeathRecoveryInitialXp;
        float mDeathRecoveryRequestCooldown;
        bool mDeathRecoveryUseQueued;
        float mReviveLockoutRemaining;

        bool mPersistentAnimationActive;
        bool mPersistentAnimationPlaying;
        std::string mPersistentAnimationGroup;
        int mPersistentAnimationBlendMask;
        int mPersistentAnimationAppliedMask;
        float mPersistentAnimationSpeed;
        float mPersistentAnimationSyncTimer;

        std::string mWalkAnimationStyle;
        float mWalkAnimationSyncTimer;

        bool mInteractionAnimationActive;
        bool mMenuInteractionAnimation = false;
        float mInteractionAnimationTime;
        float mInteractionAnimationSyncTimer;
        InteractionAnimationData mInteractionAnimation;

    };
}

#endif //OPENMW_LOCALPLAYER_HPP
