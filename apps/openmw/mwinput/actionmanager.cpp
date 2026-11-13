#include <SDL.h>
#include "actionmanager.hpp"

#include <algorithm>

#include <MyGUI_InputManager.h>
#include <MyGUI_Gui.h>
#include <MyGUI_RenderManager.h>
#include <MyGUI_TextBox.h>
#include <MyGUI_Widget.h>

#include <SDL_keyboard.h>

#include <components/settings/settings.hpp>
#include <components/misc/stringops.hpp>

/*
    Start of tes3mp addition

    Include additional headers for multiplayer purposes
*/
#include "../mwmp/Main.hpp"
#include "../mwmp/LocalPlayer.hpp"
#include "../mwmp/GUIController.hpp"
/*
    End of tes3mp addition
*/

#include "../mwbase/inputmanager.hpp"
#include "../mwbase/statemanager.hpp"
#include "../mwbase/environment.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwworld/player.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwworld/esmstore.hpp"

#include <components/esm/loadbook.hpp>
#include <components/esm/loadstat.hpp>
#include <components/esm/loadcont.hpp>
#include <components/esm/loadacti.hpp>
#include <components/esm/loadligh.hpp>
#include <components/esm/loadweap.hpp>

#include "../mwmechanics/npcstats.hpp"
#include "../mwmechanics/actorutil.hpp"
#include "../mwmechanics/activeguard.hpp"
#include "../mwmechanics/weapontype.hpp"
#include "../mwmp/ArenaHousing.hpp"
#include <MyGUI_RenderManager.h>

#include "actions.hpp"
#include "bindingsmanager.hpp"

namespace MWInput
{
    const float ZOOM_SCALE = 10.f; /// Used for scrolling camera in and out

    namespace
    {
        // The inventory-visibility flag does NOT distinguish furniture from
        // ordinary loot: a base-class default, or a modded record, may report
        // an activator/fixture as inventory-visible. Classify the hold gesture
        // from the actual TES3 record instead.
        bool opensFurnitureActionMenu(const MWWorld::ConstPtr& object)
        {
            if (object.isEmpty())
                return false;

            const std::string& type = object.getTypeName();
            if (type == typeid(ESM::Static).name()
                || type == typeid(ESM::Container).name()
                || type == typeid(ESM::Activator).name())
                return true;

            if (type == typeid(ESM::Light).name())
            {
                // Non-carryable lights are household fixtures. A torch or a
                // carryable candle must still enter manipulation directly.
                const ESM::Light* light = object.get<ESM::Light>()->mBase;
                return light != nullptr && (light->mData.mFlags & ESM::Light::Carry) == 0;
            }

            return false;
        }
    }

    ActionManager::ActionManager(BindingsManager* bindingsManager,
            osgViewer::ScreenCaptureHandler::CaptureOperation* screenCaptureOperation,
            osg::ref_ptr<osgViewer::Viewer> viewer,
            osg::ref_ptr<osgViewer::ScreenCaptureHandler> screenCaptureHandler)
        : mBindingsManager(bindingsManager)
        , mViewer(viewer)
        , mScreenCaptureHandler(screenCaptureHandler)
        , mScreenCaptureOperation(screenCaptureOperation)
        , mAlwaysRunActive(Settings::Manager::getBool("always run", "Input"))
        , mSneaking(false)
        , mAttemptJump(false)
        , mOverencumberedMessageDelay(0.f)
        , mTimeIdle(0.f)
        , mActivateHoldTime(0.f)
        , mActivateHoldPending(false)
        , mActivateHoldIsDoor(false)
        , mFurnitureMenuOpen(false)
        , mFurnitureHoldGrabbed(false)
        , mFurnitureResumeGrab(false)
    {
    }

    void ActionManager::refreshFurnitureQuickMenu()
    {
        for (int row=0;row<6;++row)
        {
            if (!mFurnitureMenuRows[row]) continue;
            mFurnitureMenuRows[row]->setVisible(row < (mFurniturePortableMenu ? 6 : 4));
            if (!mFurnitureMenuRows[row]->getVisible()) continue;
            const bool selected=row==mFurnitureSelected;
            mFurnitureMenuRows[row]->changeWidgetSkin(selected?"SandBrightText":"SandText");
            mFurnitureMenuRows[row]->setAlpha(selected?1.f:0.72f);
            const bool ru=MWBase::Environment::get().getWindowManager()->getArenaLanguage()=="ru";
            const bool locked = mwmp::ArenaHousing::get().itemLocked(mFurnitureMenuObject);
            const char* ruActions[]={"Переместить", "Назначить цену", "Убрать цену", "В инвентарь", locked ? "Разблокировать" : "Заблокировать", "Отмена"};
            const char* enActions[]={"Move", "Set price", "Remove price", "Take", locked ? "Unlock" : "Lock", "Cancel"};
            const char* ruStatic[]={"Переместить", "Забрать", "Удалить", "Отмена"};
            const char* enStatic[]={"Move", "Take", "Delete", "Cancel"};
            // An architectural static cannot enter inventory. Keep the four requested
            // actions visible, but indicate that Take is unavailable for it.
            const bool canTake = !mFurnitureMenuObject.isEmpty()
                && mFurnitureMenuObject.getClass().showsInInventory(mFurnitureMenuObject);
            const std::string suffix = !mFurniturePortableMenu && row == 1 && !canTake
                ? (ru ? " (нельзя)" : " (unavailable)") : "";
            mFurnitureMenuRows[row]->setCaption(std::string(selected?">  ":"    ")
                + (mFurniturePortableMenu ? (ru?ruActions[row]:enActions[row]) : (ru?ruStatic[row]:enStatic[row])) + suffix);
        }
    }

    bool ActionManager::scrollFurnitureQuickMenu(int direction)
    {
        if (!mFurnitureMenuOpen || direction==0) return false;
        const int count = mFurniturePortableMenu ? 6 : 4;
        mFurnitureSelected=(mFurnitureSelected+(direction>0?count-1:1))%count;
        refreshFurnitureQuickMenu();
        return true;
    }

    void ActionManager::showFurnitureQuickMenu(const MWWorld::Ptr& object)
    {
        if (!MWBase::Environment::get().getWorld()->canPhysicsGrab(object)) return;
        mFurniturePortableMenu = mwmp::ArenaHousing::get().isManagedItem(object);
        if (mFurniturePortableMenu) mwmp::ArenaHousing::get().requestHouseItem("STATE", object);
        if (!mFurnitureMenuPanel)
        {
            // Compact four-action popup for fixtures and furniture only.
            mFurnitureMenuPanel=MyGUI::Gui::getInstance().createWidget<MyGUI::Widget>(
                "HUD_Box",0,0,360,184,MyGUI::Align::Default,"Popup");
            mFurnitureMenuPanel->setNeedKeyFocus(false);
            mFurnitureMenuPanel->setNeedMouseFocus(false);
            MyGUI::TextBox* heading=mFurnitureMenuPanel->createWidget<MyGUI::TextBox>(
                "SandBrightText",12,8,336,22,MyGUI::Align::Default,"FurnitureQuickHeader");
            heading->setNeedKeyFocus(false);
            heading->setNeedMouseFocus(false);
            heading->setTextAlign(MyGUI::Align::Left | MyGUI::Align::VCenter);
            for (int row=0;row<6;++row)
            {
                mFurnitureMenuRows[row]=mFurnitureMenuPanel->createWidget<MyGUI::TextBox>(
                    "SandText",12,32+row*23,336,23,MyGUI::Align::Default,
                    "FurnitureQuickRow"+std::to_string(row));
                mFurnitureMenuRows[row]->setNeedKeyFocus(false);
                mFurnitureMenuRows[row]->setNeedMouseFocus(false);
                mFurnitureMenuRows[row]->setTextAlign(MyGUI::Align::Left | MyGUI::Align::VCenter);
            }
        }
        const MyGUI::IntSize view=MyGUI::RenderManager::getInstance().getViewSize();
        const int menuHeight = mFurniturePortableMenu ? 184 : 138;
        mFurnitureMenuPanel->setSize(360, menuHeight);
        mFurnitureMenuPanel->setPosition(std::max(0,std::min(view.width-360,view.width*7/10-180)),
            std::max(0,std::min(view.height-menuHeight,view.height*6/10-menuHeight/2)));
        MyGUI::TextBox* heading=mFurnitureMenuPanel->findWidget("FurnitureQuickHeader")->castType<MyGUI::TextBox>();
        const std::string name=object.getClass().getName(object);
        heading->setCaption(name.empty()?object.getCellRef().getRefId():name);
        mFurnitureMenuObject=object;
        mFurnitureSelected=0;
        mFurnitureUpHeld=mFurnitureDownHeld=false;
        mFurnitureMenuOpen=true;
        refreshFurnitureQuickMenu();
        mFurnitureMenuPanel->setVisible(true);
    }

