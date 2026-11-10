#include <algorithm>
#include <string>

#include <components/openmw-mp/TimedLog.hpp>
#include <components/openmw-mp/Base/BasePlayer.hpp>

#include <SDL_keyboard.h>
#include <SDL_scancode.h>
#include <SDL_system.h>

#include <MyGUI_FactoryManager.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_Gui.h>
#include <MyGUI_ImageBox.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_ProgressBar.h>
#include <MyGUI_LanguageManager.h>
#include <MyGUI_RenderManager.h>
#include <MyGUI_ScrollView.h>
#include <MyGUI_TextBox.h>

#include <extern/PicoSHA2/picosha2.h>

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/inputmanager.hpp"

#include "../mwgui/mapwindow.hpp"
#include "../mwgui/mapmarkerstyle.hpp"

#include "../mwworld/worldimp.hpp"
#include "../mwworld/player.hpp"
#include "../mwworld/cellstore.hpp"

#include "GUIController.hpp"

#include "ArenaHousing.hpp"
#include "Main.hpp"
#include "Networking.hpp"
#include "GUI/PlayerMarkerCollection.hpp"
#include "GUI/GUIDialogList.hpp"
#include "GUI/GUIChat.hpp"
#include "GUI/ServerQuestEditor.hpp"
#include "GUI/FurnitureStoreWindow.hpp"
#include "LocalPlayer.hpp"
#include "DedicatedPlayer.hpp"
#include "PlayerList.hpp"


mwmp::GUIController::GUIController()
    : mInputBox(nullptr)
    , mAccountLoginBox(nullptr)
    , mListBox(nullptr)
    , mServerQuestEditor(nullptr)
    , mFurnitureStore(nullptr)
    , mPreLoginPasswordAutoSubmitted(false)
    , mVoiceHud(nullptr)
    , mVoiceHudText(nullptr)
    , mEmbeddedRestartHud(nullptr)
    , mEmbeddedRestartProgress(nullptr)
    , mEmbeddedRestartTitle(nullptr)
    , mEmbeddedRestartMessage(nullptr)
    , mEmbeddedRestartTotalSeconds(30)
{
    mChat = nullptr;
    keySay = SDL_SCANCODE_Y;
    keyChatMode = SDL_SCANCODE_F2;
    sayHoldArmed = false;
    sayHoldTriggered = false;
    sayHoldTime = 0.f;
    sayHoldThreshold = 0.35f;
}

mwmp::GUIController::~GUIController()
{

}

bool mwmp::GUIController::requestGroupRecall()
{
    return mChat && mChat->requestGroupRecall();
}

void mwmp::GUIController::cleanUp()
{
    mPlayerMarkers.clear();
    if (mChat != nullptr)
        delete mChat;
    mChat = nullptr;
    if (mServerQuestEditor != nullptr)
        delete mServerQuestEditor;
    mServerQuestEditor = nullptr;
    if (mFurnitureStore != nullptr)
    {
        mFurnitureStore->reset();
        delete mFurnitureStore;
        mFurnitureStore = nullptr;
    }

    destroyEmbeddedRestartHud();
    destroyVoiceHud();

    // A fresh connection gets one automatic submission from the credentials
    // collected by the account card, even if the previous connection ended
    // before authentication completed.
    mPreLoginPasswordAutoSubmitted = false;
    mFurnitureCaptureReturnPending = false;
}

void mwmp::GUIController::refreshGuiMode(MWGui::GuiMode guiMode)
{
    if (MWBase::Environment::get().getWindowManager()->containsMode(guiMode))
    {
        MWBase::Environment::get().getWindowManager()->removeGuiMode(guiMode);
        MWBase::Environment::get().getWindowManager()->pushGuiMode(guiMode);
    }
}

void mwmp::GUIController::setupChat()
{
    assert(mChat == nullptr);

    float chatDelay = Settings::Manager::getFloat("delay", "Chat");
    int chatY = Settings::Manager::getInt("y", "Chat");
    int chatX = Settings::Manager::getInt("x", "Chat");
    int chatW = Settings::Manager::getInt("w", "Chat");
    int chatH = Settings::Manager::getInt("h", "Chat");

    // U024e: U024d only migrated one exact legacy rectangle, so any older
    // settings.cfg that had already been touched could keep the large chat.
    // Revision 2 deliberately applies the requested HUD rectangle once to all
    // existing installations. Afterwards the player's saved geometry wins.
    const int geometryRevision = Settings::Manager::getInt("hud geometry revision", "Chat");
    if (geometryRevision < 2)
    {
        chatX = 20;
        chatY = 40;
        chatW = 250;
        chatH = 300;
        Settings::Manager::setInt("x", "Chat", chatX);
        Settings::Manager::setInt("y", "Chat", chatY);
        Settings::Manager::setInt("w", "Chat", chatW);
        Settings::Manager::setInt("h", "Chat", chatH);
        Settings::Manager::setInt("hud geometry revision", "Chat", 2);
        Settings::Manager::saveUser();
    }

    keySay = SDL_GetScancodeFromName(Settings::Manager::getString("keySay", "Chat").c_str());
    keyChatMode = SDL_GetScancodeFromName(Settings::Manager::getString("keyChatMode", "Chat").c_str());

    // X054: how long the Say key has to stay down before the compact HUD caret
    // is promoted to the Player Menu. 0 disables the hold gesture entirely and
    // restores "tap opens the menu" for anyone who preferred X049.
    sayHoldThreshold = Settings::Manager::getFloat("player menu hold", "Chat");
    if (sayHoldThreshold < 0.f)
        sayHoldThreshold = 0.f;

    mChat = new GUIChat(chatX, chatY, chatW, chatH);
    mChat->setDelay(chatDelay);
    // X031: chat history is always available; full-hide startup mode was removed.
    mChat->setHistoryDisplayEnabled(true);
}

