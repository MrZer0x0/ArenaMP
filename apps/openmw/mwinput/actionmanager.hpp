#ifndef MWINPUT_ACTIONMANAGER_H
#define MWINPUT_ACTIONMANAGER_H

#include <array>
#include "../mwgui/quickbarrules.hpp"
#include <osg/ref_ptr>
#include <osgViewer/ViewerEventHandlers>

#include "../mwworld/ptr.hpp"

namespace osgViewer
{
    class Viewer;
    class ScreenCaptureHandler;
}

namespace MyGUI { class Widget; class TextBox; }

namespace MWInput
{
    class BindingsManager;

    class ActionManager
    {
    public:

        ActionManager(BindingsManager* bindingsManager,
            osgViewer::ScreenCaptureHandler::CaptureOperation* screenCaptureOperation,
            osg::ref_ptr<osgViewer::Viewer> viewer,
            osg::ref_ptr<osgViewer::ScreenCaptureHandler> screenCaptureHandler);

        void update(float dt, bool triedToMove);
        bool scrollFurnitureQuickMenu(int direction);
        bool isFurnitureActionMenuOpen() const { return mFurnitureMenuOpen; }

        void executeAction(int action);

        bool checkAllowedToUseItems() const;

        void toggleMainMenu();
        void toggleSpell();
        void toggleWeapon();
        void toggleInventory();
        void toggleConsole();
        void screenshot();
        void toggleJournal();
        void activate();
        void toggleWalking();
        void toggleSneaking();
        void toggleAutoMove();
        void rest();
        void quickLoad();
        void quickSave();

        void quickKey (int index);
        void showQuickKeysMenu();
        void showPlayerAnimationMenu();

        void resetIdleTime();

        bool isAlwaysRunActive() const { return mAlwaysRunActive; };
        bool isSneaking() const { return mSneaking; };

        void setAttemptJump(bool enabled) { mAttemptJump = enabled; }

        bool isPreviewModeEnabled();

    private:
        void handleGuiArrowKey(int action);

        void updateIdleTime(float dt);
        void showFurnitureQuickMenu(const MWWorld::Ptr& object);
        void finishFurnitureQuickMenu(int chosen);
        void refreshFurnitureQuickMenu();
        bool quickCastSelectedSpell();
        bool quickCastLeftHandAvailable() const;
        void prepareSpell();

        BindingsManager* mBindingsManager;
        osg::ref_ptr<osgViewer::Viewer> mViewer;
        osg::ref_ptr<osgViewer::ScreenCaptureHandler> mScreenCaptureHandler;
        osgViewer::ScreenCaptureHandler::CaptureOperation* mScreenCaptureOperation;

        std::array<MWGui::QuickBarRules::Gesture, 10> mQuickGestures{};

        bool mGuardKeyPending = false;
        bool mGuardKeyConsumed = false;
        float mGuardKeyTime = 0.f;

        // R is gesture-sensitive: tap quick-casts from the left hand when it is free;
        // hold prepares the vanilla spell stance. A busy left hand falls back to prepare.
        bool mSpellKeyPending = false;
        bool mSpellKeyConsumed = false;
        float mSpellKeyTime = 0.f;
        bool mSpellLowerRequested = false;

        // Y070: tap TAB toggles POV, hold TAB in third person temporarily enters
        // Preview mode so mouse/gamepad look orbits around the player.
        bool mPovKeyPending = false;
        bool mPovKeyConsumed = false;
        bool mPovHoldPreviewActive = false;
        bool mPovPreviewWasEnabled = false;
        float mPovKeyTime = 0.f;

        bool mAlwaysRunActive;
        bool mSneaking;
        bool mAttemptJump;

        float mOverencumberedMessageDelay;
        float mTimeIdle;

        MWWorld::Ptr mActivateHoldObject;
        float mActivateHoldTime;
        bool mActivateHoldPending;
        // Z001: the pending hold is on a managed housing door rather than on a
        // grabbable prop, so the threshold sends a purchase request instead of
        // entering placement mode.
        bool mActivateHoldIsDoor;
        // Compatibility state retained for hot-reload cleanup; long E now opens actions directly.
        bool mFurnitureMenuOpen;
        bool mFurnitureHoldGrabbed;
        bool mFurnitureResumeGrab;
        MWWorld::Ptr mFurnitureMenuObject;
        MyGUI::Widget* mFurnitureMenuPanel = nullptr;
        std::array<MyGUI::TextBox*, 6> mFurnitureMenuRows{};
        bool mFurniturePortableMenu = false;
        int mFurnitureSelected = 0;
        bool mFurnitureUpHeld = false;
        bool mFurnitureDownHeld = false;
    };
}
#endif