    void ActionManager::finishFurnitureQuickMenu(int chosen)
    {
        if (!mFurnitureMenuOpen) return;
        mFurnitureMenuOpen=false;
        if (mFurnitureMenuPanel) mFurnitureMenuPanel->setVisible(false);
        const MWWorld::Ptr target=mFurnitureMenuObject;
        mFurnitureMenuObject=MWWorld::Ptr();
        if (!MWBase::Environment::get().getWorld()->canPhysicsGrab(target)) return;
        const std::string index=std::to_string(target.getCellRef().getRefNum().mIndex)
            +"-"+std::to_string(target.getCellRef().getMpNum());
        auto& housing=mwmp::ArenaHousing::get();
        if (mFurniturePortableMenu)
        {
            const char* verbs[] = {"MOVE", "PRICE", "CLEAR", "TAKE", housing.itemLocked(target) ? "UNLOCK" : "LOCK"};
            if (chosen >= 0 && chosen < 5) housing.requestHouseItem(verbs[chosen], target);
            return;
        }
        switch (chosen)
        {
            case 0: mFurnitureResumeGrab=true;mFurnitureMenuObject=target;break;
            case 1:
                if (target.getClass().showsInInventory(target))
                    housing.sendFurnitureAction("TAKE_WORLD",target.getCellRef().getRefId()+"|"+index);
                break;
            case 2:
                // HS1: deleting is permanent, so the server first asks "Delete
                // for good?" and only then deletes; the ObjectDelete it sends
                // removes the object here and for everyone. No optimistic local
                // hide any more - a cancelled dialog must leave the object be.
                housing.sendFurnitureAction("DELETE_WORLD",target.getCellRef().getRefId()+"|"+index);
                break;
            default:break;
        }
    }

    void ActionManager::update(float dt, bool triedToMove)
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        const bool guiMode = MWBase::Environment::get().getWindowManager()->isGuiMode();
        const bool gameRunning = MWBase::Environment::get().getStateManager()->getState()
            == MWBase::StateManager::State_Running;

        const int quickActions[]={A_QuickKey1,A_QuickKey2,A_QuickKey3,A_QuickKey4,A_QuickKey5,
            A_QuickKey6,A_QuickKey7,A_QuickKey8,A_QuickKey9,A_QuickKey10};
        for (int col=0; col<10; ++col) {
            auto& gesture=mQuickGestures[col];
            if (!gameRunning || guiMode || world->isPhysicsGrabActive() || !checkAllowedToUseItems()
                || !MWBase::Environment::get().getInputManager()->getControlSwitch("playercontrols")) {
                gesture.cancel(); continue;
            }
            const auto result=gesture.update(dt,mBindingsManager->actionIsActive(quickActions[col]));
            if (result==MWGui::QuickBarRules::Gesture::Tap) quickKey(col+1);
            else if (result==MWGui::QuickBarRules::Gesture::Hold) {
                if (world->getGlobalFloat("chargenstate")==-1)
                    MWBase::Environment::get().getWindowManager()->editQuickKey(col+1);
            }
        }

        if (gameRunning)
        {
            const MWWorld::Ptr player = world->getPlayerPtr();
            const bool allowed = !guiMode && !world->isPhysicsGrabActive() && !mFurnitureMenuOpen
                && MWBase::Environment::get().getInputManager()->getControlSwitch("playercontrols")
                && MWBase::Environment::get().getInputManager()->getControlSwitch("playerfighting");
            const bool down = mBindingsManager->actionIsActive(A_ToggleWeapon);
            if (!allowed)
            {
                mGuardKeyPending = false;
                mGuardKeyConsumed = true;
                MWMechanics::ActiveGuard::request(player, false);
            }
            else if (mGuardKeyPending && !MWMechanics::ActiveGuard::canGuardWithEquipment(player))
            {
                MWMechanics::ActiveGuard::request(player, false);
                mGuardKeyPending = false;
                mGuardKeyConsumed = true;
            }
            else if (mGuardKeyPending)
            {
                mGuardKeyTime += std::max(0.f, dt);
                if (!mGuardKeyConsumed && mGuardKeyTime >= .30f && down
                    && !MWBase::Environment::get().getMechanicsManager()->isAttackingOrSpell(player))
                {
                    MWMechanics::ActiveGuard::request(player, true);
                    // Y068: F is a tap-to-sheathe / hold-to-guard gesture. Do not
                    // consume the tap merely because the timer elapsed: during a
                    // third-person equip transition guard cannot actually rise.
                    // Mark it consumed only after ActiveGuard really engaged.
                    if (MWMechanics::ActiveGuard::raised(player))
                        mGuardKeyConsumed = true;
                }
                if (!down)
                {
                    MWMechanics::ActiveGuard::request(player, false);
                    mGuardKeyPending = false;
                    if (!mGuardKeyConsumed) toggleWeapon();
                }
            }
            else if (!down) MWMechanics::ActiveGuard::request(player, false);
        }
        else { mGuardKeyPending = false; mGuardKeyConsumed = true; }

        // R / Android magic button: tap casts, hold selects vanilla spell stance.
        const bool spellAllowed = gameRunning && !guiMode && !world->isPhysicsGrabActive()
            && !mFurnitureMenuOpen
            && MWBase::Environment::get().getInputManager()->getControlSwitch("playercontrols")
            && MWBase::Environment::get().getInputManager()->getControlSwitch("playermagic");
        if (!spellAllowed)
        {
            mSpellKeyPending = false;
            mSpellKeyConsumed = true;
            mSpellKeyTime = 0.f;
            mSpellLowerRequested = false;
        }
        else if (mSpellKeyPending)
        {
            mSpellKeyTime += std::max(0.f, dt);
            const bool down = mBindingsManager->actionIsActive(A_ToggleSpell);
            if (!mSpellKeyConsumed && down && mSpellKeyTime >= 0.45f)
            {
                mSpellKeyConsumed = true;
                prepareSpell(); // toggleSpell separates transitions from an actual cast.
            }
            if (!down)
            {
                mSpellKeyPending = false;
                mSpellKeyTime = 0.f;
                if (!mSpellKeyConsumed)
                {
                    if (quickCastLeftHandAvailable())
                    {
                        // Y065: transient equip/controller states used to swallow a
                        // short R press; retain this compatibility marker for the
                        // cumulative Android patch chain.
                        // Y070: short R quick-casts either a selected spell OR a selected
                        // enchanted item. Previously an empty selected-spell string forced
                        // every enchanted item into prepared magic even when the right-hand
                        // weapon was genuinely one-handed and the left hand was free.
                        if (!quickCastSelectedSpell())
                            prepareSpell();
                    }
                    else
                        prepareSpell(); // occupied casting hand: prepare magic instead of forcing a quick cast
                }
            }
        }