void mwmp::GUIController::printChatMessage(std::string &msg)
{
    // Y050 restart countdown is a private transport message, not chat. Handle it
    // before GUIChat so it also works if chat history is temporarily hidden.
    if (handleEmbeddedRestartControl(msg))
        return;

    if (mChat != nullptr)
    {
        // X050: group state and validated party-XP controls are transported in
        // private chat messages but never rendered into chat history.
        if (mChat->handleServerControlMessage(msg))
            return;
        mChat->print(msg);
    }
}

void mwmp::GUIController::ensureEmbeddedRestartHud()
{
    const MyGUI::IntSize view = MyGUI::RenderManager::getInstance().getViewSize();
    const int width = std::max(260, std::min(520, view.width - 40));
    const int height = 62;
    const int left = std::max(0, (view.width - width) / 2);
    // Lower than screen centre, but clear of the bottom resource bars.
    const int top = std::max(20, std::min(view.height - height - 24, view.height / 2 + 85));

    if (mEmbeddedRestartHud == nullptr)
    {
        mEmbeddedRestartHud = MyGUI::Gui::getInstance().createWidget<MyGUI::Widget>(
            "", left, top, width, height, MyGUI::Align::Default, "Popup");
        mEmbeddedRestartHud->setNeedMouseFocus(false);
        mEmbeddedRestartHud->setNeedKeyFocus(false);

        mEmbeddedRestartTitle = mEmbeddedRestartHud->createWidget<MyGUI::TextBox>(
            "SandBrightText", MyGUI::IntCoord(0, 0, width, 20),
            MyGUI::Align::Top | MyGUI::Align::HStretch, "RestartTitle");
        mEmbeddedRestartTitle->setNeedMouseFocus(false);
        mEmbeddedRestartTitle->setNeedKeyFocus(false);
        mEmbeddedRestartTitle->setTextAlign(MyGUI::Align::Center);
        mEmbeddedRestartTitle->setTextShadow(true);
        mEmbeddedRestartTitle->setTextShadowColour(MyGUI::Colour::Black);

        mEmbeddedRestartProgress = mEmbeddedRestartHud->createWidget<MyGUI::ProgressBar>(
            "MW_Progress_Red", MyGUI::IntCoord(0, 22, width, 10),
            MyGUI::Align::Top | MyGUI::Align::HStretch, "RestartProgress");
        mEmbeddedRestartProgress->setNeedMouseFocus(false);
        mEmbeddedRestartProgress->setNeedKeyFocus(false);

        mEmbeddedRestartMessage = mEmbeddedRestartHud->createWidget<MyGUI::TextBox>(
            "SandBrightText", MyGUI::IntCoord(0, 36, width, 24),
            MyGUI::Align::Top | MyGUI::Align::HStretch, "RestartMessage");
        mEmbeddedRestartMessage->setNeedMouseFocus(false);
        mEmbeddedRestartMessage->setNeedKeyFocus(false);
        mEmbeddedRestartMessage->setTextAlign(MyGUI::Align::Center | MyGUI::Align::VCenter);
        mEmbeddedRestartMessage->setTextShadow(true);
        mEmbeddedRestartMessage->setTextShadowColour(MyGUI::Colour::Black);
    }
    else
    {
        // Resolution/UI scale may have changed since the previous server message.
        mEmbeddedRestartHud->setCoord(left, top, width, height);
        if (mEmbeddedRestartTitle) mEmbeddedRestartTitle->setCoord(0, 0, width, 20);
        if (mEmbeddedRestartProgress) mEmbeddedRestartProgress->setCoord(0, 22, width, 10);
        if (mEmbeddedRestartMessage) mEmbeddedRestartMessage->setCoord(0, 36, width, 24);
    }

    mEmbeddedRestartHud->setVisible(true);
}

void mwmp::GUIController::destroyEmbeddedRestartHud()
{
    if (mEmbeddedRestartHud != nullptr)
        MyGUI::Gui::getInstance().destroyWidget(mEmbeddedRestartHud);

    mEmbeddedRestartHud = nullptr;
    mEmbeddedRestartProgress = nullptr;
    mEmbeddedRestartTitle = nullptr;
    mEmbeddedRestartMessage = nullptr;
    mEmbeddedRestartTotalSeconds = 30;
}

