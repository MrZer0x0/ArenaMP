#include "../mwmp/ArenaHousing.hpp"
#include "hud.hpp"

#include <MyGUI_RenderManager.h>
#include <MyGUI_ProgressBar.h>
#include <MyGUI_Button.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_LanguageManager.h>
#include <MyGUI_ImageBox.h>
#include <MyGUI_ScrollView.h>
#include <MyGUI_TextBox.h>
#include <MyGUI_TextIterator.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <utility>

/*
    Start of tes3mp addition

    Include additional headers for multiplayer purposes
*/
#include "../mwmp/Main.hpp"
#include "../mwmp/LocalPlayer.hpp"
#include "../mwmp/Networking.hpp"
#include "../mwmp/ObjectList.hpp"
#include "../mwmp/PlayerList.hpp"
#include "../mwmp/VoiceChat.hpp"
#include "../mwworld/cellstore.hpp"
/*
    End of tes3mp addition
*/

#include <components/settings/settings.hpp>
#include <components/esm/loadmgef.hpp>
#include <components/misc/stringops.hpp>
#include <components/openmw-mp/TimedLog.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwbase/mechanicsmanager.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/containerstore.hpp"

#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/activespells.hpp"
#include "../mwmechanics/npcstats.hpp"
#include "../mwmechanics/actorutil.hpp"

#include "inventorywindow.hpp"
#include "spellicons.hpp"
#include "itemmodel.hpp"
#include "draganddrop.hpp"

#include "itemwidget.hpp"

namespace
{
    // The location name temporarily replaces the clock below the compass.
    // Only the time animates: fade it out, show the location steadily, then fade time back in.
    constexpr float sCompassInfoFadeDuration = 0.35f;
    constexpr float sCompassLocationHoldDuration = 4.f;
    constexpr float sCompassInfoSequenceDuration
        = sCompassInfoFadeDuration * 2.f + sCompassLocationHoldDuration;

    std::string getWeaponSpellBoxMode()
    {
        const auto modeKey = std::make_pair(std::string("GUI"), std::string("weapon spell box mode"));
        const auto legacyKey = std::make_pair(std::string("GUI"), std::string("persistent weapon spell boxes"));
        if (Settings::Manager::mUserSettings.find(modeKey) == Settings::Manager::mUserSettings.end())
        {
            const auto legacyIt = Settings::Manager::mUserSettings.find(legacyKey);
            if (legacyIt != Settings::Manager::mUserSettings.end())
                return (legacyIt->second == "false" || legacyIt->second == "0") ? "hidden" : "transparent";
        }

        const std::string mode = Settings::Manager::getString("weapon spell box mode", "GUI");
        if (mode == "hidden" || mode == "transparent" || mode == "visible")
            return mode;
        return Settings::Manager::getBool("persistent weapon spell boxes", "GUI")
            ? "transparent" : "hidden";
    }

    std::string getResourceBarMode()
    {
        const std::string mode = Settings::Manager::getString("resource bars mode", "HUD");
        if (mode == "always" || mode == "automatic" || mode == "hidden")
            return mode;
        return Settings::Manager::getBool("auto hide resource bars", "GUI")
            ? "automatic" : "always";
    }

    std::string getNpcBarMode()
    {
        // Settings::Manager throws for keys that are absent from settings-default.cfg,
        // and this runs once per frame. Never let a missing key take the HUD down.
        std::string mode;
        try
        {
            mode = Settings::Manager::getString("npc bar mode", "HUD");
        }
        catch (const std::exception&)
        {
            mode.clear();
        }

        if (mode == "off" || mode == "combat" || mode == "hover" || mode == "both")
            return mode;

        try
        {
            return Settings::Manager::getBool("target info panel", "GUI") ? "both" : "combat";
        }
        catch (const std::exception&)
        {
            return "both";
        }
    }

    /// Exterior cells stream their neighbours in, so a fight legitimately spans more
    /// than one cell. Only interiors are a hard boundary for overhead bars.
    bool sharesCombatSpace(const MWWorld::Ptr& actor, const MWWorld::Ptr& player)
    {
        if (!actor.isInCell() || !player.isInCell())
            return false;
        if (actor.getCell() == player.getCell())
            return true;
        return actor.getCell()->isExterior() && player.getCell()->isExterior();
    }

    // X024/X025 combat bar presentation constants.
    //
    // X025 replaced the X024 "lowered panel" behaviour with a docked stack above
    // the stamina bar; the sLowered*/sFront* constants are gone with it.
    //
    // The bar used to be placed straight from getObjectScreenBounds every frame.
    // That box is animated, so a running NPC bobbed the bar by several pixels per
    // stride, and every "not visible right now" branch hid the widget outright,
    // which read as a hard blink whenever line of sight or the 10 Hz participant
    // scan flickered. Both are now filtered.
    namespace CombatBar
    {
        // Horizontal tracking has to stay tight or the bar lags behind a strafing
        // target; vertical is filtered much harder because that is where the run
        // animation bob lives.
        constexpr float sSmoothTauX = 0.055f;
        constexpr float sSmoothTauY = 0.140f;
        constexpr float sSmoothTauSize = 0.200f;
        // The head/stack transition is deliberately slow so it reads as the bar
        // travelling into place rather than snapping to a second position.
        constexpr float sSmoothTauDock = 0.280f;
        constexpr float sSmoothTauHealth = 0.220f;

        constexpr float sFadeInTime = 0.12f;
        constexpr float sFadeOutTime = 0.30f;
        // How long a bar keeps its last good position after the actor stops being
        // resolvable. Covers the 100 ms scan period plus a short LOS flicker.
        constexpr float sLingerTime = 0.35f;

        // Beyond this jump the smoothing is skipped: a camera cut or a teleport
        // must not drag the bar across the whole screen.
        constexpr float sSnapFraction = 0.35f;

        // ------------------------------------------------------------------
        // X025 distances. Everything is expressed in paces and converted once,
        // so the thresholds below can be read straight off the design.
        // A pace is taken as one metre; OpenMW's world scale is ~70 units/metre.
        // ------------------------------------------------------------------
        constexpr float sUnitsPerStep = 70.f;

        // X027: dock only at close conversation/combat range. This is exactly
        // one third shorter than X025/X026a (15/18 -> 10/12 steps) while
        // preserving the same hysteresis ratio so the bar does not flap at the edge.
        constexpr float sDockEnterSteps = 10.f;  // closer than this -> docked stack
        constexpr float sDockExitSteps = 12.f;   // hysteresis, must exceed the above
        constexpr float sVanishSteps = 40.f;     // beyond this the bar is gone
        constexpr float sFullSizeSteps = 3.5f;   // no shrinking closer than this

        constexpr float sDockEnterDistance = sDockEnterSteps * sUnitsPerStep;
        constexpr float sDockExitDistance = sDockExitSteps * sUnitsPerStep;
        constexpr float sVanishDistance = sVanishSteps * sUnitsPerStep;
        constexpr float sFullSizeDistance = sFullSizeSteps * sUnitsPerStep;
        // The last stretch before vanishing is spent fading, so a bar never pops
        // out of existence at exactly 40 paces.
        constexpr float sFadeOutStartDistance = sVanishDistance * 0.90f;

        // Overhead bar geometry at full size, and how small it is allowed to get
        // just before it disappears. Deliberately small: the overhead bar is a
        // glance-value readout, the docked stack is where the detail lives.
        constexpr float sHeadWidthMax = 92.f;
        // Y028: hostile overhead HP is about one third thinner than Y027.
        // The docked HUD row still expands to sDockBarHeight below.
        constexpr float sHeadHeightMax = 3.f;
        constexpr float sHeadScaleMin = 0.28f;
        // Gap between the top of the actor's hit box and the bar. Kept tight so
        // the bar reads as belonging to that actor and not as floating above the
        // scene; the second term shrinks the gap along with the bar itself.
        constexpr float sHeadClearance = 2.f;
        // Smallest the widget is ever drawn at. Has to stay below the size the
        // distance ramp produces at the vanishing distance, otherwise the bar
        // would stop shrinking early and the falloff would look truncated.
        constexpr int sHeadMinWidthPixels = 22;
        // Y028: overhead combat HP is deliberately only a thin coloured line.
        // There is no frame above actors at any distance; the frame belongs solely
        // to the docked HUD presentation. Two pixels is the stable far-distance floor.
        constexpr int sHeadMinHeightPixels = 2;
        constexpr int sScreenMargin = 6;

        // ------------------------------------------------------------------
        // X025 docked stack: a fixed-width list that grows upwards from just
        // above the stamina bar. One row is a name with its bar underneath.
        // ------------------------------------------------------------------
        constexpr int sDockWidth = 196;
        constexpr int sDockBarHeight = 9;
        constexpr int sDockNameHeight = 18;
        constexpr int sDockNameFontHeight = 16;
        constexpr int sDockNameGap = 2;      // between the caption and its bar
        constexpr int sDockRowStride = 31;   // name + gap + bar + breathing room
        constexpr int sDockGap = 10;         // between the stack and the stamina bar
        constexpr std::size_t sDockMaxRows = 5;

        // An actor has to hold the new side of the threshold this long before the
        // bar actually moves. Without it an enemy circling at ~15 paces would make
        // its bar hop between the head and the stack.
        constexpr float sDockSwitchDelay = 0.25f;
        // Y028: the decorative HUD frame is a presentation layer, not a second bar.
        // It starts faintly appearing during the latter part of the trip into the HUD
        // and starts fading immediately when the bar leaves the HUD again.
        constexpr float sFrameFadeStart = 0.35f;
        // The name only becomes readable once the bar is essentially in the stack.
        constexpr float sNameFadeStart = 0.55f;

        inline float clamp01(float value)
        {
            return std::max(0.f, std::min(1.f, value));
        }

        /// Framerate-independent exponential approach.
        inline float approach(float current, float target, float tau, float dt)
        {
            if (tau <= 0.f || dt <= 0.f)
                return target;
            return current + (target - current) * (1.f - std::exp(-dt / tau));
        }

        inline float lerp(float from, float to, float t)
        {
            return from + (to - from) * t;
        }
    }

    bool npcBarShowsHover(const std::string& mode)
    {
        return mode == "hover" || mode == "both";
    }

    bool npcBarShowsCombat(const std::string& mode)
    {
        return mode == "combat" || mode == "both";
    }

    enum class HorizontalCompassMarkerKind
    {
        Enemy = 0,
        Player = 1,
        Ally = 2,
        Door = 3,
        DetectKey = 4,
        DetectEnchantment = 5,
        DetectCreature = 6
    };

    struct HorizontalCompassMarkerCandidate
    {
        MWWorld::Ptr mObject;
        std::string mIdentity;
        int mLeft = 0;
        float mDistanceSquared = 0.f;
        HorizontalCompassMarkerKind mKind = HorizontalCompassMarkerKind::Ally;
    };

    MyGUI::Colour getHorizontalCompassMarkerColour(HorizontalCompassMarkerKind kind)
    {
        switch (kind)
        {
            case HorizontalCompassMarkerKind::Enemy:
                return MyGUI::Colour(1.00f, 0.18f, 0.16f);
            case HorizontalCompassMarkerKind::Player:
                return MyGUI::Colour(0.18f, 0.42f, 0.95f);
            case HorizontalCompassMarkerKind::Ally:
                return MyGUI::Colour(0.14f, 0.74f, 0.22f);
            case HorizontalCompassMarkerKind::Door:
            case HorizontalCompassMarkerKind::DetectKey:
            case HorizontalCompassMarkerKind::DetectEnchantment:
            case HorizontalCompassMarkerKind::DetectCreature:
                // Use the same sand/gold colour as normal Morrowind location text
                // (for example the "Seyda Neen" HUD caption), not a hard-coded tint.
                return MyGUI::Colour::parse(MyGUI::LanguageManager::getInstance().replaceTags("#{fontcolour=normal}"));
        }
        return MyGUI::Colour(1.f, 1.f, 1.f);
    }

    std::string getHorizontalCompassMarkerCaption(HorizontalCompassMarkerKind kind)
    {
        switch (kind)
        {
            case HorizontalCompassMarkerKind::Door:
                return "⌂";
            case HorizontalCompassMarkerKind::DetectKey:
                return "○─";
            case HorizontalCompassMarkerKind::DetectEnchantment:
                return "✦";
            case HorizontalCompassMarkerKind::DetectCreature:
                return "△";
            case HorizontalCompassMarkerKind::Enemy:
            case HorizontalCompassMarkerKind::Player:
            case HorizontalCompassMarkerKind::Ally:
                return "●";
        }
        return "●";
    }
}

namespace MWGui
{

    /**
     * Makes it possible to use ItemModel::moveItem to move an item from an inventory to the world.
     */
    class WorldItemModel : public ItemModel
    {
    public:
        WorldItemModel(float left, float top) : mLeft(left), mTop(top) {}
        virtual ~WorldItemModel() override {}
        MWWorld::Ptr copyItem (const ItemStack& item, size_t count, bool /*allowAutoEquip*/) override
        {
            MWBase::World* world = MWBase::Environment::get().getWorld();

            MWWorld::Ptr dropped;
            if (world->canPlaceObject(mLeft, mTop))
                dropped = world->placeObject(item.mBase, mLeft, mTop, count);
            else
                dropped = world->dropObjectOnGround(world->getPlayerPtr(), item.mBase, count);
            dropped.getCellRef().setOwner("");

            /*
                Start of tes3mp addition

                Send an ID_OBJECT_PLACE packet every time an object is dropped into the world from
                the inventory screen
            */
            mwmp::ObjectList *objectList = mwmp::Main::get().getNetworking()->getObjectList();
            objectList->reset();
            objectList->packetOrigin = mwmp::CLIENT_GAMEPLAY;
            objectList->addObjectPlace(dropped, true);
            objectList->sendObjectPlace();
            /*
                End of tes3mp addition
            */

            /*
                Start of tes3mp change (major)

                Instead of actually keeping this object as is, delete it after sending the packet
                and wait for the server to send it back with a unique mpNum of its own
            */
            MWBase::Environment::get().getWorld()->deleteObject(dropped);
            /*
                End of tes3mp change (major)
            */

            return dropped;
        }

        void removeItem (const ItemStack& item, size_t count) override { throw std::runtime_error("removeItem not implemented"); }
        ModelIndex getIndex (ItemStack item) override { throw std::runtime_error("getIndex not implemented"); }
        void update() override {}
        size_t getItemCount() override { return 0; }
        ItemStack getItem (ModelIndex index) override { throw std::runtime_error("getItem not implemented"); }

    private:
        // Where to drop the item
        float mLeft;
        float mTop;
    };


