#include "keyboardmanager.hpp"

#include <cctype>

#include <MyGUI_InputManager.h>
#include <MyGUI_EditBox.h>

#include <components/settings/settings.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/inputmanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwworld/player.hpp"
#include "../mwmp/Main.hpp"
#include "../mwmp/GUIController.hpp"
#include "../mwmp/LocalPlayer.hpp"

#include "actions.hpp"
#include "bindingsmanager.hpp"
#include "sdlmappings.hpp"

namespace MWInput
{
    namespace
    {
        bool togglePostProcessSetting(const char* setting, const char* enabledMessage, const char* disabledMessage)
        {
            MWBase::WindowManager* windowManager = MWBase::Environment::get().getWindowManager();
            if (windowManager->isGuiMode())
                return false;

            const bool enabled = !Settings::Manager::getBool(setting, "Shaders");
            Settings::Manager::setBool(setting, "Shaders", enabled);
            const Settings::CategorySettingVector changed = Settings::Manager::getPendingChanges();
            MWBase::Environment::get().getWorld()->processChangedSettings(changed);
            Settings::Manager::resetPendingChanges();
            windowManager->messageBox(enabled ? enabledMessage : disabledMessage);
            return true;
        }
    }

    KeyboardManager::KeyboardManager(BindingsManager* bindingsManager)
        : mBindingsManager(bindingsManager)
    {
    }

    void KeyboardManager::textInput(const SDL_TextInputEvent &arg)
    {
        MyGUI::UString ustring(&arg.text[0]);
        MyGUI::UString::utf32string utf32string = ustring.asUTF32();
        for (MyGUI::UString::utf32string::const_iterator it = utf32string.begin(); it != utf32string.end(); ++it)
            MyGUI::InputManager::getInstance().injectKeyPress(MyGUI::KeyCode::None, *it);
    }

