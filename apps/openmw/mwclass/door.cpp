#include "door.hpp"
#include "../mwmechanics/security.hpp"

/*
    Start of tes3mp addition

    Include additional headers for multiplayer purposes
*/
#include "../mwmp/Main.hpp"
#include "../mwmp/Networking.hpp"
#include "../mwmp/ObjectList.hpp"
#include "../mwmp/ArenaHousing.hpp"
/*
    End of tes3mp addition
*/

#include <components/esm/loaddoor.hpp>
#include <components/esm/doorstate.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/soundmanager.hpp"

#include "../mwworld/ptr.hpp"
#include "../mwworld/failedaction.hpp"
#include "../mwworld/nullaction.hpp"
#include "../mwworld/actionteleport.hpp"
#include "../mwworld/actiondoor.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwphysics/physicssystem.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwworld/actiontrap.hpp"
#include "../mwworld/customdata.hpp"

#include "../mwgui/tooltips.hpp"

#include "../mwrender/objects.hpp"
#include "../mwrender/renderinginterface.hpp"
#include "../mwrender/animation.hpp"
#include "../mwrender/vismask.hpp"

#include "../mwmechanics/actorutil.hpp"

namespace MWClass
{
    class DoorCustomData : public MWWorld::TypedCustomData<DoorCustomData>
    {
    public:
        MWWorld::DoorState mDoorState = MWWorld::DoorState::Idle;

        DoorCustomData& asDoorCustomData() override
        {
            return *this;
        }
        const DoorCustomData& asDoorCustomData() const override
        {
            return *this;
        }
    };

    void Door::insertObjectRendering (const MWWorld::Ptr& ptr, const std::string& model, MWRender::RenderingInterface& renderingInterface) const
    {
        if (!model.empty())
        {
            renderingInterface.getObjects().insertModel(ptr, model, true);
            ptr.getRefData().getBaseNode()->setNodeMask(MWRender::Mask_Static);
        }
    }

    void Door::insertObject(const MWWorld::Ptr& ptr, const std::string& model, MWPhysics::PhysicsSystem& physics) const
    {
        if(!model.empty())
            physics.addObject(ptr, model, MWPhysics::CollisionType_Door);

        // Resume the door's opening/closing animation if it wasn't finished
        if (ptr.getRefData().getCustomData())
        {
            const DoorCustomData& customData = ptr.getRefData().getCustomData()->asDoorCustomData();
            if (customData.mDoorState != MWWorld::DoorState::Idle)
            {
                MWBase::Environment::get().getWorld()->activateDoor(ptr, customData.mDoorState);
            }
        }
    }

    bool Door::isDoor() const
    {
        return true;
    }

    bool Door::useAnim() const
    {
        return true;
    }

    std::string Door::getModel(const MWWorld::ConstPtr &ptr) const
    {
        const MWWorld::LiveCellRef<ESM::Door> *ref = ptr.get<ESM::Door>();

        const std::string &model = ref->mBase->mModel;
        if (!model.empty()) {
            return "meshes\\" + model;
        }
        return "";
    }

    std::string Door::getName (const MWWorld::ConstPtr& ptr) const
    {
        const MWWorld::LiveCellRef<ESM::Door> *ref = ptr.get<ESM::Door>();
        const std::string& name = ref->mBase->mName;

        return !name.empty() ? name : ref->mBase->mId;
    }

