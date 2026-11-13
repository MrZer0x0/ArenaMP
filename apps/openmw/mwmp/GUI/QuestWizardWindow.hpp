#ifndef OPENMW_MWMP_GUI_QUESTWIZARDWINDOW_HPP
#define OPENMW_MWMP_GUI_QUESTWIZARDWINDOW_HPP

#include <map>
#include <string>
#include <utility>
#include <vector>

#include <MyGUI_Button.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_ListBox.h>
#include <MyGUI_TextBox.h>
#include <MyGUI_Widget.h>

#include "apps/openmw/mwgui/windowbase.hpp"

namespace mwmp
{
    struct ServerQuestEditorQuest;

    /// ArenaMP QW1: step-by-step quest wizard for staff who do not want to
    /// learn stages, requirements and rewards. The draft lives in this window;
    /// the server receives only the wizard parameters and builds, validates and
    /// stores the real quest itself (server/scripts/questWizard.lua).
    ///
    /// Picking in the world: the window closes, the server captures the next
    /// activation (E / tap) of the moderator without performing it, and sends
    /// the object back. The window then reopens on the page it was left on.
    class QuestWizardWindow : public MWGui::WindowModal
    {
    public:
        QuestWizardWindow();

        void onOpen() override;
        bool exit() override;

        /// Called after every editor sync (quest list, save result, notice).
        void refreshFromRegistry();
        /// Called when the server answers a pick / "mark this place" request.
        void applyPick();

    private:
        enum Page
        {
            Page_Home,
            Page_Type,
            Page_Giver,
            Page_Task,
            Page_Texts,
            Page_Journal,
            Page_Reward,
            Page_Summary,
            Page_Inventory
        };

        struct Item
        {
            std::string refId;
            std::string name;
            int count = 1;
        };

        struct Answer
        {
            std::string text;
            std::string reply;
            bool success = false;
        };

        struct Draft
        {
            std::string questId; // empty for a quest that was never saved
            bool detached = false;
            std::map<std::string, std::string> values;
            std::vector<Item> fetchItems;
            std::vector<Item> rewardItems;
            Answer answers[3];
        };

        // ---- helpers
        static std::string tr(const std::string& key);
        static void applyGameFont(MyGUI::Widget* widget);
        void captureBaseGeometry(MyGUI::Widget* widget);
        void fitToViewport();
        void sendCommand(const std::vector<std::string>& fields);
        void closeWindow();
        void setNotice(const std::string& text);

        std::string value(const std::string& key) const;
        int intValue(const std::string& key, int fallback = 0) const;
        void setValue(const std::string& key, const std::string& value);
        std::string questType() const;

        void newDraft();
        bool loadDraft(const ServerQuestEditorQuest& quest);
        std::vector<std::string> serializeDraft() const;
        std::vector<std::string> draftProblems() const;
        std::string describeDraft() const;

        // ---- pages
        void showPage(Page page);
        void loadPage();
        void collectPage();
        bool canLeaveForward(std::string& why) const;
        int stepNumber(Page page) const;
        void updateBottomBar();

        void rebuildHome();
        void rebuildItemList(MyGUI::ListBox* list, const std::vector<Item>& items, std::size_t selected);
        void rebuildInventory();
        void updateTypeButtons();
        void updateTaskPanels();
        void updateRadiusButtons();
        void updateCountLabels();
        void updateTextVisibility();

        void startPick(const std::string& kind);
        void openInventory(const std::string& target);

        // ---- events
        void onClose(MyGUI::Widget*);
        void onHome(MyGUI::Widget*);
        void onBack(MyGUI::Widget*);
        void onNext(MyGUI::Widget*);
        void onHomeSelected(MyGUI::ListBox*, std::size_t index);
        void onHomeNew(MyGUI::Widget*);
        void onHomeEdit(MyGUI::Widget*);
        void onHomePublish(MyGUI::Widget*);
        void onHomeDisable(MyGUI::Widget*);
        void onOpenStudio(MyGUI::Widget*);
        void onType(MyGUI::Widget* sender);
        void onPickGiver(MyGUI::Widget*);
        void onFetchFromInv(MyGUI::Widget*);
        void onFetchPickWorld(MyGUI::Widget*);
        void onFetchRemove(MyGUI::Widget*);
        void onFetchSelected(MyGUI::ListBox*, std::size_t index);
        void onFetchCount(MyGUI::Widget* sender);
        void onFetchTake(MyGUI::Widget*);
        void onPickKill(MyGUI::Widget*);
        void onKillCount(MyGUI::Widget* sender);
        void onVisitHere(MyGUI::Widget*);
        void onRadius(MyGUI::Widget* sender);
        void onAnswerOk(MyGUI::Widget* sender);
        void onMoney(MyGUI::Widget* sender);
        void onRewardFromInv(MyGUI::Widget*);
        void onRewardPickWorld(MyGUI::Widget*);
        void onRewardRemove(MyGUI::Widget*);
        void onRewardSelected(MyGUI::ListBox*, std::size_t index);
        void onRewardCount(MyGUI::Widget* sender);
        void onSave(MyGUI::Widget* sender);
        void onInvSearch(MyGUI::EditBox*);
        void onInvAdd(MyGUI::Widget*);
        void onInvAccept(MyGUI::ListBox*, std::size_t);
        void onInvCancel(MyGUI::Widget*);