    HUD::HUD(CustomMarkerCollection &customMarkers, DragAndDrop* dragAndDrop, MWRender::LocalMap* localMapRender)
        : WindowBase("openmw_hud.layout")
        , LocalMapBase(customMarkers, localMapRender, Settings::Manager::getBool("local map hud fog of war", "Map"))
        , mGameplayHud(nullptr)
        , mHealth(nullptr)
        , mMagicka(nullptr)
        , mStamina(nullptr)
        , mDrowning(nullptr)
        , mHealthText(nullptr)
        , mMagickaText(nullptr)
        , mStaminaText(nullptr)
        , mFpsBox(nullptr)
        , mEnemyName(nullptr)
        , mEnemySummary(nullptr)
        , mDeathRecoveryPanel(nullptr)
        , mDeathRecoveryXpBar(nullptr)
        , mDeathRecoveryTitle(nullptr)
        , mDeathRecoveryXpText(nullptr)
        , mDeathRecoveryPrompt(nullptr)
        , mDeathRecoveryAction(nullptr)
        , mDeathRecoveryBlinkTimer(0.f)
        , mWeapImage(nullptr)
        , mSpellImage(nullptr)
        , mWeapStatus(nullptr)
        , mSpellStatus(nullptr)
        , mEffectBox(nullptr)
        , mMinimap(nullptr)
        , mCrosshair(nullptr)
        , mCellNameClip(nullptr)
        , mCellNameBox(nullptr)
        , mGameTimeBox(nullptr)
        , mHorizontalCompass(nullptr)
        , mHorizontalCompassCenter(nullptr)
        , mDrowningFrame(nullptr)
        , mDrowningFlash(nullptr)
        , mHealthManaStaminaBaseLeft(0)
        , mWeapBoxBaseLeft(0)
        , mSpellBoxBaseLeft(0)
        , mMinimapBoxBaseRight(0)
        , mEffectBoxBaseRight(0)
        , mDragAndDrop(dragAndDrop)
        , mCellNameTimer(0.0f)
        , mCellNameScrollOffset(0.f)
        , mCellNameScrollPause(0.f)
        , mCellNameScrollDirection(-1)
        , mCellNameScrolling(false)
        , mWeaponSpellTimer(0.f)
        , mGameTimeUpdateTimer(0.f)
        , mGameTimeCaption("00:00")
        , mGameTimeShowingCellName(false)
        , mMapVisible(true)
        , mMinimapBaseVisible(true)
        , mEffectBaseVisible(true)
        , mCrosshairBaseVisible(true)
        , mWeaponVisible(true)
        , mSpellVisible(true)
        , mWorldMouseOver(false)
        , mHorizontalCompassAngle(0.f)
        , mHorizontalCompassDirty(true)
        , mHorizontalCompassMarkerTimer(0.f)
        , mEnemyActorId(-1)
        , mEnemyHealthTimer(-1)
        , mFocusActorScreenX(0.5f)
        , mFocusActorScreenY(0.f)
        , mFocusActorDistance(-1.f)
        , mFocusActorPanelAlpha(0.f)
        , mTargetPanelCenterX(0.f)
        , mFocusActorCurrentlyFaced(false)
        , mTargetPanelPositionInitialized(false)
        , mFpsUpdateTimer(0.f)
        , mFpsAccumulatedTime(0.f)
        , mFpsFrameCount(0)
        , mIsDrowning(false)
        , mDrowningFlashTheta(0.f)
        , mHmsBaseVisible(true)
    {
        mMainWidget->setSize(MyGUI::RenderManager::getInstance().getViewSize());
        getWidget(mGameplayHud, "GameplayHud");

        // Keep every gameplay HUD element hidden until authentication or new-character
        // registration has completely finished. The login/registration dialogs and chat
        // live outside this container and therefore remain usable.
        mGameplayHud->setVisible(false);

        // Energy bars
        getWidget(mHealthFrame, "HealthFrame");
        getWidget(mMagickaFrame, "MagickaFrame");
        getWidget(mFatigueFrame, "FatigueFrame");
        getWidget(mHealth, "Health");
        getWidget(mMagicka, "Magicka");
        getWidget(mStamina, "Stamina");
        getWidget(mEnemyHealth, "EnemyHealth");
        getWidget(mEnemyName, "EnemyName");
        getWidget(mEnemySummary, "EnemySummary");
        getWidget(mDeathRecoveryPanel, "DeathRecoveryPanel");
        getWidget(mDeathRecoveryXpBar, "DeathRecoveryXpBar");
        getWidget(mDeathRecoveryTitle, "DeathRecoveryTitle");
        getWidget(mDeathRecoveryXpText, "DeathRecoveryXpText");
        getWidget(mDeathRecoveryPrompt, "DeathRecoveryPrompt");
        getWidget(mDeathRecoveryAction, "DeathRecoveryAction");
        mDeathRecoveryXpBar->setProgressRange(1000);

        // X014: fixed widget pool for world-space combat health bars. Reusing
        // widgets avoids GUI allocations while fights are running.
        constexpr std::size_t combatHealthBarPoolSize = 24;
        mCombatHealthBars.reserve(combatHealthBarPoolSize);
        for (std::size_t i = 0; i < combatHealthBarPoolSize; ++i)
        {
            CombatHealthBarState state;
            state.mWidget = mGameplayHud->createWidget<MyGUI::Widget>(
                "TransparentBG", MyGUI::IntCoord(0, 0, 118, 9),
                MyGUI::Align::Left | MyGUI::Align::Top,
                "CombatHealthBar" + MyGUI::utility::toString(i));
            state.mWidget->setNeedMouseFocus(false);
            state.mWidget->setVisible(false);

            // Y023: direct geometry, no ProgressBar Track. MW_Track_Red is just a
            // stretchable coloured widget, so an HP value can never leave a bare
            // frame because an internal progress child went missing.
            state.mFill = state.mWidget->createWidget<MyGUI::Widget>(
                "MW_Track_Red", MyGUI::IntCoord(0, 0, 118, 9),
                MyGUI::Align::Left | MyGUI::Align::Top, "Fill");
            state.mFill->setNeedMouseFocus(false);
            state.mFill->setVisible(false);

            state.mFrame = state.mWidget->createWidget<MyGUI::Widget>(
                "MW_Box", MyGUI::IntCoord(0, 0, 118, 9),
                MyGUI::Align::Left | MyGUI::Align::Top, "Frame");
            state.mFrame->setNeedMouseFocus(false);
            state.mFrame->setVisible(false);

            // X025: one caption per slot, created here so no layout file has to
            // change. It is only shown while the bar sits in the docked stack.
            state.mName = mGameplayHud->createWidget<MyGUI::TextBox>(
                "SandBrightText",
                MyGUI::IntCoord(0, 0, CombatBar::sDockWidth, CombatBar::sDockNameHeight),
                MyGUI::Align::Left | MyGUI::Align::Top,
                "CombatHealthBarName" + MyGUI::utility::toString(i));
            state.mName->setNeedMouseFocus(false);
            state.mName->setFontHeight(CombatBar::sDockNameFontHeight);
            state.mName->setTextAlign(MyGUI::Align::Left | MyGUI::Align::VCenter);
            state.mName->setVisible(false);

            mCombatHealthBars.push_back(state);
        }

        // ArenaMP Y084: keep enough fixed HUD slots for timed effects plus
        // pickups/XP. Active timed effects are protected from normal recycling.
        constexpr std::size_t hudNotificationPoolSize = 24;
        mHudNotifications.reserve(hudNotificationPoolSize);
        for (std::size_t i = 0; i < hudNotificationPoolSize; ++i)
        {
            HudNotificationState state;
            // Arena Y011: transparent parent plus one uniform medium-opacity
            // backing. This keeps the clean borderless card from Y010 while
            // removing the visible three-band gradient.
            state.mPanel = mGameplayHud->createWidget<MyGUI::Widget>(
                "", MyGUI::IntCoord(0, 0, 300, 38),
                MyGUI::Align::Left | MyGUI::Align::Top,
                "HudEventCard" + MyGUI::utility::toString(i));
            state.mPanel->setNeedMouseFocus(false);
            state.mPanel->setVisible(false);

            state.mShade = state.mPanel->createWidget<MyGUI::Widget>(
                "BlackBG", MyGUI::IntCoord(0, 0, 300, 38), MyGUI::Align::Stretch,
                "HudEventShade" + MyGUI::utility::toString(i));
            state.mShade->setNeedMouseFocus(false);
            state.mShade->setAlpha(0.22f);

            state.mIcon = state.mPanel->createWidget<MyGUI::ImageBox>(
                "ImageBox", MyGUI::IntCoord(4, 4, 30, 30), MyGUI::Align::Left | MyGUI::Align::VCenter,
                "HudEventIcon" + MyGUI::utility::toString(i));
            state.mIcon->setNeedMouseFocus(false);

            state.mTitle = state.mPanel->createWidget<MyGUI::TextBox>(
                "SandBrightText", MyGUI::IntCoord(40, 1, 170, 36),
                MyGUI::Align::Left | MyGUI::Align::VCenter,
                "HudEventTitle" + MyGUI::utility::toString(i));
            state.mTitle->setNeedMouseFocus(false);
            state.mTitle->setFontHeight(16);
            state.mTitle->getSubWidgetText()->setWordWrap(true);
            state.mTitle->setTextAlign(MyGUI::Align::Left | MyGUI::Align::VCenter);
            state.mTitle->setTextShadow(true);
            state.mTitle->setTextShadowColour(MyGUI::Colour::Black);

            state.mValue = state.mPanel->createWidget<MyGUI::TextBox>(
                "SandBrightText", MyGUI::IntCoord(210, 1, 86, 36),
                MyGUI::Align::Right | MyGUI::Align::VCenter,
                "HudEventValue" + MyGUI::utility::toString(i));
            state.mValue->setNeedMouseFocus(false);
            state.mValue->setFontHeight(16);
            state.mValue->getSubWidgetText()->setWordWrap(true);
            state.mValue->setTextAlign(MyGUI::Align::Right | MyGUI::Align::VCenter);
            state.mValue->setTextShadow(true);
            state.mValue->setTextShadowColour(MyGUI::Colour::Black);

            mHudNotifications.push_back(state);
        }

        // Arena Y021: fixed pool for small damage numbers beside the crosshair.
        // No widgets are allocated while combat is running.
        constexpr std::size_t floatingDamagePoolSize = 10;
        mFloatingDamageNumbers.reserve(floatingDamagePoolSize);
        for (std::size_t i = 0; i < floatingDamagePoolSize; ++i)
        {
            FloatingDamageState state;
            state.mWidget = mGameplayHud->createWidget<MyGUI::TextBox>(
                "SandBrightText", MyGUI::IntCoord(0, 0, 78, 26),
                MyGUI::Align::Left | MyGUI::Align::Top,
                "FloatingDamage" + MyGUI::utility::toString(i));
            state.mWidget->setNeedMouseFocus(false);
            // Y028: compact hostile feedback, smaller than Y027 and using the
            // same red family as enemy HUD/compass feedback.
            state.mWidget->setFontHeight(15);
            state.mWidget->setTextAlign(MyGUI::Align::Center);
            state.mWidget->setTextColour(MyGUI::Colour(1.00f, 0.18f, 0.16f));
            state.mWidget->setTextShadow(true);
            state.mWidget->setTextShadowColour(MyGUI::Colour::Black);
            state.mWidget->setVisible(false);
            mFloatingDamageNumbers.push_back(state);
        }

        getWidget(mHealthText, "HealthText");
        getWidget(mMagickaText, "MagickaText");
        getWidget(mStaminaText, "StaminaText");
        getWidget(mFpsBox, "FpsText");
        mHealthManaStaminaBaseLeft = mHealthFrame->getLeft();

        mHealthFrame->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onHMSClicked);
        mMagickaFrame->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onHMSClicked);
        mFatigueFrame->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onHMSClicked);

        //Drowning bar
        getWidget(mDrowningFrame, "DrowningFrame");
        getWidget(mDrowning, "Drowning");
        getWidget(mDrowningFlash, "Flash");
        mDrowning->setProgressRange(200);

        const MyGUI::IntSize& viewSize = MyGUI::RenderManager::getInstance().getViewSize();

        // Item and spell images and status bars
        getWidget(mWeapBox, "WeapBox");
        getWidget(mWeapImage, "WeapImage");
        getWidget(mWeapStatus, "WeapStatus");
        mWeapBoxBaseLeft = mWeapBox->getLeft();
        mWeapBox->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onWeaponClicked);

        getWidget(mSpellBox, "SpellBox");
        getWidget(mSpellImage, "SpellImage");
        getWidget(mSpellStatus, "SpellStatus");
        mSpellBoxBaseLeft = mSpellBox->getLeft();
        mSpellBox->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onMagicClicked);

        getWidget(mSneakBox, "SneakBox");
        mSneakBoxBaseLeft = mSneakBox->getLeft();

        getWidget(mEffectBox, "EffectBox");
        mEffectBoxBaseRight = viewSize.width - mEffectBox->getRight();

        getWidget(mMinimapBox, "MiniMapBox");
        mMinimapBoxBaseRight = viewSize.width - mMinimapBox->getRight();
        getWidget(mMinimap, "MiniMap");
        getWidget(mCompass, "Compass");
        getWidget(mMinimapButton, "MiniMapButton");
        mMinimapButton->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onMapClicked);

        getWidget(mCellNameClip, "CellNameClip");
        getWidget(mCellNameBox, "CellName");
        getWidget(mWeaponSpellBox, "WeaponSpellName");
        getWidget(mGameTimeBox, "GameTime");
        if (mGameTimeBox)
        {
            // The clock/location line is a single, larger caption below the compass.
            mGameTimeBox->setFontHeight(18);
            mGameTimeBox->setTextAlign(MyGUI::Align::Center);
            mGameTimeBox->setCaption(mGameTimeCaption);
        }
        // The old standalone cell-name line is intentionally retired. The location
        // now uses the same caption as the clock, so the two never overlap.
        if (mCellNameClip)
            mCellNameClip->setVisible(false);
        if (mCellNameBox)
            mCellNameBox->setVisible(false);

        getWidget(mHorizontalCompass, "HorizontalCompass");
        constexpr int horizontalCompassTickCount = 11;
        for (int i = 0; i < horizontalCompassTickCount; ++i)
        {
            MyGUI::TextBox* tick = mHorizontalCompass->createWidget<MyGUI::TextBox>("SandBrightText",
                MyGUI::IntCoord(0, 1, 48, 20), MyGUI::Align::Default);
            tick->setTextAlign(MyGUI::Align::Center);
            tick->setTextShadow(true);
            tick->setTextShadowColour(MyGUI::Colour::Black);
            tick->setNeedMouseFocus(false);
            mHorizontalCompassTicks.push_back(tick);
        }
        mHorizontalCompassCenter = mHorizontalCompass->createWidget<MyGUI::TextBox>("SandBrightText",
            MyGUI::IntCoord(mHorizontalCompass->getWidth() / 2 - 12, 14, 24, 10), MyGUI::Align::Default);
        mHorizontalCompassCenter->setCaption("^");
        mHorizontalCompassCenter->setTextAlign(MyGUI::Align::Center);
        mHorizontalCompassCenter->setTextShadow(true);
        mHorizontalCompassCenter->setTextShadowColour(MyGUI::Colour::Black);
        mHorizontalCompassCenter->setNeedMouseFocus(false);

        // Fixed widget pool avoids allocating GUI objects while actors move in and out of range.
        constexpr int horizontalCompassMarkerCount = 48;
        for (int i = 0; i < horizontalCompassMarkerCount; ++i)
        {
            MyGUI::TextBox* marker = mHorizontalCompass->createWidget<MyGUI::TextBox>("NormalText",
                MyGUI::IntCoord(0, 3, 32, 22), MyGUI::Align::Default);
            marker->setCaption("●");
            marker->setFontName("CompassMarkerFont");
            marker->setFontHeight(18);
            marker->setTextAlign(MyGUI::Align::Center);
            marker->setTextShadow(true);
            marker->setTextShadowColour(MyGUI::Colour::Black);
            marker->setNeedMouseFocus(false);
            marker->setAlpha(0.f);
            marker->setVisible(false);

            HorizontalCompassMarkerState state;
            state.mWidget = marker;
            mHorizontalCompassMarkers.push_back(state);
        }

        getWidget(mCrosshair, "Crosshair");

        LocalMapBase::init(mMinimap, mCompass);

        mMainWidget->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onWorldClicked);
        mMainWidget->eventMouseMove += MyGUI::newDelegate(this, &HUD::onWorldMouseOver);
        mMainWidget->eventMouseLostFocus += MyGUI::newDelegate(this, &HUD::onWorldMouseLostFocus);

        updatePositions();
        mSpellIcons = new SpellIcons();
    }

    HUD::~HUD()
    {
        mMainWidget->eventMouseLostFocus.clear();
        mMainWidget->eventMouseMove.clear();
        mMainWidget->eventMouseButtonClick.clear();

        delete mSpellIcons;
    }

    void HUD::setValue(const std::string& id, const MWMechanics::DynamicStat<float>& value)
    {
        int current = static_cast<int>(value.getCurrent());
        int modified = static_cast<int>(value.getModified());

        // Fatigue can be negative
        if (id != "FBar")
            current = std::max(0, current);

        std::string valStr = MyGUI::utility::toString(current) + " / " + MyGUI::utility::toString(modified);
        if (id == "HBar")
        {
            mHealth->setProgressRange(std::max(0, modified));
            mHealth->setProgressPosition(std::max(0, current));
            if (mHealthText)
                mHealthText->setCaption(valStr);
            mHealthFrame->setUserString("Caption_HealthDescription", "#{sHealthDesc}\n" + valStr);
            registerBarChange(mHealthBarState, current, modified, true);
        }
        else if (id == "MBar")
        {
            mMagicka->setProgressRange(std::max(0, modified));
            mMagicka->setProgressPosition(std::max(0, current));
            if (mMagickaText)
                mMagickaText->setCaption(valStr);
            mMagickaFrame->setUserString("Caption_HealthDescription", "#{sMagDesc}\n" + valStr);
            registerBarChange(mMagickaBarState, current, modified);
        }
        else if (id == "FBar")
        {
            mStamina->setProgressRange(std::max(0, modified));
            mStamina->setProgressPosition(std::max(0, current));
            if (mStaminaText)
                mStaminaText->setCaption(valStr);
            mFatigueFrame->setUserString("Caption_HealthDescription", "#{sFatDesc}\n" + valStr);
            registerBarChange(mStaminaBarState, current, modified);
        }
    }

    void HUD::setDrowningTimeLeft(float time, float maxTime)
    {
        size_t progress = static_cast<size_t>(time / maxTime * 200);
        mDrowning->setProgressPosition(progress);

        bool isDrowning = (progress == 0);
        if (isDrowning && !mIsDrowning) // Just started drowning
            mDrowningFlashTheta = 0.0f; // Start out on bright red every time.

        mDrowningFlash->setVisible(isDrowning);
        mIsDrowning = isDrowning;
    }

    void HUD::setDrowningBarVisible(bool visible)
    {
        mDrowningFrame->setVisible(visible);
    }

    void HUD::onWorldClicked(MyGUI::Widget* _sender)
    {
        if (!MWBase::Environment::get().getWindowManager ()->isGuiMode ())
            return;

        MWBase::WindowManager *winMgr = MWBase::Environment::get().getWindowManager();
        if (mDragAndDrop->mIsOnDragAndDrop)
        {
            // drop item into the gameworld
            MWBase::Environment::get().getWorld()->breakInvisibility(
                        MWMechanics::getPlayer());

            MyGUI::IntSize viewSize = MyGUI::RenderManager::getInstance().getViewSize();
            MyGUI::IntPoint cursorPosition = MyGUI::InputManager::getInstance().getMousePosition();
            float mouseX = cursorPosition.left / float(viewSize.width);
            float mouseY = cursorPosition.top / float(viewSize.height);

            WorldItemModel drop (mouseX, mouseY);
            mDragAndDrop->drop(&drop, nullptr);

            winMgr->changePointer("arrow");
        }
        else
        {
            GuiMode mode = winMgr->getMode();

            if (!winMgr->isConsoleMode() && (mode != GM_Container) && (mode != GM_Inventory))
                return;

            MWWorld::Ptr object = MWBase::Environment::get().getWorld()->getFacedObject();

            if (winMgr->isConsoleMode())
                winMgr->setConsoleSelectedObject(object);
            else //if ((mode == GM_Container) || (mode == GM_Inventory))
            {
                // pick up object
                if (!object.isEmpty())
                /*
                    Start of tes3mp change (major)

                    Disable unilateral picking up of objects on this client

                    Instead, send an ID_OBJECT_ACTIVATE packet every time an item is made to pick up
                    an item here, and expect the server's reply to our packet to cause the actual
                    picking up of items
                */
                    //winMgr->getInventoryWindow()->pickUpObject(object);
                {
                    mwmp::ObjectList *objectList = mwmp::Main::get().getNetworking()->getObjectList();
                    objectList->reset();
                    objectList->packetOrigin = mwmp::CLIENT_GAMEPLAY;
                    objectList->addObjectActivate(object, MWMechanics::getPlayer());
                    objectList->sendObjectActivate();
                }
                /*
                    End of tes3mp change (major)
                */
            }
        }
    }

    void HUD::onWorldMouseOver(MyGUI::Widget* _sender, int x, int y)
    {
        if (mDragAndDrop->mIsOnDragAndDrop)
        {
            mWorldMouseOver = false;

            MyGUI::IntSize viewSize = MyGUI::RenderManager::getInstance().getViewSize();
            MyGUI::IntPoint cursorPosition = MyGUI::InputManager::getInstance().getMousePosition();
            float mouseX = cursorPosition.left / float(viewSize.width);
            float mouseY = cursorPosition.top / float(viewSize.height);

            MWBase::World* world = MWBase::Environment::get().getWorld();

            // if we can't drop the object at the wanted position, show the "drop on ground" cursor.
            bool canDrop = world->canPlaceObject(mouseX, mouseY);

            if (!canDrop)
                MWBase::Environment::get().getWindowManager()->changePointer("drop_ground");
            else
                MWBase::Environment::get().getWindowManager()->changePointer("arrow");

        }
        else
        {
            MWBase::Environment::get().getWindowManager()->changePointer("arrow");
            mWorldMouseOver = true;
        }
    }

    void HUD::onWorldMouseLostFocus(MyGUI::Widget* _sender, MyGUI::Widget* _new)
    {
        MWBase::Environment::get().getWindowManager()->changePointer("arrow");
        mWorldMouseOver = false;
    }

    void HUD::onHMSClicked(MyGUI::Widget* _sender)
    {
        MWBase::Environment::get().getWindowManager()->toggleVisible(GW_Stats);
    }

    void HUD::onMapClicked(MyGUI::Widget* _sender)
    {
        MWBase::Environment::get().getWindowManager()->toggleVisible(GW_Map);
    }

    void HUD::onWeaponClicked(MyGUI::Widget* _sender)
    {
        const MWWorld::Ptr& player = MWMechanics::getPlayer();
        if (player.getClass().getNpcStats(player).isWerewolf())
        {
            MWBase::Environment::get().getWindowManager()->messageBox("#{sWerewolfRefusal}");
            return;
        }

        MWBase::Environment::get().getWindowManager()->toggleVisible(GW_Inventory);
    }

    void HUD::onMagicClicked(MyGUI::Widget* _sender)
    {
        const MWWorld::Ptr& player = MWMechanics::getPlayer();
        if (player.getClass().getNpcStats(player).isWerewolf())
        {
            MWBase::Environment::get().getWindowManager()->messageBox("#{sWerewolfRefusal}");
            return;
        }

        MWBase::Environment::get().getWindowManager()->toggleVisible(GW_Magic);
    }


    void HUD::onResChange(int width, int height)
    {
        mMainWidget->setSize(width, height);
        updatePositions();
    }

    void HUD::setCellName(const std::string& cellName)
    {
        if (mCellName != cellName)
        {
            mCellName = cellName;
            mCellNameTimer = Settings::Manager::getBool("show cell name", "HUD")
                ? sCompassInfoSequenceDuration : 0.f;

            // Cell names are now presented in the clock line under the compass.
            // Keep the legacy layout widgets hidden so there is only one HUD caption.
            if (mCellNameClip)
                mCellNameClip->setVisible(false);
            if (mCellNameBox)
                mCellNameBox->setVisible(false);
        }
    }

    void HUD::onFrame(float dt)
    {
        const bool loginFinished = mwmp::Main::isInitialized()
            && mwmp::Main::get().getLocalPlayer()
            && mwmp::Main::get().getLocalPlayer()->isLoggedIn();
        mDeathRecoveryBlinkTimer += std::max(0.f, dt);

        // LocalPlayer::isLoggedIn() becomes true only after an existing character has
        // been received or after the complete new-character registration sequence.
        // Toggling one parent preserves the individual visibility state of every HUD
        // child, so map/settings/autohide choices are restored correctly after login.
        if (mGameplayHud && mGameplayHud->getVisible() != loginFinished)
            mGameplayHud->setVisible(loginFinished);

        // Y039: surface the recovery state without creating fake spells or save data.
        // The server owns XP checkpoints/revive; this HUD only interpolates the visible
        // countdown and exposes the E/touch affordance.
        if (mDeathRecoveryPanel && loginFinished)
        {
            mwmp::LocalPlayer* local = mwmp::Main::get().getLocalPlayer();
            const auto arenaText = [](const std::string& key) {
                return MyGUI::LanguageManager::getInstance().replaceTags("#{arenamp=" + key + "}");
            };
            mwmp::Networking* net = mwmp::Main::get().getNetworking();
            if (net && net->isRestoringConnection())
            {
                const float remaining = std::max(0.f, net->getRestoreRemaining());
                const float duration = std::max(0.001f, net->getRestoreDuration());
                const int seconds = static_cast<int>(std::ceil(remaining));
                mDeathRecoveryPanel->setVisible(true);
                mDeathRecoveryXpBar->setVisible(true);
                mDeathRecoveryXpText->setVisible(true);
                mDeathRecoveryXpBar->setProgressPosition(static_cast<std::size_t>(
                    std::lround(std::clamp(remaining / duration, 0.f, 1.f) * 1000.f)));
                mDeathRecoveryTitle->setCaption(arenaText("network.reconnect.title"));
                mDeathRecoveryXpText->setCaption(arenaText("network.reconnect.remaining") + ": "
                    + MyGUI::utility::toString(seconds) + " s");
                mDeathRecoveryPrompt->setCaption(arenaText("network.reconnect.hint"));
                mDeathRecoveryAction->setVisible(false);
            }
            else if (local && local->isBountyDeathActive())
            {
                const float remaining = local->getBountyDeathRemaining();
                const float duration = std::max(0.001f, local->getBountyDeathDuration());
                const int seconds = static_cast<int>(std::ceil(remaining));
                mDeathRecoveryPanel->setVisible(true);
                mDeathRecoveryXpBar->setVisible(true);
                mDeathRecoveryXpText->setVisible(true);
                mDeathRecoveryXpBar->setProgressPosition(static_cast<std::size_t>(
                    std::lround(std::clamp(remaining / duration, 0.f, 1.f) * 1000.f)));
                mDeathRecoveryTitle->setCaption(arenaText("death.bounty.title"));
                mDeathRecoveryXpText->setCaption(arenaText("death.bounty.remaining") + ": "
                    + MyGUI::utility::toString(seconds / 60) + ":" + (seconds % 60 < 10 ? "0" : "")
                    + MyGUI::utility::toString(seconds % 60));
                mDeathRecoveryPrompt->setCaption(arenaText("death.bounty.hint"));
                mDeathRecoveryAction->setVisible(false);
            }
            else if (local && local->isRespawnWaitVisible())
            {
                const float remaining=local->getRespawnWaitRemaining();
                const float duration=std::max(.001f,local->getRespawnWaitDuration());
                const int seconds=static_cast<int>(std::ceil(remaining));
                mDeathRecoveryPanel->setVisible(true);
                mDeathRecoveryXpBar->setVisible(true);
                mDeathRecoveryXpText->setVisible(true);
                mDeathRecoveryXpBar->setProgressPosition(static_cast<std::size_t>(
                    std::lround(std::clamp(remaining/duration,0.f,1.f)*1000.f)));
                mDeathRecoveryTitle->setCaption(arenaText("death.respawn.title"));
                mDeathRecoveryXpText->setCaption(arenaText("death.respawn.remaining")+": "
                    +MyGUI::utility::toString(seconds/60)+":"+(seconds%60<10 ? "0" : "")
                    +MyGUI::utility::toString(seconds%60));
                mDeathRecoveryPrompt->setCaption(arenaText("death.respawn.hint"));
                mDeathRecoveryAction->setVisible(false);
            }
            else if (local && local->isDeathRecoveryActive())
            {
                const float duration = std::max(0.001f, local->getDeathRecoveryDurationSeconds());
                const float remaining = std::max(0.f, local->getDeathRecoveryRemainingSeconds());
                const float initialXp = std::max(0.f, local->getDeathRecoveryInitialXp());
                float fraction = std::clamp(remaining / duration, 0.f, 1.f);
                float xp = initialXp * fraction;

                // Y040: the client interpolates between the server's one-second
                // checkpoints, but the server owns the real number and its decay
                // window is configurable. Clamp to the authoritative value so the
                // HUD can lag behind reality but never promise XP that is gone.
                const MWWorld::Ptr playerPtr = MWMechanics::getPlayer();
                if (!playerPtr.isEmpty() && playerPtr.getClass().isNpc())
                {
                    const float serverXp = std::max(0.f,
                        playerPtr.getClass().getNpcStats(playerPtr).getExperience());
                    xp = std::min(xp, serverXp);
                    if (initialXp > 0.f)
                        fraction = std::clamp(xp / initialXp, 0.f, 1.f);
                }

                const int potions = local->getRestoreHealthPotionCount();
                const int requiredPotions = local->getSelfDeathRecoveryPotionRequirement();
                // MyGUI::utility::toString concatenates every argument, so passing a
                // precision to it printed a stray digit after the seconds value.
                std::ostringstream secondsStream;
                secondsStream << std::fixed << std::setprecision(1) << (duration * fraction);
                mDeathRecoveryPanel->setVisible(true);
                mDeathRecoveryXpBar->setVisible(true);
                mDeathRecoveryXpText->setVisible(true);
                mDeathRecoveryXpBar->setProgressPosition(static_cast<std::size_t>(std::lround(fraction * 1000.f)));
                mDeathRecoveryTitle->setCaption(arenaText("death.recovery.title"));
                mDeathRecoveryXpText->setCaption(arenaText("death.recovery.xp") + ": "
                    + MyGUI::utility::toString(static_cast<int>(std::lround(xp))) + "  |  "
                    + secondsStream.str() + " s");
                if (potions >= requiredPotions)
                {
                    mDeathRecoveryPrompt->setCaption(arenaText("death.recovery.self_prompt") + "\n"
                        + arenaText("death.recovery.self_status") + "  |  "
                        + arenaText("death.recovery.potions") + ": " + MyGUI::utility::toString(potions)
                        + " / " + MyGUI::utility::toString(requiredPotions));
                    mDeathRecoveryAction->setCaption(arenaText("death.recovery.action"));
                    mDeathRecoveryAction->setVisible(true);
                }
                else
                {
                    mDeathRecoveryPrompt->setCaption(arenaText("death.recovery.self_no_potion") + "  |  "
                        + arenaText("death.recovery.potions") + ": " + MyGUI::utility::toString(potions)
                        + " / " + MyGUI::utility::toString(requiredPotions));
                    mDeathRecoveryAction->setVisible(false);
                }
            }
            else if (local)
            {
                std::string allyName;
                int allyLevel = 1;
                if (local->getRecoverableAllyName(allyName, &allyLevel))
                {
                    const int potions = local->getRestoreHealthPotionCount();
                    const int requiredPotions = local->getRequiredDeathRecoveryPotionCount(allyLevel);
                    mDeathRecoveryPanel->setVisible(true);
                    mDeathRecoveryXpBar->setVisible(false);
                    mDeathRecoveryXpText->setVisible(false);
                    mDeathRecoveryTitle->setCaption(arenaText("death.recovery.ally_title") + ": " + allyName);
                    if (potions >= requiredPotions)
                    {
                        mDeathRecoveryPrompt->setCaption(arenaText("death.recovery.ally_prompt") + "\n"
                            + arenaText("death.recovery.ally_status") + "  |  "
                            + arenaText("death.recovery.potions") + ": " + MyGUI::utility::toString(potions)
                            + " / " + MyGUI::utility::toString(requiredPotions));
                        mDeathRecoveryAction->setCaption(arenaText("death.recovery.action"));
                        mDeathRecoveryAction->setVisible(true);
                    }
                    else
                    {
                        mDeathRecoveryPrompt->setCaption(arenaText("death.recovery.touch_hint") + "  |  "
                            + arenaText("death.recovery.potions") + ": " + MyGUI::utility::toString(potions)
                            + " / " + MyGUI::utility::toString(requiredPotions));
                        mDeathRecoveryAction->setVisible(false);
                    }
                }
                else
                {
                    mDeathRecoveryPanel->setVisible(false);
                    mDeathRecoveryAction->setVisible(false);
                }
            }
            else
            {
                mDeathRecoveryPanel->setVisible(false);
                mDeathRecoveryAction->setVisible(false);
            }
        }
        else if (mDeathRecoveryPanel)
        {
            mDeathRecoveryPanel->setVisible(false);
            if (mDeathRecoveryAction)
                mDeathRecoveryAction->setVisible(false);
        }

        if (mDeathRecoveryAction && mDeathRecoveryAction->getVisible())
        {
            const float pulse = 0.5f + 0.5f * std::sin(mDeathRecoveryBlinkTimer * 7.f);
            mDeathRecoveryAction->setTextColour(MyGUI::Colour(1.f, 0.12f + 0.35f * pulse, 0.12f + 0.35f * pulse));
            mDeathRecoveryAction->setAlpha(0.62f + 0.38f * pulse);
        }

        LocalMapBase::onFrame(dt);

        // Apply HUD preferences live so changes made in the dedicated HUD tab do
        // not require reopening the game or rebuilding the HUD layout.
        const bool showMinimap = mMinimapBaseVisible && Settings::Manager::getBool("show minimap", "HUD");
        if (mMinimapBox->getVisible() != showMinimap)
        {
            mMinimapBox->setVisible(showMinimap);
            updatePositions();
        }
        if (mCompass)
            mCompass->setVisible(showMinimap);
        // Clock/location visibility and timed replacement are handled together below.
        if (mEffectBox)
            mEffectBox->setVisible(mEffectBaseVisible && Settings::Manager::getBool("show status effects", "HUD"));
        if (mFpsBox)
            mFpsBox->setVisible(Settings::Manager::getBool("show fps", "HUD"));
        if (mCrosshair)
            mCrosshair->setVisible(mCrosshairBaseVisible && Settings::Manager::getBool("crosshair", "HUD"));

        updateHorizontalCompass();
        updateHorizontalCompassMarkers(dt);

        updateGameTimeAndCellName(dt);

        mWeaponSpellTimer -= dt;
        if (mWeaponSpellTimer < 0)
            mWeaponSpellBox->setVisible(false);

        mFpsAccumulatedTime += dt;
        ++mFpsFrameCount;
        mFpsUpdateTimer -= dt;
        if (mFpsBox && mFpsUpdateTimer <= 0.f)
        {
            const float safeTime = std::max(0.0001f, mFpsAccumulatedTime);
            const int fps = static_cast<int>(std::lround(static_cast<double>(mFpsFrameCount) / safeTime));
            mFpsBox->setCaption("FPS: " + MyGUI::utility::toString(fps));
            mFpsUpdateTimer = 0.25f;
            mFpsAccumulatedTime = 0.f;
            mFpsFrameCount = 0;
        }

        updateCombatHealthBars(dt);
        updateFloatingDamageNumbers(dt);
        updateHudEventFeed(dt);
        updateFocusedTargetPanel(dt);

        const std::string npcBarMode = getNpcBarMode();
        const bool showHoverNpcBar = npcBarShowsHover(npcBarMode);
        const bool focusedTargetAlive = !mFocusActor.isEmpty()
            && !mFocusActor.getClass().getCreatureStats(mFocusActor).isDead();
        const bool dialogueOpen = MWBase::Environment::get().getWindowManager()->containsMode(GM_Dialogue);
        const bool focusedTargetPanel = showHoverNpcBar && focusedTargetAlive && !dialogueOpen
            && mFocusActorPanelAlpha > 0.01f;
        mEnemyHealthTimer -= dt;
        if (mEnemyHealth->getVisible() && !focusedTargetPanel)
            mEnemyHealth->setVisible(false);

        if (mIsDrowning)
            mDrowningFlashTheta += dt * osg::PI*2;

        mSpellIcons->updateWidgets(mEffectBox, true);
        if (mEffectBox)
            mEffectBox->setVisible(mEffectBaseVisible
                && Settings::Manager::getBool("show status effects", "HUD")
                && mEffectBox->getChildCount() > 0);

        if (focusedTargetPanel && mEnemyHealth->getVisible())
            updateEnemyHealthBar();

        if (focusedTargetPanel)
        {
            mEnemyHealth->setVisible(true);
            if (mEnemyName)
                mEnemyName->setVisible(true);
            if (mEnemySummary)
                mEnemySummary->setVisible(true);
            updateEnemyHealthBar();
        }

        if (mIsDrowning)
        {
            float intensity = (cos(mDrowningFlashTheta) + 2.0f) / 3.0f;

            mDrowningFlash->setAlpha(intensity);
        }

        MWMechanics::DrawState_ drawState = MWMechanics::DrawState_Nothing;
        const MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
        if (!player.isEmpty())
            drawState = player.getClass().getCreatureStats(player).getDrawState();

        if (!showHoverNpcBar || dialogueOpen)
            mEnemyHealth->setVisible(false);

        const bool showFocusedTargetInfo = focusedTargetPanel && mEnemyHealth->getVisible();
        if (mEnemyName)
            mEnemyName->setVisible(showFocusedTargetInfo);
        if (mEnemySummary)
            mEnemySummary->setVisible(showFocusedTargetInfo);

        updateAutoHideBar(mHealthFrame, mHealthBarState, dt, false);
        updateAutoHideBar(mMagickaFrame, mMagickaBarState, dt,
            drawState == MWMechanics::DrawState_Spell, mSpellBox);
        updateAutoHideBar(mFatigueFrame, mStaminaBarState, dt,
            drawState == MWMechanics::DrawState_Weapon, mWeapBox);
    }

    void HUD::updateGameTimeAndCellName(float dt)
    {
        if (!mGameTimeBox)
            return;

        dt = std::max(0.f, dt);
        const bool showTime = Settings::Manager::getBool("show game time", "HUD");
        const bool showLocation = Settings::Manager::getBool("show cell name", "HUD");

        // Keep the underlying time string current even while the location name is shown,
        // so the clock is correct immediately when it fades back in.
        mGameTimeUpdateTimer -= dt;
        if (mGameTimeUpdateTimer <= 0.f)
        {
            const float gameHour = MWBase::Environment::get().getWorld()->getTimeStamp().getHour();
            int hours = static_cast<int>(std::floor(gameHour)) % 24;
            int minutes = static_cast<int>(std::floor((gameHour - std::floor(gameHour)) * 60.f + 0.5f));
            if (minutes >= 60)
            {
                minutes = 0;
                hours = (hours + 1) % 24;
            }

            std::ostringstream stream;
            stream << std::setfill('0') << std::setw(2) << hours << ':'
                   << std::setfill('0') << std::setw(2) << minutes;
            mGameTimeCaption = stream.str();
            mGameTimeUpdateTimer = 0.2f;
        }

        if (!showLocation)
            mCellNameTimer = 0.f;
        else if (mCellNameTimer > 0.f)
            mCellNameTimer = std::max(0.f, mCellNameTimer - dt);

        bool displayLocation = false;
        float alpha = 1.f;
        const bool locationSequence = showLocation && !mCellName.empty() && mCellNameTimer > 0.f;

        if (locationSequence)
        {
            // If the clock itself is disabled, the location can appear immediately; there
            // is no visible time caption to fade away first.
            if (!showTime)
            {
                displayLocation = true;
                alpha = 1.f;
            }
            else
            {
                const float elapsed = sCompassInfoSequenceDuration - mCellNameTimer;
                const float fade = sCompassInfoFadeDuration;
                const float locationEnd = fade + sCompassLocationHoldDuration;

                if (elapsed < fade)
                {
                    // Phase 1: only the clock fades out.
                    displayLocation = false;
                    alpha = 1.f - elapsed / fade;
                }
                else if (elapsed < locationEnd)
                {
                    // Location itself is steady and fully opaque.
                    displayLocation = true;
                    alpha = 1.f;
                }
                else
                {
                    // Switch back to the clock at zero alpha and fade only the time in.
                    displayLocation = false;
                    alpha = (elapsed - locationEnd) / fade;
                }
            }
        }

        alpha = std::max(0.f, std::min(1.f, alpha));
        const bool visible = displayLocation ? showLocation : showTime;
        mGameTimeBox->setVisible(visible);
        mGameTimeBox->setAlpha(visible ? alpha : 0.f);

        if (!visible)
            return;

        if (displayLocation)
        {
            {
                const std::string title = mwmp::ArenaHousing::get().displayName(mCellName);
                if (title == mCellName)
                    mGameTimeBox->setCaptionWithReplacing("#{sCell=" + mCellName + "}");
                else
                    mGameTimeBox->setCaption(title);
                mGameTimeShowingCellName = true;
            }
        }
        else
        {
            mGameTimeBox->setCaption(mGameTimeCaption);
            mGameTimeShowingCellName = false;
        }
    }

    void HUD::setPlayerDir(float x, float y)
    {
        LocalMapBase::setPlayerDir(x, y);
        const float angle = std::atan2(x, y);
        if (angle != mHorizontalCompassAngle)
        {
            mHorizontalCompassAngle = angle;
            mHorizontalCompassDirty = true;
        }
    }

    void HUD::updateHorizontalCompass()
    {
        if (!mHorizontalCompass)
            return;

        const bool enabled = Settings::Manager::getBool("horizontal compass", "HUD");
        if (mHorizontalCompass->getVisible() != enabled)
            mHorizontalCompass->setVisible(enabled);
        if (!enabled || !mHorizontalCompassDirty)
            return;

        static const char* directions[] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
        constexpr float stepDegrees = 15.f;
        constexpr float pixelsPerDegree = 4.f;
        constexpr int labelWidth = 48;

        float heading = osg::RadiansToDegrees(mHorizontalCompassAngle);
        while (heading < 0.f)
            heading += 360.f;
        while (heading >= 360.f)
            heading -= 360.f;

        const int nearestStep = static_cast<int>(std::floor(heading / stepDegrees + 0.5f));
        const int halfCount = static_cast<int>(mHorizontalCompassTicks.size()) / 2;
        for (size_t i = 0; i < mHorizontalCompassTicks.size(); ++i)
        {
            const int markerStep = nearestStep + static_cast<int>(i) - halfCount;
            const float markerDegrees = markerStep * stepDegrees;
            const float delta = markerDegrees - heading;
            const int left = static_cast<int>(std::lround(
                mHorizontalCompass->getWidth() * 0.5f + delta * pixelsPerDegree - labelWidth * 0.5f));
            MyGUI::TextBox* tick = mHorizontalCompassTicks[i];
            tick->setPosition(left, 1);

            int wrappedStep = markerStep % 24;
            if (wrappedStep < 0)
                wrappedStep += 24;
            if (wrappedStep % 3 == 0)
                tick->setCaption(directions[(wrappedStep / 3) % 8]);
            else
                tick->setCaption("|");
        }

        mHorizontalCompassDirty = false;
    }

    void HUD::updateHorizontalCompassMarkers(float dt)
    {
        if (!mHorizontalCompass)
            return;

        dt = std::max(0.f, dt);
        const bool enabled = Settings::Manager::getBool("horizontal compass", "HUD")
            && mGameplayHud && mGameplayHud->getVisible();
        if (!enabled)
        {
            for (HorizontalCompassMarkerState& state : mHorizontalCompassMarkers)
            {
                state.mObject = MWWorld::Ptr();
                state.mIdentity.clear();
                state.mAlpha = 0.f;
                state.mTargetAlpha = 0.f;
                state.mSeen = false;
                state.mWidget->setAlpha(0.f);
                state.mWidget->setVisible(false);
            }
            mHorizontalCompassMarkerTimer = 0.f;
            return;
        }

        mHorizontalCompassMarkerTimer -= dt;
        if (mHorizontalCompassMarkerTimer <= 0.f)
        {
            mHorizontalCompassMarkerTimer = 0.075f;

            for (HorizontalCompassMarkerState& state : mHorizontalCompassMarkers)
                state.mSeen = false;

            MWBase::World* world = MWBase::Environment::get().getWorld();
            MWBase::MechanicsManager* mechanics = MWBase::Environment::get().getMechanicsManager();
            if (world && mechanics)
            {
                const MWWorld::Ptr player = world->getPlayerPtr();
                if (!player.isEmpty() && player.isInCell())
                {
                    // Lower enum value has visual priority when the same reference belongs
                    // to several groups (for example an enemy also found by Detect Animal).
                    std::map<MWWorld::Ptr, HorizontalCompassMarkerKind> objects;
                    const auto addObject = [&objects](const MWWorld::Ptr& object, HorizontalCompassMarkerKind kind)
                    {
                        if (object.isEmpty())
                            return;
                        const auto [it, inserted] = objects.emplace(object, kind);
                        if (!inserted && static_cast<int>(kind) < static_cast<int>(it->second))
                            it->second = kind;
                    };

                    for (const MWWorld::Ptr& enemy : mechanics->getActorsFighting(player))
                        addObject(enemy, HorizontalCompassMarkerKind::Enemy);

                    std::set<MWWorld::Ptr> allies;
                    mechanics->getActorsSidingWith(player, allies);
                    for (const MWWorld::Ptr& ally : allies)
                    {
                        // Network players have their own blue marker; followers and summons stay green.
                        if (!mwmp::PlayerList::isDedicatedPlayer(ally))
                            addObject(ally, HorizontalCompassMarkerKind::Ally);
                    }

                    if (mwmp::Main::isInitialized() && mwmp::Main::get().getLocalPlayer())
                    {
                        const std::vector<RakNet::RakNetGUID> playerGuids
                            = mwmp::PlayerList::getPlayersInCell(*player.getCell()->getCell());
                        for (const RakNet::RakNetGUID& guid : playerGuids)
                        {
                            mwmp::DedicatedPlayer* remotePlayer = mwmp::PlayerList::getPlayer(guid);
                            if (remotePlayer)
                                addObject(remotePlayer->getPtr(), HorizontalCompassMarkerKind::Player);
                        }
                    }

                    // Reuse the engine's native detection logic. The effective magnitude
                    // already combines spells, abilities, potions and enchanted items, so
                    // these compass markers obey Detect Key / Detect Enchantment / Detect Animal
                    // ranges exactly like the local-map magic markers do.
                    const auto addDetected = [world, &player, &addObject](MWBase::World::DetectionType type,
                        HorizontalCompassMarkerKind kind)
                    {
                        std::vector<MWWorld::Ptr> detected;
                        world->listDetectedReferences(player, detected, type);
                        for (const MWWorld::Ptr& object : detected)
                            addObject(object, kind);
                    };
                    addDetected(MWBase::World::Detect_Key, HorizontalCompassMarkerKind::DetectKey);
                    addDetected(MWBase::World::Detect_Enchantment, HorizontalCompassMarkerKind::DetectEnchantment);
                    addDetected(MWBase::World::Detect_Creature, HorizontalCompassMarkerKind::DetectCreature);

                    constexpr float markerRange = 8192.f;
                    constexpr float markerRangeSquared = markerRange * markerRange;
                    constexpr float pixelsPerDegree = 4.f;
                    constexpr int compassInnerPadding = 10;
                    const int markerWidth = mHorizontalCompassMarkers.empty()
                        ? 32
                        : mHorizontalCompassMarkers.front().mWidget->getWidth();
                    const osg::Vec3f playerPosition = player.getRefData().getPosition().asVec3();
                    const int compassWidth = mHorizontalCompass->getWidth();
                    const float compassCenter = compassWidth * 0.5f;
                    const float maximumMarkerOffset = std::max(1.f,
                        compassCenter - compassInnerPadding - markerWidth * 0.5f);
                    const float maximumVisibleDegrees = maximumMarkerOffset / pixelsPerDegree;

                    std::vector<HorizontalCompassMarkerCandidate> candidates;
                    candidates.reserve(objects.size() + 24);

                    const auto appendCandidate = [&](const MWWorld::Ptr& object, const std::string& identity,
                        HorizontalCompassMarkerKind kind, float worldX, float worldY, float distanceSquared)
                    {
                        if (distanceSquared > markerRangeSquared || distanceSquared < 1.f)
                            return;

                        const float bearing = std::atan2(worldX - playerPosition.x(), worldY - playerPosition.y());
                        // Marker positions are in world coordinates. The map heading
                        // includes the interior NorthMarker rotation, so subtracting
                        // it here incorrectly rotates every marker inside that cell.
                        // Keep map-relative heading for cardinal labels, but compare
                        // world bearings with the player's world yaw.
                        float relativeAngle = bearing - player.getRefData().getPosition().rot[2];
                        while (relativeAngle > osg::PI)
                            relativeAngle -= osg::PI * 2.f;
                        while (relativeAngle < -osg::PI)
                            relativeAngle += osg::PI * 2.f;

                        // Skyrim-like edge behaviour: markers outside the visible compass arc
                        // stay pinned to the nearest edge rather than disappearing behind you.
                        const float relativeDegrees = osg::RadiansToDegrees(relativeAngle);
                        const float displayedDegrees = std::max(-maximumVisibleDegrees,
                            std::min(maximumVisibleDegrees, relativeDegrees));
                        const int left = static_cast<int>(std::lround(
                            compassCenter + displayedDegrees * pixelsPerDegree - markerWidth * 0.5f));

                        HorizontalCompassMarkerCandidate candidate;
                        candidate.mObject = object;
                        candidate.mIdentity = identity;
                        candidate.mLeft = left;
                        candidate.mDistanceSquared = distanceSquared;
                        candidate.mKind = kind;
                        candidates.push_back(candidate);
                    };

                    for (const auto& [object, kind] : objects)
                    {
                        if (object == player || !object.isInCell())
                            continue;
                        if (object.getRefData().getCount() <= 0 || !object.getRefData().isEnabled()
                            || object.getRefData().isDeleted())
                            continue;

                        const bool actorMarker = kind == HorizontalCompassMarkerKind::Enemy
                            || kind == HorizontalCompassMarkerKind::Player
                            || kind == HorizontalCompassMarkerKind::Ally
                            || kind == HorizontalCompassMarkerKind::DetectCreature;
                        if (actorMarker)
                        {
                            if (!object.getClass().isActor() || object.getClass().getCreatureStats(object).isDead())
                                continue;
                            // Combat/follower/network-player markers are cell-local. Detect Animal
                            // may originate in an adjacent active exterior and is allowed to remain.
                            if (kind != HorizontalCompassMarkerKind::DetectCreature
                                && object.getCell() != player.getCell())
                                continue;
                        }

                        const osg::Vec3f position = object.getRefData().getPosition().asVec3();
                        const float dx = position.x() - playerPosition.x();
                        const float dy = position.y() - playerPosition.y();
                        appendCandidate(object, std::string(), kind, position.x(), position.y(), dx * dx + dy * dy);
                    }

                    // Door markers are now an exterior-navigation aid. While outdoors,
                    // show nearby teleport doors that lead into interiors (buildings, caves, etc.).
                    // Interior-to-interior/exterior door clutter is intentionally omitted.
                    if (player.getCell()->getCell()->isExterior())
                    {
                        std::vector<MWBase::World::DoorMarker> doors;
                        world->getDoorMarkers(player.getCell(), doors);
                        for (const MWBase::World::DoorMarker& door : doors)
                        {
                            // mPaged == false identifies an interior destination.
                            if (door.dest.mPaged)
                                continue;

                            const float dx = door.x - playerPosition.x();
                            const float dy = door.y - playerPosition.y();
                            std::ostringstream identity;
                            identity << "door:" << door.name << ':'
                                << static_cast<int>(std::lround(door.x)) << ':'
                                << static_cast<int>(std::lround(door.y));
                            appendCandidate(MWWorld::Ptr(), identity.str(), HorizontalCompassMarkerKind::Door,
                                door.x, door.y, dx * dx + dy * dy);
                        }
                    }

                    std::sort(candidates.begin(), candidates.end(),
                        [](const HorizontalCompassMarkerCandidate& left,
                            const HorizontalCompassMarkerCandidate& right)
                        {
                            if (left.mDistanceSquared != right.mDistanceSquared)
                                return left.mDistanceSquared < right.mDistanceSquared;
                            return static_cast<int>(left.mKind) < static_cast<int>(right.mKind);
                        });

                    const size_t candidateCount = std::min(candidates.size(), mHorizontalCompassMarkers.size());
                    for (size_t candidateIndex = 0; candidateIndex < candidateCount; ++candidateIndex)
                    {
                        const HorizontalCompassMarkerCandidate& candidate = candidates[candidateIndex];
                        HorizontalCompassMarkerState* selectedState = nullptr;

                        // Keep the same widget attached to the same object/door so markers
                        // do not swap positions or blink between 75 ms scans.
                        for (HorizontalCompassMarkerState& state : mHorizontalCompassMarkers)
                        {
                            const bool sameObject = !candidate.mObject.isEmpty()
                                && !state.mObject.isEmpty() && state.mObject == candidate.mObject;
                            const bool sameIdentity = candidate.mObject.isEmpty()
                                && state.mObject.isEmpty() && !candidate.mIdentity.empty()
                                && state.mIdentity == candidate.mIdentity;
                            if (sameObject || sameIdentity)
                            {
                                selectedState = &state;
                                break;
                            }
                        }

                        if (!selectedState)
                        {
                            for (HorizontalCompassMarkerState& state : mHorizontalCompassMarkers)
                            {
                                if (!state.mSeen && state.mObject.isEmpty() && state.mIdentity.empty())
                                {
                                    selectedState = &state;
                                    break;
                                }
                            }
                        }
                        if (!selectedState)
                        {
                            for (HorizontalCompassMarkerState& state : mHorizontalCompassMarkers)
                            {
                                if (state.mSeen)
                                    continue;
                                if (!selectedState || state.mAlpha < selectedState->mAlpha)
                                    selectedState = &state;
                            }
                        }
                        if (!selectedState)
                            continue;

                        const bool newMarker = (!candidate.mObject.isEmpty()
                                && (selectedState->mObject.isEmpty() || selectedState->mObject != candidate.mObject))
                            || (candidate.mObject.isEmpty()
                                && (selectedState->mIdentity != candidate.mIdentity || !selectedState->mObject.isEmpty()));
                        const float oldTargetLeft = selectedState->mTargetLeft;
                        selectedState->mObject = candidate.mObject;
                        selectedState->mIdentity = candidate.mIdentity;
                        selectedState->mSeen = true;
                        selectedState->mTargetLeft = static_cast<float>(candidate.mLeft);
                        selectedState->mTargetAlpha = 1.f;
                        if (!candidate.mObject.isEmpty()
                            && mwmp::PlayerList::isDedicatedPlayer(candidate.mObject))
                        {
                            // Full brightness nearby, still readable at the range limit.
                            const float distance = std::sqrt(candidate.mDistanceSquared);
                            const float fade = std::clamp((distance - 256.f) / (markerRange - 256.f), 0.f, 1.f);
                            selectedState->mTargetAlpha = 1.f - 0.65f * fade;
                        }
                        selectedState->mWidget->setCaption(getHorizontalCompassMarkerCaption(candidate.mKind));
                        selectedState->mWidget->setTextColour(getHorizontalCompassMarkerColour(candidate.mKind));

                        if (newMarker)
                        {
                            selectedState->mSpeechBlend = 0.f;
                            selectedState->mPulsePhase = 0.f;
                            selectedState->mCurrentLeft = selectedState->mTargetLeft;
                            selectedState->mAlpha = 0.f;
                            selectedState->mWidget->setAlpha(0.f);
                        }
                        else if (std::abs(selectedState->mTargetLeft - oldTargetLeft) > compassWidth * 0.65f)
                        {
                            // Crossing the exact rear direction changes the nearest edge. Snap only this
                            // wraparound transition so the marker does not incorrectly travel through centre.
                            selectedState->mCurrentLeft = selectedState->mTargetLeft;
                        }
                    }
                }
            }

            for (HorizontalCompassMarkerState& state : mHorizontalCompassMarkers)
            {
                if (!state.mSeen)
                    state.mTargetAlpha = 0.f;
            }
        }

        // Position is exponentially smoothed each frame. Alpha uses separate fade-in/fade-out
        // speeds so a newly detected target appears quickly but disappears more softly.
        const float positionBlend = 1.f - std::exp(-14.f * dt);
        constexpr float fadeInSpeed = 7.5f;
        constexpr float fadeOutSpeed = 4.5f;
        for (HorizontalCompassMarkerState& state : mHorizontalCompassMarkers)
        {
            state.mCurrentLeft += (state.mTargetLeft - state.mCurrentLeft) * positionBlend;
            bool speaking = false;
            if (state.mSeen && !state.mObject.isEmpty() && mwmp::Main::isInitialized())
            {
                auto* remote = mwmp::PlayerList::getPlayer(state.mObject);
                auto* voice = mwmp::Main::get().getVoiceChat();
                speaking = remote && voice && voice->isSpeakerActive(remote->guid);
            }
            state.mSpeechBlend += ((speaking ? 1.f : 0.f) - state.mSpeechBlend)
                * (1.f - std::exp(-10.f * dt));
            state.mPulsePhase = std::fmod(state.mPulsePhase + dt * 2.f * osg::PI * 1.5f, 2.f * osg::PI);
            if (state.mTargetAlpha > state.mAlpha)
                state.mAlpha = std::min(state.mTargetAlpha, state.mAlpha + fadeInSpeed * dt);
            else
                state.mAlpha = std::max(state.mTargetAlpha, state.mAlpha - fadeOutSpeed * dt);

            const bool visible = state.mAlpha > 0.01f || state.mTargetAlpha > 0.f;
            state.mWidget->setVisible(visible);
            if (visible)
            {
                constexpr int compassInnerPadding = 10;
                const int markerWidth = state.mWidget->getWidth();
                const float minimumLeft = static_cast<float>(compassInnerPadding);
                const float maximumLeft = static_cast<float>(std::max(
                    compassInnerPadding, mHorizontalCompass->getWidth() - compassInnerPadding - markerWidth));
                state.mCurrentLeft = std::max(minimumLeft, std::min(maximumLeft, state.mCurrentLeft));
                state.mWidget->setPosition(static_cast<int>(std::lround(state.mCurrentLeft)), 3);
                const float pulse = 0.5f + 0.5f * std::cos(state.mPulsePhase);
                state.mWidget->setAlpha(state.mAlpha * (1.f - 0.4f * state.mSpeechBlend * pulse));
            }
            else
            {
                state.mObject = MWWorld::Ptr();
                state.mIdentity.clear();
                state.mAlpha = 0.f;
                state.mWidget->setAlpha(0.f);
            }
        }
    }


    void HUD::registerBarChange(AutoHideBarState& state, int current, int modified, bool wakeOnIncrease)
    {
        const bool firstUpdate = !state.initialized;
        const bool maximumChanged = state.initialized && state.modified != modified;
        const bool valueDecreased = state.initialized && current < state.current;
        const bool valueIncreased = state.initialized && current > state.current;

        state.current = current;
        state.modified = modified;
        state.initialized = true;

        // Y017: health healing is actionable feedback and must wake the HP bar even
        // when it has already auto-hidden. Magicka/fatigue keep wakeOnIncrease=false
        // so passive regeneration does not continuously restart their hide timer.
        if (firstUpdate || maximumChanged || valueDecreased || (wakeOnIncrease && valueIncreased))
        {
            state.idleTimer = 0.f;
            state.alpha = 1.f;
        }
    }

    void HUD::applyBarAlpha(MyGUI::Widget* widget, float alpha)
    {
        if (!widget)
            return;

        widget->setAlpha(std::max(0.f, std::min(1.f, alpha)));
    }

    void HUD::updateAutoHideBar(MyGUI::Widget* frame, AutoHideBarState& state, float dt,
        bool forceVisible, MyGUI::Widget* persistentIcon)
    {
        if (!frame || !state.initialized)
            return;

        const bool barEnabled = frame == mHealthFrame
            ? Settings::Manager::getBool("show health bar", "HUD")
            : (frame == mMagickaFrame
                ? Settings::Manager::getBool("show magicka bar", "HUD")
                : Settings::Manager::getBool("show stamina bar", "HUD"));

        if (!mHmsBaseVisible)
        {
            frame->setVisible(false);
            return;
        }

        const auto applyResourceState = [&](float alpha)
        {
            alpha = std::max(0.f, std::min(1.f, alpha));
            const std::string boxMode = persistentIcon ? getWeaponSpellBoxMode() : "hidden";
            const bool keepIcon = persistentIcon && boxMode != "hidden";

            if (!keepIcon)
            {
                frame->setVisible(alpha > 0.f);
                applyBarAlpha(frame, alpha);
                if (alpha > 0.f)
                {
                    for (unsigned int i = 0; i < frame->getChildCount(); ++i)
                    {
                        MyGUI::Widget* child = frame->getChildAt(i);
                        const bool iconAllowed = child != persistentIcon
                            || (persistentIcon == mWeapBox ? mWeaponVisible : mSpellVisible);
                        child->setVisible(iconAllowed);
                        applyBarAlpha(child, 1.f);
                    }
                }
                return;
            }

            // Weapon and spell boxes live inside the stamina/magicka frame. Keep the
            // parent alive, fade only the bar children, and apply the selected box mode.
            frame->setVisible(true);
            applyBarAlpha(frame, 1.f);
            for (unsigned int i = 0; i < frame->getChildCount(); ++i)
            {
                MyGUI::Widget* child = frame->getChildAt(i);
                if (child == persistentIcon)
                    continue;
                child->setVisible(alpha > 0.f);
                applyBarAlpha(child, alpha);
            }

            const bool iconAllowed = persistentIcon == mWeapBox ? mWeaponVisible : mSpellVisible;
            persistentIcon->setVisible(iconAllowed);
            const float persistentAlpha = boxMode == "visible" ? 1.f : 0.4f;
            applyBarAlpha(persistentIcon, std::max(persistentAlpha, alpha));
        };

        const std::string resourceMode = getResourceBarMode();
        if (!barEnabled || resourceMode == "hidden")
        {
            applyResourceState(0.f);
            return;
        }

        if (resourceMode == "always")
        {
            state.alpha = 1.f;
            applyResourceState(1.f);
            return;
        }

        // Keep the relevant resource bar visible for as long as the player is
        // actively holding a weapon or has magic readied. Start the normal
        // auto-hide delay only after the weapon/spell is put away.
        if (forceVisible)
        {
            state.idleTimer = 0.f;
            state.alpha = 1.f;
            applyResourceState(1.f);
            return;
        }

        state.idleTimer += dt;

        const bool isFull = state.modified <= 0 || state.current >= state.modified;
        const float hideDelay = isFull ? 7.f : 20.f;
        const float fadeDuration = 0.35f;

        float targetAlpha = 1.f;
        if (state.idleTimer > hideDelay)
            targetAlpha = std::max(0.f, 1.f - (state.idleTimer - hideDelay) / fadeDuration);

        state.alpha = targetAlpha;
        applyResourceState(state.alpha);
    }

    void HUD::setSelectedSpell(const std::string& spellId, int successChancePercent)
    {
        const ESM::Spell* spell =
            MWBase::Environment::get().getWorld()->getStore().get<ESM::Spell>().find(spellId);

        std::string spellName = spell->mName;
        if (spellName != mSpellName && mSpellVisible)
        {
            mWeaponSpellTimer = 5.0f;
            mSpellName = spellName;
            mWeaponSpellBox->setCaption(mSpellName);
            mWeaponSpellBox->setVisible(true);
        }

        mSpellStatus->setProgressRange(100);
        mSpellStatus->setProgressPosition(successChancePercent);



        mSpellBox->setUserString("ToolTipType", "Spell");
        mSpellBox->setUserString("Spell", spellId);

        // use the icon of the first effect
        const ESM::MagicEffect* effect =
            MWBase::Environment::get().getWorld()->getStore().get<ESM::MagicEffect>().find(spell->mEffects.mList.front().mEffectID);

        std::string icon = effect->mIcon;
        int slashPos = icon.rfind('\\');
        icon.insert(slashPos+1, "b_");
        icon = MWBase::Environment::get().getWindowManager()->correctIconPath(icon);

        mSpellImage->setSpellIcon(icon);
    }

    void HUD::setSelectedEnchantItem(const MWWorld::Ptr& item, int chargePercent)
    {
        std::string itemName = item.getClass().getName(item);
        if (itemName != mSpellName && mSpellVisible)
        {
            mWeaponSpellTimer = 5.0f;
            mSpellName = itemName;
            mWeaponSpellBox->setCaption(mSpellName);
            mWeaponSpellBox->setVisible(true);
        }

        mSpellStatus->setProgressRange(100);
        mSpellStatus->setProgressPosition(chargePercent);



        mSpellBox->setUserString("ToolTipType", "ItemPtr");
        mSpellBox->setUserData(MWWorld::Ptr(item));

        mSpellImage->setItem(item);
    }

    void HUD::setSelectedWeapon(const MWWorld::Ptr& item, int durabilityPercent)
    {
        std::string itemName = item.getClass().getName(item);
        if (itemName != mWeaponName && mWeaponVisible)
        {
            mWeaponSpellTimer = 5.0f;
            mWeaponName = itemName;
            mWeaponSpellBox->setCaption(mWeaponName);
            mWeaponSpellBox->setVisible(true);
        }

        mWeapBox->clearUserStrings();
        mWeapBox->setUserString("ToolTipType", "ItemPtr");
        mWeapBox->setUserData(MWWorld::Ptr(item));

        mWeapStatus->setProgressRange(100);
        mWeapStatus->setProgressPosition(durabilityPercent);



        mWeapImage->setItem(item);
    }

    void HUD::unsetSelectedSpell()
    {
        std::string spellName = "#{sNone}";
        if (spellName != mSpellName && mSpellVisible)
        {
            mWeaponSpellTimer = 5.0f;
            mSpellName = spellName;
            mWeaponSpellBox->setCaptionWithReplacing(mSpellName);
            mWeaponSpellBox->setVisible(true);
        }

        mSpellStatus->setProgressRange(100);
        mSpellStatus->setProgressPosition(0);


        mSpellImage->setItem(MWWorld::Ptr());
        mSpellBox->clearUserStrings();
    }

    void HUD::unsetSelectedWeapon()
    {
        std::string itemName = "#{sSkillHandtohand}";
        if (itemName != mWeaponName && mWeaponVisible)
        {
            mWeaponSpellTimer = 5.0f;
            mWeaponName = itemName;
            mWeaponSpellBox->setCaptionWithReplacing(mWeaponName);
            mWeaponSpellBox->setVisible(true);
        }

        mWeapStatus->setProgressRange(100);
        mWeapStatus->setProgressPosition(0);



        MWBase::World *world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();

        mWeapImage->setItem(MWWorld::Ptr());
        std::string icon = (player.getClass().getNpcStats(player).isWerewolf()) ? "icons\\k\\tx_werewolf_hand.dds" : "icons\\k\\stealth_handtohand.dds";
        mWeapImage->setIcon(icon);

        mWeapBox->clearUserStrings();
        mWeapBox->setUserString("ToolTipType", "Layout");
        mWeapBox->setUserString("ToolTipLayout", "HandToHandToolTip");
        mWeapBox->setUserString("Caption_HandToHandText", itemName);
        mWeapBox->setUserString("ImageTexture_HandToHandImage", icon);
    }

    void HUD::setCrosshairVisible(bool visible)
    {
        mCrosshairBaseVisible = visible;
        mCrosshair->setVisible(visible && Settings::Manager::getBool("crosshair", "HUD"));
    }

    void HUD::setCrosshairOwned(bool owned)
    {
        const int size = owned ? 32 : 64;
        mCrosshair->changeWidgetSkin(owned ? "HUD_Crosshair_Owned" : "HUD_Crosshair");

        // Keep both reticles exactly centred. The ownership hand is intentionally
        // half the size of the normal crosshair and must not inherit its 64x64 box.
        mCrosshair->setCoord(
            (mMainWidget->getWidth() - size) / 2,
            (mMainWidget->getHeight() - size) / 2,
            size, size);
    }

    void HUD::setHmsVisible(bool visible)
    {
        mHmsBaseVisible = visible;

        mHealth->setVisible(visible);
        mMagicka->setVisible(visible);
        mStamina->setVisible(visible);

        if (!visible)
        {
            mHealthFrame->setVisible(false);
            mMagickaFrame->setVisible(false);
            mFatigueFrame->setVisible(false);
        }
        else
        {
            registerBarChange(mHealthBarState, mHealthBarState.current, mHealthBarState.modified);
            registerBarChange(mMagickaBarState, mMagickaBarState.current, mMagickaBarState.modified);
            registerBarChange(mStaminaBarState, mStaminaBarState.current, mStaminaBarState.modified);

            mHealthFrame->setVisible(true);
            mMagickaFrame->setVisible(true);
            mFatigueFrame->setVisible(true);
            applyBarAlpha(mHealthFrame, 1.f);
            applyBarAlpha(mMagickaFrame, 1.f);
            applyBarAlpha(mFatigueFrame, 1.f);
        }

        updatePositions();
    }

    void HUD::setWeapVisible(bool visible)
    {
        mWeapBox->setVisible(visible);
        updatePositions();
    }

    void HUD::setSpellVisible(bool visible)
    {
        mSpellBox->setVisible(visible);
        updatePositions();
    }

    void HUD::setSneakVisible(bool visible)
    {
        mSneakBox->setVisible(visible);
        updatePositions();
    }

    void HUD::setEffectVisible(bool visible)
    {
        mEffectBaseVisible = visible;
        mEffectBox->setVisible(visible && Settings::Manager::getBool("show status effects", "HUD"));
        updatePositions();
    }

    void HUD::setMinimapVisible(bool visible)
    {
        mMinimapBaseVisible = visible;
        mMinimapBox->setVisible(visible && Settings::Manager::getBool("show minimap", "HUD"));
        updatePositions();
    }

    void HUD::updatePositions()
    {
        int weapDx = 0, spellDx = 0;
        if (!mHealth->getVisible())
            spellDx = weapDx = mWeapBoxBaseLeft - mHealthManaStaminaBaseLeft;

        if (!mWeapBox->getVisible())
            spellDx += mSpellBoxBaseLeft - mWeapBoxBaseLeft;

        mWeaponVisible = mWeapBox->getVisible();
        mSpellVisible = mSpellBox->getVisible();
        if (!mWeaponVisible && !mSpellVisible)
            mWeaponSpellBox->setVisible(false);

        mWeapBox->setPosition(mWeapBoxBaseLeft - weapDx, mWeapBox->getTop());
        mSpellBox->setPosition(mSpellBoxBaseLeft - spellDx, mSpellBox->getTop());

        const MyGUI::IntSize& viewSize = MyGUI::RenderManager::getInstance().getViewSize();
        mSneakBox->setPosition((viewSize.width - mSneakBox->getWidth()) / 2,
                               (viewSize.height - mSneakBox->getHeight()) / 2);

        if (mHorizontalCompass && mGameTimeBox)
        {
            // One centered information line directly under the horizontal compass.
            // It is deliberately wider than the old clock so long location names fit.
            const int infoWidth = std::max(360, mHorizontalCompass->getWidth());
            constexpr int infoHeight = 28;
            constexpr int infoGap = 1;
            const int infoLeft = (viewSize.width - infoWidth) / 2;
            const int infoTop = mHorizontalCompass->getTop() + mHorizontalCompass->getHeight() + infoGap;
            mGameTimeBox->setCoord(infoLeft, infoTop, infoWidth, infoHeight);
            mGameTimeBox->setFontHeight(18);
            mGameTimeBox->setTextAlign(MyGUI::Align::Center);
        }
        if (mCellNameClip)
            mCellNameClip->setVisible(false);
        if (mCellNameBox)
            mCellNameBox->setVisible(false);

        // effect box can have variable width -> variable left coordinate
        int effectsDx = 0;
        if (!mMinimapBox->getVisible ())
            effectsDx = mEffectBoxBaseRight - mMinimapBoxBaseRight;

        mMapVisible = mMinimapBox->getVisible ();

        mEffectBox->setPosition((viewSize.width - mEffectBoxBaseRight) - mEffectBox->getWidth() + effectsDx, mEffectBox->getTop());
    }

    void HUD::setFocusObject(const MWWorld::Ptr& focus)
    {
        const bool validActor = !focus.isEmpty() && focus.getClass().isActor()
            && focus != MWMechanics::getPlayer()
            && !focus.getClass().getCreatureStats(focus).isDead();

        if (validActor)
        {
            if (mFocusActor.isEmpty() || mFocusActor != focus)
            {
                mFocusActor = focus;
                mFocusActorPanelAlpha = 0.f;
                mTargetPanelPositionInitialized = false;
            }
            mFocusActorCurrentlyFaced = true;
            mFocusActorDistance = MWBase::Environment::get().getWorld()->getDistanceToFacedObject();
        }
        else
        {
            // Keep the previous actor while the panel fades out. Clearing it immediately
            // caused name, level and health to blink whenever the activation ray briefly
            // left an animated actor for one frame.
            mFocusActorCurrentlyFaced = false;
            mFocusActorDistance = -1.f;
        }
    }

    void HUD::setFocusObjectScreenCoords(float min_x, float min_y, float max_x, float max_y)
    {
        mFocusActorScreenX = (min_x + max_x) * 0.5f;
        mFocusActorScreenY = min_y;
    }

    void HUD::updateFocusedTargetPanel(float dt)
    {
        dt = std::max(0.f, dt);

        bool actorAlive = !mFocusActor.isEmpty();
        if (actorAlive)
        {
            actorAlive = mFocusActor.isInCell()
                && mFocusActor.getRefData().getCount() > 0
                && mFocusActor.getRefData().isEnabled()
                && !mFocusActor.getRefData().isDeleted()
                && !mFocusActor.getClass().getCreatureStats(mFocusActor).isDead();
        }

        MWBase::World* world = MWBase::Environment::get().getWorld();
        const bool dialogueOpen = MWBase::Environment::get().getWindowManager()->containsMode(GM_Dialogue);
        const bool hoverEnabled = npcBarShowsHover(getNpcBarMode());
        bool panelShouldBeVisible = false;

        bool focusActorInCombat = false;
        if (actorAlive)
        {
            for (const CombatHealthBarState& state : mCombatHealthBars)
            {
                if (!state.mActor.isEmpty() && state.mActor == mFocusActor)
                {
                    focusActorInCombat = true;
                    break;
                }
            }
        }

        const MyGUI::IntSize& viewSize = MyGUI::RenderManager::getInstance().getViewSize();
        if (actorAlive && !focusActorInCombat
            && mFocusActorCurrentlyFaced && hoverEnabled && !dialogueOpen && world)
        {
            // The target panel is intentionally binary: fully visible or fully hidden.
            // Do not leave semi-transparent text or health bars over the actor's face.
            const float horizontalOffset = std::abs(mFocusActorScreenX - 0.5f);
            constexpr float centreVisibleLimit = 0.46f;
            bool withinVisibleDistance = true;
            bool hidePeacefulActorAtPointBlank = false;

            if (mFocusActorDistance >= 0.f)
            {
                const float maximumDistance = std::max(1.f, world->getMaxActivationDistance());
                withinVisibleDistance = mFocusActorDistance <= maximumDistance * 1.05f;

                const MWWorld::Ptr player = MWMechanics::getPlayer();
                const MWMechanics::CreatureStats& targetStats
                    = mFocusActor.getClass().getCreatureStats(mFocusActor);
                const bool aggressiveTarget = targetStats.getAiSequence().isInCombat(player)
                    || MWBase::Environment::get().getMechanicsManager()->isAggressive(mFocusActor, player);

                // Only first-person needs the close-range face protection. In third-person,
                // preview and vanity modes the panel remains visible even at point-blank range.
                if (world->isFirstPerson() && !aggressiveTarget)
                {
                    // Hide only when the peaceful actor is directly in the player's face.
                    // Hysteresis prevents rapid toggling when standing on the boundary.
                    const float closeHideDistance = std::min(52.f, maximumDistance * 0.27f);
                    const float closeShowDistance = std::max(closeHideDistance + 14.f,
                        std::min(68.f, maximumDistance * 0.35f));
                    hidePeacefulActorAtPointBlank = mFocusActorPanelAlpha > 0.5f
                        ? mFocusActorDistance <= closeHideDistance
                        : mFocusActorDistance < closeShowDistance;
                }
            }

            panelShouldBeVisible = horizontalOffset <= centreVisibleLimit
                && withinVisibleDistance
                && !hidePeacefulActorAtPointBlank;

            // Keep the panel stable under the compass. It follows the actor slightly
            // from side to side, but ignores vertical animation of the bounding box.
            const float screenCentre = viewSize.width * 0.5f;
            const float maximumTravel = viewSize.width * 0.12f;
            const float actorOffset = (mFocusActorScreenX - 0.5f) * viewSize.width * 0.35f;
            const float desiredCentre = screenCentre
                + std::max(-maximumTravel, std::min(maximumTravel, actorOffset));

            if (!mTargetPanelPositionInitialized)
            {
                mTargetPanelCenterX = desiredCentre;
                mTargetPanelPositionInitialized = true;
            }
            else
            {
                const float positionBlend = 1.f - std::exp(-7.f * dt);
                mTargetPanelCenterX += (desiredCentre - mTargetPanelCenterX) * positionBlend;
            }
        }

        // No intermediate opacity. Appearance is now immediate (more than three times
        // faster than the previous fade), and close-range hiding is immediate as well.
        mFocusActorPanelAlpha = panelShouldBeVisible ? 1.f : 0.f;

        if ((!actorAlive || !mFocusActorCurrentlyFaced) && mFocusActorPanelAlpha < 0.01f)
        {
            mFocusActor = MWWorld::Ptr();
            mFocusActorPanelAlpha = 0.f;
            mTargetPanelPositionInitialized = false;
        }
    }

    void HUD::pushDamageNumber(float damage, bool combo)
    {
        // Y025: damage feedback is anchored to the screen centre, not to the
        // transient visibility state of the crosshair widget. ArenaMP may hide or
        // rebuild the reticle in the exact frame a confirmed hit arrives.
        if (damage <= 0.f || !mGameplayHud || !mGameplayHud->getVisible()
            || mFloatingDamageNumbers.empty())
            return;

        FloatingDamageState* slot = nullptr;
        for (FloatingDamageState& state : mFloatingDamageNumbers)
        {
            if (!state.mActive)
            {
                slot = &state;
                break;
            }
        }

        // If every slot is busy, recycle the oldest one. This is preferable to
        // allocating a widget in the middle of a rapid multi-hit sequence.
        if (!slot)
        {
            slot = &mFloatingDamageNumbers.front();
            for (FloatingDamageState& state : mFloatingDamageNumbers)
                if (state.mAge > slot->mAge)
                    slot = &state;
        }

        const std::uint64_t sequence = mFloatingDamageSequence++;
        slot->mAge = 0.f;
        slot->mLifetime = 0.85f;
        slot->mSide = (sequence & 1u) ? 1.f : -1.f;
        const int lane = static_cast<int>((sequence / 2u) % 3u) - 1;
        slot->mLaneOffset = static_cast<float>(lane * 7);
        slot->mActive = true;

        // Weapon damage in Morrowind can be fractional internally, but the compact
        // RPG readout deliberately uses the nearest real HP point.
        const int shownDamage = std::max(1, static_cast<int>(std::lround(damage)));
        slot->mWidget->setSize(combo ? 160 : 78, 26);
        slot->mWidget->setTextColour(combo ? MyGUI::Colour(1.f, 0.55f, 0.10f)
                                         : MyGUI::Colour(1.f, 0.18f, 0.16f));
        const std::string label = combo
            ? MyGUI::LanguageManager::getInstance().replaceTags("#{arenamp=combat.combo}").asUTF8() + " "
            : std::string();
        slot->mWidget->setCaption(label + "-" + MyGUI::utility::toString(shownDamage));
        slot->mWidget->setAlpha(1.f);
        slot->mWidget->setVisible(true);
    }

    void HUD::updateFloatingDamageNumbers(float dt)
    {
        if (mFloatingDamageNumbers.empty())
            return;

        const MyGUI::IntSize& view = MyGUI::RenderManager::getInstance().getViewSize();
        const float centreX = static_cast<float>(view.width) * 0.5f;
        const float centreY = static_cast<float>(view.height) * 0.5f;

        for (FloatingDamageState& state : mFloatingDamageNumbers)
        {
            if (!state.mActive)
                continue;

            state.mAge += std::max(0.f, dt);
            const float t = std::min(1.f, state.mAge / std::max(0.01f, state.mLifetime));
            if (t >= 1.f)
            {
                state.mActive = false;
                state.mWidget->setVisible(false);
                continue;
            }

            // Start just outside the 64 px normal reticle, drift a little farther
            // sideways and upwards, then dissolve. Font height 15 keeps the red
            // negative damage readout subordinate to the reticle itself.
            const float outward = 40.f + 28.f * t + std::max(0.f, (state.mWidget->getWidth() - 78) * 0.5f);
            const float rise = 24.f * t;
            const float x = centreX + state.mSide * outward - state.mWidget->getWidth() * 0.5f;
            const float y = centreY - 13.f + state.mLaneOffset - rise;
            state.mWidget->setPosition(static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y)));

            // Keep the first instant crisp, then fade progressively faster near the end.
            const float fadeT = std::max(0.f, (t - 0.12f) / 0.88f);
            state.mWidget->setAlpha(1.f - fadeT * fadeT);
        }
    }

    void HUD::hideCombatHealthBars()
    {
        mCombatHealthBarScanTimer = 0.f;
        for (CombatHealthBarState& state : mCombatHealthBars)
        {
            state.mActor = MWWorld::Ptr();
            state.mAlly = false;
            // X024: clear the smoothed geometry too. Keeping it would make the next
            // actor to land in this slot start its fade-in at the previous actor's
            // screen position and slide across the view.
            state.mHasScreenState = false;
            state.mAlpha = 0.f;
            state.mTargetAlpha = 0.f;
            state.mDisplayHealth = -1.f;
            state.mLingerTimer = 0.f;
            state.mHasValidHealth = false;
            state.mFrameHealthFraction = 0.f;
            state.mDocked = false;
            state.mDockBlend = 0.f;
            state.mDockSwitchTimer = 0.f;
            state.mDockRow = -1;
            state.mNameCaption.clear();
            if (state.mWidget)
                state.mWidget->setVisible(false);
            if (state.mFill)
                state.mFill->setVisible(false);
            if (state.mFrame)
                state.mFrame->setVisible(false);
            if (state.mName)
                state.mName->setVisible(false);
        }
    }

    void HUD::applyCombatHealthBar(CombatHealthBarState& state, float dt)
    {
        MyGUI::Widget* bar = state.mWidget;
        MyGUI::Widget* fill = state.mFill;
        if (!bar || !fill)
            return;

        // Y027: a combat slot is drawable only when both the validity marker and
        // the current health fraction agree. These values are updated on different
        // paths, so treating either one alone as authoritative can expose a docked
        // decorative frame while the actual fill is empty.
        if (!state.mHasValidHealth || !(state.mFrameHealthFraction > 0.f))
        {
            bar->setVisible(false);
            fill->setVisible(false);
            if (state.mFrame)
                state.mFrame->setVisible(false);
            if (state.mName)
                state.mName->setVisible(false);
            return;
        }

        const float fadeTau = state.mTargetAlpha > state.mAlpha
            ? CombatBar::sFadeInTime : CombatBar::sFadeOutTime;
        state.mAlpha = CombatBar::approach(state.mAlpha, state.mTargetAlpha, fadeTau, dt);

        if (state.mAlpha <= 0.004f || !state.mHasScreenState)
        {
            state.mAlpha = std::max(0.f, state.mAlpha);
            if (state.mTargetAlpha <= 0.f)
            {
                state.mAlpha = 0.f;
                state.mHasScreenState = false;
                state.mDocked = false;
                state.mDockBlend = 0.f;
                state.mDockRow = -1;
                state.mFrameHealthFraction = 0.f;
                state.mHasValidHealth = false;
            }
            bar->setVisible(false);
            fill->setVisible(false);
            if (state.mFrame)
                state.mFrame->setVisible(false);
            if (state.mName)
                state.mName->setVisible(false);
            return;
        }

        // If this frame temporarily failed to resolve the actor, keep the last
        // verified HP fraction during the existing linger/fade window. A fresh slot
        // still has no screen state, so it cannot expose an empty frame.

        const int width = std::max(CombatBar::sHeadMinWidthPixels,
            static_cast<int>(std::lround(state.mWidth)));
        const int height = std::max(CombatBar::sHeadMinHeightPixels,
            static_cast<int>(std::lround(state.mHeight)));
        const int left = static_cast<int>(std::lround(state.mCentreX - width * 0.5f));
        const int top = static_cast<int>(std::lround(state.mCentreY - height * 0.5f));
        const float alpha = std::min(1.f, state.mAlpha);

        bar->setCoord(left, top, width, height);
        bar->setVisible(true);

        // Y028: overhead is always a bare red line. The decorative frame fades in
        // continuously during the latter part of the trip to the HUD and fades out
        // immediately as the same dockBlend runs backwards toward the actor's head.
        // This avoids the old hard on/off threshold at 0.985.
        const float frameRevealLinear = CombatBar::clamp01(
            (state.mDockBlend - CombatBar::sFrameFadeStart)
            / std::max(0.001f, 1.f - CombatBar::sFrameFadeStart));
        const float frameReveal = frameRevealLinear * frameRevealLinear
            * (3.f - 2.f * frameRevealLinear); // smoothstep
        const int inset = static_cast<int>(std::lround(2.f * frameReveal));
        const int fillHeight = std::max(1, height - inset * 2);
        const int availableWidth = std::max(1, width - inset * 2);
        const float healthFraction = CombatBar::clamp01(state.mFrameHealthFraction);
        int fillWidth = static_cast<int>(std::lround(availableWidth * healthFraction));
        if (healthFraction > 0.f)
            fillWidth = std::max(1, fillWidth);
        fillWidth = std::min(availableWidth, std::max(0, fillWidth));

        if (fillWidth > 0)
        {
            fill->setCoord(inset, inset, fillWidth, fillHeight);
            fill->setAlpha(alpha);
            fill->setVisible(true);
        }
        else
        {
            fill->setVisible(false);
        }

        if (state.mFrame)
        {
            state.mFrame->setCoord(0, 0, width, height);
            const float frameAlpha = alpha * frameReveal;
            state.mFrame->setAlpha(frameAlpha);
            // Structural safety rule retained from Y027: a frame is never visible
            // without a visible fill, even while its alpha is being interpolated.
            state.mFrame->setVisible(frameAlpha > 0.01f && fillWidth > 0);
        }

        if (state.mName)
        {
            // Name remains a dock-only affordance. It fades during the final part
            // of the trip, while the actual frame appears only at the destination.
            const float nameReveal = CombatBar::clamp01(
                (state.mDockBlend - CombatBar::sNameFadeStart)
                / std::max(0.001f, 1.f - CombatBar::sNameFadeStart));
            const float nameAlpha = alpha * nameReveal;

            if (nameAlpha <= 0.01f || state.mNameCaption.empty())
            {
                state.mName->setVisible(false);
            }
            else
            {
                const int nameWidth = std::max(width, CombatBar::sDockWidth);
                state.mName->setCoord(left,
                    top - CombatBar::sDockNameGap - CombatBar::sDockNameHeight,
                    nameWidth, CombatBar::sDockNameHeight);
                state.mName->setAlpha(nameAlpha);
                state.mName->setVisible(true);
            }
        }
    }

    void HUD::updateCombatHealthBars(float dt)
    {
        dt = std::max(0.f, dt);

        const bool enabled = npcBarShowsCombat(getNpcBarMode())
            && mGameplayHud && mGameplayHud->getVisible()
            && !MWBase::Environment::get().getWindowManager()->containsMode(GM_Dialogue);
        if (!enabled)
        {
            hideCombatHealthBars();
            return;
        }

        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWBase::MechanicsManager* mechanics = MWBase::Environment::get().getMechanicsManager();
        if (!world || !mechanics)
        {
            hideCombatHealthBars();
            return;
        }

        const MWWorld::Ptr player = world->getPlayerPtr();
        if (player.isEmpty() || !player.isInCell())
        {
            mCombatPlayerCell = nullptr;
            hideCombatHealthBars();
            return;
        }

        // Y025: world-space combat widgets must not survive a CellStore hand-off.
        // In particular exterior -> interior -> exterior used to preserve a docked
        // frame while the new cell had not supplied valid actor stats yet.
        MWWorld::CellStore* playerCell = player.getCell();
        if (mCombatPlayerCell != playerCell)
        {
            hideCombatHealthBars();
            mCombatPlayerCell = playerCell;
        }

        // Rebuild only the participant list at 10 Hz. Projection, distance scaling
        // and health values still update every frame, so bars stay attached to heads.
        mCombatHealthBarScanTimer -= dt;
        if (mCombatHealthBarScanTimer <= 0.f)
        {
            mCombatHealthBarScanTimer = 0.10f;

            struct Candidate
            {
                MWWorld::Ptr mActor;
                bool mAlly = false;
                float mDistanceSquared = 0.f;
            };

            std::map<MWWorld::Ptr, bool> participants; // false = enemy, true = ally
            for (const MWWorld::Ptr& enemy : mechanics->getActorsFighting(player))
            {
                if (!enemy.isEmpty())
                    participants[enemy] = false;
            }

            // Y022: this combat HP system is enemy-only. Friendly actors retain
            // their compass relationship markers but do not consume combat-bar slots.

            const osg::Vec3f playerPosition = player.getRefData().getPosition().asVec3();
            constexpr float maximumBarDistanceSquared
                = CombatBar::sVanishDistance * CombatBar::sVanishDistance;

            std::vector<Candidate> candidates;
            candidates.reserve(participants.size());
            for (const auto& participant : participants)
            {
                const MWWorld::Ptr& actor = participant.first;
                if (actor == player || !sharesCombatSpace(actor, player))
                    continue;
                if (actor.getRefData().getCount() <= 0 || !actor.getRefData().isEnabled()
                    || actor.getRefData().isDeleted() || !actor.getClass().isActor())
                    continue;

                const MWMechanics::CreatureStats& stats = actor.getClass().getCreatureStats(actor);
                if (stats.isDead() || stats.getHealth().getCurrent() <= 0.f)
                    continue;
                if (!world->getLOS(player, actor))
                    continue;

                const osg::Vec3f delta = actor.getRefData().getPosition().asVec3() - playerPosition;
                const float distanceSquared = delta.length2();
                if (distanceSquared > maximumBarDistanceSquared)
                    continue;

                Candidate candidate;
                candidate.mActor = actor;
                candidate.mAlly = false;
                candidate.mDistanceSquared = distanceSquared;
                candidates.push_back(candidate);
            }

            std::sort(candidates.begin(), candidates.end(),
                [](const Candidate& left, const Candidate& right)
                {
                    return left.mDistanceSquared < right.mDistanceSquared;
                });

            const std::size_t capacity = mCombatHealthBars.size();
            if (candidates.size() > capacity)
                candidates.resize(capacity);

            // Keep every actor on the widget it already owns. Re-filling the pool in
            // distance order made two participants swap widgets whenever they traded
            // places, which swapped their colours for a frame and reset the progress
            // bar in the middle of a fight.
            std::vector<bool> slotTaken(capacity, false);
            std::vector<bool> candidatePlaced(candidates.size(), false);

            for (std::size_t i = 0; i < capacity; ++i)
            {
                CombatHealthBarState& state = mCombatHealthBars[i];
                if (state.mActor.isEmpty())
                    continue;

                bool stillFighting = false;
                for (std::size_t c = 0; c < candidates.size(); ++c)
                {
                    if (candidatePlaced[c] || candidates[c].mActor != state.mActor)
                        continue;

                    state.mAlly = candidates[c].mAlly;
                    candidatePlaced[c] = true;
                    slotTaken[i] = true;
                    stillFighting = true;
                    break;
                }

                if (!stillFighting)
                {
                    // X025: the widget is no longer switched off here. Leaving combat
                    // goes through the same fade as everything else, and the row this
                    // actor held in the stack is released at once so the entries above
                    // it start sliding down immediately.
                    state.mActor = MWWorld::Ptr();
                    state.mDocked = false;
                    state.mDockRow = -1;
                }
            }

            // X025: prefer whichever free slot is furthest through its fade-out.
            // Dropping a new actor onto a slot that is still visibly fading would
            // otherwise make the bar slide in from the previous owner's position.
            std::vector<std::size_t> freeSlots;
            freeSlots.reserve(capacity);
            for (std::size_t i = 0; i < capacity; ++i)
            {
                if (!slotTaken[i])
                    freeSlots.push_back(i);
            }

            std::sort(freeSlots.begin(), freeSlots.end(),
                [this](std::size_t left, std::size_t right)
                {
                    return mCombatHealthBars[left].mAlpha < mCombatHealthBars[right].mAlpha;
                });

            std::size_t nextFree = 0;
            for (std::size_t c = 0; c < candidates.size(); ++c)
            {
                if (candidatePlaced[c])
                    continue;
                if (nextFree >= freeSlots.size())
                    break;

                CombatHealthBarState& state = mCombatHealthBars[freeSlots[nextFree]];
                state.mActor = candidates[c].mActor;
                state.mAlly = candidates[c].mAlly;
                // A fresh occupant starts from scratch: no inherited screen position,
                // no inherited row in the stack, no inherited health reading.
                state.mHasScreenState = false;
                state.mAlpha = 0.f;
                state.mTargetAlpha = 0.f;
                state.mDisplayHealth = -1.f;
                state.mLingerTimer = 0.f;
                state.mDocked = false;
                state.mDockBlend = 0.f;
                state.mDockSwitchTimer = 0.f;
                state.mDockRow = -1;
                state.mHasValidHealth = false;
                state.mFrameHealthFraction = 0.f;
                state.mNameCaption.clear();
                if (state.mFill)
                    state.mFill->setVisible(false);
                if (state.mFrame)
                    state.mFrame->setVisible(false);

                state.mFrameHealthFraction = 0.f;
                slotTaken[freeSlots[nextFree]] = true;
                ++nextFree;
            }

        }

        const MyGUI::IntSize& viewSize = MyGUI::RenderManager::getInstance().getViewSize();
        const osg::Vec3f playerPosition = player.getRefData().getPosition().asVec3();
        const float viewWidth = static_cast<float>(std::max(1, viewSize.width));
        const float viewHeight = static_cast<float>(std::max(1, viewSize.height));

        // ------------------------------------------------------------------
        // X025 pass 1 - resolve every slot and work out its overhead anchor.
        //
        // Nothing is pushed into a widget yet: the docked placement of one bar
        // depends on how many other bars are docked, so the geometry can only be
        // finished once every slot has stated what it wants.
        // ------------------------------------------------------------------
        std::size_t dockedCount = 0;
        for (const CombatHealthBarState& state : mCombatHealthBars)
        {
            if (state.mDocked)
                ++dockedCount;
        }

        for (CombatHealthBarState& state : mCombatHealthBars)
        {
            state.mFrameResolved = false;
            state.mFrameDrop = false;
            state.mFrameAlpha = 1.f;
            state.mFrameDistance = 0.f;

            MyGUI::Widget* bar = state.mWidget;
            if (!bar)
                continue;

            // A slot that cannot be drawn right now is not switched off on the spot.
            // It keeps its last smoothed placement for a short grace period and then
            // fades, so a momentary LOS break, a scan boundary or a participant
            // leaving combat never produces a blink.
            const MWWorld::Ptr& actor = state.mActor;
            if (actor.isEmpty()
                || !sharesCombatSpace(actor, player)
                || actor.getRefData().getCount() <= 0 || !actor.getRefData().isEnabled()
                || actor.getRefData().isDeleted() || !actor.getClass().isActor())
            {
                state.mFrameDrop = true;
                continue;
            }

            MWMechanics::CreatureStats& stats = actor.getClass().getCreatureStats(actor);
            const float maximumHealth = stats.getHealth().getModified();
            const float currentHealth = stats.getHealth().getCurrent();
            if (stats.isDead() || !std::isfinite(maximumHealth) || !std::isfinite(currentHealth)
                || maximumHealth <= 0.f || currentHealth <= 0.f)
            {
                // Death still fades rather than blinking, but without the grace
                // period: there is nothing left to track.
                state.mLingerTimer = 0.f;
                continue;
            }

            const float distance
                = (actor.getRefData().getPosition().asVec3() - playerPosition).length();
            if (distance >= CombatBar::sVanishDistance)
                continue;

            // Size falls off with distance: full size inside the melee radius,
            // shrinking towards sHeadScaleMin as the actor walks away, with the
            // alpha ramp below finishing the job at the vanishing distance.
            const float distanceT = CombatBar::clamp01(
                (distance - CombatBar::sFullSizeDistance)
                / (CombatBar::sVanishDistance - CombatBar::sFullSizeDistance));
            const float scale = std::max(CombatBar::sHeadScaleMin,
                1.f - (1.f - CombatBar::sHeadScaleMin) * distanceT);

            state.mHeadWidth = CombatBar::sHeadWidthMax * scale;
            state.mHeadHeight = CombatBar::sHeadHeightMax * scale;

            float minX = 0.f;
            float minY = 0.f;
            float maxX = 0.f;
            float maxY = 0.f;
            bool anchorUsable = false;

            if (world->getObjectScreenBounds(actor, minX, minY, maxX, maxY))
            {
                // Sit on top of the hit box, and stay there: instead of culling the
                // bar once the head leaves the top of the view, clamp it to the
                // edge. That is exactly the case of a tall or very close actor,
                // where losing the bar would be worst.
                const float headCentreX = (minX + maxX) * 0.5f * viewWidth;
                float headCentreY = minY * viewHeight - state.mHeadHeight * 0.5f
                    - (CombatBar::sHeadClearance + CombatBar::sHeadClearance * scale);
                headCentreY = std::max(headCentreY,
                    state.mHeadHeight * 0.5f + CombatBar::sScreenMargin);

                anchorUsable = headCentreX > -state.mHeadWidth
                    && headCentreX < viewWidth + state.mHeadWidth
                    && headCentreY < viewHeight + state.mHeadHeight;

                if (anchorUsable)
                {
                    state.mHeadCentreX = headCentreX;
                    state.mHeadCentreY = headCentreY;
                }
            }

            // A docked bar survives without a usable anchor. Turning your back on
            // the enemy you are trading blows with must not empty the stack.
            if (!anchorUsable && !state.mDocked)
                continue;

            state.mFrameDistance = distance;
            state.mFrameResolved = true;

            // Y022: the same stable ProgressBar skin is used for every distance.
            // No live skin replacement occurs while the actor approaches the HUD.

            if (distance > CombatBar::sFadeOutStartDistance)
                state.mFrameAlpha = CombatBar::clamp01(
                    (CombatBar::sVanishDistance - distance)
                    / (CombatBar::sVanishDistance - CombatBar::sFadeOutStartDistance));

            // Dock decision: two thresholds plus a dwell timer. An actor pacing
            // around the 15 pace mark holds the new side for sDockSwitchDelay before
            // anything moves, and only gives the row back at 18 paces.
            const bool wantsDock = state.mDocked
                ? distance < CombatBar::sDockExitDistance
                : distance < CombatBar::sDockEnterDistance;

            if (wantsDock == state.mDocked)
            {
                state.mDockSwitchTimer = 0.f;
            }
            else
            {
                state.mDockSwitchTimer += dt;
                if (state.mDockSwitchTimer >= CombatBar::sDockSwitchDelay)
                {
                    state.mDockSwitchTimer = 0.f;
                    if (!wantsDock)
                    {
                        state.mDocked = false;
                        state.mDockRow = -1;
                        if (dockedCount > 0)
                            --dockedCount;
                    }
                    else if (dockedCount < CombatBar::sDockMaxRows)
                    {
                        // The stack is capped on purpose. Anything that does not fit
                        // simply keeps its overhead bar instead of covering the view.
                        state.mDocked = true;
                        state.mDockSequence = ++mCombatDockSequenceCounter;
                        ++dockedCount;
                    }
                }
            }

            // Health is smoothed too, so a hit slides the bar down instead of
            // snapping it. A near-full swing is treated as a heal or a respawn and
            // is applied at once.
            if (state.mDisplayHealth < 0.f
                || std::abs(state.mDisplayHealth - currentHealth) > maximumHealth * 0.9f)
                state.mDisplayHealth = currentHealth;
            else
                state.mDisplayHealth = CombatBar::approach(
                    state.mDisplayHealth, currentHealth, CombatBar::sSmoothTauHealth, dt);

            const float healthFraction = CombatBar::clamp01(state.mDisplayHealth / maximumHealth);
            state.mFrameHealthFraction = healthFraction;
            state.mHasValidHealth = healthFraction > 0.f;

            if (state.mName)
            {
                const std::string caption = actor.getClass().getName(actor);
                if (caption != state.mNameCaption)
                {
                    state.mNameCaption = caption;
                    state.mName->setCaption(caption);
                }
            }
        }

        // ------------------------------------------------------------------
        // X025 pass 2 - order the stack and find where it starts.
        //
        // Rows are handed out by the order in which actors joined, never by
        // distance: sorting a live list by distance makes entries swap places every
        // time two enemies trade positions, which is the single most unstable thing
        // such a panel can do. When an entry leaves, the ones above it slide down
        // under the ordinary position smoothing.
        // ------------------------------------------------------------------
        std::vector<std::size_t> dockedSlots;
        dockedSlots.reserve(CombatBar::sDockMaxRows);
        for (std::size_t i = 0; i < mCombatHealthBars.size(); ++i)
        {
            if (mCombatHealthBars[i].mDocked)
                dockedSlots.push_back(i);
        }

        std::sort(dockedSlots.begin(), dockedSlots.end(),
            [this](std::size_t left, std::size_t right)
            {
                return mCombatHealthBars[left].mDockSequence
                    < mCombatHealthBars[right].mDockSequence;
            });

        for (std::size_t row = 0; row < dockedSlots.size(); ++row)
            mCombatHealthBars[dockedSlots[row]].mDockRow = static_cast<int>(row);

        // Anchor: the bottom left corner of the stack, sitting directly above the
        // stamina bar and growing upwards. Read from the widget rather than
        // hardcoded, so it follows the HUD layout and any GUI scaling.
        float dockLeft = static_cast<float>(CombatBar::sScreenMargin);
        float dockBottom = viewHeight * 0.5f;
        {
            MyGUI::IntPoint origin;
            if (mGameplayHud)
                origin = mGameplayHud->getAbsolutePosition();
            if (mFatigueFrame)
            {
                const MyGUI::IntCoord frame = mFatigueFrame->getAbsoluteCoord();
                dockLeft = static_cast<float>(frame.left - origin.left);
                dockBottom = static_cast<float>(frame.top - origin.top) - CombatBar::sDockGap;
            }
        }
        dockLeft = std::max(static_cast<float>(CombatBar::sScreenMargin),
            std::min(dockLeft, viewWidth - CombatBar::sDockWidth - CombatBar::sScreenMargin));

        // ------------------------------------------------------------------
        // X025 pass 3 - blend head anchor against stack row, smooth, draw.
        // ------------------------------------------------------------------
        for (CombatHealthBarState& state : mCombatHealthBars)
        {
            if (!state.mWidget)
                continue;

            if (state.mFrameResolved)
            {
                const float dockTarget = state.mDocked ? 1.f : 0.f;
                state.mDockBlend = CombatBar::approach(
                    state.mDockBlend, dockTarget, CombatBar::sSmoothTauDock, dt);
                if (state.mDockBlend < 0.002f)
                    state.mDockBlend = 0.f;
                else if (state.mDockBlend > 0.998f)
                    state.mDockBlend = 1.f;

                float targetCentreX = state.mHeadCentreX;
                float targetCentreY = state.mHeadCentreY;
                float targetWidth = state.mHeadWidth;
                float targetHeight = state.mHeadHeight;

                if (state.mDockBlend > 0.f)
                {
                    const int row = std::max(0, state.mDockRow);
                    const float rowBottom
                        = dockBottom - row * static_cast<float>(CombatBar::sDockRowStride);
                    const float dockCentreX = dockLeft + CombatBar::sDockWidth * 0.5f;
                    const float dockCentreY = rowBottom - CombatBar::sDockBarHeight * 0.5f;

                    targetCentreX = CombatBar::lerp(targetCentreX, dockCentreX, state.mDockBlend);
                    targetCentreY = CombatBar::lerp(targetCentreY, dockCentreY, state.mDockBlend);
                    targetWidth = CombatBar::lerp(targetWidth,
                        static_cast<float>(CombatBar::sDockWidth), state.mDockBlend);
                    targetHeight = CombatBar::lerp(targetHeight,
                        static_cast<float>(CombatBar::sDockBarHeight), state.mDockBlend);
                }

                state.mLingerTimer = CombatBar::sLingerTime;
                state.mTargetAlpha = state.mFrameAlpha;

                if (!state.mHasScreenState)
                {
                    // First frame for this actor: start where it belongs and fade in.
                    state.mCentreX = targetCentreX;
                    state.mCentreY = targetCentreY;
                    state.mWidth = targetWidth;
                    state.mHeight = targetHeight;
                    state.mHasScreenState = true;
                }
                else
                {
                    // The teleport guard is suspended mid-transition: the dock target
                    // is already an interpolation, and letting it snap would defeat
                    // the whole point of the travel animation.
                    const float snapDistance = viewWidth * CombatBar::sSnapFraction;
                    const bool travelling = state.mDockBlend > 0.f && state.mDockBlend < 1.f;

                    if (!travelling
                        && (std::abs(targetCentreX - state.mCentreX) > snapDistance
                            || std::abs(targetCentreY - state.mCentreY) > snapDistance))
                    {
                        // Camera cut or teleport: never drag the bar across the view.
                        state.mCentreX = targetCentreX;
                        state.mCentreY = targetCentreY;
                    }
                    else
                    {
                        state.mCentreX = CombatBar::approach(
                            state.mCentreX, targetCentreX, CombatBar::sSmoothTauX, dt);
                        state.mCentreY = CombatBar::approach(
                            state.mCentreY, targetCentreY, CombatBar::sSmoothTauY, dt);
                    }

                    state.mWidth = CombatBar::approach(
                        state.mWidth, targetWidth, CombatBar::sSmoothTauSize, dt);
                    state.mHeight = CombatBar::approach(
                        state.mHeight, targetHeight, CombatBar::sSmoothTauSize, dt);
                }
            }
            else
            {
                if (state.mFrameDrop)
                    state.mLingerTimer = 0.f;
                else
                    state.mLingerTimer = std::max(0.f, state.mLingerTimer - dt);

                // Hold the last good placement while the grace period runs, then fade.
                if (state.mLingerTimer <= 0.f)
                {
                    state.mTargetAlpha = 0.f;
                    state.mDisplayHealth = -1.f;
                }
            }

            applyCombatHealthBar(state, dt);
        }
    }


    HUD::HudNotificationState* HUD::pushHudNotification(HudEventKind kind, const std::string& key,
        const std::string& icon, const std::string& title, int amount,
        const std::string& valueText, int totalCount)
    {
        if (title.empty() || mHudNotifications.empty())
            return nullptr;

        const auto updatePickupCaption = [](HudNotificationState& state)
        {
            if (!state.mValue)
                return;
            std::string caption = "+" + MyGUI::utility::toString(state.mAmount);
            if (state.mTotalCount > state.mAmount)
                caption += " (" + MyGUI::utility::toString(state.mTotalCount) + ")";
            state.mValue->setCaption(caption);
        };

        // Pickups of the same item (especially gold) coalesce while their card is
        // alive. This turns rapid +5/+20/+100 changes into one stable +125 card.
        if (kind == HudEventKind::Item || kind == HudEventKind::Gold)
        {
            for (HudNotificationState& state : mHudNotifications)
            {
                if (!state.mActive || state.mKind != kind || state.mKey != key)
                    continue;

                state.mAmount += amount;
                if (totalCount > 0)
                    state.mTotalCount = totalCount;
                state.mAge = 0.f;
                state.mSequence = ++mHudNotificationSequence;
                updatePickupCaption(state);
                return &state;
            }
        }

        HudNotificationState* target = nullptr;
        for (HudNotificationState& state : mHudNotifications)
        {
            if (!state.mActive)
            {
                target = &state;
                break;
            }
        }
        if (!target)
        {
            // Y084: never evict a still-running timed magic effect just because a
            // pickup/XP notification arrived. Prefer recycling the oldest non-magic
            // card. If every slot is an active timed effect, keep the effect stack
            // intact and drop the transient notification instead.
            auto oldestTransient = mHudNotifications.end();
            for (auto it = mHudNotifications.begin(); it != mHudNotifications.end(); ++it)
            {
                if (it->mKind == HudEventKind::Magic)
                    continue;
                if (oldestTransient == mHudNotifications.end()
                    || it->mSequence < oldestTransient->mSequence)
                    oldestTransient = it;
            }
            if (oldestTransient == mHudNotifications.end())
                return nullptr;
            target = &*oldestTransient;
        }

        target->mKind = kind;
        target->mKey = key;
        target->mTitleText = title;
        target->mValueText = valueText;
        target->mAmount = std::max(1, amount);
        target->mNumericAmount = 0.f;
        target->mTotalCount = totalCount > 0 ? totalCount : -1;
        target->mSpellId.clear();
        target->mSpellCasterActorId = -1;
        target->mSpellTimestampDay = -1;
        target->mSpellTimestampHour = -1.f;
        target->mAge = 0.f;
        target->mLifetime = kind == HudEventKind::Magic ? 4.8f : 4.2f;
        target->mSequence = ++mHudNotificationSequence;
        target->mActive = true;

        // A recycled XP slot must not tint the next pickup/spell card. Restore
        // the normal feed presentation first, then XP can deliberately restyle it.
        const MyGUI::Colour headerColour = MyGUI::Colour::parse(
            MyGUI::LanguageManager::getInstance().replaceTags("#{fontcolour=header}"));
        if (target->mShade)
        {
            target->mShade->setColour(MyGUI::Colour::Black);
            target->mShade->setAlpha(0.22f);
        }
        if (target->mTitle)
        {
            target->mTitle->setCaption(title);
            target->mTitle->setTextColour(headerColour);
            target->mTitle->setCoord(icon.empty()
                ? MyGUI::IntCoord(6, 1, 204, 36)
                : MyGUI::IntCoord(40, 1, 170, 36));
        }
        if (target->mValue)
        {
            target->mValue->setTextColour(headerColour);
            if (kind == HudEventKind::Gold || kind == HudEventKind::Item)
                updatePickupCaption(*target);
            else
                target->mValue->setCaption(valueText);
        }
        if (target->mIcon)
        {
            if (icon.empty())
            {
                target->mIcon->setImageTexture("");
                target->mIcon->setVisible(false);
            }
            else
            {
                std::string resolved = icon;
                try
                {
                    resolved = MWBase::Environment::get().getWindowManager()->correctIconPath(resolved);
                }
                catch (const std::exception&)
                {
                    resolved.clear();
                }
                target->mIcon->setImageTexture(resolved);
                target->mIcon->setVisible(true);
            }
        }
        if (target->mPanel)
        {
            target->mPanel->setAlpha(0.f);
            target->mPanel->setVisible(true);
        }
        return target;
    }


    void HUD::pushSystemNotification(const std::string& title, const std::string& value,
        const std::string& icon, const std::string& key)
    {
        if (title.empty())
            return;

        const std::string notificationKey = key.empty()
            ? "system:" + MyGUI::utility::toString(mHudNotificationSequence + 1)
            : key;
        HudNotificationState* state = pushHudNotification(
            HudEventKind::System, notificationKey, icon, title, 1, value);
        if (state)
            state->mLifetime = 4.4f;
    }

    void HUD::pushExperienceNotification(float amount, const std::string& reason)
    {
        if (!std::isfinite(amount) || mHudNotifications.empty())
            return;

        const bool neutral = std::fabs(amount) < 0.0001f;
        if (neutral && reason.empty())
            return;

        const std::string label = MyGUI::LanguageManager::getInstance().replaceTags(
            "#{arenamp=xp.label.experience}");
        // Y099: skill/training GMSTs may reset inline colour to black after
        // the skill name. Resolve translation first, remove embedded colours,
        // then escape literal hashes so the feed's white neutral style applies
        // to the complete message, regardless of the installed language pack.
        const MyGUI::UString resolvedTitle = MyGUI::LanguageManager::getInstance().replaceTags(
            reason.empty() ? label : label + ": " + reason);
        const std::string title = MyGUI::TextIterator::toTagsString(
            MyGUI::TextIterator::getOnlyText(resolvedTitle)).asUTF8();
        const char* signClass = amount > 0.f ? "positive" : (amount < 0.f ? "negative" : "neutral");
        const std::string key = std::string("experience:") + signClass + ":" + reason;

        const auto formatExperience = [](float value)
        {
            if (std::fabs(value) < 0.0001f)
                return std::string();
            std::ostringstream stream;
            if (value > 0.f)
                stream << "+";
            if (std::fabs(value - std::round(value)) < 0.05f)
                stream << static_cast<int>(std::round(value));
            else
                stream << std::fixed << std::setprecision(1) << value;
            stream << " XP";
            return stream.str();
        };

        const auto styleExperience = [](HudNotificationState& state, float value)
        {
            MyGUI::Colour textColour = MyGUI::Colour::White;
            MyGUI::Colour background = MyGUI::Colour::Black;
            float alpha = 0.28f;
            if (value > 0.0001f)
            {
                textColour = MyGUI::Colour(0.42f, 1.0f, 0.42f);
                background = MyGUI::Colour(0.08f, 0.30f, 0.10f);
                alpha = 0.36f;
            }
            else if (value < -0.0001f)
            {
                textColour = MyGUI::Colour(1.0f, 0.48f, 0.48f);
                background = MyGUI::Colour(0.36f, 0.07f, 0.07f);
                alpha = 0.36f;
            }

            if (state.mShade)
            {
                state.mShade->setColour(background);
                state.mShade->setAlpha(alpha);
            }
            if (state.mTitle)
                state.mTitle->setTextColour(textColour);
            if (state.mValue)
                state.mValue->setTextColour(textColour);
        };

        // Repeated gains/losses of the same sign and source coalesce. A loss can
        // never merge into a gain, which keeps the colour semantics unambiguous.
        if (!neutral)
        {
            for (HudNotificationState& state : mHudNotifications)
            {
                if (!state.mActive || state.mKind != HudEventKind::Experience || state.mKey != key)
                    continue;

                state.mNumericAmount += amount;
                state.mAge = 0.f;
                state.mSequence = ++mHudNotificationSequence;
                if (state.mValue)
                    state.mValue->setCaption(formatExperience(state.mNumericAmount));
                styleExperience(state, state.mNumericAmount);
                return;
            }
        }

        // XP deliberately has no texture icon. This also removes the old missing
        // Luck icon that rendered as a pink square on installations without it.
        HudNotificationState* state = pushHudNotification(
            HudEventKind::Experience, key, std::string(), title, 1, formatExperience(amount));
        if (!state)
            return;
        state->mNumericAmount = amount;
        state->mLifetime = 4.4f;
        styleExperience(*state, amount);
    }

    void HUD::scanHudEventSources()
    {
        const MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
        if (player.isEmpty())
            return;

        struct ItemSnapshot
        {
            int mCount = 0;
            std::string mName;
            std::string mIcon;
        };

        std::map<std::string, ItemSnapshot> inventoryNow;
        MWWorld::ContainerStore& store = player.getClass().getContainerStore(player);
        for (MWWorld::ContainerStoreIterator it = store.begin(); it != store.end(); ++it)
        {
            MWWorld::Ptr item = *it;
            const int stackCount = item.getRefData().getCount();
            if (stackCount <= 0)
                continue;

            const bool gold = item.getClass().isGold(item);
            const std::string key = gold ? std::string("__arena_gold__")
                : Misc::StringUtils::lowerCase(item.getCellRef().getRefId());
            ItemSnapshot& snapshot = inventoryNow[key];
            if (gold)
            {
                snapshot.mCount += stackCount * std::max(1, item.getClass().getValue(item));
                if (snapshot.mName.empty())
                    snapshot.mName = item.getClass().getName(item);
            }
            else
                snapshot.mCount += stackCount;

            if (snapshot.mName.empty())
                snapshot.mName = item.getClass().getName(item);
            if (snapshot.mName.empty())
                snapshot.mName = item.getCellRef().getRefId();
            if (snapshot.mIcon.empty())
                snapshot.mIcon = item.getClass().getInventoryIcon(item);
        }

        const MWMechanics::ActiveSpells& activeSpells
            = player.getClass().getCreatureStats(player).getActiveSpells();
        std::map<std::string, int> spellsNow;
        std::map<std::string, int> spellOccurrence;

        bool serverInventorySet = false;
        std::uint64_t currentInventorySetGeneration = mHudInventorySetGeneration;
        if (mwmp::Main::isInitialized() && mwmp::Main::get().getLocalPlayer())
        {
            currentInventorySetGeneration
                = mwmp::Main::get().getLocalPlayer()->hudInventorySetGeneration;
            serverInventorySet = currentInventorySetGeneration != mHudInventorySetGeneration;
        }

        // During load/new-game startup keep reseeding for a short grace period.
        // This prevents the player's entire save inventory/effect list from being
        // presented as newly acquired content.
        if (!mHudEventSnapshotsReady || mHudEventWarmup > 0.f)
        {
            mHudInventorySnapshot.clear();
            for (const auto& entry : inventoryNow)
                mHudInventorySnapshot[entry.first] = entry.second.mCount;
            mHudActiveSpellSnapshot.clear();
            for (auto it = activeSpells.begin(); it != activeSpells.end(); ++it)
            {
                const std::string spellKey = Misc::StringUtils::lowerCase(it->first)
                    + "\x1f" + it->second.mDisplayName;
                ++mHudActiveSpellSnapshot[spellKey];
            }
            mHudInventorySetGeneration = currentInventorySetGeneration;
            mHudEventSnapshotsReady = true;
            return;
        }

        // Generic protection for clear/refill cycles: only arm it when most
        // previously known item *kinds disappear entirely* in one scan. Mere count
        // reductions (selling/consuming stacks) do not trigger it. Unlike a hard
        // "more than N gained kinds" rule, this does not suppress a legitimate
        // Take All that adds many different items at once.
        std::size_t lostKinds = 0;
        for (const auto& previous : mHudInventorySnapshot)
        {
            const auto nowIt = inventoryNow.find(previous.first);
            if (nowIt == inventoryNow.end())
                ++lostKinds;
        }
        if (mHudInventorySnapshot.size() >= sHudEventReseedMinKinds
            && static_cast<float>(lostKinds)
                >= static_cast<float>(mHudInventorySnapshot.size()) * sHudEventReseedLostFraction)
        {
            mHudInventoryReseedGrace = 0.5f;
        }

        if (serverInventorySet)
        {
            // A server InventoryChanges::SET is an authoritative wholesale state
            // replacement, not a sequence of pickups. Reseed exactly on that event.
            mHudInventorySetGeneration = currentInventorySetGeneration;
        }
        const bool suppressInventoryEvents = serverInventorySet || mHudInventoryReseedGrace > 0.f;
        if (!suppressInventoryEvents)
        {
            for (const auto& entry : inventoryNow)
            {
                const auto previousIt = mHudInventorySnapshot.find(entry.first);
                const int previous = previousIt == mHudInventorySnapshot.end() ? 0 : previousIt->second;
                const int gained = entry.second.mCount - previous;
                if (gained <= 0)
                    continue;
                // Arena Y010: if this stack already existed before the pickup,
                // append the committed total, e.g. +5 (10). First acquisition
                // stays compact (+5). Later coalesced pickups update both numbers.
                const int totalCount = previous > 0 ? entry.second.mCount : -1;
                pushHudNotification(entry.first == "__arena_gold__" ? HudEventKind::Gold : HudEventKind::Item,
                    entry.first, entry.second.mIcon, entry.second.mName, gained, std::string(), totalCount);
            }
        }
        mHudInventorySnapshot.clear();
        for (const auto& entry : inventoryNow)
            mHudInventorySnapshot[entry.first] = entry.second.mCount;

        for (auto it = activeSpells.begin(); it != activeSpells.end(); ++it)
        {
            const MWMechanics::ActiveSpells::ActiveSpellParams& params = it->second;
            const std::string spellKey = Misc::StringUtils::lowerCase(it->first)
                + "\x1f" + params.mDisplayName;
            const int occurrence = ++spellOccurrence[spellKey];
            ++spellsNow[spellKey];

            const auto previousIt = mHudActiveSpellSnapshot.find(spellKey);
            const int previousCount = previousIt == mHudActiveSpellSnapshot.end() ? 0 : previousIt->second;
            if (occurrence <= previousCount)
                continue;

            std::string icon;
            float timeLeft = -1.f;
            if (!params.mEffects.empty())
            {
                const ESM::ActiveEffect& first = params.mEffects.front();
                const ESM::MagicEffect* effect = MWBase::Environment::get().getWorld()
                    ->getStore().get<ESM::MagicEffect>().find(first.mEffectId);
                if (effect)
                    icon = effect->mIcon;

                for (const ESM::ActiveEffect& active : params.mEffects)
                    timeLeft = std::max(timeLeft, active.mTimeLeft);
            }

            const std::string duration = timeLeft > 0.f
                ? MyGUI::utility::toString(static_cast<int>(std::ceil(timeLeft))) + "s"
                : std::string();
            const std::string title = params.mDisplayName.empty() ? it->first : params.mDisplayName;
            HudNotificationState* card = pushHudNotification(HudEventKind::Magic,
                "magic:" + spellKey, icon, title, 1, duration);
            if (card)
            {
                card->mSpellId = it->first;
                card->mSpellCasterActorId = params.mCasterActorId;
                card->mSpellTimestampDay = params.mTimeStamp.getDay();
                card->mSpellTimestampHour = params.mTimeStamp.getHour();
            }
        }
        mHudActiveSpellSnapshot.swap(spellsNow);
    }

    void HUD::refreshHudMagicDurations()
    {
        bool anyMagicCard = false;
        for (const HudNotificationState& state : mHudNotifications)
        {
            if (state.mActive && state.mKind == HudEventKind::Magic && !state.mSpellId.empty())
            {
                anyMagicCard = true;
                break;
            }
        }
        if (!anyMagicCard)
            return;

        const MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
        if (player.isEmpty())
            return;

        const MWMechanics::ActiveSpells& activeSpells
            = player.getClass().getCreatureStats(player).getActiveSpells();

        for (HudNotificationState& state : mHudNotifications)
        {
            if (!state.mActive || state.mKind != HudEventKind::Magic || state.mSpellId.empty())
                continue;
            if (!state.mValue)
                continue;

            float timeLeft = -1.f;
            bool stillActive = false;
            for (auto it = activeSpells.begin(); it != activeSpells.end(); ++it)
            {
                const MWMechanics::ActiveSpells::ActiveSpellParams& params = it->second;
                if (it->first != state.mSpellId
                    || params.mCasterActorId != state.mSpellCasterActorId
                    || params.mTimeStamp.getDay() != state.mSpellTimestampDay
                    || std::abs(params.mTimeStamp.getHour() - state.mSpellTimestampHour) > 0.0001f)
                    continue;

                stillActive = true;
                for (const ESM::ActiveEffect& active : params.mEffects)
                    timeLeft = std::max(timeLeft, active.mTimeLeft);
                break;
            }

            if (stillActive && timeLeft > 0.f)
            {
                state.mValue->setCaption(
                    MyGUI::utility::toString(static_cast<int>(std::ceil(timeLeft))) + "s");
                // Keep the card alive for the whole real effect duration. mAge
                // continues normally, so fade-in is not restarted every frame.
                state.mLifetime = std::max(state.mLifetime, state.mAge + timeLeft + 0.65f);
            }
            else
            {
                state.mValue->setCaption(std::string());
                // Dispelled/expired effects leave the stack promptly with a short fade.
                state.mLifetime = std::min(state.mLifetime, state.mAge + 0.55f);
            }
        }
    }

    void HUD::updateHudEventFeed(float dt)
    {
        dt = std::max(0.f, dt);
        if (mHudEventWarmup > 0.f)
            mHudEventWarmup = std::max(0.f, mHudEventWarmup - dt);
        if (mHudInventoryReseedGrace > 0.f)
            mHudInventoryReseedGrace = std::max(0.f, mHudInventoryReseedGrace - dt);

        mHudEventScanTimer -= dt;
        if (mHudEventScanTimer <= 0.f)
        {
            mHudEventScanTimer = 0.12f;
            scanHudEventSources();
        }

        refreshHudMagicDurations();

        std::vector<std::size_t> active;
        active.reserve(mHudNotifications.size());
        for (std::size_t i = 0; i < mHudNotifications.size(); ++i)
        {
            HudNotificationState& state = mHudNotifications[i];
            if (!state.mActive)
                continue;
            state.mAge += dt;
            if (state.mAge >= state.mLifetime)
            {
                state.mActive = false;
                if (state.mPanel)
                    state.mPanel->setVisible(false);
                continue;
            }
            active.push_back(i);
        }

        // Newest card stays closest to the combat/stamina block; older cards grow
        // upward. If close-range combat bars are docked, the feed automatically
        // starts above that stack instead of covering it.
        std::sort(active.begin(), active.end(), [this](std::size_t left, std::size_t right)
        {
            return mHudNotifications[left].mSequence > mHudNotifications[right].mSequence;
        });

        std::size_t dockedRows = 0;
        for (const CombatHealthBarState& combat : mCombatHealthBars)
        {
            if (combat.mDocked && combat.mAlpha > 0.05f)
                ++dockedRows;
        }
        dockedRows = std::min<std::size_t>(dockedRows, CombatBar::sDockMaxRows);

        // Y008: the cards are children of mGameplayHud, so every anchor has to be
        // expressed relative to it. The old fallback assigned raw screen coordinates
        // from RenderManager, which only lined up while mGameplayHud sat at 0,0.
        MyGUI::IntPoint origin;
        if (mGameplayHud)
            origin = mGameplayHud->getAbsolutePosition();
        const MyGUI::IntSize viewSize = MyGUI::RenderManager::getInstance().getViewSize();
        // Arena Y010: event cards hug the actual right edge with a 2 px safety
        // margin. The stamina frame remains the vertical anchor only; its own
        // horizontal inset no longer pushes the feed inward.
        int anchorRight = viewSize.width - 2 - origin.left;
        int anchorBottom = viewSize.height - 44 - origin.top;
        if (mFatigueFrame)
        {
            const MyGUI::IntCoord frame = mFatigueFrame->getAbsoluteCoord();
            anchorBottom = frame.top - origin.top - CombatBar::sDockGap
                - static_cast<int>(dockedRows) * CombatBar::sDockRowStride - 8;
        }

        // Alpha 0.10: wrap both columns and stack actual measured card heights.
        // Positions use GUI coordinates, so the edge margin also follows UI scaling.
        anchorRight = viewSize.width - 6 - origin.left;
        const int originalCardWidth = std::max(300, std::min(480, viewSize.width / 3));
        const int cardWidth = std::max(1, std::min((originalCardWidth * 85) / 100, anchorRight - 6));
        const int cardLeft = anchorRight - cardWidth;
        constexpr int cardGap = 2;
        int nextBottom = anchorBottom;
        for (std::size_t row = 0; row < active.size(); ++row)
        {
            HudNotificationState& state = mHudNotifications[active[row]];
            const float fadeIn = std::min(1.f, state.mAge / 0.14f);
            const float fadeOut = state.mAge > state.mLifetime - 0.55f
                ? std::max(0.f, (state.mLifetime - state.mAge) / 0.55f) : 1.f;
            const float alpha = std::min(fadeIn, fadeOut);
            const bool hasIcon = state.mIcon && state.mIcon->getVisible();
            const bool hasValue = state.mValue && !state.mValue->getCaption().empty();
            const int textLeft = hasIcon ? 36 : 4;
            const int valueWidth = hasValue ? std::min(120, std::max(60, cardWidth / 4)) : 0;
            const int titleWidth = std::max(1, cardWidth - textLeft - 4
                - (hasValue ? valueWidth + 4 : 0));
            state.mTitle->setCoord(textLeft, 3, titleWidth, 32);
            state.mValue->setCoord(cardWidth - 6 - valueWidth, 3, std::max(1, valueWidth), 32);
            const int textHeight = std::max(state.mTitle->getTextSize().height,
                hasValue ? state.mValue->getTextSize().height : 0);
            const int cardHeight = std::max(hasIcon ? 32 : 24, textHeight + 4);
            const int top = nextBottom - cardHeight;
            nextBottom = top - cardGap;
            if (state.mPanel)
            {
                state.mPanel->setCoord(cardLeft, top, cardWidth, cardHeight);
                state.mTitle->setCoord(textLeft, 2, titleWidth, cardHeight - 4);
                state.mValue->setCoord(cardWidth - 4 - valueWidth, 2,
                    std::max(1, valueWidth), cardHeight - 4);
                if (state.mIcon)
                    state.mIcon->setPosition(3, (cardHeight - state.mIcon->getHeight()) / 2);
                state.mPanel->setAlpha(alpha);
                // Older cards that no longer fit must not draw outside the viewport.
                state.mPanel->setVisible(alpha > 0.01f && top >= 6 - origin.top);
            }
        }
    }

    void HUD::resetHudEventFeed()
    {
        mHudInventorySnapshot.clear();
        mHudActiveSpellSnapshot.clear();
        mHudEventSnapshotsReady = false;
        mHudEventWarmup = 1.f;
        mHudInventoryReseedGrace = 0.f;
        mHudEventScanTimer = 0.f;
        for (HudNotificationState& state : mHudNotifications)
        {
            state.mActive = false;
            state.mAge = 0.f;
            state.mAmount = 0;
            state.mNumericAmount = 0.f;
            state.mTotalCount = -1;
            state.mKey.clear();
            state.mSpellId.clear();
            state.mSpellCasterActorId = -1;
            state.mSpellTimestampDay = -1;
            state.mSpellTimestampHour = -1.f;
            if (state.mPanel)
                state.mPanel->setVisible(false);
        }
    }

    void HUD::updateEnemyHealthBar()
    {
        const std::string npcBarMode = getNpcBarMode();
        const bool usingFocusActor = npcBarShowsHover(npcBarMode)
            && !mFocusActor.isEmpty()
            && !mFocusActor.getClass().getCreatureStats(mFocusActor).isDead()
            && !MWBase::Environment::get().getWindowManager()->containsMode(GM_Dialogue)
            && mFocusActorPanelAlpha > 0.01f;

        MWWorld::Ptr enemy;
        if (usingFocusActor)
            enemy = mFocusActor;

        if (enemy.isEmpty())
        {
            mEnemyHealth->setVisible(false);
            if (mEnemyName)
                mEnemyName->setVisible(false);
            if (mEnemySummary)
                mEnemySummary->setVisible(false);
            return;
        }

        MWMechanics::CreatureStats& stats = enemy.getClass().getCreatureStats(enemy);
        if (stats.isDead() || stats.getHealth().getCurrent() <= 0.f)
        {
            mEnemyHealth->setVisible(false);
            if (mEnemyName)
                mEnemyName->setVisible(false);
            if (mEnemySummary)
                mEnemySummary->setVisible(false);
            return;
        }

        const float maximumHealth = stats.getHealth().getModified();
        const float currentHealth = stats.getHealth().getCurrent();
        const int maximumHealthPoints = std::max(1, static_cast<int>(std::lround(maximumHealth)));
        const int currentHealthPoints = std::max(0, std::min(maximumHealthPoints,
            static_cast<int>(std::lround(currentHealth))));

        mEnemyHealth->setProgressRange(static_cast<size_t>(maximumHealthPoints));
        mEnemyHealth->setProgressPosition(static_cast<size_t>(currentHealthPoints));

        const float alpha = 1.f;
        mEnemyHealth->setAlpha(alpha);

        if (usingFocusActor)
        {
            // Faced actor: stable name, level and health panel below the compass.
            mEnemyHealth->setSize(190, 16);
            if (mEnemyName)
            {
                mEnemyName->setSize(240, 20);
                // Y071: player level packets can be transient/stale during login and cell hand-off.
                if (mwmp::PlayerList::isDedicatedPlayer(enemy))
                    mEnemyName->setCaption(enemy.getClass().getName(enemy));
                else
                    mEnemyName->setCaption(enemy.getClass().getName(enemy) + "  -  "
                        + MyGUI::utility::toString(stats.getLevel()) + " lvl");
                mEnemyName->setAlpha(alpha);
            }
            if (mEnemySummary)
            {
                mEnemySummary->setSize(190, 16);
                mEnemySummary->setCaption(MyGUI::utility::toString(currentHealthPoints) + " / "
                    + MyGUI::utility::toString(maximumHealthPoints));
                mEnemySummary->setAlpha(alpha);
            }

            const MyGUI::IntSize& viewSize = MyGUI::RenderManager::getInstance().getViewSize();
            const int nameWidth = mEnemyName ? mEnemyName->getWidth() : 0;
            const int nameHeight = mEnemyName ? mEnemyName->getHeight() : 0;
            const int barWidth = mEnemyHealth->getWidth();
            const int totalWidth = std::max(nameWidth, barWidth);
            const int totalHeight = nameHeight + 2 + mEnemyHealth->getHeight();

            constexpr int targetPanelSafeMargin = 14;
            const int horizontalMargin = std::min(targetPanelSafeMargin,
                std::max(0, (viewSize.width - totalWidth) / 2));
            const int verticalMargin = std::min(targetPanelSafeMargin,
                std::max(0, (viewSize.height - totalHeight) / 2));
            const int maximumLeft = std::max(horizontalMargin,
                viewSize.width - horizontalMargin - totalWidth);
            const int maximumTop = std::max(verticalMargin,
                viewSize.height - verticalMargin - totalHeight);

            const int stableCentreX = mTargetPanelPositionInitialized
                ? static_cast<int>(std::lround(mTargetPanelCenterX))
                : viewSize.width / 2;
            const int panelLeft = std::max(horizontalMargin,
                std::min(stableCentreX - totalWidth / 2, maximumLeft));

            // Keep the target panel below the compass and its shared clock/location line.
            // Reserving the line even during a fade prevents the target panel from jumping.
            int baseY = verticalMargin;
            if (mHorizontalCompass && mHorizontalCompass->getVisible())
            {
                const MyGUI::IntCoord compassCoord = mHorizontalCompass->getAbsoluteCoord();
                constexpr int targetPanelCompassGap = 7;
                baseY = std::max(baseY,
                    compassCoord.top + compassCoord.height + targetPanelCompassGap);
            }
            if (mGameTimeBox)
            {
                const MyGUI::IntCoord infoCoord = mGameTimeBox->getAbsoluteCoord();
                constexpr int targetPanelInfoGap = 5;
                baseY = std::max(baseY, infoCoord.top + infoCoord.height + targetPanelInfoGap);
            }
            baseY = std::min(baseY, maximumTop);

            if (mEnemyName)
                mEnemyName->setPosition(panelLeft + (totalWidth - nameWidth) / 2, baseY);
            const int barLeft = panelLeft + (totalWidth - barWidth) / 2;
            mEnemyHealth->setPosition(barLeft, baseY + nameHeight + 2);
            if (mEnemySummary)
                mEnemySummary->setPosition(barLeft, baseY + nameHeight + 2);
        }
    }

    void HUD::setEnemy(const MWWorld::Ptr &enemy)
    {
        mEnemyActorId = enemy.getClass().getCreatureStats(enemy).getActorId();
        if (mEnemyName)
            mEnemyName->setVisible(false);
        if (mEnemySummary)
            mEnemySummary->setVisible(false);
        mEnemyHealthTimer = MWBase::Environment::get().getWorld()->getStore().get<ESM::GameSetting>().find("fNPCHealthBarTime")->mValue.getFloat();
        // X014: persistent combat health is rendered in world space above
        // the actual actor. Keep the actor id only for compatibility with callbacks.
    }

    void HUD::resetEnemy()
    {
        mEnemyActorId = -1;
        mEnemyHealthTimer = -1;
        if (mEnemyName) mEnemyName->setVisible(false);
        if (mEnemySummary) mEnemySummary->setVisible(false);
    }

    void HUD::clear()
    {



        unsetSelectedSpell();
        unsetSelectedWeapon();
        resetEnemy();
        mFocusActor = MWWorld::Ptr();
        mFocusActorCurrentlyFaced = false;
        mFocusActorDistance = -1.f;
        mFocusActorPanelAlpha = 0.f;
        mTargetPanelPositionInitialized = false;
        hideCombatHealthBars();
        resetHudEventFeed();
        if (mEnemyName)
            mEnemyName->setVisible(false);
        if (mEnemySummary)
            mEnemySummary->setVisible(false);
    }

    void HUD::customMarkerCreated(MyGUI::Widget *marker)
    {
        marker->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onMapClicked);
    }

    void HUD::doorMarkerCreated(MyGUI::Widget *marker)
    {
        marker->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onMapClicked);
    }

}