    std::shared_ptr<MWWorld::Action> Door::activate (const MWWorld::Ptr& ptr,
        const MWWorld::Ptr& actor) const
    {
        MWWorld::LiveCellRef<ESM::Door> *ref = ptr.get<ESM::Door>();

        const std::string &openSound = ref->mBase->mOpenSound;
        const std::string &closeSound = ref->mBase->mCloseSound;
        const std::string lockedSound = "LockedDoor";
        const std::string trapActivationSound = "Disarm Trap Fail";

        MWWorld::ContainerStore &invStore = actor.getClass().getContainerStore(actor);

        bool isLocked = ptr.getCellRef().getLockLevel() > 0;
        bool isTrapped = !ptr.getCellRef().getTrap().empty();
        bool hasKey = false;
        std::string keyName;

        // FIXME: If NPC activate teleporting door, it can lead to crash due to iterator invalidation in the Actors update.
        // Make such activation a no-op for now, like how it is in the vanilla game.
        if (actor != MWMechanics::getPlayer() && ptr.getCellRef().getTeleport())
        {
            std::shared_ptr<MWWorld::Action> action(new MWWorld::FailedAction(std::string(), ptr));
            action->setSound(lockedSound);
            return action;
        }

        // make door glow if player activates it with telekinesis
        if (actor == MWMechanics::getPlayer() &&
            MWBase::Environment::get().getWorld()->getDistanceToFacedObject() >
            MWBase::Environment::get().getWorld()->getMaxActivationDistance())
        {
            MWRender::Animation* animation = MWBase::Environment::get().getWorld()->getAnimation(ptr);
            if(animation)
            {
                const MWWorld::ESMStore& store = MWBase::Environment::get().getWorld()->getStore();
                int index = ESM::MagicEffect::effectStringToId("sEffectTelekinesis");
                const ESM::MagicEffect *effect = store.get<ESM::MagicEffect>().find(index);

                animation->addSpellCastGlow(effect, 1); // 1 second glow to match the time taken for a door opening or closing
            }
        }

        const std::string keyId = ptr.getCellRef().getKey();
        if (!keyId.empty())
        {
            MWWorld::Ptr keyPtr = invStore.search(keyId);
            if (!keyPtr.isEmpty())
            {
                hasKey = true;
                keyName = keyPtr.getClass().getName(keyPtr);
            }
        }

        // Z010: ask the housing server before the authored lock rejects activation.
        // A live trap must resolve first, even for a managed house door: without
        // a probe, ordinary activation triggers it rather than bypassing it.
        // This never unlocks the physical door or sends an ObjectLock packet.
        if (isLocked && !isTrapped && actor == MWMechanics::getPlayer()
            && (!hasKey || mwmp::ArenaHousing::get().canBypassDoorLock(ptr))
            && mwmp::ArenaHousing::get().isManagedDoor(ptr)
            && mwmp::ArenaHousing::get().requestForDoor(ptr, false))
            return std::shared_ptr<MWWorld::Action>(new MWWorld::FailedAction(std::string(), ptr));

        if (isLocked && hasKey)
        {
            if(actor == MWMechanics::getPlayer())
                MWBase::Environment::get().getWindowManager()->messageBox(keyName + " #{sKeyUsed}");

            /*
                Start of tes3mp change (major)

                Disable unilateral unlocking on this client and expect the server's reply to our
                packet to do it instead
            */
            //ptr.getCellRef().unlock(); //Call the function here. because that makes sense.
            /*
                End of tes3mp change (major)
            */

            // using a key disarms the trap
            if(isTrapped)
            {
                /*
                    Start of tes3mp change (major)

                    Disable unilateral trap disarming on this client and expect the server's reply to our
                    packet to do it instead
                */
                //ptr.getCellRef().setTrap("");
                //MWBase::Environment::get().getSoundManager()->playSound3D(ptr, "Disarm Trap", 1.0f, 1.0f);
                /*
                    End of tes3mp change (major)
                */

                isTrapped = false;

                /*
                    Start of tes3mp addition

                    Send an ID_OBJECT_TRAP packet every time a trap is disarmed
                */
                mwmp::ObjectList *objectList = mwmp::Main::get().getNetworking()->getObjectList();
                objectList->reset();
                objectList->packetOrigin = mwmp::CLIENT_GAMEPLAY;
                objectList->addObjectTrap(ptr, ptr.getRefData().getPosition(), true);
                objectList->sendObjectTrap();
                /*
                    End of tes3mp addition
                */
            }

            /*
                Start of tes3mp addition

                Send an ID_OBJECT_LOCK packet every time a door is unlocked here
            */
            if (isLocked)
            {
                mwmp::ObjectList *objectList = mwmp::Main::get().getNetworking()->getObjectList();
                objectList->reset();
                objectList->packetOrigin = mwmp::CLIENT_GAMEPLAY;
                objectList->addObjectLock(ptr, 0);
                objectList->sendObjectLock();
            }
            /*
                End of tes3mp addition
            */
        }

        if (!hasKey && MWMechanics::Security::autoActivate(ptr, actor))
            return std::shared_ptr<MWWorld::Action>(new MWWorld::FailedAction(std::string(), ptr));

        if (!isLocked || hasKey)
        {
            if(isTrapped)
            {
                // Trap activation
                std::shared_ptr<MWWorld::Action> action(new MWWorld::ActionTrap(ptr.getCellRef().getTrap(), ptr));
                action->setSound(trapActivationSound);
                return action;
            }

            if (ptr.getCellRef().getTeleport())
            {
                if (actor == MWMechanics::getPlayer() && MWBase::Environment::get().getWorld()->getDistanceToFacedObject() > MWBase::Environment::get().getWorld()->getMaxActivationDistance())
                {
                    // player activated teleport door with telekinesis
                    std::shared_ptr<MWWorld::Action> action(new MWWorld::FailedAction);
                    return action;
                }
                else
                {
                    /*
                        Start of tes3mp change (major)

                        If there is a destination override in the mwmp::Worldstate for this door's original
                        destination, use it
                    */
                    std::string destinationCell = ptr.getCellRef().getDestCell();

                    if (mwmp::Main::get().getNetworking()->getWorldstate()->destinationOverrides.count(destinationCell) != 0)
                        destinationCell = mwmp::Main::get().getNetworking()->getWorldstate()->destinationOverrides[destinationCell];

                    /*
                        Start of ArenaMP addition (Z001)

                        A teleport door normally resolves entirely on this client, which is why the
                        archived housing system could only "correct" the player after the fact - you
                        walked into a house and were immediately pulled back out.

                        For the handful of destinations the server has declared managed property, ask
                        first. The client returns a NullAction and performs no movement at all; the
                        server either approves the plain entry (ArenaHousing then completes exactly
                        this teleport) or places the player inside a private instance itself. Doors
                        the server has not declared are untouched and behave as they always have.
                    */
                    if (actor == MWMechanics::getPlayer()
                        && mwmp::ArenaHousing::get().isManaged(destinationCell)
                        && mwmp::ArenaHousing::get().requestForDoor(ptr, false, hasKey))
                    {
                        std::shared_ptr<MWWorld::Action> action(new MWWorld::NullAction);
                        action->setSound(openSound);
                        return action;
                    }
                    /*
                        End of ArenaMP addition (Z001)
                    */

                    std::shared_ptr<MWWorld::Action> action(new MWWorld::ActionTeleport(destinationCell, ptr.getCellRef().getDoorDest(), true));
                    /*
                        End of tes3mp change (major)
                    */

                    action->setSound(openSound);
                    return action;
                }
            }
            else
            {
                // animated door
                std::shared_ptr<MWWorld::Action> action(new MWWorld::ActionDoor(ptr));
                const auto doorState = getDoorState(ptr);
                bool opening = true;
                float doorRot = ptr.getRefData().getPosition().rot[2] - ptr.getCellRef().getPosition().rot[2];
                if (doorState == MWWorld::DoorState::Opening)
                    opening = false;
                if (doorState == MWWorld::DoorState::Idle && doorRot != 0)
                    opening = false;

                if (opening)
                {
                    MWBase::Environment::get().getSoundManager()->fadeOutSound3D(ptr,
                            closeSound, 0.5f);
                    // Doors rotate at 90 degrees per second, so start the sound at
                    // where it would be at the current rotation.
                    float offset = doorRot/(osg::PI * 0.5f);
                    action->setSoundOffset(offset);
                    action->setSound(openSound);
                }
                else
                {
                    MWBase::Environment::get().getSoundManager()->fadeOutSound3D(ptr,
                                                openSound, 0.5f);
                    float offset = 1.0f - doorRot/(osg::PI * 0.5f);
                    action->setSoundOffset(std::max(offset, 0.0f));
                    action->setSound(closeSound);
                }

                return action;
            }
        }
        else
        {
            // locked, and we can't open.
            std::shared_ptr<MWWorld::Action> action(new MWWorld::FailedAction(std::string(), ptr));
            action->setSound(lockedSound);
            return action;
        }
    }

