#ifndef OPENMW_LOCALACTOR_HPP
#define OPENMW_LOCALACTOR_HPP

#include <components/openmw-mp/Base/BaseActor.hpp>
#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/activespells.hpp"
#include "../mwworld/manualref.hpp"
#include "../mwworld/timestamp.hpp"

namespace mwmp
{
    class LocalActor : public BaseActor
    {
    public:

        LocalActor();
        virtual ~LocalActor();

        void update(bool forceUpdate);

        void updateCell();
        /// X024: false while OpenMW transiently reports the INT_MIN,INT_MIN grid.
        bool hasValidDestinationCell() const;
        void updatePosition(bool forceUpdate);
        void updateAnimFlags(bool forceUpdate);
        void updateAnimPlay();
        void updateSpeech();
        void updateStatsDynamic(bool forceUpdate);
        void updateEquipment(bool forceUpdate, bool sendImmediately = false);
        void updateAttackOrCast();
        void updateAiState(bool forceUpdate);

        void sendEquipment();
        void sendSpellsActive();
        void sendSpellsActiveAddition(const std::string id, bool isStackingSpell, const MWMechanics::ActiveSpells::ActiveSpellParams& params);
        void sendSpellsActiveRemoval(const std::string id, bool isStackingSpell, MWWorld::TimeStamp timestamp);
        void sendDeath(char newDeathState);

        MWWorld::Ptr getPtr();
        void setPtr(const MWWorld::Ptr& newPtr);

        bool hasSentData;

    private:
        MWWorld::Ptr ptr;

        bool posWasChanged;
        int stopPositionResends;
        bool equipmentChanged;

        bool wasRunning;
        bool wasSneaking;
        bool wasForceJumping;
        bool wasForceMoveJumping;

        bool wasJumping;
        bool wasFlying;

        MWMechanics::DrawState_ lastDrawState;

        MWMechanics::DynamicStat<float> oldHealth;
        MWMechanics::DynamicStat<float> oldMagicka;
        MWMechanics::DynamicStat<float> oldFatigue;

        bool mAiStateInitialized = false;
        bool mLastAiWasCombat = false;
        int mLastAiTargetActorId = -1;
        std::size_t mLastAiTargetSignature = 0;
        std::size_t mLastAiDoorCount = 0;
        bool mLastAiHadHome = false;
        float mAiHeartbeatTimer = 0.f;
    };
}

#endif //OPENMW_LOCALACTOR_HPP
