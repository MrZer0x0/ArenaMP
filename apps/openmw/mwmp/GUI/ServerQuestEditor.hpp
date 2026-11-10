#ifndef OPENMW_MWMP_GUI_SERVERQUESTEDITOR_HPP
#define OPENMW_MWMP_GUI_SERVERQUESTEDITOR_HPP

#include <string>
#include <vector>

#include <MyGUI_Button.h>
#include <MyGUI_ComboBox.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_ListBox.h>
#include <MyGUI_TabControl.h>
#include <MyGUI_TextBox.h>
#include <MyGUI_Widget.h>

#include <utility>

#include "apps/openmw/mwgui/windowbase.hpp"

namespace mwmp
{
    struct ServerQuestEditorQuest;
    struct ServerQuestEditorStage;
    struct ServerQuestEditorChoice;
    struct ServerQuestEditorRequirement;
    struct ServerQuestEditorReward;

    class ServerQuestEditorWindow : public MWGui::WindowModal
    {
    public:
        ServerQuestEditorWindow();

        void onOpen() override;
        bool exit() override;
        void refreshFromRegistry();

    private:
        void sendCommand(const std::vector<std::string>& fields);
        const ServerQuestEditorQuest* selectedQuest() const;
        const ServerQuestEditorStage* selectedStage() const;
        const ServerQuestEditorChoice* selectedOfferChoice() const;
        const ServerQuestEditorChoice* selectedStageChoice() const;

        void rebuildQuestList();
        void rebuildQuestDetails();
        void rebuildTopics();
        void rebuildOfferChoices();
        void rebuildStages();
        void rebuildStageChoices();
        void rebuildLogic();
        void rebuildValidation();
        void updateNotice();
        void clearEditorFields();

        /// X045: localisation. All Quest Studio captions live in
        /// l10n/arenamp/<lang>.ini under the questeditor.* prefix, exactly like
        /// the quest manager window, so English and Russian come from the same
        /// mechanism as the rest of the ArenaMP interface.
        static std::string tr(const std::string& key);
        void applyLocalization();
        void localizeLabel(const char* widgetName, const char* key);

        /// X048: the layout is authored at a fixed size and MyGUI never reflows it.
        /// On a small window (or with a large GUI scaling factor) the bottom bar,
        /// including the Close button, ends up outside the visible area. The original
        /// geometry of every widget is captured once and re-applied scaled so the
        /// whole studio always fits, whatever the resolution.
        void captureBaseGeometry(MyGUI::Widget* widget);
        void fitToViewport();
        /// X048: force the game's own font on the studio widgets. Parts of the layout
        /// use MyGUI core skins whose default font has no Cyrillic glyphs, which is
        /// what turns Russian quest names and journal text into empty boxes.
        static void applyGameFont(MyGUI::Widget* widget);

        void selectQuestById(const std::string& id, bool requestDetails);
        void selectStageByIndex(int stage);
        void setComboValue(MyGUI::ComboBox* combo, const std::string& value);
        std::string comboValue(MyGUI::ComboBox* combo) const;
        static std::string boolText(bool value);
        static int parseInt(const std::string& value, int fallback = 0);

        void notifyClose(MyGUI::Widget* sender);
        void notifySearchChanged(MyGUI::EditBox* sender);
        void notifyQuestSelected(MyGUI::ListBox* sender, std::size_t index);
        void notifyNewQuest(MyGUI::Widget* sender);
        void notifyCloneQuest(MyGUI::Widget* sender);
        void notifyDeleteQuest(MyGUI::Widget* sender);
        void notifySaveOverview(MyGUI::Widget* sender);
        void notifyPickGiver(MyGUI::Widget* sender);
        void notifyClearUnique(MyGUI::Widget* sender);

        void notifyTopicSelected(MyGUI::ListBox* sender, std::size_t index);
        void notifyNewTopic(MyGUI::Widget* sender);
        void notifySaveTopic(MyGUI::Widget* sender);
        void notifyDeleteTopic(MyGUI::Widget* sender);
        void notifySaveOffer(MyGUI::Widget* sender);
        void notifyOfferChoiceSelected(MyGUI::ListBox* sender, std::size_t index);
        void notifyNewOfferChoice(MyGUI::Widget* sender);
        void notifySaveOfferChoice(MyGUI::Widget* sender);
        void notifyDeleteOfferChoice(MyGUI::Widget* sender);