    bool Door::canLock(const MWWorld::ConstPtr &ptr) const
    {
        return true;
    }

    bool Door::allowTelekinesis(const MWWorld::ConstPtr &ptr) const
    {
        if (ptr.getCellRef().getTeleport() && ptr.getCellRef().getLockLevel() <= 0 && ptr.getCellRef().getTrap().empty())
            return false;
        else
            return true;
    }

    std::string Door::getScript (const MWWorld::ConstPtr& ptr) const
    {
        const MWWorld::LiveCellRef<ESM::Door> *ref = ptr.get<ESM::Door>();

        return ref->mBase->mScript;
    }

    void Door::registerSelf()
    {
        std::shared_ptr<Class> instance (new Door);

        registerClass (typeid (ESM::Door).name(), instance);
    }

    MWGui::ToolTipInfo Door::getToolTipInfo (const MWWorld::ConstPtr& ptr, int count) const
    {
        const MWWorld::LiveCellRef<ESM::Door> *ref = ptr.get<ESM::Door>();

        MWGui::ToolTipInfo info;
        info.caption = MyGUI::TextIterator::toTagsString(getName(ptr));

        std::string text;

        if (ptr.getCellRef().getTeleport())
        {
            const std::string housingHint = mwmp::ArenaHousing::get().hintForDoor(ptr);
            if (!housingHint.empty())
                text += "\n" + MyGUI::TextIterator::toTagsString(housingHint);
            else
            {
                text += "\n#{sTo}";
                text += "\n" + getDestination(*ref);
            }
        }

        int lockLevel = ptr.getCellRef().getLockLevel();
        if (lockLevel > 0 && lockLevel != ESM::UnbreakableLock)
            text += "\n#{sLockLevel}: " + MWGui::ToolTips::toString(ptr.getCellRef().getLockLevel());
        else if (ptr.getCellRef().getLockLevel() < 0)
            text += "\n#{sUnlocked}";
        if (ptr.getCellRef().getTrap() != "")
            text += "\n#{sTrapped}";

        if (MWBase::Environment::get().getWindowManager()->getFullHelp())
        {
            text += MWGui::ToolTips::getCellRefString(ptr.getCellRef());
            text += MWGui::ToolTips::getMiscString(ref->mBase->mScript, "Script");
        }
        info.text = text;

        return info;
    }