    void KeyboardManager::keyPressed(const SDL_KeyboardEvent &arg)
    {
        // HACK: to make default keybinding for the console work without printing an extra "^" upon closing
        // This assumes that SDL_TextInput events always come *after* the key event
        // (which is somewhat reasonable, and hopefully true for all SDL platforms)
        auto kc = sdlKeyToMyGUI(arg.keysym.sym);
        if (mBindingsManager->getKeyBinding(A_Console) == arg.keysym.scancode
                && MWBase::Environment::get().getWindowManager()->isConsoleMode())
            SDL_StopTextInput();

        // Layout keys must be handled before MyGUI consumes keys for the focused
        // history widget, and before Escape can open the pause menu.
        if (!mBindingsManager->isDetectingBindingState()
            && mwmp::Main::isInitialized()
            && mwmp::Main::get().getGUIController()->pressedChatLayoutKey(arg.keysym.scancode, arg.repeat != 0))
        {
            MWBase::Environment::get().getInputManager()->setJoystickLastUsed(false);
            return;
        }
        // Android's Take button can generate a complete down/up between frames.
        // Forward the actual Activate binding on key-down, before GUI input can
        // swallow it; LocalPlayer will validate recovery on the next update.
        // Never treat a literal E as Activate when the player has rebound it.
        const SDL_Scancode activateKey = mBindingsManager->getKeyBinding(A_Activate);
        if (!arg.repeat && activateKey != SDL_SCANCODE_UNKNOWN
            && arg.keysym.scancode == activateKey
            && !mBindingsManager->isDetectingBindingState() && mwmp::Main::isInitialized())
        {
            if (mwmp::LocalPlayer* localPlayer = mwmp::Main::get().getLocalPlayer())
            {
                std::string allyName;
                if (localPlayer->isDeathRecoveryActive()
                    || localPlayer->getRecoverableAllyName(allyName))
                    localPlayer->notifyDeathRecoveryUsePressed();
            }
        }

        bool consumed = false;
        if (!arg.repeat && !mBindingsManager->isDetectingBindingState())
        {
            if (arg.keysym.scancode == SDL_SCANCODE_F3)
                consumed = togglePostProcessSetting("hdr lighting", "#{arenamp=hotkey.hdr_on}", "#{arenamp=hotkey.hdr_off}");
            else if (arg.keysym.scancode == SDL_SCANCODE_F4)
                consumed = togglePostProcessSetting("bloom enabled", "#{arenamp=hotkey.bloom_on}", "#{arenamp=hotkey.bloom_off}");

            // Placement mode owns only its dedicated literal helper keys.
            // Ready Weapon / Ready Magic intentionally continue through the
            // binding system because ActionManager repurposes those actions for rotation.
            MWBase::World* world = MWBase::Environment::get().getWorld();
            const bool placementActive = world && world->isPhysicsGrabActive()
                && !MWBase::Environment::get().getWindowManager()->isGuiMode();
            if (!consumed && placementActive)
            {
                switch (arg.keysym.scancode)
                {
                    case SDL_SCANCODE_SPACE:
                        world->togglePhysicsGrabPhysics();
                        consumed = true;
                        break;
                    case SDL_SCANCODE_TAB:
                        world->cyclePhysicsGrabMoveMode();
                        consumed = true;
                        break;
                    case SDL_SCANCODE_LCTRL:
                    case SDL_SCANCODE_RCTRL:
                        world->resetPhysicsGrabTransform();
                        consumed = true;
                        break;
                    // Do not hard-code R/F here. Ready Weapon / Ready Magic may
                    // be rebound, and ActionManager owns those actions while placement
                    // is active. Let the binding system receive the real key press.
                    default:
                        break;
                }
            }

            // Arena X011 multiline chat. MyGUI's native multiline EditBox uses
            // Return as a line break, while Arena chat historically uses Return
            // to send. Intercept only the focused TES3MP chat editor here:
            //   Enter             -> submit
            //   Shift+Enter       -> newline
            //   Ctrl+Enter        -> newline
            // The newline is inserted as Unicode text at MyGUI's cursor so UTF-8
            // messages (including Cyrillic) keep correct cursor positions.
            if (!consumed && (arg.keysym.scancode == SDL_SCANCODE_RETURN
                    || arg.keysym.scancode == SDL_SCANCODE_KP_ENTER))
            {
                MyGUI::Widget* focus = MyGUI::InputManager::getInstance().getKeyFocusWidget();
                MyGUI::EditBox* edit = focus ? focus->castType<MyGUI::EditBox>(false) : nullptr;

                // WindowBase loads layouts with a runtime widget-name prefix.
                // Comparing against exactly "edit_Command" therefore misses the
                // real chat field on some builds. Match the stable suffix instead.
                bool isChatEditor = false;
                if (edit)
                {
                    const std::string& widgetName = edit->getName();
                    static const std::string suffix = "edit_Command";
                    isChatEditor = widgetName.size() >= suffix.size()
                        && widgetName.compare(widgetName.size() - suffix.size(), suffix.size(), suffix) == 0;
                }

                if (isChatEditor)
                {
                    const bool insertNewline = (arg.keysym.mod & (KMOD_SHIFT | KMOD_CTRL)) != 0;
                    if (insertNewline)
                    {
                        MyGUI::UString text = edit->getCaption();
                        const size_t cursor = std::min(edit->getTextCursor(), text.size());
                        text.insert(cursor, MyGUI::UString("\n"));
                        edit->setCaption(text);
                        edit->setTextCursor(cursor + 1);
                    }
                    else
                        edit->eventEditSelectAccept(edit);

                    consumed = true;
                }
            }
        }

        // SDL text input normally suppresses the matching printable key event and
        // sends only SDL_TEXTINPUT. That is correct for ordinary typing, but it
        // also swallowed Ctrl+C/V/X/Z before MyGUI::EditBox could execute its
        // clipboard/undo commands. Let the standard editing shortcuts through as
        // key events while keeping normal character entry on SDL_TEXTINPUT.
        const bool editingModifier = (arg.keysym.mod & (KMOD_CTRL | KMOD_GUI)) != 0;
        const bool textEditingShortcut = SDL_IsTextInputActive() && editingModifier
            && (arg.keysym.scancode == SDL_SCANCODE_C
                || arg.keysym.scancode == SDL_SCANCODE_V
                || arg.keysym.scancode == SDL_SCANCODE_X
                || arg.keysym.scancode == SDL_SCANCODE_Z
                || arg.keysym.scancode == SDL_SCANCODE_Y
                || arg.keysym.scancode == SDL_SCANCODE_A);

        consumed = consumed || (SDL_IsTextInputActive() && !textEditingShortcut
                        && (!(SDLK_SCANCODE_MASK & arg.keysym.sym)
                        && (std::isprint(arg.keysym.sym)
                        // Don't trust isprint for symbols outside the extended ASCII range
                        || (kc == MyGUI::KeyCode::None && arg.keysym.sym > 0xff))));
        if (!consumed && kc != MyGUI::KeyCode::None && !mBindingsManager->isDetectingBindingState())
        {
            MWBase::WindowManager* windowManager = MWBase::Environment::get().getWindowManager();

            // In GUI mode WASD mirrors the arrow keys. Text-entry widgets keep
            // literal WASD so search fields, books and console editing are unaffected.
            if (windowManager->isGuiMode() && !SDL_IsTextInputActive())
            {
                switch (arg.keysym.scancode)
                {
                    case SDL_SCANCODE_W: kc = MyGUI::KeyCode::ArrowUp; break;
                    case SDL_SCANCODE_S: kc = MyGUI::KeyCode::ArrowDown; break;
                    case SDL_SCANCODE_A: kc = MyGUI::KeyCode::ArrowLeft; break;
                    case SDL_SCANCODE_D: kc = MyGUI::KeyCode::ArrowRight; break;
                    default: break;
                }
            }

            // QuickLoot remains non-modal, but axis-placement mode owns the
            // movement bindings. Do not let UI navigation consume a key that is
            // currently supposed to translate the held object.
            MWBase::World* world = MWBase::Environment::get().getWorld();
            const bool placementMoveMode = world && world->isPhysicsGrabActive()
                && world->getPhysicsGrabMoveMode() != 0;
            const bool placementMovementKey = placementMoveMode
                && (arg.keysym.scancode == mBindingsManager->getKeyBinding(A_MoveLeft)
                    || arg.keysym.scancode == mBindingsManager->getKeyBinding(A_MoveRight)
                    || arg.keysym.scancode == mBindingsManager->getKeyBinding(A_MoveForward)
                    || arg.keysym.scancode == mBindingsManager->getKeyBinding(A_MoveBackward));

            // Activate belongs to ActionManager's tap/hold gesture even if the
            // user bound it to a QuickLoot shortcut (F/Enter/take-all).
            const bool gestureActivate = !windowManager->isGuiMode()
                && arg.keysym.scancode == mBindingsManager->getKeyBinding(A_Activate);
            if (!gestureActivate && !placementMovementKey && !windowManager->isGuiMode()
                && windowManager->handleQuickLootKeyPress(kc))
                consumed = true;
            else if (!gestureActivate && !placementMovementKey && windowManager->injectKeyPress(kc, 0, arg.repeat))
                consumed = true;

            mBindingsManager->setPlayerControlsEnabled(!consumed);
        }

        if (arg.repeat)
            return;

        MWBase::InputManager* input = MWBase::Environment::get().getInputManager();
        if (!input->controlsDisabled() && !consumed)
            mBindingsManager->keyPressed(arg);

        input->setJoystickLastUsed(false);
    }

    void KeyboardManager::keyReleased(const SDL_KeyboardEvent &arg)
    {
        MWBase::Environment::get().getInputManager()->setJoystickLastUsed(false);
        auto kc = sdlKeyToMyGUI(arg.keysym.sym);

        if (!mBindingsManager->isDetectingBindingState())
            mBindingsManager->setPlayerControlsEnabled(!MyGUI::InputManager::getInstance().injectKeyRelease(kc));

        // Always forward releases to clear a binding that may have been held
        // before placement mode consumed its key press (for example Ctrl/Sneak).
        mBindingsManager->keyReleased(arg);
    }
}