        void notifyStageSelected(MyGUI::ListBox* sender, std::size_t index);
        void notifyToggleInitial(MyGUI::Widget* sender);
        void notifyToggleComplete(MyGUI::Widget* sender);
        void notifyToggleFail(MyGUI::Widget* sender);
        void notifyNewStage(MyGUI::Widget* sender);
        void notifySaveStage(MyGUI::Widget* sender);
        void notifyDeleteStage(MyGUI::Widget* sender);
        void notifyStageChoiceSelected(MyGUI::ListBox* sender, std::size_t index);
        void notifyNewStageChoice(MyGUI::Widget* sender);
        void notifySaveStageChoice(MyGUI::Widget* sender);
        void notifyDeleteStageChoice(MyGUI::Widget* sender);
        void notifyAddNext(MyGUI::Widget* sender);
        void notifyDeleteNext(MyGUI::Widget* sender);

        void notifyLogicTargetChanged(MyGUI::ComboBox* sender, std::size_t index);
        void notifyRequirementSelected(MyGUI::ListBox* sender, std::size_t index);
        void notifyAddRequirement(MyGUI::Widget* sender);
        void notifyDeleteRequirement(MyGUI::Widget* sender);
        void notifyRewardSelected(MyGUI::ListBox* sender, std::size_t index);
        void notifyAddReward(MyGUI::Widget* sender);
        void notifyDeleteReward(MyGUI::Widget* sender);

        void notifyValidate(MyGUI::Widget* sender);
        void notifyPublish(MyGUI::Widget* sender);
        void notifyDisable(MyGUI::Widget* sender);
        void notifyRefresh(MyGUI::Widget* sender);

        MyGUI::TextBox* mTitle = nullptr;
        MyGUI::EditBox* mQuestSearch = nullptr;
        MyGUI::ListBox* mQuestList = nullptr;
        MyGUI::TextBox* mQuestCounter = nullptr;
        MyGUI::EditBox* mNewQuestId = nullptr;
        MyGUI::EditBox* mNewQuestName = nullptr;
        MyGUI::Button* mNewQuestButton = nullptr;
        MyGUI::Button* mCloneQuestButton = nullptr;
        MyGUI::Button* mDeleteQuestButton = nullptr;
        MyGUI::TabControl* mTabs = nullptr;

        MyGUI::EditBox* mQuestId = nullptr;
        MyGUI::TextBox* mQuestStatus = nullptr;
        MyGUI::EditBox* mQuestName = nullptr;
        MyGUI::ComboBox* mProgressMode = nullptr;
        MyGUI::TextBox* mQuestVersion = nullptr;
        MyGUI::TextBox* mQuestAuthor = nullptr;
        MyGUI::EditBox* mGiverRefId = nullptr;
        MyGUI::EditBox* mGiverCell = nullptr;
        MyGUI::EditBox* mGiverUnique = nullptr;
        MyGUI::EditBox* mInitialStage = nullptr;
        MyGUI::Button* mPickGiverButton = nullptr;
        MyGUI::Button* mClearUniqueButton = nullptr;
        MyGUI::Button* mSaveOverviewButton = nullptr;

        MyGUI::ListBox* mTopicList = nullptr;
        MyGUI::EditBox* mTopicId = nullptr;
        MyGUI::EditBox* mTopicText = nullptr;
        MyGUI::ComboBox* mTopicEnabled = nullptr;
        MyGUI::Button* mNewTopicButton = nullptr;
        MyGUI::Button* mSaveTopicButton = nullptr;
        MyGUI::Button* mDeleteTopicButton = nullptr;
        MyGUI::EditBox* mOfferDialogue = nullptr;
        MyGUI::Button* mSaveOfferButton = nullptr;
        MyGUI::ListBox* mOfferChoiceList = nullptr;
        MyGUI::EditBox* mOfferChoiceId = nullptr;
        MyGUI::ComboBox* mOfferChoiceAction = nullptr;
        MyGUI::EditBox* mOfferChoiceTarget = nullptr;
        MyGUI::EditBox* mOfferChoiceText = nullptr;
        MyGUI::Button* mNewOfferChoiceButton = nullptr;
        MyGUI::Button* mSaveOfferChoiceButton = nullptr;
        MyGUI::Button* mDeleteOfferChoiceButton = nullptr;