// U035 ---------------------------------------------------------------------
// "Who is talking" overlay.
//
// Built at runtime from the same borderless-widget recipe as the restart HUD,
// so no .layout file and no skin has to be touched. It is rebuilt only when the
// caption actually changes: MyGUI re-lays out a TextBox on every setCaption,
// and this is called every frame while someone speaks.
// ---------------------------------------------------------------------------
void mwmp::GUIController::updateVoiceHud(const std::vector<VoiceHudSpeaker>& speakers,
    bool, bool transmitting, float localLevel, bool)
{
    // Show the speaking players independently of our local green transmit glow.
    // The old "Microphone transmitting" and "You are speaking" captions stay removed.
    if (!transmitting && speakers.empty())
    {
        if (mVoiceHud != nullptr)
            mVoiceHud->setVisible(false);
        mVoiceHudCaption.clear();
        return;
    }

    const MyGUI::IntSize view = MyGUI::RenderManager::getInstance().getViewSize();
    if (view.width <= 0 || view.height <= 0)
        return;

    if (mVoiceHud == nullptr)
    {
        mVoiceHud = MyGUI::Gui::getInstance().createWidget<MyGUI::Widget>(
            "", 0, 0, view.width, view.height, MyGUI::Align::Stretch, "Popup");
        mVoiceHud->setNeedMouseFocus(false);
        mVoiceHud->setNeedKeyFocus(false);

        // Three thin strokes per edge create a green transmit-only glow.
        for (int i = 0; i < 12; ++i)
        {
            MyGUI::ImageBox* glow = mVoiceHud->createWidget<MyGUI::ImageBox>(
                "ImageBox", MyGUI::IntCoord(0, 0, 1, 1), MyGUI::Align::Default, "VoiceEdgeGlow");
            glow->setImageTexture("white");
            glow->setColour(MyGUI::Colour(0.27f, 0.92f, 0.43f));
            glow->setNeedMouseFocus(false);
            glow->setNeedKeyFocus(false);
            mVoiceHudGlow.push_back(glow);
        }

        // Separate compact remote-speaker list; never show local mic status here.
        mVoiceHudText = mVoiceHud->createWidget<MyGUI::TextBox>(
            "SandBrightText", MyGUI::IntCoord(12, 12, 240, 22),
            MyGUI::Align::Default, "VoiceSpeakers");
        mVoiceHudText->setNeedMouseFocus(false);
        mVoiceHudText->setNeedKeyFocus(false);
        mVoiceHudText->setTextShadow(true);
        mVoiceHudText->setTextShadowColour(MyGUI::Colour::Black);
    }

    mVoiceHud->setCoord(0, 0, view.width, view.height);
    const int thickness[3] = {8, 4, 2};
    const float baseAlpha[3] = {0.07f, 0.16f, 0.48f};
    const float activity = 0.72f + 0.28f * std::clamp(localLevel, 0.f, 1.f);
    std::size_t index = 0;
    for (int layer = 0; layer < 3; ++layer)
    {
        const int t = std::max(1, thickness[layer]);
        const float alpha = transmitting ? baseAlpha[layer] * activity : 0.f;
        MyGUI::ImageBox* top = mVoiceHudGlow[index++];
        MyGUI::ImageBox* bottom = mVoiceHudGlow[index++];
        MyGUI::ImageBox* left = mVoiceHudGlow[index++];
        MyGUI::ImageBox* right = mVoiceHudGlow[index++];
        top->setCoord(0, 0, view.width, t);
        bottom->setCoord(0, std::max(0, view.height - t), view.width, t);
        left->setCoord(0, 0, t, view.height);
        right->setCoord(std::max(0, view.width - t), 0, t, view.height);
        top->setAlpha(alpha);
        bottom->setAlpha(alpha);
        left->setAlpha(alpha);
        right->setAlpha(alpha);
    }

    if (mVoiceHudText != nullptr)
    {
        // Speaker names originate on the network. Escape MyGUI colour codes
        // and remove control characters so names cannot modify the overlay.
        std::string caption;
        for (const VoiceHudSpeaker& speaker : speakers)
        {
            if (!caption.empty())
                caption += "\n";
            std::string name;
            for (const char c : speaker.mName)
            {
                if (c == '\n' || c == '\r' || static_cast<unsigned char>(c) < 0x20)
                    continue;
                if (c == '#')
                    name += "##";
                else
                    name += c;
            }
            const int segments = 8;
            const int active = std::clamp(static_cast<int>(speaker.mLevel * segments + 0.5f), 0, segments);
            std::string meter = "[";
            meter.append(static_cast<std::size_t>(active), '|');
            meter.append(static_cast<std::size_t>(segments - active), '.');
            meter += "] ";
            meter += std::to_string(std::clamp(static_cast<int>(speaker.mLevel * 100.f + 0.5f), 0, 100));
            meter += "%";
            caption += std::string(speaker.mLocal ? "#8DFF9F" : "#9FD7FF") + name
                + "#FFFFFF  " + meter;
        }
        const int width = std::max(190, std::min(420, view.width / 2));
        const int height = std::max(20, 6 + static_cast<int>(speakers.size()) * 20);
        const int top = std::max(12, std::min(view.height / 2, view.height - height - 110));
        mVoiceHudText->setCoord(12, top, width, height);
        mVoiceHudText->setVisible(!speakers.empty());
        if (caption != mVoiceHudCaption)
        {
            mVoiceHudText->setCaptionWithReplacing(caption);
            mVoiceHudCaption = caption;
        }
    }
    mVoiceHud->setVisible(true);
}

void mwmp::GUIController::destroyVoiceHud()
{
    if (mVoiceHud != nullptr)
        MyGUI::Gui::getInstance().destroyWidget(mVoiceHud);

    mVoiceHud = nullptr;
    mVoiceHudGlow.clear();
    mVoiceHudText = nullptr;
    mVoiceHudCaption.clear();
}