        // Y073: a second R press in prepared magic means lower hands, never
        // another quick cast. If a real cast is finishing, keep the request until
        // it ends instead of dropping the input or cancelling its release/cost.
        if (spellAllowed && mSpellLowerRequested)
        {
            if (world->getPlayer().getDrawState() == MWMechanics::DrawState_Spell)
                toggleSpell();
            if (world->getPlayer().getDrawState() != MWMechanics::DrawState_Spell)
                mSpellLowerRequested = false;
        }

        // Y070: restore classic hold-TAB third-person orbit. A short tap keeps
        // the normal POV toggle. Holding TAB for a deliberate moment enters the
        // engine's Preview mode, where MouseManager::vanityRotateCamera rotates
        // only the camera around the player. Releasing TAB restores normal camera.
        const bool povAllowed = gameRunning && !guiMode && !world->isPhysicsGrabActive()
            && MWBase::Environment::get().getInputManager()->getControlSwitch("playercontrols")
            && MWBase::Environment::get().getInputManager()->getControlSwitch("playerviewswitch");
        if (!povAllowed)
        {
            if (mPovHoldPreviewActive && !mPovPreviewWasEnabled)
                world->togglePreviewMode(false);
            mPovKeyPending = false;
            mPovKeyConsumed = false;
            mPovHoldPreviewActive = false;
            mPovPreviewWasEnabled = false;
            mPovKeyTime = 0.f;
        }
        else if (mPovKeyPending)
        {
            const bool down = mBindingsManager->actionIsActive(A_TogglePOV);
            if (down)
            {
                mPovKeyTime += std::max(0.f, dt);
                resetIdleTime();
                if (!mPovKeyConsumed && mPovKeyTime >= .25f && !world->isFirstPerson())
                {
                    mPovPreviewWasEnabled = world->isPreviewModeEnabled();
                    if (!mPovPreviewWasEnabled)
                        world->togglePreviewMode(true);
                    // A first-person upper-body transition may defer/refuse the
                    // mode change. Consume the hold only after Preview is active.
                    if (world->isPreviewModeEnabled())
                    {
                        mPovHoldPreviewActive = true;
                        mPovKeyConsumed = true;
                    }
                }
            }
            else
            {
                if (mPovHoldPreviewActive && !mPovPreviewWasEnabled)
                    world->togglePreviewMode(false);
                if (!mPovKeyConsumed)
                    world->togglePOV();
                mPovKeyPending = false;
                mPovKeyConsumed = false;
                mPovHoldPreviewActive = false;
                mPovPreviewWasEnabled = false;
                mPovKeyTime = 0.f;
            }
        }

        // A compact non-modal HUD in the same position/skin as QuickLoot.
        // W/S changes row; a new E press selects it. The initial held E cannot
        // accidentally choose an action; movement is suppressed while visible.
        if (mFurnitureMenuOpen)
        {
            refreshFurnitureQuickMenu();
            if (guiMode || !gameRunning || !world->canPhysicsGrab(mFurnitureMenuObject))
            {
                finishFurnitureQuickMenu(-1);
                return;
            }
            const bool up=mBindingsManager->actionIsActive(A_MoveForward);
            const bool down=mBindingsManager->actionIsActive(A_MoveBackward);
            if (up && !mFurnitureUpHeld) {mFurnitureSelected=(mFurnitureSelected+(mFurniturePortableMenu?5:3))%(mFurniturePortableMenu?6:4);refreshFurnitureQuickMenu();}
            if (down && !mFurnitureDownHeld) {mFurnitureSelected=(mFurnitureSelected+1)%(mFurniturePortableMenu?6:4);refreshFurnitureQuickMenu();}
            mFurnitureUpHeld=up;
            mFurnitureDownHeld=down;
            MWWorld::Player& player=world->getPlayer();
            player.setAutoMove(false);
            player.setLeftRight(0);
            player.setForwardBackward(0);
            player.setUpDown(0);
            return;
        }

        if (mFurnitureResumeGrab && !guiMode && gameRunning)
        {
            mFurnitureResumeGrab = false;
            if (!mFurnitureMenuObject.isEmpty() && world->getFacedObject() == mFurnitureMenuObject)
                world->beginPhysicsGrab(mFurnitureMenuObject);
            mFurnitureMenuObject = MWWorld::Ptr();
        }

        // Activate is gesture-sensitive only for managed doors and grabbable decor.
        // A short press must remain completely vanilla (open/use/take). Holding the
        // same binding manipulates inventory items directly; furniture opens its
        // action chooser. Both paths require home editing or staff rights.
        if (mActivateHoldPending)
        {
            const bool stillHeld = mBindingsManager->actionIsActive(A_Activate);
            if (!gameRunning || guiMode)
            {
                if (world->isPhysicsGrabActive())
                    world->finishPhysicsGrab();
                mActivateHoldObject = MWWorld::Ptr();
                mActivateHoldTime = 0.f;
                mActivateHoldPending = false;
                mActivateHoldIsDoor = false;
                mFurnitureHoldGrabbed = false;
            }
            // Z001: the same press-and-hold gesture that grabs a prop buys a
            // house when it lands on the door of a vacant one. A short tap is
            // still an ordinary activation, so nobody has to learn a new key -
            // on Android this is the Take button, held.
            else if (mActivateHoldIsDoor)
            {
                if (stillHeld)
                {
                    mActivateHoldTime += dt;
                    if (mActivateHoldTime >= 0.6f)
                    {
                        const MWWorld::Ptr facedNow = world->getFacedObject();
                        if (!mActivateHoldObject.isEmpty() && facedNow == mActivateHoldObject)
                            mwmp::ArenaHousing::get().requestForDoor(mActivateHoldObject, true);
                        mActivateHoldObject = MWWorld::Ptr();
                        mActivateHoldTime = 0.f;
                        mActivateHoldPending = false;
                        mActivateHoldIsDoor = false;
                    }
                }
                else
                {
                    // Released early: this was a tap, so let the door open the
                    // way it normally would.
                    activate();
                    mActivateHoldObject = MWWorld::Ptr();
                    mActivateHoldTime = 0.f;
                    mActivateHoldPending = false;
                    mActivateHoldIsDoor = false;
                }
            }
            else if (stillHeld)
            {
                mActivateHoldTime += dt;
                if (mActivateHoldTime >= 0.45f)
                {
                    // Property items use the action chooser; other loose items
                    // retain direct manipulation. Recheck rights after the hold.
                    const MWWorld::Ptr target = mActivateHoldObject;
                    const MWWorld::Ptr facedNow = world->getFacedObject();
                    mActivateHoldObject = MWWorld::Ptr();
                    mActivateHoldTime = 0.f;
                    mActivateHoldPending = false;
                    mActivateHoldIsDoor = false;
                    mFurnitureHoldGrabbed = false;
                    if (!target.isEmpty() && target.isInCell() && facedNow == target
                        && world->canPhysicsGrab(target))
                    {
                        MWBase::Environment::get().getWindowManager()->setPhysicsGrabHint(false, 0, false);
                        if (target.getTypeName() == typeid(ESM::Book).name()
                            && !mwmp::ArenaHousing::get().ownsPlace(target))
                            world->getPlayer().activate();
                        else if (mwmp::ArenaHousing::get().isManagedItem(target)
                            && !mwmp::ArenaHousing::get().ownsPlace(target))
                            mwmp::ArenaHousing::get().requestHouseItem("ACTIVATE", target);
                        else if (opensFurnitureActionMenu(target) || mwmp::ArenaHousing::get().isManagedItem(target))
                            showFurnitureQuickMenu(target);
                        else
                            world->beginPhysicsGrab(target);
                    }
                }
            }
            else
            {
                // Short E is ordinary TES/OpenMW activation: open a chest, use a
                // bed/activator or take a loose inventory item. Do not convert a
                // normal tap into decoration placement.
                const MWWorld::Ptr target = mActivateHoldObject;
                if (!target.isEmpty() && world->getFacedObject() == target)
                    activate();
                mFurnitureHoldGrabbed = false;
                mActivateHoldObject = MWWorld::Ptr();
                mActivateHoldTime = 0.f;
                mActivateHoldPending = false;
                mActivateHoldIsDoor = false;
            }
        }