        MyGUI::ListBox* mStageList = nullptr;
        MyGUI::EditBox* mStageIndex = nullptr;
        MyGUI::Button* mStageInitialButton = nullptr;
        MyGUI::Button* mStageCompleteButton = nullptr;
        MyGUI::Button* mStageFailButton = nullptr;
        MyGUI::EditBox* mStageJournal = nullptr;
        MyGUI::EditBox* mStageDialogue = nullptr;
        MyGUI::Button* mNewStageButton = nullptr;
        MyGUI::Button* mSaveStageButton = nullptr;
        MyGUI::Button* mDeleteStageButton = nullptr;
        MyGUI::ListBox* mStageChoiceList = nullptr;
        MyGUI::EditBox* mStageChoiceId = nullptr;
        MyGUI::ComboBox* mStageChoiceAction = nullptr;
        MyGUI::EditBox* mStageChoiceTarget = nullptr;
        MyGUI::EditBox* mStageChoiceText = nullptr;
        MyGUI::Button* mNewStageChoiceButton = nullptr;
        MyGUI::Button* mSaveStageChoiceButton = nullptr;
        MyGUI::Button* mDeleteStageChoiceButton = nullptr;
        MyGUI::EditBox* mNextStageTarget = nullptr;
        MyGUI::Button* mAddNextButton = nullptr;
        MyGUI::Button* mDeleteNextButton = nullptr;

        MyGUI::ComboBox* mLogicTarget = nullptr;
        MyGUI::TextBox* mLogicContext = nullptr;
        MyGUI::ListBox* mRequirementList = nullptr;
        MyGUI::ComboBox* mRequirementType = nullptr;
        MyGUI::ComboBox* mRequirementOp = nullptr;
        MyGUI::EditBox* mRequirementValue = nullptr;
        MyGUI::EditBox* mRequirementRef = nullptr;
        MyGUI::Button* mAddRequirementButton = nullptr;
        MyGUI::Button* mDeleteRequirementButton = nullptr;
        MyGUI::ListBox* mRewardList = nullptr;
        MyGUI::ComboBox* mRewardType = nullptr;
        MyGUI::EditBox* mRewardValueA = nullptr;
        MyGUI::EditBox* mRewardValueB = nullptr;
        MyGUI::Button* mAddRewardButton = nullptr;
        MyGUI::Button* mDeleteRewardButton = nullptr;

        MyGUI::EditBox* mOverviewHint = nullptr;
        MyGUI::EditBox* mLogicHint = nullptr;
        MyGUI::EditBox* mHelpText = nullptr;
        MyGUI::EditBox* mValidationText = nullptr;
        MyGUI::Button* mValidateButton = nullptr;
        MyGUI::Button* mPublishButton = nullptr;
        MyGUI::Button* mDisableButton = nullptr;
        MyGUI::Button* mRefreshButton = nullptr;
        MyGUI::TextBox* mNotice = nullptr;
        MyGUI::Button* mCloseButton = nullptr;

        std::vector<std::string> mVisibleQuestIds;
        std::string mSelectedQuestId;
        std::string mSelectedTopicId;
        int mSelectedStageIndex = 0;
        std::string mSelectedOfferChoiceId;
        std::string mSelectedStageChoiceId;
        int mSelectedRequirementIndex = -1;
        int mSelectedRewardIndex = -1;
        int mSelectedNextStage = 0;
        bool mStageInitial = false;
        bool mStageComplete = false;
        bool mStageFail = false;
        bool mRefreshing = false;

        std::vector<std::pair<MyGUI::Widget*, MyGUI::IntCoord>> mBaseGeometry;
        MyGUI::IntSize mBaseSize;
        float mAppliedScale = 1.0f;
    };
}

#endif