bool mwmp::GUIController::handleEmbeddedRestartControl(const std::string& message)
{
    static const std::string prefix = "@@AMP_RESTART@@";
    if (message.compare(0, prefix.size(), prefix) != 0)
        return false;

    std::string value = message.substr(prefix.size());
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r' || value.back() == ' '))
        value.pop_back();

    // Y052: an aborted or rescheduled countdown clears the overlay immediately.
    // Previously the HUD stayed on screen until the GUIController was destroyed.
    if (value == "CANCEL")
    {
        destroyEmbeddedRestartHud();
        return true;
    }

    int seconds = 0;
    try
    {
        seconds = std::max(0, std::stoi(value));
    }
    catch (...)
    {
        return true;
    }

    ensureEmbeddedRestartHud();
    if (seconds > mEmbeddedRestartTotalSeconds)
        mEmbeddedRestartTotalSeconds = seconds;

    if (mEmbeddedRestartProgress != nullptr)
    {
        mEmbeddedRestartProgress->setProgressRange(std::max(1, mEmbeddedRestartTotalSeconds));
        mEmbeddedRestartProgress->setProgressPosition(std::min(seconds, mEmbeddedRestartTotalSeconds));
    }

    const auto arenaText = [](const std::string& key)
    {
        return MyGUI::LanguageManager::getInstance().replaceTags("#{arenamp=" + key + "}");
    };

    std::string title = arenaText("restart.countdown");
    const std::string token = "%s";
    const std::size_t pos = title.find(token);
    if (pos != std::string::npos)
        title.replace(pos, token.size(), std::to_string(seconds));
    else
        title += " " + std::to_string(seconds);

    if (mEmbeddedRestartTitle != nullptr)
        mEmbeddedRestartTitle->setCaption(title);
    if (mEmbeddedRestartMessage != nullptr)
        mEmbeddedRestartMessage->setCaption(arenaText("restart.leave_request"));

    return true;
}


void mwmp::GUIController::setChatVisible(bool chatVisible)
{
    if (!chatVisible)
    {
        mChat->setHistoryReviewState(false);
        mChat->hideSmoothly();
    }
    else
        mChat->refreshPresentation();
}

void mwmp::GUIController::showDialogList(const mwmp::BasePlayer::GUIMessageBox &guiMessageBox)
{
    MWBase::WindowManager *windowManager = MWBase::Environment::get().getWindowManager();

    if (mListBox != NULL)
    {
        windowManager->removeDialog(mListBox);
        windowManager->removeCurrentModal(mListBox);
        mListBox = NULL;
    }

    std::vector<std::string> list;

    std::string buf;

    for (const auto &data : guiMessageBox.data)
    {
        if (data == '\n')
        {
            list.push_back(buf);
            buf.erase();
            continue;
        }
        buf += data;
    }

    list.push_back(buf);

    mListBox = new GUIDialogList(guiMessageBox.label, list);
    windowManager->pushGuiMode((MWGui::GuiMode)GM_TES3MP_ListBox);
}


void mwmp::GUIController::beginFurnitureCapture()
{
    mFurnitureCaptureReturnPending = true;
    if (mChat != nullptr)
        mChat->closeForWorldAction();
    ArenaHousing::get().armFurnitureCapture();
}

void mwmp::GUIController::openFurniturePlayerMenu()
{
    if (mChat != nullptr)
        mChat->openDecorationsPage();
}

void mwmp::GUIController::receiveFurnitureMessage(const std::string& payload)
{
    const bool begin = payload.compare(0, 8, "FS_BEGIN") == 0;
    const bool decorOpen = mChat && mChat->editState && mChat->menuState
        && mChat->activeTab == GUIChat::TAB_DECOR && !mChat->mainMenuOpen && mChat->mDecorPane;

    bool safeCaptureReturn = false;
    if (begin && mFurnitureCaptureReturnPending && mChat)
    {
        MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
        safeCaptureReturn = !player.isEmpty() && player.getCell() != nullptr
            && Main::get().getLocalPlayer()->isLoggedIn();
        if (safeCaptureReturn)
        {
            mChat->openDecorationsPage(false);
            mFurnitureCaptureReturnPending = false;
        }
    }

    const bool hostReady = decorOpen || safeCaptureReturn
        || (mChat && mChat->editState && mChat->menuState
            && mChat->activeTab == GUIChat::TAB_DECOR && mChat->mDecorPane);

    // Ignore unsolicited/stale furniture traffic while the world is still
    // loading. Opening MyGUI + RTT objects from a network packet before the
    // first cell exists was a reproducible Android crash path. The catalogue is
    // requested fresh as soon as the Decorations tab is actually opened.
    if (!mFurnitureStore && !hostReady)
        return;

    if (!mFurnitureStore)
        mFurnitureStore = new FurnitureStoreWindow();

    if (begin && mChat && mChat->mDecorPane && hostReady)
        mFurnitureStore->showInPlayerMenu(mChat->mDecorPane);

    mFurnitureStore->receive(payload);
}

void mwmp::GUIController::showServerQuestEditor()
{
    if (mServerQuestEditor == nullptr)
        mServerQuestEditor = new ServerQuestEditorWindow();
    else
        mServerQuestEditor->refreshFromRegistry();

    MWBase::WindowManager* windowManager = MWBase::Environment::get().getWindowManager();
    const MWGui::GuiMode mode = static_cast<MWGui::GuiMode>(GM_ARENAMP_QuestEditor);
    if (!windowManager->containsMode(mode))
        windowManager->pushGuiMode(mode);
    else
        mServerQuestEditor->setVisible(true);
}

