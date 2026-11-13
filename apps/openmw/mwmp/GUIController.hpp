#ifndef OPENMW_GUICONTROLLER_HPP
#define OPENMW_GUICONTROLLER_HPP

#include <string>
#include <vector>

#include <components/settings/settings.hpp>

#include "apps/openmw/mwgui/mode.hpp"

#include <components/openmw-mp/Base/BasePlayer.hpp>
#include "GUI/PlayerMarkerCollection.hpp"
#include "GUI/TextInputDialog.hpp"
#include "GUI/GUILogin.hpp"

namespace MyGUI
{
    class Widget;
    class ProgressBar;
    class TextBox;
    class ImageBox;
}

namespace MWGui
{
    class LocalMapBase;
    class MapWindow;
}

namespace mwmp
{
    class GUIDialogList;
    class GUIChat;
    class ServerQuestEditorWindow;
    class FurnitureStoreWindow;
    class QuestWizardWindow;
    class GUIController
    {
    public:
        enum GM
        {
            GM_VR_MetaMenu = MWGui::GM_PlayerAnimationMenu + 1, // Put this dummy GuiMode here because it's used in VR
            GM_TES3MP_InputBox,
            GM_TES3MP_ListBox,
            GM_ARENAMP_QuestEditor,
            GM_ARENAMP_PlayerMenu,
            GM_ARENAMP_Furniture,
            GM_ARENAMP_QuestWizard // QW1: beginner quest wizard

        };
        GUIController();
        ~GUIController();
        void cleanUp();

        void refreshGuiMode(MWGui::GuiMode guiMode);

        void setupChat();

        void printChatMessage(std::string &msg);
        void setChatVisible(bool chatVisible);

        void showMessageBox(const BasePlayer::GUIMessageBox &guiMessageBox);
        void showCustomMessageBox(const BasePlayer::GUIMessageBox &guiMessageBox);
        void showInputBox(const BasePlayer::GUIMessageBox &guiMessageBox);

        void showDialogList(const BasePlayer::GUIMessageBox &guiMessageBox);

        // X039: real MyGUI server quest editor, opened by the hidden quest transport.
        void showServerQuestEditor();
        void receiveFurnitureMessage(const std::string& payload);
        // Close the Player Menu, then arm the one-shot world furniture picker.
        // Used by the integrated Decorations page's "Add furniture" button.
        void beginFurnitureCapture();
        void openFurniturePlayerMenu();
        void refreshServerQuestEditor();
        // QW1: beginner quest wizard. showQuestWizard() is called after an editor
        // sync in the "wizard" view; receiveWizardPick() after a world pick.
        void showQuestWizard();
        void receiveWizardPick();

        /// Returns 0 if there was no events
        bool pressedKey(int key);
        bool pressedChatLayoutKey(int key, bool repeat);

        void changeChatMode();

        bool requestGroupRecall();
        bool getChatEditState();
        std::string getChatHistoryText() const;
        bool getChatHistoryCoord(int& x, int& y, int& width, int& height) const;
        void setChatMainMenuOpen(bool state);

        void update(float dt);

        void processCustomMessageBoxInput(int pressedButton);

        void WM_UpdateVisible(MWGui::GuiMode mode);

        void updatePlayersMarkers(MWGui::LocalMapBase *localMapBase);
        void updateGlobalMapMarkerTooltips(MWGui::MapWindow *pWindow);

        ESM::CustomMarker createMarker(const RakNet::RakNetGUID &guid);
        PlayerMarkerCollection mPlayerMarkers;
    private:
        void setGlobalMapMarkerTooltip(MWGui::MapWindow *mapWindow ,MyGUI::Widget* markerWidget, int x, int y);

    private:
        // X054: tap-vs-hold arbitration for the Say key. The press itself opens
        // the historic HUD caret; only a sustained hold promotes it to the full
        // Player Menu.
        void updateSayKeyHold(float dt);

        // Y050: hidden server restart control envelope and borderless HUD overlay.
        bool handleEmbeddedRestartControl(const std::string& message);
        void ensureEmbeddedRestartHud();
        void destroyEmbeddedRestartHud();

        // U035b build fix: VoiceChat owns the data source for the private voice
        // HUD. Keep the HUD implementation private and grant only VoiceChat access.
        friend class VoiceChat;

        // U035: remote speakers and an independent local transmit glow;
        // local microphone status text is intentionally hidden.
        struct VoiceHudSpeaker
        {
            std::string mName;
            float mLevel = 0.f;   // 0..1 speech energy, drives the level bar
            bool mLocal = false;  // true for the local player while transmitting
        };
        void updateVoiceHud(const std::vector<VoiceHudSpeaker>& speakers,
            bool micOpen, bool transmitting, float localLevel, bool toggleMode);
        void destroyVoiceHud();

        GUIChat *mChat;
        int keySay;
        int keyChatMode;
        bool sayHoldArmed;
        bool sayHoldTriggered;
        float sayHoldTime;
        float sayHoldThreshold;

        long id;
        TextInputDialog *mInputBox;
        GUILogin *mAccountLoginBox;
        GUIDialogList *mListBox;
        ServerQuestEditorWindow *mServerQuestEditor;
        FurnitureStoreWindow* mFurnitureStore;
        QuestWizardWindow* mQuestWizard = nullptr;
        bool mFurnitureCaptureReturnPending = false;
        bool mPreLoginPasswordAutoSubmitted;

        MyGUI::Widget* mVoiceHud;
        std::vector<MyGUI::ImageBox*> mVoiceHudGlow;
        MyGUI::TextBox* mVoiceHudText;
        std::string mVoiceHudCaption;

        MyGUI::Widget* mEmbeddedRestartHud;
        MyGUI::ProgressBar* mEmbeddedRestartProgress;
        MyGUI::TextBox* mEmbeddedRestartTitle;
        MyGUI::TextBox* mEmbeddedRestartMessage;
        int mEmbeddedRestartTotalSeconds;
        void onInputBoxDone(MWGui::WindowBase* parWindow);
        void onAccountLoginDone(MWGui::WindowBase* parWindow);
        void submitInputReply(const std::string& rawText);
        //MyGUI::Widget *oldFocusWidget, *currentFocusWidget;
    };
}

#endif //OPENMW_GUICONTROLLER_HPP