        // Placement is a gameplay-only state. Entering a GUI/pause transition
        // commits the current kinematic transform and removes the persistent hint.
        if (guiMode || !gameRunning)
        {
            if (world->isPhysicsGrabActive())
                world->finishPhysicsGrab();
            MWBase::Environment::get().getWindowManager()->setPhysicsGrabHint(false, 0, false);
            mAttemptJump = false;
            return;
        }

        const bool physicsGrabActive = world->isPhysicsGrabActive();
        const int physicsGrabMoveMode = physicsGrabActive ? world->getPhysicsGrabMoveMode() : 0;
        if (physicsGrabMoveMode != 0)
            triedToMove = false;

        if (physicsGrabActive)
        {
            // Rotate only while Ready Weapon is physically held. Releasing the
            // key/button stops immediately; no mouse-capture mode is latched.
            if (mBindingsManager->actionIsActive(A_ToggleWeapon))
                world->rotatePhysicsGrab(1.f, 0.f, dt);

            if (physicsGrabMoveMode != 0)
            {
                const float digitalFirst =
                    (mBindingsManager->actionIsActive(A_MoveRight) ? 1.f : 0.f)
                    - (mBindingsManager->actionIsActive(A_MoveLeft) ? 1.f : 0.f);
                const float digitalSecond =
                    (mBindingsManager->actionIsActive(A_MoveForward) ? 1.f : 0.f)
                    - (mBindingsManager->actionIsActive(A_MoveBackward) ? 1.f : 0.f);
                const float analogFirst =
                    (mBindingsManager->getActionValue(A_MoveLeftRight) - 0.5f) * 2.f;
                const float analogSecond =
                    (0.5f - mBindingsManager->getActionValue(A_MoveForwardBackward)) * 2.f;
                float firstAxis = std::max(-1.f, std::min(1.f, digitalFirst + analogFirst));
                float secondAxis = std::max(-1.f, std::min(1.f, digitalSecond + analogSecond));
                if (mBindingsManager->actionIsActive(A_Run))
                {
                    firstAxis *= 0.25f;
                    secondAxis *= 0.25f;
                }
                world->translatePhysicsGrab(firstAxis, secondAxis, dt);
            }
        }

        // A borderless dark help panel remains visible for the entire grab.
        MWBase::Environment::get().getWindowManager()->setPhysicsGrabHint(
            physicsGrabActive, physicsGrabMoveMode,
            physicsGrabActive && world->isPhysicsGrabPhysicsEnabled());

        // QuickLoot is shown only while the player is looking at a non-empty container.
        // Keep W/S state tracked by the binding system, but suppress forward/backward
        // movement for exactly that visible interval. This also handles the case where
        // W or S was already held before the overlay appeared and resumes movement as
        // soon as the player looks away and the overlay closes.
        const bool blockForwardBackward =
            MWBase::Environment::get().getWindowManager()->isQuickLootVisible();

        // Configure player movement according to keyboard input. Actual movement will
        // be done in the physics system.
        if (MWBase::Environment::get().getInputManager()->getControlSwitch("playercontrols"))
        {
            bool alwaysRunAllowed = false;

            MWWorld::Player& player = MWBase::Environment::get().getWorld()->getPlayer();

            if (physicsGrabMoveMode != 0)
            {
                // In an axis-placement mode the normal walking channels are
                // repurposed exclusively for the held object.
                player.setAutoMove(false);
                player.setLeftRight(0);
                player.setForwardBackward(0);
                player.setUpDown(0);
            }
            else if (mBindingsManager->actionIsActive(A_MoveLeft) != mBindingsManager->actionIsActive(A_MoveRight))
            {
                alwaysRunAllowed = true;
                triedToMove = true;
                player.setLeftRight(mBindingsManager->actionIsActive(A_MoveRight) ? 1 : -1);
            }

            if (physicsGrabMoveMode != 0)
            {
                player.setForwardBackward(0);
            }
            else if (blockForwardBackward)
            {
                // Do not let W/S move the player while QuickLoot is visible.
                player.setForwardBackward(0);
            }
            else if (mBindingsManager->actionIsActive(A_MoveForward) != mBindingsManager->actionIsActive(A_MoveBackward))
            {
                alwaysRunAllowed = true;
                triedToMove = true;
                player.setAutoMove (false);
                player.setForwardBackward(mBindingsManager->actionIsActive(A_MoveForward) ? 1 : -1);
            }

            if (physicsGrabMoveMode == 0 && player.getAutoMove())
            {
                alwaysRunAllowed = true;
                triedToMove = true;
                player.setForwardBackward (1);
            }

            if (mAttemptJump && MWBase::Environment::get().getInputManager()->getControlSwitch("playerjumping"))
            {
                // Jump is suppressed in every placement submode, including Walk,
                // so controller/alternate bindings cannot move the player while editing.
                if (!physicsGrabActive)
                {
                    player.setUpDown(1);
                    triedToMove = true;
                    mOverencumberedMessageDelay = 0.f;
                }
                mAttemptJump = false;
            }

            // if player tried to start moving, but can't (due to being overencumbered), display a notification.
            if (triedToMove)
            {
                MWWorld::Ptr playerPtr = MWBase::Environment::get().getWorld ()->getPlayerPtr();
                mOverencumberedMessageDelay -= dt;
                if (playerPtr.getClass().getEncumbrance(playerPtr) > playerPtr.getClass().getCapacity(playerPtr))
                {
                    player.setAutoMove (false);
                    if (mOverencumberedMessageDelay <= 0)
                    {
                        MWBase::Environment::get().getWindowManager()->messageBox("#{sNotifyMessage59}");
                        mOverencumberedMessageDelay = 1.0;
                    }
                }
            }

            if (triedToMove)
                MWBase::Environment::get().getInputManager()->resetIdleTime();

            static const bool isToggleSneak = Settings::Manager::getBool("toggle sneak", "Input");
            if (!isToggleSneak && !physicsGrabActive)
            {
                if(!MWBase::Environment::get().getInputManager()->joystickLastUsed())
                    player.setSneak(mBindingsManager->actionIsActive(A_Sneak));
            }

            float xAxis = mBindingsManager->getActionValue(A_MoveLeftRight);
            float yAxis = mBindingsManager->getActionValue(A_MoveForwardBackward);
            bool isRunning = osg::Vec2f(xAxis * 2 - 1, yAxis * 2 - 1).length2() > 0.25f;
            if ((mAlwaysRunActive && alwaysRunAllowed) || isRunning)
                player.setRunState(!mBindingsManager->actionIsActive(A_Run));
            else
                player.setRunState(mBindingsManager->actionIsActive(A_Run));
        }