void mwmp::GUIController::refreshServerQuestEditor()
{
    if (mServerQuestEditor != nullptr)
        mServerQuestEditor->refreshFromRegistry();
}

void mwmp::GUIController::showMessageBox(const BasePlayer::GUIMessageBox &guiMessageBox)
{
    MWBase::WindowManager *windowManager = MWBase::Environment::get().getWindowManager();
    windowManager->messageBox(guiMessageBox.label);
}

std::vector<std::string> splitString(const std::string &str, char delim = ';')
{
    std::istringstream ss(str);
    std::vector<std::string> result;
    std::string token;
    while (std::getline(ss, token, delim))
        result.push_back(token);
    return result;
}

void mwmp::GUIController::showCustomMessageBox(const BasePlayer::GUIMessageBox &guiMessageBox)
{
    MWBase::WindowManager* windowManager = MWBase::Environment::get().getWindowManager();
    std::vector<std::string> buttons = splitString(guiMessageBox.buttons);
    windowManager->interactiveMessageBox(guiMessageBox.label, buttons, false, true);
}

void mwmp::GUIController::showInputBox(const BasePlayer::GUIMessageBox &guiMessageBox)
{
    MWBase::WindowManager *windowManager = MWBase::Environment::get().getWindowManager();
    LocalPlayer* localPlayer = Main::get().getLocalPlayer();

    const bool passwordDialog = guiMessageBox.type == BasePlayer::GUIMessageBox::PasswordDialog;
    const bool preLoginAccountPrompt = passwordDialog && !localPlayer->isLoggedIn();

    // CoreScripts' stable GUI ids are LOGIN=1 and REGISTER=2 (tableHelper.enum
    // is 1-based). Existing accounts may use the password already collected on
    // the initial ArenaMP account card for a fast login. New accounts must never
    // auto-submit: registration gets a dedicated confirmation screen first.
    // CoreScripts uses REGISTER=2. Keep a note-based fallback as well because
    // older/custom server scripts may preserve PasswordDialog semantics while
    // using a different GUI id. The registration prompt is the only pre-login
    // password dialog that normally carries explanatory note text.
    const bool registrationPrompt = preLoginAccountPrompt
        && (guiMessageBox.id == 2 || !guiMessageBox.note.empty());
    const bool loginPrompt = preLoginAccountPrompt && !registrationPrompt;

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
        "ArenaMP account PasswordDialog: id=%d registration=%d login=%d",
        guiMessageBox.id, registrationPrompt ? 1 : 0, loginPrompt ? 1 : 0);

    if (loginPrompt && !mPreLoginPasswordAutoSubmitted)
    {
        const std::string savedPassword = Settings::Manager::getString("password", "Login");
        if (!savedPassword.empty())
        {
            mPreLoginPasswordAutoSubmitted = true;
            submitInputReply(savedPassword);
            return;
        }
    }

    windowManager->removeDialog(mInputBox);
    mInputBox = nullptr;
    windowManager->removeDialog(mAccountLoginBox);
    mAccountLoginBox = nullptr;

    // Do not stack the same TES3MP modal mode repeatedly when a server replaces
    // one password prompt with another (login retry / registration). A duplicate
    // mode can keep LocalPlayer::processCharGen() blocked even after the visible
    // dialog has gone away.
    const MWGui::GuiMode inputMode = (MWGui::GuiMode)GM_TES3MP_InputBox;
    if (!windowManager->containsMode(inputMode))
        windowManager->pushGuiMode(inputMode);

    if (preLoginAccountPrompt)
    {
        // Once the server knows the player name it also knows whether the account
        // exists. Registration therefore has its own branded card with a second
        // password field. The account name is locked in both modes because changing
        // it after PlayerBaseInfo requires reconnecting.
        mAccountLoginBox = new GUILogin(registrationPrompt ? GUILogin::RegisterMode : GUILogin::LoginMode);
        mAccountLoginBox->setLogin(Settings::Manager::getString("name", "Login"));
        mAccountLoginBox->setPassword(registrationPrompt
            ? Settings::Manager::getString("password", "Login")
            : "");
        mAccountLoginBox->setLanguage(Settings::Manager::getString("interface language", "General"));
        mAccountLoginBox->setLoginEditable(false);
        mAccountLoginBox->setRetryMode(loginPrompt && mPreLoginPasswordAutoSubmitted);
        mAccountLoginBox->eventDone += MyGUI::newDelegate(this, &GUIController::onAccountLoginDone);
        mAccountLoginBox->setVisible(true);
        return;
    }

    // Ordinary server InputDialog/PasswordDialog windows used after login stay
    // compact and keep their original semantics.
    mInputBox = new TextInputDialog();
    mInputBox->setEditPassword(passwordDialog);
    mInputBox->setTextLabel(guiMessageBox.label);
    mInputBox->setTextNote(guiMessageBox.note);
    mInputBox->eventDone += MyGUI::newDelegate(this, &GUIController::onInputBoxDone);
    mInputBox->setVisible(true);
}