        // ---- widgets
        MyGUI::TextBox* mTitle = nullptr;
        MyGUI::TextBox* mStep = nullptr;
        MyGUI::EditBox* mNotice = nullptr;
        MyGUI::Button* mHomeButton = nullptr;
        MyGUI::Button* mBackButton = nullptr;
        MyGUI::Button* mNextButton = nullptr;
        MyGUI::Button* mCloseButton = nullptr;
        MyGUI::Widget* mPages[Page_Inventory + 1] = {};

        MyGUI::ListBox* mHomeList = nullptr;
        MyGUI::EditBox* mHomeInfo = nullptr;
        MyGUI::Button* mHomeNew = nullptr;
        MyGUI::Button* mHomeEdit = nullptr;
        MyGUI::Button* mHomePublish = nullptr;
        MyGUI::Button* mHomeDisable = nullptr;
        MyGUI::Button* mHomeStudio = nullptr;

        MyGUI::EditBox* mName = nullptr;
        MyGUI::Button* mTypeButtons[4] = {};

        MyGUI::TextBox* mGiverInfo = nullptr;
        MyGUI::EditBox* mGiverRefId = nullptr;
        MyGUI::EditBox* mGiverCell = nullptr;
        MyGUI::EditBox* mTopic = nullptr;

        MyGUI::Widget* mTaskPanels[4] = {};
        MyGUI::ListBox* mFetchList = nullptr;
        MyGUI::TextBox* mFetchCount = nullptr;
        MyGUI::Button* mFetchMinus = nullptr;
        MyGUI::Button* mFetchPlus = nullptr;
        MyGUI::Button* mFetchTake = nullptr;
        MyGUI::TextBox* mKillInfo = nullptr;
        MyGUI::EditBox* mKillRefId = nullptr;
        MyGUI::TextBox* mKillCount = nullptr;
        MyGUI::Button* mKillMinus = nullptr;
        MyGUI::Button* mKillPlus = nullptr;
        MyGUI::TextBox* mVisitInfo = nullptr;
        MyGUI::Button* mRadiusButtons[4] = {};
        MyGUI::EditBox* mVisitName = nullptr;
        MyGUI::EditBox* mTalkQuestion = nullptr;
        MyGUI::EditBox* mAnswerText[3] = {};
        MyGUI::EditBox* mAnswerReply[3] = {};
        MyGUI::Button* mAnswerOk[3] = {};

        MyGUI::EditBox* mTextEdits[7] = {};
        MyGUI::Widget* mTextLabels[7] = {};
        MyGUI::EditBox* mJournalEdits[4] = {};
        MyGUI::Widget* mJournalLabels[4] = {};

        MyGUI::EditBox* mGold = nullptr;
        MyGUI::EditBox* mXp = nullptr;
        MyGUI::Button* mMoneyButtons[4] = {}; // gold -, gold +, xp -, xp +
        MyGUI::ListBox* mRewardList = nullptr;
        MyGUI::TextBox* mRewardCount = nullptr;
        MyGUI::Button* mRewardMinus = nullptr;
        MyGUI::Button* mRewardPlus = nullptr;

        MyGUI::EditBox* mSummary = nullptr;
        MyGUI::Button* mSaveDraft = nullptr;
        MyGUI::Button* mSavePublish = nullptr;

        MyGUI::EditBox* mInvSearch = nullptr;
        MyGUI::ListBox* mInvList = nullptr;
        MyGUI::EditBox* mInvInfo = nullptr;

        // ---- state
        Draft mDraft;
        bool mHasDraft = false;
        Page mPage = Page_Home;
        Page mPickReturnPage = Page_Home;
        std::string mPendingPick;
        unsigned mLastPickSerial = 0;
        unsigned mLastResultSerial = 0;
        std::string mHomeSelectedId;
        std::vector<std::string> mHomeIds;
        std::size_t mFetchSelected = 0;
        std::size_t mRewardSelected = 0;
        std::string mInventoryTarget;
        std::vector<Item> mInventory;
        std::vector<std::size_t> mInventoryVisible;
        std::string mServerErrors;

        MyGUI::IntSize mBaseSize;
        std::vector<std::pair<MyGUI::Widget*, MyGUI::IntCoord>> mBaseGeometry;
        float mAppliedScale = 1.0f;
    };
}

#endif