    std::string Door::getDestination(const MWWorld::LiveCellRef<ESM::Door>& door)
    {
        const MWWorld::ESMStore& store = MWBase::Environment::get().getWorld()->getStore();

        std::string dest = door.mRef.getDestCell();
        if (dest.empty())
        {
            // door leads to exterior, use cell name (if any), otherwise translated region name
            int x, y;
            auto world = MWBase::Environment::get().getWorld();
            world->positionToIndex(door.mRef.getDoorDest().pos[0], door.mRef.getDoorDest().pos[1], x, y);
            const ESM::Cell* cell = world->getStore().get<ESM::Cell>().search(x, y);
            dest = world->getCellName(cell);
        }
        /*
            Start of tes3mp addition

            If there is a destination override in the mwmp::Worldstate for this door's original
            destination, use it
        */
        else if (mwmp::Main::get().getNetworking()->getWorldstate()->destinationOverrides.count(dest) != 0)
            dest = mwmp::Main::get().getNetworking()->getWorldstate()->destinationOverrides[dest];
        /*
            End of tes3mp addition
        */

        return "#{sCell=" + dest + "}";
    }

    MWWorld::Ptr Door::copyToCellImpl(const MWWorld::ConstPtr &ptr, MWWorld::CellStore &cell) const
    {
        const MWWorld::LiveCellRef<ESM::Door> *ref = ptr.get<ESM::Door>();

        return MWWorld::Ptr(cell.insert(ref), &cell);
    }

    void Door::ensureCustomData(const MWWorld::Ptr &ptr) const
    {
        if (!ptr.getRefData().getCustomData())
        {
            ptr.getRefData().setCustomData(std::make_unique<DoorCustomData>());
        }
    }

    MWWorld::DoorState Door::getDoorState (const MWWorld::ConstPtr &ptr) const
    {
        if (!ptr.getRefData().getCustomData())
            return MWWorld::DoorState::Idle;
        const DoorCustomData& customData = ptr.getRefData().getCustomData()->asDoorCustomData();
        return customData.mDoorState;
    }

    void Door::setDoorState (const MWWorld::Ptr &ptr, MWWorld::DoorState state) const
    {
        if (ptr.getCellRef().getTeleport())
            throw std::runtime_error("load doors can't be moved");

        ensureCustomData(ptr);
        DoorCustomData& customData = ptr.getRefData().getCustomData()->asDoorCustomData();
        customData.mDoorState = state;
    }

    void Door::readAdditionalState (const MWWorld::Ptr& ptr, const ESM::ObjectState& state) const
    {
        if (!state.mHasCustomState)
            return;

        ensureCustomData(ptr);
        DoorCustomData& customData = ptr.getRefData().getCustomData()->asDoorCustomData();
        const ESM::DoorState& doorState = state.asDoorState();
        customData.mDoorState = MWWorld::DoorState(doorState.mDoorState);
    }

    void Door::writeAdditionalState (const MWWorld::ConstPtr& ptr, ESM::ObjectState& state) const
    {
        if (!ptr.getRefData().getCustomData())
        {
            state.mHasCustomState = false;
            return;
        }

        const DoorCustomData& customData = ptr.getRefData().getCustomData()->asDoorCustomData();
        ESM::DoorState& doorState = state.asDoorState();
        doorState.mDoorState = int(customData.mDoorState);
    }

}