void mwmp::GUIController::submitInputReply(const std::string& rawText)
{
    LocalPlayer *localPlayer = Main::get().getLocalPlayer();
    std::string textInput = rawText;

    // Password dialogs use TES3MP's historical client-side double hashing. Keep
    // the plain password only in local settings; the protocol payload remains
    // exactly what existing servers expect.
    if (localPlayer->guiMessageBox.type == BasePlayer::GUIMessageBox::PasswordDialog)
    {
        if (!localPlayer->isLoggedIn())
        {
            Settings::Manager::setString("password", "Login", rawText);
            Settings::Manager::saveUser();
        }

        textInput = picosha2::hash256_hex_string(textInput);
        textInput = picosha2::hash256_hex_string(textInput
            + picosha2::hash256_hex_string(picosha2::hash256_hex_string(textInput)));
    }

    localPlayer->guiMessageBox.data = textInput;

    PlayerPacket *playerPacket = Main::get().getNetworking()->getPlayerPacket(ID_GUI_MESSAGEBOX);
    playerPacket->setPlayer(localPlayer);
    playerPacket->Send();
}

void mwmp::GUIController::onInputBoxDone(MWGui::WindowBase *parWindow)
{
    (void)parWindow;
    submitInputReply(mInputBox->getTextInput());

    MWBase::WindowManager *windowManager = MWBase::Environment::get().getWindowManager();
    windowManager->removeDialog(mInputBox);
    mInputBox = nullptr;
    windowManager->popGuiMode();
}

void mwmp::GUIController::onAccountLoginDone(MWGui::WindowBase *parWindow)
{
    (void)parWindow;
    if (!mAccountLoginBox)
        return;

    // Capture everything before destroying the modal. In particular, close the
    // TES3MP input GUI mode BEFORE the password reply is sent: registration can
    // make the server answer with ID_PLAYER_CHARGEN immediately, and CharGen is
    // intentionally blocked while any GUI mode is still active.
    const std::string password = mAccountLoginBox->getPassword();
    const std::string language = mAccountLoginBox->getLanguage();
    const bool registration = mAccountLoginBox->isRegistrationMode();

    Settings::Manager::setString("password", "Login", password);
    Settings::Manager::setString("interface language", "General", language);
    Settings::Manager::saveUser();
    MWBase::Environment::get().getWindowManager()->setArenaLanguage(language);

    if (!registration)
        mPreLoginPasswordAutoSubmitted = true;

    MWBase::WindowManager *windowManager = MWBase::Environment::get().getWindowManager();
    windowManager->removeDialog(mAccountLoginBox);
    mAccountLoginBox = nullptr;
    windowManager->removeGuiMode((MWGui::GuiMode)GM_TES3MP_InputBox);

    // The account card is shown after the initial PlayerBaseInfo handshake, so
    // re-send the selected RU/EN flag before the password response. The server
    // can then localise registration/login result messages for this player.
    LocalPlayer* localPlayer = Main::get().getLocalPlayer();
    localPlayer->updateLanguage();
    PlayerPacket* baseInfoPacket = Main::get().getNetworking()->getPlayerPacket(ID_PLAYER_BASEINFO);
    baseInfoPacket->setPlayer(localPlayer);
    baseInfoPacket->Send();

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
        "Submitting ArenaMP %s password reply after closing TES3MP input mode",
        registration ? "registration" : "login");
    submitInputReply(password);
}

bool mwmp::GUIController::pressedChatLayoutKey(int key, bool repeat)
{
    if (mChat == nullptr)
        return false;
    MWBase::WindowManager* wm = MWBase::Environment::get().getWindowManager();
    const auto mode = wm->getMode();
    if (wm->isConsoleMode() || (mode != MWGui::GM_None
        && !(mode == static_cast<MWGui::GuiMode>(GM_ARENAMP_PlayerMenu) && mChat->historyReviewState)))
        return false;
    if (key == keyChatMode)
    {
        if (!repeat)
            mChat->pressedChatMode();
        return true;
    }
    if (key == SDL_SCANCODE_ESCAPE && mChat->historyReviewState)
    {
        if (!repeat)
            mChat->setHistoryReviewState(false);
        return true;
    }
    return false;
}

bool mwmp::GUIController::pressedKey(int key)
{
    MWBase::WindowManager *windowManager = MWBase::Environment::get().getWindowManager();
    if (mChat == nullptr || windowManager->isConsoleMode())
        return false;
    const auto mode = windowManager->getMode();
    if (mode != MWGui::GM_None
        && !(mode == static_cast<MWGui::GuiMode>(GM_ARENAMP_PlayerMenu) && mChat->historyReviewState))
        return false;
    if (key == keyChatMode)
    {
        mChat->pressedChatMode();
        return true;
    }
    else if (key == keySay)
    {
        // X054: once the caret is already in the chat the Say key is just an
        // ordinary character. Only a press that arrives from gameplay opens it.
        if (mChat->getEditState())
            return false;

        if (sayHoldThreshold <= 0.f)
        {
            mChat->openPlayerMenu();
            return true;
        }

        mChat->pressedSay();
        // The key is still physically down at this point, so mark it before the
        // first update tick: SDL may already have queued a text-input event for
        // it against the editor we just focused.
        mChat->setSayKeyHeld(true);
        sayHoldArmed = true;
        sayHoldTriggered = false;
        sayHoldTime = 0.f;
        return true;
    }
    return false;
}