        if ((!blockForwardBackward && (mBindingsManager->actionIsActive(A_MoveForward) ||
                mBindingsManager->actionIsActive(A_MoveBackward))) ||
            mBindingsManager->actionIsActive(A_MoveLeft) ||
            mBindingsManager->actionIsActive(A_MoveRight) ||
            mBindingsManager->actionIsActive(A_Jump) ||
            mBindingsManager->actionIsActive(A_Sneak) ||
            mBindingsManager->actionIsActive(A_TogglePOV) ||
            mBindingsManager->actionIsActive(A_ZoomIn) ||
            mBindingsManager->actionIsActive(A_ZoomOut))
        {
            resetIdleTime();
        }
        else
        {
            updateIdleTime(dt);
        }

        mAttemptJump = false;
    }
    
    bool ActionManager::isPreviewModeEnabled()
    {
        return MWBase::Environment::get().getWorld()->isPreviewModeEnabled();
    }

    void ActionManager::resetIdleTime()
    {
        if (mTimeIdle < 0)
            MWBase::Environment::get().getWorld()->toggleVanityMode(false);
        mTimeIdle = 0.f;
    }

    void ActionManager::updateIdleTime(float dt)
    {
        static const float vanityDelay = MWBase::Environment::get().getWorld()->getStore().get<ESM::GameSetting>()
                .find("fVanityDelay")->mValue.getFloat();
        if (mTimeIdle >= 0.f)
            mTimeIdle += dt;
        if (mTimeIdle > vanityDelay)
        {
            MWBase::Environment::get().getWorld()->toggleVanityMode(true);
            mTimeIdle = -1.f;
        }
    }

    void ActionManager::executeAction(int action)
    {
        auto* inputManager = MWBase::Environment::get().getInputManager();
        auto* windowManager = MWBase::Environment::get().getWindowManager();
        // trigger action activated
        switch (action)
        {
        case A_GameMenu:
            if (mFurnitureMenuOpen) {finishFurnitureQuickMenu(-1);break;}
            toggleMainMenu ();
            break;
        case A_Screenshot:
            screenshot();
            break;
        case A_Inventory:
            if (MWBase::Environment::get().getWorld()->isPhysicsGrabActive())
            {
                MWBase::Environment::get().getWorld()->cancelPhysicsGrab();
                mActivateHoldObject = MWWorld::Ptr();
                mActivateHoldTime = 0.f;
                mActivateHoldPending = false;
                windowManager->setPhysicsGrabHint(false, 0, false);
            }
            else
                toggleInventory ();
            break;
        case A_Console:
            toggleConsole ();
            break;
        case A_Activate:
            inputManager->resetIdleTime();
            if (mFurnitureMenuOpen) {finishFurnitureQuickMenu(mFurnitureSelected);break;}
            if (mActivateHoldPending) break; // Repeated down events never restart the hold timer.
            if (!windowManager->isGuiMode() && inputManager->getControlSwitch("playercontrols"))
            {
                MWBase::World* world = MWBase::Environment::get().getWorld();

                // A second press of the same Take/Activate action ends placement.
                // Physics ON releases a live prop; Physics OFF freezes the exact
                // arranged transform. It never also activates the object underneath.
                if (world->isPhysicsGrabActive())
                {
                    world->finishPhysicsGrab();
                    mActivateHoldObject = MWWorld::Ptr();
                    mActivateHoldTime = 0.f;
                    mActivateHoldPending = false;
                    mFurnitureHoldGrabbed = false;
                    windowManager->setPhysicsGrabHint(false, 0, false);
                    break;
                }

                MWWorld::Ptr faced = world->getFacedObject();
                // FC1: large or distant decorative pieces are out of ordinary
                // activation reach; capture mode looks further.
                if (faced.isEmpty() && mwmp::ArenaHousing::get().isFurnitureCapturePending())
                    faced = mwmp::ArenaHousing::get().findFurnitureCaptureTarget();
                // Only after a deliberate Admin > Decoration > Capture action.
                // Server independently checks staff rank and real active record.
                if (mwmp::ArenaHousing::get().consumeFurnitureCapture(faced)) break;
                if (!faced.isEmpty() && world->canPhysicsGrab(faced))
                {
                    // Do not consume normal activation on key-down. Wait for the
                    // release/hold decision in update(): tap = vanilla activation,
                    // hold = direct item manipulation or furniture actions.
                    // The world permission check restricts both to home editors/staff.
                    mActivateHoldObject = faced;
                    mActivateHoldTime = 0.f;
                    mActivateHoldPending = true;
                    mActivateHoldIsDoor = false;
                    mFurnitureHoldGrabbed = false;
                    break;
                }

                // Z001: a door leading to registered property also answers to a
                // hold. The activation itself is deferred until we know which
                // gesture this was, so a tap still opens the door immediately.
                if (!faced.isEmpty() && mwmp::ArenaHousing::get().isManagedDoor(faced))
                {
                    mActivateHoldObject = faced;
                    mActivateHoldTime = 0.f;
                    mActivateHoldPending = true;
                    mActivateHoldIsDoor = true;
                    break;
                }
            }
            activate();
            break;
        case A_MoveLeft:
        case A_MoveRight:
        case A_MoveForward:
        case A_MoveBackward:
            // In X-Y/X-Z/Z-Y placement modes these action channels belong to
            // the object exclusively. Do not also synthesize GUI arrow presses
            // (QuickLoot/list navigation, etc.). Walk mode keeps vanilla input.
            if (MWBase::Environment::get().getWorld()->isPhysicsGrabActive()
                && MWBase::Environment::get().getWorld()->getPhysicsGrabMoveMode() != 0)
                break;
            handleGuiArrowKey(action);
            break;
        case A_Journal:
            toggleJournal();
            break;
        case A_AutoMove:
            toggleAutoMove();
            break;
        case A_AlwaysRun:
            toggleWalking();
            break;
        case A_ToggleWeapon:
            if (MWBase::Environment::get().getWorld()->isPhysicsGrabActive())
            {
                // Continuous held-button rotation is handled in update().
            }
            else
            {
                auto* world = MWBase::Environment::get().getWorld();
                if (MWBase::Environment::get().getWindowManager()->isGuiMode()) break;
                if (world->getPlayer().getDrawState() == MWMechanics::DrawState_Weapon
                    && MWMechanics::ActiveGuard::canGuardWithEquipment(world->getPlayerPtr()))
                {
                    if (!mGuardKeyPending)
                    {
                        mGuardKeyPending = true;
                        mGuardKeyConsumed = false;
                        mGuardKeyTime = 0.f;
                    }
                }
                else toggleWeapon();
            }
            break;
        case A_Rest:
            rest();
            break;
        case A_ToggleSpell:
            if (MWBase::Environment::get().getWorld()->isPhysicsGrabActive())
            {
                // One press advances one axis: Z -> X -> Y -> Z.
                MWBase::Environment::get().getWorld()->cyclePhysicsGrabRotationAxis();
            }
            else if (!mSpellKeyPending)
            {
                mSpellKeyPending = true;
                mSpellLowerRequested = MWBase::Environment::get().getWorld()->getPlayer().getDrawState()
                    == MWMechanics::DrawState_Spell;
                mSpellKeyConsumed = mSpellLowerRequested;
                mSpellKeyTime = 0.f;
            }
            break;
        case A_QuickKey1:
            if (!windowManager->isGuiMode() && !SDL_IsTextInputActive()) mQuickGestures[0].press();
            break;
        case A_QuickKey2:
            if (!windowManager->isGuiMode() && !SDL_IsTextInputActive()) mQuickGestures[1].press();
            break;
        case A_QuickKey3:
            if (!windowManager->isGuiMode() && !SDL_IsTextInputActive()) mQuickGestures[2].press();
            break;
        case A_QuickKey4:
            if (!windowManager->isGuiMode() && !SDL_IsTextInputActive()) mQuickGestures[3].press();
            break;
        case A_QuickKey5:
            if (!windowManager->isGuiMode() && !SDL_IsTextInputActive()) mQuickGestures[4].press();
            break;
        case A_QuickKey6:
            if (!windowManager->isGuiMode() && !SDL_IsTextInputActive()) mQuickGestures[5].press();
            break;
        case A_QuickKey7:
            if (!windowManager->isGuiMode() && !SDL_IsTextInputActive()) mQuickGestures[6].press();
            break;
        case A_QuickKey8:
            if (!windowManager->isGuiMode() && !SDL_IsTextInputActive()) mQuickGestures[7].press();
            break;
        case A_QuickKey9:
            if (!windowManager->isGuiMode() && !SDL_IsTextInputActive()) mQuickGestures[8].press();
            break;
        case A_QuickKey10:
            if (!windowManager->isGuiMode() && !SDL_IsTextInputActive()) mQuickGestures[9].press();
            break;
        case A_QuickKeysMenu:
            showQuickKeysMenu();
            break;
        case A_PlayerAnimationMenu:
            showPlayerAnimationMenu();
            break;
        case A_ToggleHUD:
            windowManager->toggleHud();
            break;
        case A_ToggleDebug:
            windowManager->toggleDebugWindow();
            break;
        case A_ZoomIn:
            if (inputManager->getControlSwitch("playerviewswitch") && inputManager->getControlSwitch("playercontrols") && !windowManager->isGuiMode())
                MWBase::Environment::get().getWorld()->adjustCameraDistance(-ZOOM_SCALE);
            break;
        case A_ZoomOut:
            if (inputManager->getControlSwitch("playerviewswitch") && inputManager->getControlSwitch("playercontrols") && !windowManager->isGuiMode())
                MWBase::Environment::get().getWorld()->adjustCameraDistance(ZOOM_SCALE);
            break;
        case A_QuickSave:
            quickSave();
            break;
        case A_QuickLoad:
            quickLoad();
            break;
        case A_CycleSpellLeft:
            if (checkAllowedToUseItems() && windowManager->isAllowed(MWGui::GW_Magic))
                MWBase::Environment::get().getWindowManager()->cycleSpell(false);
            break;
        case A_CycleSpellRight:
            if (checkAllowedToUseItems() && windowManager->isAllowed(MWGui::GW_Magic))
                MWBase::Environment::get().getWindowManager()->cycleSpell(true);
            break;
        case A_CycleWeaponLeft:
            if (checkAllowedToUseItems() && windowManager->isAllowed(MWGui::GW_Inventory))
                MWBase::Environment::get().getWindowManager()->cycleWeapon(false);
            break;
        case A_CycleWeaponRight:
            if (checkAllowedToUseItems() && windowManager->isAllowed(MWGui::GW_Inventory))
                MWBase::Environment::get().getWindowManager()->cycleWeapon(true);
            break;
        case A_Sneak:
            if (MWBase::Environment::get().getWorld()->isPhysicsGrabActive())
                MWBase::Environment::get().getWorld()->resetPhysicsGrabTransform();
            else
            {
                static const bool isToggleSneak = Settings::Manager::getBool("toggle sneak", "Input");
                if (isToggleSneak)
                    toggleSneaking();
            }
            break;
        case A_Jump:
            // Placement is always kinematic in X006. Jump is simply consumed here
            // while an object is held; ordinary jumping remains handled by the
            // existing movement path outside placement.
            break;
        case A_TogglePOV:
            if (MWBase::Environment::get().getWorld()->isPhysicsGrabActive())
                MWBase::Environment::get().getWorld()->cyclePhysicsGrabMoveMode();
            else if (inputManager->getControlSwitch("playerviewswitch")
                && inputManager->getControlSwitch("playercontrols")
                && !windowManager->isGuiMode() && !mPovKeyPending)
            {
                // Y070: defer the decision until release. Tap = normal POV toggle;
                // hold in third person = temporary free orbit/Preview mode.
                mPovKeyPending = true;
                mPovKeyConsumed = false;
                mPovHoldPreviewActive = false;
                mPovPreviewWasEnabled = false;
                mPovKeyTime = 0.f;
            }
            break;
        }
    }

    bool ActionManager::checkAllowedToUseItems() const
    {
        MWWorld::Ptr player = MWMechanics::getPlayer();
        if (player.getClass().getNpcStats(player).isWerewolf())
        {
            // Cannot use items or spells while in werewolf form
            MWBase::Environment::get().getWindowManager()->messageBox("#{sWerewolfRefusal}");
            return false;
        }
        return true;
    }

    void ActionManager::screenshot()
    {
        const std::string& settingStr = Settings::Manager::getString("screenshot type", "Video");
        bool regularScreenshot = settingStr.size() == 0 || settingStr.compare("regular") == 0;

        if (regularScreenshot)
        {
            mScreenCaptureHandler->setFramesToCapture(1);
            mScreenCaptureHandler->captureNextFrame(*mViewer);
        }
        else
        {
            osg::ref_ptr<osg::Image> screenshot (new osg::Image);

            if (MWBase::Environment::get().getWorld()->screenshot360(screenshot.get()))
            {
                (*mScreenCaptureOperation) (*(screenshot.get()), 0);
                // FIXME: mScreenCaptureHandler->getCaptureOperation() causes crash for some reason
            }
        }
    }

    void ActionManager::toggleMainMenu()
    {
        /*
            Start  of tes3mp addition

            Don't allow the main menu to be toggled while TES3MP listboxes are open
        */
        if (MWBase::Environment::get().getWindowManager()->getMode() == mwmp::GUIController::GM_TES3MP_ListBox)
        {
            return;
        }
        /*
            End of tes3mp addition
        */

        if (MyGUI::InputManager::getInstance().isModalAny())
        {
            MWBase::Environment::get().getWindowManager()->exitCurrentModal();
            return;
        }

        if (MWBase::Environment::get().getWindowManager()->isConsoleMode())
        {
            MWBase::Environment::get().getWindowManager()->toggleConsole();
            return;
        }

        if (!MWBase::Environment::get().getWindowManager()->isGuiMode()) //No open GUIs, open up the MainMenu
        {
            MWBase::Environment::get().getWindowManager()->pushGuiMode (MWGui::GM_MainMenu);
        }
        else //Close current GUI
        {
            MWBase::Environment::get().getWindowManager()->exitCurrentGuiMode();
        }
    }

    bool ActionManager::quickCastLeftHandAvailable() const
    {
        const MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
        if (player.isEmpty() || !player.getClass().hasInventoryStore(player))
            return true;

        MWWorld::InventoryStore& inventory = player.getClass().getInventoryStore(player);
        if (inventory.getSlot(MWWorld::InventoryStore::Slot_CarriedLeft) != inventory.end())
            return false;

        // A drawn two-handed weapon occupies the left hand even though TES3 stores it
        // only in Slot_CarriedRight. Sheathed weapons do not block a left-hand flick.
        if (player.getClass().getCreatureStats(player).getDrawState() == MWMechanics::DrawState_Weapon)
        {
            MWWorld::ContainerStoreIterator right = inventory.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
            if (right != inventory.end() && right->getTypeName() == typeid(ESM::Weapon).name())
            {
                const ESM::WeaponType* weaponType
                    = MWMechanics::getWeaponType(right->get<ESM::Weapon>()->mBase->mData.mType);
                if (weaponType && (weaponType->mFlags & ESM::WeaponType::TwoHanded))
                    return false;
            }
        }
        return true;
    }

    bool ActionManager::quickCastSelectedSpell()
    {
        if (MWBase::Environment::get().getWindowManager()->isGuiMode())
            return false;
        if (!MWBase::Environment::get().getInputManager()->getControlSwitch("playermagic")
            || !MWBase::Environment::get().getInputManager()->getControlSwitch("playercontrols"))
            return false;
        if (!checkAllowedToUseItems())
            return false;

        MWBase::World* world = MWBase::Environment::get().getWorld();
        const MWWorld::Ptr player = world->getPlayerPtr();
        const std::string spellId = MWBase::Environment::get().getWindowManager()->getSelectedSpell();
        if (spellId.empty())
        {
            // Y070: an enchanted item is a valid quick-cast source too. Keep the
            // string empty; CharacterController resolves the exact selected item
            // and uses the ordinary Cast::ITEM path on the release key.
            if (!player.getClass().hasInventoryStore(player))
                return false;
            MWWorld::InventoryStore& inventory = player.getClass().getInventoryStore(player);
            if (inventory.getSelectedEnchantItem() == inventory.end())
                return false;
        }

        // Y065: do not pre-reject on the broad isAttackingOrSpell() flag here.
        // It also covers harmless equip/unequip transitions and caused short-R
        // presses to disappear. CharacterController performs the authoritative
        // attack/stagger/guard checks and can normalize those transient states.
        return MWBase::Environment::get().getMechanicsManager()->startQuickCast(player, spellId, true);
    }

    void ActionManager::prepareSpell()
    {
        MWWorld::Player& player = MWBase::Environment::get().getWorld()->getPlayer();
        if (player.getDrawState() != MWMechanics::DrawState_Spell)
            toggleSpell();
    }

    void ActionManager::toggleSpell()
    {
        if (MWBase::Environment::get().getWindowManager()->isGuiMode()) return;
        if (!MWBase::Environment::get().getInputManager()->getControlSwitch("playermagic")
            || !MWBase::Environment::get().getInputManager()->getControlSwitch("playercontrols"))
            return;

        MWWorld::Player& player = MWBase::Environment::get().getWorld()->getPlayer();
        MWBase::MechanicsManager* mechanics = MWBase::Environment::get().getMechanicsManager();
        const bool transition = mechanics->isWeaponTransition(player.getPlayer());
        if (mechanics->isAttackingOrSpell(player.getPlayer()) && !transition)
            return; // Preserve real casts/attacks; the input layer retries lowering.

        if (transition)
            mechanics->setAttackingOrSpell(player.getPlayer(), false);

        // Lowering is always possible even after a selected spell/enchantment
        // was removed or used up. Pure equip/unequip may be reversed like F.
        if (player.getDrawState() == MWMechanics::DrawState_Spell)
        {
            player.setDrawState(MWMechanics::DrawState_Nothing);
            return;
        }

        if (!checkAllowedToUseItems()) return;
        MWWorld::InventoryStore& inventory = player.getPlayer().getClass().getInventoryStore(player.getPlayer());
        if (MWBase::Environment::get().getWindowManager()->getSelectedSpell().empty()
            && inventory.getSelectedEnchantItem() == inventory.end())
            return;
        player.setDrawState(MWMechanics::DrawState_Spell);
    }

    void ActionManager::quickLoad()
    {
        if (!MyGUI::InputManager::getInstance().isModalAny())
            MWBase::Environment::get().getStateManager()->quickLoad();
    }

    void ActionManager::quickSave()
    {
        if (!MyGUI::InputManager::getInstance().isModalAny())
            MWBase::Environment::get().getStateManager()->quickSave();
    }

    void ActionManager::toggleWeapon()
    {
        if (MWBase::Environment::get().getWindowManager()->isGuiMode()) return;

        // Not allowed before the inventory window is accessible
        if (!MWBase::Environment::get().getInputManager()->getControlSwitch("playerfighting") || !MWBase::Environment::get().getInputManager()->getControlSwitch("playercontrols"))
            return;

        MWWorld::Player& player = MWBase::Environment::get().getWorld()->getPlayer();
        // We want to interrupt animation only if attack is preparing, but still is not triggered
        // Otherwise we will get a "speedshooting" exploit, when player can skip reload animation by hitting "Toggle Weapon" key twice
        MWBase::MechanicsManager* mechanics = MWBase::Environment::get().getMechanicsManager();
        const bool weaponTransition = mechanics->isWeaponTransition(player.getPlayer());
        if (mechanics->isAttackPreparing(player.getPlayer()))
            player.setAttackingOrSpell(false);
        else if (mechanics->isAttackingOrSpell(player.getPlayer()) && !weaponTransition)
            return;

        // Y068: allow F to reverse/finish a pure equip/unequip transition.
        // This fixes the third-person case where the long visual draw animation
        // made sheath input disappear, while real attack/reload/cast states
        // remain protected by the check above.
        if (weaponTransition)
            mechanics->setAttackingOrSpell(player.getPlayer(), false);

        MWMechanics::DrawState_ state = player.getDrawState();
        if (state == MWMechanics::DrawState_Spell || state == MWMechanics::DrawState_Nothing)
            player.setDrawState(MWMechanics::DrawState_Weapon);
        else
            player.setDrawState(MWMechanics::DrawState_Nothing);
    }

    void ActionManager::rest()
    {
        if (!MWBase::Environment::get().getInputManager()->getControlSwitch("playercontrols"))
            return;

        if (!MWBase::Environment::get().getWindowManager()->getRestEnabled() || MWBase::Environment::get().getWindowManager()->isGuiMode())
            return;

        /*
            Start of tes3mp addition

            Ignore attempts to rest if the player has not logged in on the server yet

            Set LocalPlayer's isUsingBed to be able to distinguish bed use from regular rest
            menu use
        */
        if (!mwmp::Main::get().getLocalPlayer()->isLoggedIn())
            return;

        mwmp::Main::get().getLocalPlayer()->isUsingBed = false;
        /*
            End of tes3mp addition
        */

        MWBase::Environment::get().getWindowManager()->pushGuiMode(MWGui::GM_Rest); //Open rest GUI
    }

    void ActionManager::toggleInventory()
    {
        if (!MWBase::Environment::get().getInputManager()->getControlSwitch("playercontrols"))
            return;

        if (MyGUI::InputManager::getInstance().isModalAny())
            return;

        if (MWBase::Environment::get().getWindowManager()->isConsoleMode())
            return;

        /*
            Start of tes3mp addition

            Ignore attempts to open inventory if the player has not logged in on the server yet
        */
        if (!mwmp::Main::get().getLocalPlayer()->isLoggedIn())
            return;
        /*
            End of tes3mp addition
        */

        // Toggle between game mode and inventory mode
        if(!MWBase::Environment::get().getWindowManager()->isGuiMode())
            MWBase::Environment::get().getWindowManager()->pushGuiMode(MWGui::GM_Inventory);
        else
        {
            MWGui::GuiMode mode = MWBase::Environment::get().getWindowManager()->getMode();
            if(mode == MWGui::GM_Inventory || mode == MWGui::GM_Container)
                MWBase::Environment::get().getWindowManager()->popGuiMode();
        }

        // .. but don't touch any other mode, except container.
    }

    void ActionManager::toggleConsole()
    {
        if (MyGUI::InputManager::getInstance().isModalAny())
            return;

        /*
            Start of tes3mp addition

            If a player's console is disabled by the server, go no further
        */
        if (!mwmp::Main::get().getLocalPlayer()->consoleAllowed)
            return;
        /*
            End of tes3mp addition
        */

        MWBase::Environment::get().getWindowManager()->toggleConsole();
    }

    void ActionManager::toggleJournal()
    {
        if (!MWBase::Environment::get().getInputManager()->getControlSwitch("playercontrols"))
            return;
        if (MyGUI::InputManager::getInstance ().isModalAny())
            return;

        if (MWBase::Environment::get().getWindowManager()->getMode() != MWGui::GM_Journal
                && MWBase::Environment::get().getWindowManager()->getMode() != MWGui::GM_MainMenu
                && MWBase::Environment::get().getWindowManager()->getMode() != MWGui::GM_Settings
                && MWBase::Environment::get().getWindowManager ()->getJournalAllowed())
        {
            MWBase::Environment::get().getWindowManager()->pushGuiMode(MWGui::GM_Journal);
        }
        else if (MWBase::Environment::get().getWindowManager()->containsMode(MWGui::GM_Journal))
        {
            MWBase::Environment::get().getWindowManager()->removeGuiMode(MWGui::GM_Journal);
        }
    }

    void ActionManager::quickKey (int index)
    {
        if (!MWBase::Environment::get().getInputManager()->getControlSwitch("playercontrols") || !MWBase::Environment::get().getInputManager()->getControlSwitch("playerfighting") || !MWBase::Environment::get().getInputManager()->getControlSwitch("playermagic"))
            return;
        if (!checkAllowedToUseItems())
            return;

        if (MWBase::Environment::get().getWorld()->getGlobalFloat ("chargenstate")!=-1)
            return;

        if (!MWBase::Environment::get().getWindowManager()->isGuiMode())
            MWBase::Environment::get().getWindowManager()->activateQuickKey (index);
    }

    void ActionManager::showQuickKeysMenu()
    {
        if (!MWBase::Environment::get().getWindowManager()->isGuiMode ()
                && MWBase::Environment::get().getWorld()->getGlobalFloat ("chargenstate")==-1)
        {
            if (!checkAllowedToUseItems())
                return;

            MWBase::Environment::get().getWindowManager()->pushGuiMode (MWGui::GM_QuickKeysMenu);
        }
        else if (MWBase::Environment::get().getWindowManager()->getMode () == MWGui::GM_QuickKeysMenu)
        {
            while (MyGUI::InputManager::getInstance().isModalAny())
            { //Handle any open Modal windows
                MWBase::Environment::get().getWindowManager()->exitCurrentModal();
            }
            MWBase::Environment::get().getWindowManager()->exitCurrentGuiMode(); //And handle the actual main window
        }
    }

    void ActionManager::showPlayerAnimationMenu()
    {
        MWBase::WindowManager* windowManager = MWBase::Environment::get().getWindowManager();
        if (!windowManager->isGuiMode()
            && MWBase::Environment::get().getWorld()->getGlobalFloat("chargenstate") == -1)
        {
            windowManager->pushGuiMode(MWGui::GM_PlayerAnimationMenu);
        }
        else if (windowManager->getMode() == MWGui::GM_PlayerAnimationMenu)
        {
            windowManager->exitCurrentGuiMode();
        }
    }

    void ActionManager::activate()
    {
        if (MWBase::Environment::get().getWindowManager()->isGuiMode())
        {
            bool joystickUsed = MWBase::Environment::get().getInputManager()->joystickLastUsed();
            if (!SDL_IsTextInputActive() && !mBindingsManager->isLeftOrRightButton(A_Activate, joystickUsed))
                MWBase::Environment::get().getWindowManager()->injectKeyPress(MyGUI::KeyCode::Return, 0, false);
        }
        else if (MWBase::Environment::get().getInputManager()->getControlSwitch("playercontrols"))
        {
            const auto target = MWBase::Environment::get().getWorld()->getFacedObject();
            auto& housing = mwmp::ArenaHousing::get();
            if (!target.isEmpty() && !housing.canUseHouseUtility(target))
            {
                const bool isContainer = target.getTypeName() == typeid(ESM::Container).name();
                const std::string script = target.getClass().getScript(target);
                const std::string refId = target.getCellRef().getRefId();
                const bool isBed = target.getClass().isActivator()
                    && (script.compare(0, 3, "Bed") == 0
                        || Misc::StringUtils::lowerCase(refId).find("bed") != std::string::npos);
                if (isContainer || isBed)
                {
                    const bool ru = MWBase::Environment::get().getWindowManager()->getArenaLanguage() == "ru";
                    MWBase::Environment::get().getWindowManager()->messageBox(ru
                        ? (isBed ? "Владелец запретил пользоваться кроватями в этом доме."
                                 : "Владелец запретил открывать контейнеры в этом доме.")
                        : (isBed ? "The owner has disabled bed use in this home."
                                 : "The owner has disabled container access in this home."));
                    return;
                }
            }
            if (!target.isEmpty() && target.getTypeName() == typeid(ESM::Book).name())
            {
                MWBase::Environment::get().getWorld()->getPlayer().activate();
                return;
            }
            if (mwmp::ArenaHousing::get().isManagedItem(target))
            {
                mwmp::ArenaHousing::get().requestHouseItem("ACTIVATE", target);
                return;
            }
            // QuickLoot follows the player's actual Activate binding (keyboard, mouse or controller).
            // If it handled the action, do not also activate/open the world object.
            if (MWBase::Environment::get().getWindowManager()->activateQuickLoot())
                return;

            // H012: E is for activating or grabbing objects. Decoration is a
            // deliberate Player Menu action, not an accidental empty-space popup.
            MWWorld::Player& player = MWBase::Environment::get().getWorld()->getPlayer();
            player.activate();
        }
    }

    void ActionManager::toggleAutoMove()
    {
        if (MWBase::Environment::get().getWindowManager()->isGuiMode()) return;

        if (MWBase::Environment::get().getInputManager()->getControlSwitch("playercontrols"))
        {
            MWWorld::Player& player = MWBase::Environment::get().getWorld()->getPlayer();
            player.setAutoMove (!player.getAutoMove());
        }
    }

    void ActionManager::toggleWalking()
    {
        if (MWBase::Environment::get().getWindowManager()->isGuiMode() || SDL_IsTextInputActive()) return;
        mAlwaysRunActive = !mAlwaysRunActive;

        Settings::Manager::setBool("always run", "Input", mAlwaysRunActive);
    }

    void ActionManager::toggleSneaking()
    {
        if (MWBase::Environment::get().getWindowManager()->isGuiMode()) return;
        if (!MWBase::Environment::get().getInputManager()->getControlSwitch("playercontrols")) return;
        mSneaking = !mSneaking;
        MWWorld::Player& player = MWBase::Environment::get().getWorld()->getPlayer();
        player.setSneak(mSneaking);
    }

    void ActionManager::handleGuiArrowKey(int action)
    {
        bool joystickUsed = MWBase::Environment::get().getInputManager()->joystickLastUsed();
        // This is currently keyboard-specific code
        // TODO: see if GUI controls can be refactored into a single function
        if (joystickUsed)
            return;

        if (SDL_IsTextInputActive())
            return;

        if (mBindingsManager->isLeftOrRightButton(action, joystickUsed))
            return;

        MyGUI::KeyCode key;
        switch (action)
        {
        case A_MoveLeft:
            key = MyGUI::KeyCode::ArrowLeft;
            break;
        case A_MoveRight:
            key = MyGUI::KeyCode::ArrowRight;
            break;
        case A_MoveForward:
            key = MyGUI::KeyCode::ArrowUp;
            break;
        case A_MoveBackward:
        default:
            key = MyGUI::KeyCode::ArrowDown;
            break;
        }

        MWBase::Environment::get().getWindowManager()->injectKeyPress(key, 0, false);
    }
}