void mwmp::GUIController::updateSayKeyHold(float dt)
{
    if (mChat == nullptr)
        return;

    const Uint8* keyboard = SDL_GetKeyboardState(nullptr);
    const bool down = sayHoldArmed && keyboard != nullptr && keySay > 0
        && keySay < SDL_NUM_SCANCODES && keyboard[keySay] != 0;

    if (!down)
    {
        if (sayHoldArmed)
            mChat->setSayKeyHeld(false);
        sayHoldArmed = false;
        sayHoldTriggered = false;
        sayHoldTime = 0.f;
        return;
    }

    if (sayHoldTriggered)
        return;

    sayHoldTime += std::max(0.f, dt);
    if (sayHoldTime >= sayHoldThreshold)
    {
        sayHoldTriggered = true;
        mChat->openPlayerMenu();
    }
}

void mwmp::GUIController::changeChatMode()
{
    mChat->pressedChatMode();
}

bool mwmp::GUIController::getChatEditState()
{
    return mChat->editState;
}

std::string mwmp::GUIController::getChatHistoryText() const
{
    return mChat != nullptr ? mChat->getHistoryText() : std::string();
}

bool mwmp::GUIController::getChatHistoryCoord(int& x, int& y, int& width, int& height) const
{
    if (mChat == nullptr || mChat->mHistory == nullptr)
        return false;

    const MyGUI::IntCoord coord = mChat->mHistory->getAbsoluteCoord();
    if (coord.width <= 0 || coord.height <= 0)
        return false;

    x = coord.left;
    y = coord.top;
    width = coord.width;
    height = coord.height;
    return true;
}

void mwmp::GUIController::setChatMainMenuOpen(bool state)
{
    if (mChat != nullptr)
        mChat->setMainMenuOpen(state);
}

void mwmp::GUIController::update(float dt)
{
    FurnitureStoreWindow::collectRetiredPreviews();
    if (mChat != nullptr)
    {
        updateSayKeyHold(dt);
        mChat->update(dt);
    }
    if (mFurnitureStore && mChat)
        mFurnitureStore->syncPlayerMenu(mChat->editState && mChat->menuState &&
            mChat->activeTab==GUIChat::TAB_DECOR && !mChat->mainMenuOpen);

    // Z001: housing owns two timers - the deadline on a door request the server
    // has not answered, and the short settle delay before a freshly entered
    // private instance is swept.
    ArenaHousing::get().update(dt);

    // Re-arm automatic credential submission only after a successful login, so
    // a wrong password produces the retry card instead of an infinite resend loop.
    if (Main::get().getLocalPlayer()->isLoggedIn())
        mPreLoginPasswordAutoSubmitted = false;
}

void mwmp::GUIController::processCustomMessageBoxInput(int pressedButton)
{
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE, "Pressed: %d", pressedButton);

    LocalPlayer* localPlayer = Main::get().getLocalPlayer();
    localPlayer->guiMessageBox.data = MyGUI::utility::toString(pressedButton);

    PlayerPacket* playerPacket = Main::get().getNetworking()->getPlayerPacket(ID_GUI_MESSAGEBOX);
    playerPacket->setPlayer(Main::get().getLocalPlayer());
    playerPacket->Send();
}

void mwmp::GUIController::WM_UpdateVisible(MWGui::GuiMode mode)
{
    switch((int)mode)
    {
        case GM_TES3MP_InputBox:
        {
            if (mInputBox != nullptr)
                mInputBox->setVisible(true);
            if (mAccountLoginBox != nullptr)
                mAccountLoginBox->setVisible(true);
            break;
        }
        case GM_TES3MP_ListBox:
        {
            if (mListBox != 0)
                mListBox->setVisible(true);
            break;
        }
        case GM_ARENAMP_QuestEditor:
        {
            if (mServerQuestEditor != nullptr)
            {
                mServerQuestEditor->refreshFromRegistry();
                mServerQuestEditor->setVisible(true);
            }
            break;
        }
        case GM_ARENAMP_Furniture:
            if (mFurnitureStore) mFurnitureStore->setVisible(true);
            break;
        case GM_ARENAMP_PlayerMenu:
            // GUIChat owns its widgets. The mode exists so the core WindowManager
            // releases mouse-look and exposes the cursor while chat is interactive.
            break;
        default:
            break;
    }
}

ESM::CustomMarker mwmp::GUIController::createMarker(const RakNet::RakNetGUID &guid)
{
    DedicatedPlayer *player = PlayerList::getPlayer(guid);
    ESM::CustomMarker mEditingMarker;
    if (!player)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Unknown player guid: %s", guid.ToString());
        return mEditingMarker;
    }

    mEditingMarker.mNote = player->npc.mName;

    const ESM::Cell *playerCell = &player->cell;

    mEditingMarker.mCell = player->cell.mCellId;

    mEditingMarker.mWorldX = player->position.pos[0];
    mEditingMarker.mWorldY = player->position.pos[1];

    mEditingMarker.mCell.mPaged = playerCell->isExterior();
    if (!playerCell->isExterior())
        mEditingMarker.mCell.mWorldspace = playerCell->mName;
    else
    {
        mEditingMarker.mCell.mWorldspace = ESM::CellId::sDefaultWorldspace;

        // Don't remove these, or the markers will stop showing up in exteriors
        mEditingMarker.mCell.mIndex.mX = playerCell->getGridX();
        mEditingMarker.mCell.mIndex.mY = playerCell->getGridY();
    }
    return mEditingMarker;
}


void mwmp::GUIController::updatePlayersMarkers(MWGui::LocalMapBase *localMapBase)
{
    // Y049: live player positions remain tooltip-only on the local/HUD map (the
    // older per-network-update widgets were unstable). Only persistent group
    // markers are materialized here, and they change rarely.
    for (MyGUI::Widget* widget : localMapBase->mPlayerMarkerWidgets)
        MyGUI::Gui::getInstance().destroyWidget(widget);
    localMapBase->mPlayerMarkerWidgets.clear();

    if (!localMapBase->mLocalMap)
        return;

    std::size_t created = 0;
    constexpr std::size_t maxVisibleGroupMarkers = 48;
    for (int dX = -localMapBase->mCellDistance; dX <= localMapBase->mCellDistance && created < maxVisibleGroupMarkers; ++dX)
    {
        for (int dY = -localMapBase->mCellDistance; dY <= localMapBase->mCellDistance && created < maxVisibleGroupMarkers; ++dY)
        {
            ESM::CellId cellId;
            cellId.mPaged = !localMapBase->mInterior;
            cellId.mWorldspace = localMapBase->mInterior ? localMapBase->mPrefix : ESM::CellId::sDefaultWorldspace;
            cellId.mIndex.mX = localMapBase->mCurX + dX;
            cellId.mIndex.mY = localMapBase->mCurY + dY;
            PlayerMarkerCollection::RangeType range = mPlayerMarkers.getMarkers(cellId);
            for (auto it = range.first; it != range.second && created < maxVisibleGroupMarkers; ++it)
            {
                const ESM::CustomMarker& marker = it->second;
                const MWGui::ArenaMapMarkerStyle style = MWGui::parseArenaMapMarker(marker.mNote);
                if (!style.styled || !style.group || marker.mNote.rfind("@AMP_GMARK@|", 0) != 0)
                    continue;

                MWGui::LocalMapBase::MarkerUserData markerPos(localMapBase->mLocalMapRender);
                const MyGUI::IntPoint pos = localMapBase->getMarkerPosition(marker.mWorldX, marker.mWorldY, markerPos);
                MyGUI::Widget* widget = localMapBase->mLocalMap->createWidget<MyGUI::Widget>(
                    "CustomMarkerButton", MyGUI::IntCoord(pos.left - 8, pos.top - 8, 16, 16), MyGUI::Align::Default);
                widget->setDepth(1);
                widget->setColour(MWGui::arenaMarkerColour(style.color));
                widget->setUserString("ToolTipType", "Layout");
                widget->setUserString("ToolTipLayout", "TextToolTipOneLine");
                widget->setUserString("Caption_TextOneLine", style.kind + "  " + style.text);
                widget->setNeedMouseFocus(true);

                MyGUI::TextBox* glyph = widget->createWidget<MyGUI::TextBox>(
                    "SandBrightText", MyGUI::IntCoord(0, -1, 16, 16), MyGUI::Align::Stretch);
                glyph->setCaption(style.kind);
                glyph->setTextAlign(MyGUI::Align::Center);
                glyph->setNeedMouseFocus(false);
                localMapBase->mPlayerMarkerWidgets.push_back(widget);
                ++created;
            }
        }
    }
}

void mwmp::GUIController::setGlobalMapMarkerTooltip(MWGui::MapWindow *mapWindow, MyGUI::Widget *markerWidget, int x, int y)
{
    ESM::CellId cellId;
    cellId.mIndex.mX = x;
    cellId.mIndex.mY = y;
    cellId.mWorldspace = ESM::CellId::sDefaultWorldspace;
    cellId.mPaged = true;
    PlayerMarkerCollection::RangeType markers = mPlayerMarkers.getMarkers(cellId);
    std::vector<std::string> destNotes;
    bool hasLivePlayer = false;
    for (PlayerMarkerCollection::ContainerType::const_iterator it = markers.first; it != markers.second; ++it)
    {
        const MWGui::ArenaMapMarkerStyle style = MWGui::parseArenaMapMarker(it->second.mNote);
        if (style.styled)
            destNotes.push_back(style.kind + "  " + style.text);
        else
        {
            destNotes.push_back(it->second.mNote);
            hasLivePlayer = true;
        }
    }

    // Y049: a visited exterior cell containing at least one connected player is
    // a red square on the world map. Group notes alone do not trigger the red.
    markerWidget->setColour(hasLivePlayer ? MyGUI::Colour(1.f, 0.12f, 0.10f)
                                          : MyGUI::Colour::parse(MyGUI::LanguageManager::getInstance().replaceTags("#{fontcolour=normal}")));

    if (!destNotes.empty())
    {
        MWGui::LocalMapBase::MarkerUserData data (nullptr);
        data.notes = destNotes;
        data.caption = markerWidget->getUserString("Caption_TextOneLine");

        markerWidget->setUserData(data);
        markerWidget->setUserString("ToolTipType", "MapMarker");
    }
    else
        markerWidget->setUserString("ToolTipType", "Layout");
}

void mwmp::GUIController::updateGlobalMapMarkerTooltips(MWGui::MapWindow *mapWindow)
{
    for (const auto &widget : mapWindow->mGlobalMapMarkers)
    {
        const int x = widget.first.first;
        const int y = widget.first.second;
        setGlobalMapMarkerTooltip(mapWindow, widget.second, x, y);
    }
}
