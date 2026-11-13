#include "QuestWizardWindow.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <locale>
#include <map>
#include <sstream>

#include <MyGUI_FontManager.h>
#include <MyGUI_LanguageManager.h>
#include <MyGUI_RenderManager.h>

#include "apps/openmw/mwbase/environment.hpp"
#include "apps/openmw/mwbase/windowmanager.hpp"
#include "apps/openmw/mwbase/world.hpp"
#include "apps/openmw/mwmechanics/actorutil.hpp"
#include "apps/openmw/mwworld/class.hpp"
#include "apps/openmw/mwworld/containerstore.hpp"
#include "apps/openmw/mwworld/esmstore.hpp"
#include "apps/openmw/mwworld/manualref.hpp"

#include "../GUIController.hpp"
#include "../LocalPlayer.hpp"
#include "../Main.hpp"
#include "../Networking.hpp"
#include "../ServerQuestRegistry.hpp"

namespace
{
    // Same order as the server (questWizard.lua SCALAR_ORDER).
    const char* const ScalarKeys[] = { "type", "name", "topic", "giverRefId", "giverCell", "giverUnique",
        "takeItems", "killRefId", "killCount", "visitCell", "visitX", "visitY", "visitZ", "visitRadius",
        "visitName", "talkQuestion", "offer", "accept", "decline", "waitText", "returnText", "turnIn", "done",
        "journalStart", "journalMid", "journalDone", "journalFail", "gold", "xp" };

    const char* const TypeIds[] = { "fetch", "kill", "visit", "talk" };
    const char* const TypeNames[] = { "Fetch", "Kill", "Visit", "Talk" };
    const char* const TextKeys[] = { "offer", "accept", "decline", "waitText", "returnText", "turnIn", "done" };
    const char* const TextNames[] = { "Offer", "Accept", "Decline", "Wait", "Return", "TurnIn", "Done" };
    const char* const JournalKeys[] = { "journalStart", "journalMid", "journalDone", "journalFail" };
    const char* const JournalNames[] = { "Start", "Mid", "Done", "Fail" };
    // Game units: about 70 per metre. 0 means "anywhere in that location".
    const int RadiusValues[] = { 0, 350, 1050, 3500 };
    const std::size_t MaxItems = 8;

    std::string lowerAscii(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }

    std::string trimmed(const std::string& value)
    {
        std::size_t begin = 0;
        std::size_t end = value.size();
        while (begin < end && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
        while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
        return value.substr(begin, end - begin);
    }

    std::string percentEncode(const std::string& value)
    {
        static const char* hex = "0123456789ABCDEF";
        std::string out;
        out.reserve(value.size());
        for (unsigned char c : value)
        {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == ' ')
                out.push_back(static_cast<char>(c));
            else
            {
                out.push_back('%');
                out.push_back(hex[(c >> 4) & 0x0f]);
                out.push_back(hex[c & 0x0f]);
            }
        }
        return out;
    }

    /// '#' starts a colour code in MyGUI captions; display-only text is escaped.
    std::string escapeTags(const std::string& value)
    {
        std::string out;
        out.reserve(value.size());
        for (char c : value)
        {
            out.push_back(c);
            if (c == '#') out.push_back('#');
        }
        return out;
    }

    int toInt(const std::string& value, int fallback)
    {
        if (value.empty()) return fallback;
        char* end = nullptr;
        const long result = std::strtol(value.c_str(), &end, 10);
        return end != value.c_str() ? static_cast<int>(result) : fallback;
    }

    /// Coordinates go to Lua tonumber(), which always wants a '.' separator.
    std::string formatFloat(float value)
    {
        std::ostringstream stream;
        stream.imbue(std::locale::classic());
        stream.setf(std::ios::fixed);
        stream.precision(1);
        stream << value;
        return stream.str();
    }

    std::string replaceAll(std::string text, const std::string& from, const std::string& to)
    {
        std::size_t position = 0;
        while ((position = text.find(from, position)) != std::string::npos)
        {
            text.replace(position, from.size(), to);
            position += to.size();
        }
        return text;
    }

    /// In-game name of a record, or the refId when it has none / is unknown.
    std::string objectName(const std::string& refId)
    {
        if (refId.empty())
            return std::string();
        try
        {
            MWWorld::ManualRef ref(MWBase::Environment::get().getWorld()->getStore(), refId);
            const MWWorld::Ptr ptr = ref.getPtr();
            const std::string name = ptr.getClass().getName(ptr);
            if (!name.empty())
                return name;
        }
        catch (const std::exception&)
        {
        }
        return refId;
    }

    std::string itemLine(const std::string& name, const std::string& refId, int count)
    {
        std::string line = std::to_string(count) + " x " + name;
        if (lowerAscii(name) != lowerAscii(refId))
            line += "  (" + refId + ")";
        return escapeTags(line);
    }
}

namespace mwmp
{
    QuestWizardWindow::QuestWizardWindow()
        : WindowModal("arenamp_questwizard.layout")
    {
        getWidget(mTitle, "WzTitle");
        getWidget(mStep, "WzStep");
        getWidget(mNotice, "WzNotice");
        getWidget(mHomeButton, "WzHome");
        getWidget(mBackButton, "WzBack");
        getWidget(mNextButton, "WzNext");
        getWidget(mCloseButton, "WzClose");

        static const char* pageNames[] = { "PageHome", "PageType", "PageGiver", "PageTask", "PageTexts",
            "PageJournal", "PageReward", "PageSummary", "PageInventory" };
        for (int i = 0; i <= Page_Inventory; ++i)
            getWidget(mPages[i], pageNames[i]);

        getWidget(mHomeList, "HomeList");
        getWidget(mHomeInfo, "HomeInfo");
        getWidget(mHomeNew, "HomeNew");
        getWidget(mHomeEdit, "HomeEdit");
        getWidget(mHomePublish, "HomePublish");
        getWidget(mHomeDisable, "HomeDisable");
        getWidget(mHomeStudio, "HomeStudio");

        getWidget(mName, "WzName");
        for (int i = 0; i < 4; ++i)
        {
            getWidget(mTypeButtons[i], std::string("Type") + TypeNames[i]);
            getWidget(mTaskPanels[i], std::string("Task") + TypeNames[i]);
            mTypeButtons[i]->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestWizardWindow::onType);
        }

        getWidget(mGiverInfo, "GiverInfo");
        getWidget(mGiverRefId, "GiverRefId");
        getWidget(mGiverCell, "GiverCell");
        getWidget(mTopic, "WzTopic");

        getWidget(mFetchList, "FetchList");
        getWidget(mFetchCount, "FetchCount");
        getWidget(mFetchMinus, "FetchMinus");
        getWidget(mFetchPlus, "FetchPlus");
        getWidget(mFetchTake, "FetchTake");
        getWidget(mKillInfo, "KillInfo");
        getWidget(mKillRefId, "KillRefId");
        getWidget(mKillCount, "KillCount");
        getWidget(mKillMinus, "KillMinus");
        getWidget(mKillPlus, "KillPlus");
        getWidget(mVisitInfo, "VisitInfo");
        getWidget(mVisitName, "VisitName");
        for (int i = 0; i < 4; ++i)
        {
            getWidget(mRadiusButtons[i], "VisitR" + std::to_string(i));
            mRadiusButtons[i]->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestWizardWindow::onRadius);
        }
        getWidget(mTalkQuestion, "TalkQuestion");
        for (int i = 0; i < 3; ++i)
        {
            const std::string n = std::to_string(i + 1);
            getWidget(mAnswerText[i], "AnswerText" + n);
            getWidget(mAnswerReply[i], "AnswerReply" + n);
            getWidget(mAnswerOk[i], "AnswerOk" + n);
            mAnswerOk[i]->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestWizardWindow::onAnswerOk);
        }

        for (int i = 0; i < 7; ++i)
        {
            getWidget(mTextEdits[i], std::string("T") + TextNames[i]);
            getWidget(mTextLabels[i], std::string("LblT") + TextNames[i]);
        }
        for (int i = 0; i < 4; ++i)
        {
            getWidget(mJournalEdits[i], std::string("J") + JournalNames[i]);
            getWidget(mJournalLabels[i], std::string("LblJ") + JournalNames[i]);
        }

        getWidget(mGold, "Gold");
        getWidget(mXp, "Xp");
        getWidget(mMoneyButtons[0], "GoldMinus");
        getWidget(mMoneyButtons[1], "GoldPlus");
        getWidget(mMoneyButtons[2], "XpMinus");
        getWidget(mMoneyButtons[3], "XpPlus");
        for (MyGUI::Button* button : mMoneyButtons)
            button->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestWizardWindow::onMoney);
        getWidget(mRewardList, "RewardList");
        getWidget(mRewardCount, "RewardCount");
        getWidget(mRewardMinus, "RewardMinus");
        getWidget(mRewardPlus, "RewardPlus");

        getWidget(mSummary, "SummaryText");
        getWidget(mSaveDraft, "SaveDraft");
        getWidget(mSavePublish, "SavePublish");

        getWidget(mInvSearch, "InvSearch");
        getWidget(mInvList, "InvList");
        getWidget(mInvInfo, "InvInfo");

        // ---- static captions
        auto caption = [this](const char* widget, const std::string& key) {
            MyGUI::Widget* target = nullptr;
            getWidget(target, widget);
            if (MyGUI::TextBox* text = target->castType<MyGUI::TextBox>(false))
                text->setCaption(tr(key));
        };
        mTitle->setCaption(tr("questwizard.title"));
        mHomeButton->setCaption(tr("questwizard.btn.home"));
        mBackButton->setCaption(tr("questwizard.btn.back"));
        mCloseButton->setCaption(tr("questwizard.btn.close"));

        caption("LblHome", "questwizard.home.list");
        caption("HomeNew", "questwizard.home.new");
        caption("HomeEdit", "questwizard.home.edit");
        caption("HomePublish", "questwizard.home.publish");
        caption("HomeDisable", "questwizard.home.disable");
        caption("HomeStudio", "questwizard.home.studio");
        caption("HomeHint", "questwizard.home.hint");

        caption("LblName", "questwizard.type.name");
        for (int i = 0; i < 4; ++i)
            caption((std::string("Type") + TypeNames[i] + "Desc").c_str(),
                std::string("questwizard.type.") + TypeIds[i] + ".desc");
        caption("TypeHint", "questwizard.type.hint");

        caption("GiverHint", "questwizard.giver.hint");
        caption("GiverPick", "questwizard.giver.pick");
        caption("LblGiverRef", "questwizard.lbl.refid");
        caption("LblGiverCell", "questwizard.lbl.cell");
        caption("LblTopic", "questwizard.giver.topic");
        caption("TopicHint", "questwizard.giver.topic_hint");

        caption("FetchHint", "questwizard.fetch.hint");
        caption("FetchFromInv", "questwizard.btn.from_inventory");
        caption("FetchPickWorld", "questwizard.btn.pick_item");
        caption("FetchRemove", "questwizard.btn.remove");
        caption("LblFetchCount", "questwizard.fetch.count");
        caption("FetchTakeHint", "questwizard.fetch.take_hint");
        caption("KillHint", "questwizard.kill.hint");
        caption("KillPick", "questwizard.kill.pick");
        caption("LblKillRef", "questwizard.lbl.refid");
        caption("LblKillCount", "questwizard.kill.count");
        caption("KillHint2", "questwizard.kill.hint2");
        caption("VisitHint", "questwizard.visit.hint");
        caption("VisitHere", "questwizard.visit.here");
        caption("LblRadius", "questwizard.visit.radius");
        for (int i = 0; i < 4; ++i)
            caption(("VisitR" + std::to_string(i)).c_str(), "questwizard.visit.radius" + std::to_string(i));
        caption("LblVisitName", "questwizard.visit.name");
        caption("VisitHint2", "questwizard.visit.hint2");
        caption("LblQuestion", "questwizard.talk.question");
        for (int i = 1; i <= 3; ++i)
            caption(("LblAnswer" + std::to_string(i)).c_str(), "questwizard.talk.answer" + std::to_string(i));

        for (int i = 0; i < 7; ++i)
            caption((std::string("LblT") + TextNames[i]).c_str(), std::string("questwizard.text.") + TextKeys[i]);
        caption("TextsHint", "questwizard.text.hint");
        caption("JournalHint", "questwizard.journal.hint");
        for (int i = 0; i < 4; ++i)
            caption((std::string("LblJ") + JournalNames[i]).c_str(), std::string("questwizard.journal.") + JournalKeys[i]);

        caption("RewardHint", "questwizard.reward.hint");
        caption("LblGold", "questwizard.reward.gold");
        caption("LblXp", "questwizard.reward.xp");
        caption("GoldMinus", "questwizard.btn.minus100");
        caption("GoldPlus", "questwizard.btn.plus100");
        caption("XpMinus", "questwizard.btn.minus10");
        caption("XpPlus", "questwizard.btn.plus10");
        caption("LblRewardItems", "questwizard.reward.items");
        caption("RewardFromInv", "questwizard.btn.from_inventory");
        caption("RewardPickWorld", "questwizard.btn.pick_item");
        caption("RewardRemove", "questwizard.btn.remove");
        caption("RewardHint2", "questwizard.reward.hint2");
        for (const char* name : { "FetchMinus", "KillMinus", "RewardMinus" }) caption(name, "questwizard.btn.minus");
        for (const char* name : { "FetchPlus", "KillPlus", "RewardPlus" }) caption(name, "questwizard.btn.plus");

        caption("SaveDraft", "questwizard.summary.save_draft");
        caption("SavePublish", "questwizard.summary.save_publish");
        caption("SummaryStudio", "questwizard.home.studio");
        caption("SummaryHint", "questwizard.summary.hint");

        caption("InvHint", "questwizard.inv.hint");
        caption("InvAdd", "questwizard.inv.add");
        caption("InvCancel", "questwizard.btn.back");

        for (MyGUI::EditBox* box : { mHomeInfo, mSummary, mTalkQuestion })
        {
            box->setEditMultiLine(true);
            box->setEditWordWrap(true);
        }
        for (int i = 0; i < 3; ++i)
        {
            mAnswerReply[i]->setEditMultiLine(true);
            mAnswerReply[i]->setEditWordWrap(true);
        }
        for (int i : { 0, 3, 4, 6 })
        {
            mTextEdits[i]->setEditMultiLine(true);
            mTextEdits[i]->setEditWordWrap(true);
        }
        for (MyGUI::EditBox* box : mJournalEdits)
        {
            box->setEditMultiLine(true);
            box->setEditWordWrap(true);
        }
        for (MyGUI::EditBox* box : { mHomeInfo, mSummary })
        {
            box->setEditStatic(true);
            box->setEditReadOnly(true);
        }
        // Long texts are capped by the server at 2000 characters.
        for (MyGUI::EditBox* box : mTextEdits) box->setMaxTextLength(2000);
        for (MyGUI::EditBox* box : mJournalEdits) box->setMaxTextLength(2000);
        mTalkQuestion->setMaxTextLength(2000);

        // ---- events
        mCloseButton->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestWizardWindow::onClose);
        mHomeButton->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestWizardWindow::onHome);
        mBackButton->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestWizardWindow::onBack);
        mNextButton->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestWizardWindow::onNext);
        mHomeList->eventListChangePosition += MyGUI::newDelegate(this, &QuestWizardWindow::onHomeSelected);
        mHomeList->eventListSelectAccept += MyGUI::newDelegate(this, &QuestWizardWindow::onHomeSelected);
        mHomeNew->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestWizardWindow::onHomeNew);
        mHomeEdit->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestWizardWindow::onHomeEdit);
        mHomePublish->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestWizardWindow::onHomePublish);
        mHomeDisable->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestWizardWindow::onHomeDisable);
        mHomeStudio->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestWizardWindow::onOpenStudio);

        MyGUI::Widget* widget = nullptr;
        getWidget(widget, "GiverPick");
        widget->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestWizardWindow::onPickGiver);
        getWidget(widget, "FetchFromInv");
        widget->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestWizardWindow::onFetchFromInv);
        getWidget(widget, "FetchPickWorld");
        widget->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestWizardWindow::onFetchPickWorld);
        getWidget(widget, "FetchRemove");
        widget->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestWizardWindow::onFetchRemove);
        mFetchList->eventListChangePosition += MyGUI::newDelegate(this, &QuestWizardWindow::onFetchSelected);
        mFetchMinus->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestWizardWindow::onFetchCount);
        mFetchPlus->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestWizardWindow::onFetchCount);
        mFetchTake->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestWizardWindow::onFetchTake);
        getWidget(widget, "KillPick");
        widget->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestWizardWindow::onPickKill);
        mKillMinus->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestWizardWindow::onKillCount);
        mKillPlus->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestWizardWindow::onKillCount);
        getWidget(widget, "VisitHere");
        widget->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestWizardWindow::onVisitHere);
        getWidget(widget, "RewardFromInv");
        widget->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestWizardWindow::onRewardFromInv);
        getWidget(widget, "RewardPickWorld");
        widget->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestWizardWindow::onRewardPickWorld);
        getWidget(widget, "RewardRemove");
        widget->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestWizardWindow::onRewardRemove);
        mRewardList->eventListChangePosition += MyGUI::newDelegate(this, &QuestWizardWindow::onRewardSelected);
        mRewardMinus->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestWizardWindow::onRewardCount);
        mRewardPlus->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestWizardWindow::onRewardCount);
        mSaveDraft->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestWizardWindow::onSave);
        mSavePublish->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestWizardWindow::onSave);
        getWidget(widget, "SummaryStudio");
        widget->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestWizardWindow::onOpenStudio);
        mInvSearch->eventEditTextChange += MyGUI::newDelegate(this, &QuestWizardWindow::onInvSearch);
        mInvList->eventListSelectAccept += MyGUI::newDelegate(this, &QuestWizardWindow::onInvAccept);
        getWidget(widget, "InvAdd");
        widget->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestWizardWindow::onInvAdd);
        getWidget(widget, "InvCancel");
        widget->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestWizardWindow::onInvCancel);

        // Results that arrived before this window existed are not ours.
        mLastResultSerial = ServerQuestRegistry::get().getWizardResult().serial;

        mBaseSize = mMainWidget->getSize();
        captureBaseGeometry(mMainWidget);
        applyGameFont(mMainWidget);
        fitToViewport();
        center();
        showPage(Page_Home);
    }

    // ------------------------------------------------------------------ helpers

    std::string QuestWizardWindow::tr(const std::string& key)
    {
        return MyGUI::LanguageManager::getInstance().replaceTags("#{arenamp=" + key + "}");
    }

    void QuestWizardWindow::applyGameFont(MyGUI::Widget* widget)
    {
        if (widget == nullptr)
            return;
        // Same reason as the advanced studio: MyGUI core skins keep a Latin-only font.
        static const std::string fontName = "Default";
        if (MyGUI::FontManager::getInstance().getByName(fontName) != nullptr)
        {
            if (MyGUI::EditBox* edit = widget->castType<MyGUI::EditBox>(false))
                edit->setFontName(fontName);
            else if (MyGUI::TextBox* text = widget->castType<MyGUI::TextBox>(false))
                text->setFontName(fontName);
        }
        MyGUI::EnumeratorWidgetPtr children = widget->getEnumerator();
        while (children.next())
            applyGameFont(children.current());
    }

    void QuestWizardWindow::captureBaseGeometry(MyGUI::Widget* widget)
    {
        if (widget == nullptr)
            return;
        mBaseGeometry.emplace_back(widget, widget->getCoord());
        MyGUI::EnumeratorWidgetPtr children = widget->getEnumerator();
        while (children.next())
            captureBaseGeometry(children.current());
    }

    void QuestWizardWindow::fitToViewport()
    {
        if (mMainWidget == nullptr || mBaseSize.width <= 0 || mBaseSize.height <= 0)
            return;
        const MyGUI::IntSize view = MyGUI::RenderManager::getInstance().getViewSize();
        if (view.width <= 0 || view.height <= 0)
            return;

        // Shrink to fit a small (phone) screen; on a large screen grow up to 20%
        // while keeping 8% of the screen free around the window. Fonts keep their
        // size, so a larger window simply gives the fields and buttons more room.
        const float margin = 0.98f;
        float scale = std::min(view.width * margin / static_cast<float>(mBaseSize.width),
            view.height * margin / static_cast<float>(mBaseSize.height));
        if (scale > 1.0f)
            scale = std::max(1.0f, std::min(1.2f, std::min(view.width * 0.92f / static_cast<float>(mBaseSize.width),
                view.height * 0.92f / static_cast<float>(mBaseSize.height))));
        scale = std::max(0.55f, scale);
        if (std::abs(scale - mAppliedScale) < 0.001f)
            return;
        mAppliedScale = scale;

        for (const std::pair<MyGUI::Widget*, MyGUI::IntCoord>& entry : mBaseGeometry)
        {
            if (entry.first == nullptr)
                continue;
            const MyGUI::IntCoord& base = entry.second;
            entry.first->setCoord(static_cast<int>(base.left * scale + 0.5f), static_cast<int>(base.top * scale + 0.5f),
                static_cast<int>(base.width * scale + 0.5f), static_cast<int>(base.height * scale + 0.5f));
        }
        mMainWidget->setSize(static_cast<int>(mBaseSize.width * scale + 0.5f),
            static_cast<int>(mBaseSize.height * scale + 0.5f));
    }

    void QuestWizardWindow::sendCommand(const std::vector<std::string>& fields)
    {
        LocalPlayer* localPlayer = Main::get().getLocalPlayer();
        if (localPlayer == nullptr)
            return;

        std::string data = "EDITOR_CMD";
        for (const std::string& field : fields)
        {
            data.push_back('\t');
            data += percentEncode(field);
        }

        localPlayer->guiMessageBox.id = ServerQuestRegistry::TransportGuiId;
        localPlayer->guiMessageBox.type = BasePlayer::GUIMessageBox::CustomMessageBox;
        localPlayer->guiMessageBox.label.clear();
        localPlayer->guiMessageBox.note.clear();
        localPlayer->guiMessageBox.buttons.clear();
        localPlayer->guiMessageBox.data = data;

        auto* playerPacket = Main::get().getNetworking()->getPlayerPacket(ID_GUI_MESSAGEBOX);
        playerPacket->setPlayer(localPlayer);
        playerPacket->Send();
    }

    void QuestWizardWindow::closeWindow()
    {
        setVisible(false);
        MWBase::Environment::get().getWindowManager()->removeGuiMode(
            static_cast<MWGui::GuiMode>(GUIController::GM_ARENAMP_QuestWizard));
    }

    void QuestWizardWindow::setNotice(const std::string& text)
    {
        mNotice->setCaption(escapeTags(text));
    }

    std::string QuestWizardWindow::value(const std::string& key) const
    {
        const auto found = mDraft.values.find(key);
        return found == mDraft.values.end() ? std::string() : found->second;
    }

    int QuestWizardWindow::intValue(const std::string& key, int fallback) const
    {
        return toInt(value(key), fallback);
    }

    void QuestWizardWindow::setValue(const std::string& key, const std::string& newValue)
    {
        mDraft.values[key] = newValue;
    }

    std::string QuestWizardWindow::questType() const
    {
        const std::string type = value("type");
        for (const char* id : TypeIds)
            if (type == id)
                return type;
        return "fetch";
    }

    // -------------------------------------------------------------------- draft

    void QuestWizardWindow::newDraft()
    {
        mDraft = Draft();
        setValue("type", "fetch");
        setValue("takeItems", "1");
        setValue("killCount", "1");
        setValue("visitRadius", "0");
        setValue("gold", "0");
        setValue("xp", "0");
        setValue("offer", tr("questwizard.default.offer"));
        setValue("accept", tr("questwizard.default.accept"));
        setValue("decline", tr("questwizard.default.decline"));
        setValue("waitText", tr("questwizard.default.wait"));
        setValue("returnText", tr("questwizard.default.return"));
        setValue("turnIn", tr("questwizard.default.turnin"));
        setValue("done", tr("questwizard.default.done"));
        setValue("journalStart", tr("questwizard.default.journal_start"));
        setValue("journalMid", tr("questwizard.default.journal_mid"));
        setValue("journalDone", tr("questwizard.default.journal_done"));
        setValue("journalFail", tr("questwizard.default.journal_fail"));
        setValue("talkQuestion", tr("questwizard.default.question"));
        mDraft.answers[0].success = true;
        mHasDraft = true;
        mFetchSelected = mRewardSelected = 0;
        mServerErrors.clear();
    }

    bool QuestWizardWindow::loadDraft(const ServerQuestEditorQuest& quest)
    {
        if (!quest.hasWizard)
            return false;
        mDraft = Draft();
        mDraft.questId = quest.id;
        mDraft.detached = quest.wizardDetached;
        Item* lastFetch = nullptr;
        Item* lastReward = nullptr;
        int answerIndex = -1;
        for (const std::pair<std::string, std::string>& field : quest.wizardFields)
        {
            const std::string& key = field.first;
            const std::string& data = field.second;
            if (key == "fetchItem" && mDraft.fetchItems.size() < MaxItems)
            {
                mDraft.fetchItems.push_back({ data, objectName(data), 1 });
                lastFetch = &mDraft.fetchItems.back();
            }
            else if (key == "fetchCount" && lastFetch != nullptr)
                lastFetch->count = std::max(1, toInt(data, 1));
            else if (key == "rewardItem" && mDraft.rewardItems.size() < MaxItems)
            {
                mDraft.rewardItems.push_back({ data, objectName(data), 1 });
                lastReward = &mDraft.rewardItems.back();
            }
            else if (key == "rewardCount" && lastReward != nullptr)
                lastReward->count = std::max(1, toInt(data, 1));
            else if (key == "answer")
            {
                ++answerIndex;
                if (answerIndex < 3)
                    mDraft.answers[answerIndex].success = data == "1";
            }
            else if (key == "answerText" && answerIndex >= 0 && answerIndex < 3)
                mDraft.answers[answerIndex].text = data;
            else if (key == "answerReply" && answerIndex >= 0 && answerIndex < 3)
                mDraft.answers[answerIndex].reply = data;
            else
                mDraft.values[key] = data;
        }
        mHasDraft = true;
        mFetchSelected = mRewardSelected = 0;
        mServerErrors.clear();
        return true;
    }

    std::vector<std::string> QuestWizardWindow::serializeDraft() const
    {
        std::vector<std::string> out;
        auto put = [&out](const std::string& key, const std::string& data) {
            out.push_back(key);
            out.push_back(data);
        };
        for (const char* key : ScalarKeys)
            put(key, value(key));
        for (const Item& item : mDraft.fetchItems)
        {
            put("fetchItem", item.refId);
            put("fetchCount", std::to_string(item.count));
        }
        for (const Item& item : mDraft.rewardItems)
        {
            put("rewardItem", item.refId);
            put("rewardCount", std::to_string(item.count));
        }
        for (const Answer& answer : mDraft.answers)
        {
            if (trimmed(answer.text).empty())
                continue;
            put("answer", answer.success ? "1" : "0");
            put("answerText", answer.text);
            put("answerReply", answer.reply);
        }
        return out;
    }

    std::vector<std::string> QuestWizardWindow::draftProblems() const
    {
        // A quick local copy of the server checks so the summary page can point
        // at the missing piece before anything is sent. The server re-checks.
        std::vector<std::string> problems;
        const std::string type = questType();
        if (trimmed(value("name")).empty()) problems.push_back(tr("questwizard.problem.name"));
        if (trimmed(value("giverRefId")).empty()) problems.push_back(tr("questwizard.problem.giver"));
        if (trimmed(value("offer")).empty()) problems.push_back(tr("questwizard.problem.offer"));
        if (trimmed(value("accept")).empty() || trimmed(value("decline")).empty())
            problems.push_back(tr("questwizard.problem.buttons"));
        if (type == "fetch" && mDraft.fetchItems.empty()) problems.push_back(tr("questwizard.problem.fetch"));
        if (type == "kill" && trimmed(value("killRefId")).empty()) problems.push_back(tr("questwizard.problem.kill"));
        if (type == "visit" && trimmed(value("visitCell")).empty()) problems.push_back(tr("questwizard.problem.visit"));
        if (type == "talk")
        {
            if (trimmed(value("talkQuestion")).empty()) problems.push_back(tr("questwizard.problem.question"));
            int used = 0;
            for (const Answer& answer : mDraft.answers)
            {
                if (trimmed(answer.text).empty()) continue;
                ++used;
                if (trimmed(answer.reply).empty())
                {
                    problems.push_back(tr("questwizard.problem.reply"));
                    break;
                }
            }
            if (used == 0) problems.push_back(tr("questwizard.problem.answers"));
        }
        else
        {
            if (trimmed(value("returnText")).empty()) problems.push_back(tr("questwizard.problem.return"));
            if (trimmed(value("turnIn")).empty()) problems.push_back(tr("questwizard.problem.turnin"));
        }
        return problems;
    }

    std::string QuestWizardWindow::describeDraft() const
    {
        const std::string type = questType();
        std::ostringstream text;
        text << tr("questwizard.summary.name") << ": " << value("name") << "\n";
        text << tr("questwizard.summary.type") << ": " << tr("questwizard.type." + type) << "\n";
        const std::string giver = value("giverRefId");
        text << tr("questwizard.summary.giver") << ": "
             << (giver.empty() ? tr("questwizard.summary.none") : objectName(giver) + " (" + giver + ")");
        if (!value("giverCell").empty())
            text << ", " << value("giverCell");
        text << "\n" << tr("questwizard.summary.topic") << ": "
             << (value("topic").empty() ? value("name") : value("topic")) << "\n\n";

        text << tr("questwizard.summary.task") << ": ";
        if (type == "fetch")
        {
            text << tr("questwizard.summary.fetch") << "\n";
            for (const Item& item : mDraft.fetchItems)
                text << "  - " << item.count << " x " << item.name << "\n";
            text << "  " << (value("takeItems") != "0" ? tr("questwizard.fetch.take_yes")
                                                         : tr("questwizard.fetch.take_no")) << "\n";
        }
        else if (type == "kill")
            text << tr("questwizard.summary.kill") << " " << intValue("killCount", 1) << " x "
                 << objectName(value("killRefId")) << "\n";
        else if (type == "visit")
        {
            text << tr("questwizard.summary.visit") << " "
                 << (value("visitName").empty() ? value("visitCell") : value("visitName")) << "\n";
            const int radius = intValue("visitRadius", 0);
            for (int i = 0; i < 4; ++i)
                if (RadiusValues[i] == radius)
                    text << "  " << tr("questwizard.visit.radius" + std::to_string(i)) << "\n";
        }
        else
        {
            text << tr("questwizard.summary.talk") << "\n";
            for (const Answer& answer : mDraft.answers)
                if (!trimmed(answer.text).empty())
                    text << "  - " << answer.text << " -> "
                         << (answer.success ? tr("questwizard.talk.ok") : tr("questwizard.talk.wrong")) << "\n";
        }

        text << "\n" << tr("questwizard.summary.reward") << ": ";
        bool any = false;
        if (intValue("gold") > 0) { text << intValue("gold") << " " << tr("questwizard.summary.gold"); any = true; }
        if (intValue("xp") > 0) { text << (any ? ", " : "") << intValue("xp") << " " << tr("questwizard.summary.xp"); any = true; }
        for (const Item& item : mDraft.rewardItems)
        {
            text << (any ? ", " : "") << item.count << " x " << item.name;
            any = true;
        }
        if (!any) text << tr("questwizard.summary.none");
        text << "\n\n";

        const std::vector<std::string> problems = draftProblems();
        if (problems.empty())
            text << tr("questwizard.summary.ready") << "\n";
        else
        {
            text << tr("questwizard.summary.problems") << "\n";
            for (const std::string& problem : problems)
                text << "  - " << problem << "\n";
        }
        if (!mServerErrors.empty())
            text << "\n" << tr("questwizard.summary.server") << "\n" << mServerErrors << "\n";
        if (mDraft.detached)
            text << "\n" << tr("questwizard.notice.detached") << "\n";
        return text.str();
    }

    // -------------------------------------------------------------------- pages

    int QuestWizardWindow::stepNumber(Page page) const
    {
        switch (page)
        {
            case Page_Type: return 1;
            case Page_Giver: return 2;
            case Page_Task: return 3;
            case Page_Texts: return 4;
            case Page_Journal: return 5;
            case Page_Reward: return 6;
            case Page_Summary: return 7;
            default: return 0;
        }
    }

    void QuestWizardWindow::showPage(Page page)
    {
        mPage = page;
        for (int i = 0; i <= Page_Inventory; ++i)
            mPages[i]->setVisible(i == page);
        loadPage();
        updateBottomBar();
    }

    void QuestWizardWindow::updateBottomBar()
    {
        const int step = stepNumber(mPage);
        if (step > 0)
        {
            static const char* stepKeys[] = { "", "questwizard.step.type", "questwizard.step.giver",
                "questwizard.step.task", "questwizard.step.texts", "questwizard.step.journal",
                "questwizard.step.reward", "questwizard.step.summary" };
            std::string caption = tr("questwizard.step.format");
            caption = replaceAll(caption, "{n}", std::to_string(step));
            caption = replaceAll(caption, "{total}", "7");
            caption = replaceAll(caption, "{name}", tr(stepKeys[step]));
            mStep->setCaption(caption);
        }
        else if (mPage == Page_Inventory)
            mStep->setCaption(tr("questwizard.step.inventory"));
        else
            mStep->setCaption(tr("questwizard.step.home"));

        const bool inWizard = step > 0;
        mHomeButton->setVisible(mPage != Page_Home);
        mBackButton->setVisible(inWizard && step > 1);
        mNextButton->setVisible(inWizard && step < 7);
        mNextButton->setCaption(tr("questwizard.btn.next"));
    }

    void QuestWizardWindow::loadPage()
    {
        switch (mPage)
        {
            case Page_Home:
                rebuildHome();
                break;
            case Page_Type:
                mName->setOnlyText(value("name"));
                updateTypeButtons();
                break;
            case Page_Giver:
            {
                const std::string refId = value("giverRefId");
                std::string info = refId.empty() ? tr("questwizard.giver.none")
                    : tr("questwizard.giver.chosen") + " " + objectName(refId);
                if (!refId.empty())
                    info += value("giverCell").empty() ? " - " + tr("questwizard.giver.anywhere")
                                                       : " - " + value("giverCell");
                mGiverInfo->setCaption(escapeTags(info));
                mGiverRefId->setOnlyText(refId);
                mGiverCell->setOnlyText(value("giverCell"));
                mTopic->setOnlyText(value("topic"));
                break;
            }
            case Page_Task:
            {
                updateTaskPanels();
                rebuildItemList(mFetchList, mDraft.fetchItems, mFetchSelected);
                mFetchTake->setCaption(value("takeItems") != "0" ? tr("questwizard.fetch.take_yes")
                                                                 : tr("questwizard.fetch.take_no"));
                const std::string killRef = value("killRefId");
                mKillInfo->setCaption(escapeTags(killRef.empty() ? tr("questwizard.kill.none")
                    : tr("questwizard.kill.chosen") + " " + objectName(killRef)));
                mKillRefId->setOnlyText(killRef);
                const std::string cell = value("visitCell");
                mVisitInfo->setCaption(escapeTags(cell.empty() ? tr("questwizard.visit.none")
                    : tr("questwizard.visit.chosen") + " " + cell));
                mVisitName->setOnlyText(value("visitName"));
                updateRadiusButtons();
                mTalkQuestion->setOnlyText(value("talkQuestion"));
                for (int i = 0; i < 3; ++i)
                {
                    mAnswerText[i]->setOnlyText(mDraft.answers[i].text);
                    mAnswerReply[i]->setOnlyText(mDraft.answers[i].reply);
                    mAnswerOk[i]->setCaption(mDraft.answers[i].success ? tr("questwizard.talk.ok")
                                                                       : tr("questwizard.talk.wrong"));
                }
                updateCountLabels();
                break;
            }
            case Page_Texts:
                for (int i = 0; i < 7; ++i)
                    mTextEdits[i]->setOnlyText(value(TextKeys[i]));
                updateTextVisibility();
                break;
            case Page_Journal:
                for (int i = 0; i < 4; ++i)
                    mJournalEdits[i]->setOnlyText(value(JournalKeys[i]));
                updateTextVisibility();
                break;
            case Page_Reward:
                mGold->setOnlyText(std::to_string(std::max(0, intValue("gold"))));
                mXp->setOnlyText(std::to_string(std::max(0, intValue("xp"))));
                rebuildItemList(mRewardList, mDraft.rewardItems, mRewardSelected);
                updateCountLabels();
                break;
            case Page_Summary:
                mSummary->setOnlyText(describeDraft());
                mSavePublish->setEnabled(ServerQuestRegistry::get().getEditorModel().canPublish);
                break;
            case Page_Inventory:
                rebuildInventory();
                break;
        }
    }

    void QuestWizardWindow::collectPage()
    {
        switch (mPage)
        {
            case Page_Type:
                setValue("name", trimmed(mName->getOnlyText()));
                break;
            case Page_Giver:
                setValue("giverRefId", trimmed(mGiverRefId->getOnlyText()));
                setValue("giverCell", trimmed(mGiverCell->getOnlyText()));
                setValue("topic", trimmed(mTopic->getOnlyText()));
                break;
            case Page_Task:
            {
                const std::string killRef = trimmed(mKillRefId->getOnlyText());
                if (lowerAscii(killRef) != lowerAscii(value("killRefId")))
                    setValue("killRefId", killRef);
                setValue("visitName", trimmed(mVisitName->getOnlyText()));
                setValue("talkQuestion", mTalkQuestion->getOnlyText());
                for (int i = 0; i < 3; ++i)
                {
                    mDraft.answers[i].text = trimmed(mAnswerText[i]->getOnlyText());
                    mDraft.answers[i].reply = mAnswerReply[i]->getOnlyText();
                }
                break;
            }
            case Page_Texts:
                for (int i = 0; i < 7; ++i)
                    setValue(TextKeys[i], mTextEdits[i]->getOnlyText());
                break;
            case Page_Journal:
                for (int i = 0; i < 4; ++i)
                    setValue(JournalKeys[i], mJournalEdits[i]->getOnlyText());
                break;
            case Page_Reward:
                setValue("gold", std::to_string(std::max(0, toInt(mGold->getOnlyText(), 0))));
                setValue("xp", std::to_string(std::max(0, toInt(mXp->getOnlyText(), 0))));
                break;
            default:
                break;
        }
    }

    bool QuestWizardWindow::canLeaveForward(std::string& why) const
    {
        const std::string type = questType();
        switch (mPage)
        {
            case Page_Type:
                if (trimmed(value("name")).empty()) { why = tr("questwizard.problem.name"); return false; }
                return true;
            case Page_Giver:
                if (trimmed(value("giverRefId")).empty()) { why = tr("questwizard.problem.giver"); return false; }
                return true;
            case Page_Task:
                if (type == "fetch" && mDraft.fetchItems.empty()) { why = tr("questwizard.problem.fetch"); return false; }
                if (type == "kill" && trimmed(value("killRefId")).empty()) { why = tr("questwizard.problem.kill"); return false; }
                if (type == "visit" && trimmed(value("visitCell")).empty()) { why = tr("questwizard.problem.visit"); return false; }
                if (type == "talk")
                {
                    bool any = false;
                    for (const Answer& answer : mDraft.answers)
                        any = any || !answer.text.empty();
                    if (!any) { why = tr("questwizard.problem.answers"); return false; }
                }
                return true;
            default:
                return true;
        }
    }

    void QuestWizardWindow::rebuildHome()
    {
        const ServerQuestEditorModel& model = ServerQuestRegistry::get().getEditorModel();
        mHomeList->removeAllItems();
        mHomeIds.clear();
        std::size_t selected = MyGUI::ITEM_NONE;
        for (const ServerQuestEditorQuest& quest : model.quests)
        {
            const std::string status = quest.status == "published" ? tr("questeditor.status.published")
                : quest.status == "disabled" ? tr("questeditor.status.disabled") : tr("questeditor.status.draft");
            std::string line = status + " " + (quest.name.empty() ? quest.id : quest.name);
            if (!quest.hasWizard)
                line += "  " + tr("questwizard.home.mark_studio");
            mHomeList->addItem(escapeTags(line));
            if (quest.id == mHomeSelectedId)
                selected = mHomeIds.size();
            mHomeIds.push_back(quest.id);
        }
        if (selected == MyGUI::ITEM_NONE)
            mHomeSelectedId.clear();
        else
            mHomeList->setIndexSelected(selected);

        const ServerQuestEditorQuest* quest = nullptr;
        for (const ServerQuestEditorQuest& candidate : model.quests)
            if (candidate.id == mHomeSelectedId)
                quest = &candidate;

        std::string info;
        if (quest == nullptr)
            info = model.quests.empty() ? tr("questwizard.home.empty") : tr("questwizard.home.select");
        else
        {
            info = quest->name + "\n";
            info += quest->status == "published" ? tr("questwizard.home.is_published")
                : quest->status == "disabled" ? tr("questwizard.home.is_disabled") : tr("questwizard.home.is_draft");
            info += "\n";
            if (!quest->hasWizard)
                info += tr("questwizard.home.studio_only");
            else if (quest->wizardDetached)
                info += tr("questwizard.notice.detached");
            else
                info += tr("questwizard.home.wizard_made");
            if (!quest->giverRefId.empty())
                info += "\n" + tr("questwizard.summary.giver") + ": " + objectName(quest->giverRefId);
        }
        mHomeInfo->setOnlyText(info);

        mHomeEdit->setEnabled(quest != nullptr && quest->hasWizard);
        mHomePublish->setEnabled(quest != nullptr && model.canPublish && quest->status != "published");
        mHomeDisable->setEnabled(quest != nullptr && quest->status == "published");
    }

    void QuestWizardWindow::rebuildItemList(MyGUI::ListBox* list, const std::vector<Item>& items, std::size_t selected)
    {
        list->removeAllItems();
        for (const Item& item : items)
            list->addItem(itemLine(item.name, item.refId, item.count));
        if (!items.empty())
            list->setIndexSelected(std::min(selected, items.size() - 1));
    }

    void QuestWizardWindow::rebuildInventory()
    {
        mInventory.clear();
        try
        {
            MWWorld::Ptr player = MWMechanics::getPlayer();
            MWWorld::ContainerStore& store = player.getClass().getContainerStore(player);
            std::map<std::string, std::size_t> byId;
            for (MWWorld::ContainerStoreIterator it = store.begin(); it != store.end(); ++it)
            {
                const std::string refId = it->getCellRef().getRefId();
                const int count = it->getRefData().getCount();
                if (refId.empty() || count <= 0)
                    continue;
                const std::string key = lowerAscii(refId);
                const auto found = byId.find(key);
                if (found != byId.end())
                {
                    mInventory[found->second].count += count;
                    continue;
                }
                std::string name = it->getClass().getName(*it);
                if (name.empty()) name = refId;
                byId[key] = mInventory.size();
                mInventory.push_back({ refId, name, count });
            }
        }
        catch (const std::exception&)
        {
        }
        std::sort(mInventory.begin(), mInventory.end(), [](const Item& a, const Item& b) {
            return lowerAscii(a.name) < lowerAscii(b.name);
        });

        const std::string filter = lowerAscii(trimmed(mInvSearch->getOnlyText()));
        mInvList->removeAllItems();
        mInventoryVisible.clear();
        for (std::size_t i = 0; i < mInventory.size(); ++i)
        {
            const Item& item = mInventory[i];
            if (!filter.empty() && lowerAscii(item.name + " " + item.refId).find(filter) == std::string::npos)
                continue;
            mInvList->addItem(itemLine(item.name, item.refId, item.count));
            mInventoryVisible.push_back(i);
        }
        mInvInfo->setCaption(mInventory.empty() ? tr("questwizard.inv.empty") : tr("questwizard.inv.info"));
    }

    void QuestWizardWindow::updateTypeButtons()
    {
        const std::string type = questType();
        for (int i = 0; i < 4; ++i)
        {
            const bool active = type == TypeIds[i];
            const std::string name = tr(std::string("questwizard.type.") + TypeIds[i]);
            mTypeButtons[i]->setCaption(active ? "> " + name + " <" : name);
            mTypeButtons[i]->setStateSelected(active);
        }
    }

    void QuestWizardWindow::updateTaskPanels()
    {
        const std::string type = questType();
        for (int i = 0; i < 4; ++i)
            mTaskPanels[i]->setVisible(type == TypeIds[i]);
    }

    void QuestWizardWindow::updateRadiusButtons()
    {
        const int radius = intValue("visitRadius", 0);
        for (int i = 0; i < 4; ++i)
        {
            const bool active = RadiusValues[i] == radius;
            const std::string name = tr("questwizard.visit.radius" + std::to_string(i));
            mRadiusButtons[i]->setCaption(active ? "> " + name + " <" : name);
            mRadiusButtons[i]->setStateSelected(active);
        }
    }

    void QuestWizardWindow::updateCountLabels()
    {
        mFetchCount->setCaption(mDraft.fetchItems.empty() ? "-"
            : std::to_string(mDraft.fetchItems[std::min(mFetchSelected, mDraft.fetchItems.size() - 1)].count));
        mRewardCount->setCaption(mDraft.rewardItems.empty() ? "-"
            : std::to_string(mDraft.rewardItems[std::min(mRewardSelected, mDraft.rewardItems.size() - 1)].count));
        mKillCount->setCaption(std::to_string(std::max(1, intValue("killCount", 1))));
    }

    void QuestWizardWindow::updateTextVisibility()
    {
        // Show only the texts the chosen quest type really uses.
        const std::string type = questType();
        const bool talk = type == "talk";
        const bool waits = type == "kill" || type == "visit";
        const bool textVisible[7] = { true, true, true, waits, !talk, !talk, !talk };
        for (int i = 0; i < 7; ++i)
        {
            mTextEdits[i]->setVisible(textVisible[i]);
            mTextLabels[i]->setVisible(textVisible[i]);
        }
        const bool journalVisible[4] = { true, waits, true, talk };
        for (int i = 0; i < 4; ++i)
        {
            mJournalEdits[i]->setVisible(journalVisible[i]);
            mJournalLabels[i]->setVisible(journalVisible[i]);
        }
    }

    void QuestWizardWindow::startPick(const std::string& kind)
    {
        collectPage();
        mPendingPick = kind;
        mPickReturnPage = mPage;
        sendCommand({ "wizard_pick", kind });
        MWBase::Environment::get().getWindowManager()->messageBox(tr("questwizard.pick.prompt." + kind));
        closeWindow();
    }

    void QuestWizardWindow::openInventory(const std::string& target)
    {
        collectPage();
        mInventoryTarget = target;
        mPickReturnPage = mPage;
        mInvSearch->setOnlyText("");
        showPage(Page_Inventory);
    }

    // ----------------------------------------------------------- public entries

    void QuestWizardWindow::onOpen()
    {
        WindowModal::onOpen();
        fitToViewport();
        center();
        if (!mPendingPick.empty())
        {
            // The window was reopened (e.g. /quest) while a pick was still armed:
            // the moderator changed their mind, so stop capturing activations.
            sendCommand({ "wizard_pick_cancel" });
            mPendingPick.clear();
            setNotice(tr("questwizard.notice.pick_cancelled"));
        }
        // onOpen runs on every GUI mode refresh; sync results are applied by
        // GUIController::showQuestWizard, so only the home list is redrawn here.
        if (mPage == Page_Home)
            rebuildHome();
    }

    bool QuestWizardWindow::exit()
    {
        onClose(nullptr);
        return false;
    }

    void QuestWizardWindow::refreshFromRegistry()
    {
        const ServerQuestEditorModel& model = ServerQuestRegistry::get().getEditorModel();
        const ServerQuestWizardResult& result = ServerQuestRegistry::get().getWizardResult();
        if (result.serial != mLastResultSerial)
        {
            mLastResultSerial = result.serial;
            if (result.ok)
            {
                mDraft.questId = result.questId;
                mDraft.detached = false;
                mServerErrors.clear();
                mHomeSelectedId = result.questId;
                setNotice(result.message);
                showPage(Page_Home);
                return;
            }
            mServerErrors = result.message;
            setNotice(tr("questwizard.notice.not_saved"));
            if (mPage == Page_Summary)
                loadPage();
            return;
        }
        if (mPage == Page_Home)
        {
            if (!model.selectedQuestId.empty())
                mHomeSelectedId = model.selectedQuestId;
            rebuildHome();
            setNotice(model.notice);
        }
        else if (!model.notice.empty())
            setNotice(model.notice);
    }

    void QuestWizardWindow::applyPick()
    {
        const ServerQuestWizardPick& pick = ServerQuestRegistry::get().getWizardPick();
        if (pick.serial == mLastPickSerial)
            return;
        mLastPickSerial = pick.serial;
        if (!mHasDraft)
            newDraft();

        if (pick.kind == "visit")
        {
            collectPage();
            setValue("visitCell", pick.cell);
            setValue("visitX", formatFloat(pick.x));
            setValue("visitY", formatFloat(pick.y));
            setValue("visitZ", formatFloat(pick.z));
            setNotice(tr("questwizard.notice.place_marked") + " " + pick.cell);
            loadPage();
            return;
        }

        const std::string name = objectName(pick.refId);
        if (pick.kind == "giver")
        {
            setValue("giverRefId", pick.refId);
            setValue("giverCell", pick.cell);
            setValue("giverUnique", pick.uniqueIndex);
        }
        else if (pick.kind == "kill")
            setValue("killRefId", pick.refId);
        else if (pick.kind == "fetch" || pick.kind == "reward")
        {
            std::vector<Item>& items = pick.kind == "fetch" ? mDraft.fetchItems : mDraft.rewardItems;
            bool merged = false;
            for (std::size_t i = 0; i < items.size(); ++i)
                if (lowerAscii(items[i].refId) == lowerAscii(pick.refId))
                {
                    items[i].count += 1;
                    (pick.kind == "fetch" ? mFetchSelected : mRewardSelected) = i;
                    merged = true;
                }
            if (!merged && items.size() < MaxItems)
            {
                items.push_back({ pick.refId, name, 1 });
                (pick.kind == "fetch" ? mFetchSelected : mRewardSelected) = items.size() - 1;
            }
        }
        mPendingPick.clear();
        setNotice(tr("questwizard.notice.picked") + " " + name);
        showPage(mPickReturnPage);
    }

    // ------------------------------------------------------------------- events

    void QuestWizardWindow::onClose(MyGUI::Widget*)
    {
        collectPage();
        closeWindow();
    }

    void QuestWizardWindow::onHome(MyGUI::Widget*)
    {
        collectPage();
        setNotice(mHasDraft && mPage != Page_Inventory ? tr("questwizard.notice.draft_kept") : "");
        showPage(Page_Home);
    }

    void QuestWizardWindow::onBack(MyGUI::Widget*)
    {
        collectPage();
        const int step = stepNumber(mPage);
        if (step > 1)
            showPage(static_cast<Page>(mPage - 1));
    }

    void QuestWizardWindow::onNext(MyGUI::Widget*)
    {
        collectPage();
        std::string why;
        if (!canLeaveForward(why))
        {
            setNotice(why);
            return;
        }
        setNotice("");
        const int step = stepNumber(mPage);
        if (step > 0 && step < 7)
            showPage(static_cast<Page>(mPage + 1));
    }

    void QuestWizardWindow::onHomeSelected(MyGUI::ListBox*, std::size_t index)
    {
        if (index == MyGUI::ITEM_NONE || index >= mHomeIds.size())
            return;
        mHomeSelectedId = mHomeIds[index];
        rebuildHome();
    }

    void QuestWizardWindow::onHomeNew(MyGUI::Widget*)
    {
        newDraft();
        setNotice(tr("questwizard.notice.new"));
        showPage(Page_Type);
    }

    void QuestWizardWindow::onHomeEdit(MyGUI::Widget*)
    {
        for (const ServerQuestEditorQuest& quest : ServerQuestRegistry::get().getEditorModel().quests)
        {
            if (quest.id != mHomeSelectedId)
                continue;
            if (!loadDraft(quest))
            {
                setNotice(tr("questwizard.home.studio_only"));
                return;
            }
            setNotice(quest.wizardDetached ? tr("questwizard.notice.detached") : "");
            showPage(Page_Type);
            return;
        }
    }

    void QuestWizardWindow::onHomePublish(MyGUI::Widget*)
    {
        if (!mHomeSelectedId.empty())
            sendCommand({ "wizard_publish", mHomeSelectedId });
    }

    void QuestWizardWindow::onHomeDisable(MyGUI::Widget*)
    {
        if (!mHomeSelectedId.empty())
            sendCommand({ "wizard_disable", mHomeSelectedId });
    }

    void QuestWizardWindow::onOpenStudio(MyGUI::Widget*)
    {
        collectPage();
        const std::string id = mPage == Page_Home ? mHomeSelectedId : mDraft.questId;
        closeWindow();
        // The server switches this moderator to the advanced view and resyncs,
        // which opens the advanced studio on the same quest.
        sendCommand({ "wizard_open_studio", id });
    }

    void QuestWizardWindow::onType(MyGUI::Widget* sender)
    {
        collectPage();
        for (int i = 0; i < 4; ++i)
            if (sender == mTypeButtons[i])
                setValue("type", TypeIds[i]);
        updateTypeButtons();
    }

    void QuestWizardWindow::onPickGiver(MyGUI::Widget*) { startPick("giver"); }
    void QuestWizardWindow::onFetchFromInv(MyGUI::Widget*) { openInventory("fetch"); }
    void QuestWizardWindow::onFetchPickWorld(MyGUI::Widget*) { startPick("fetch"); }
    void QuestWizardWindow::onPickKill(MyGUI::Widget*) { startPick("kill"); }
    void QuestWizardWindow::onRewardFromInv(MyGUI::Widget*) { openInventory("reward"); }
    void QuestWizardWindow::onRewardPickWorld(MyGUI::Widget*) { startPick("reward"); }

    void QuestWizardWindow::onFetchRemove(MyGUI::Widget*)
    {
        collectPage();
        if (mFetchSelected < mDraft.fetchItems.size())
            mDraft.fetchItems.erase(mDraft.fetchItems.begin() + mFetchSelected);
        if (mFetchSelected > 0 && mFetchSelected >= mDraft.fetchItems.size())
            --mFetchSelected;
        loadPage();
    }

    void QuestWizardWindow::onFetchSelected(MyGUI::ListBox*, std::size_t index)
    {
        if (index == MyGUI::ITEM_NONE) return;
        mFetchSelected = index;
        updateCountLabels();
    }

    void QuestWizardWindow::onFetchCount(MyGUI::Widget* sender)
    {
        if (mFetchSelected >= mDraft.fetchItems.size()) return;
        int& count = mDraft.fetchItems[mFetchSelected].count;
        count = std::max(1, std::min(1000, count + (sender == mFetchPlus ? 1 : -1)));
        collectPage();
        loadPage();
    }

    void QuestWizardWindow::onFetchTake(MyGUI::Widget*)
    {
        setValue("takeItems", value("takeItems") != "0" ? "0" : "1");
        mFetchTake->setCaption(value("takeItems") != "0" ? tr("questwizard.fetch.take_yes")
                                                         : tr("questwizard.fetch.take_no"));
    }

    void QuestWizardWindow::onKillCount(MyGUI::Widget* sender)
    {
        const int count = std::max(1, std::min(1000, intValue("killCount", 1) + (sender == mKillPlus ? 1 : -1)));
        setValue("killCount", std::to_string(count));
        updateCountLabels();
    }

    void QuestWizardWindow::onVisitHere(MyGUI::Widget*)
    {
        collectPage();
        sendCommand({ "wizard_here" });
    }

    void QuestWizardWindow::onRadius(MyGUI::Widget* sender)
    {
        for (int i = 0; i < 4; ++i)
            if (sender == mRadiusButtons[i])
                setValue("visitRadius", std::to_string(RadiusValues[i]));
        updateRadiusButtons();
    }

    void QuestWizardWindow::onAnswerOk(MyGUI::Widget* sender)
    {
        for (int i = 0; i < 3; ++i)
            if (sender == mAnswerOk[i])
            {
                mDraft.answers[i].success = !mDraft.answers[i].success;
                mAnswerOk[i]->setCaption(mDraft.answers[i].success ? tr("questwizard.talk.ok")
                                                                   : tr("questwizard.talk.wrong"));
            }
    }

    void QuestWizardWindow::onMoney(MyGUI::Widget* sender)
    {
        collectPage();
        if (sender == mMoneyButtons[0] || sender == mMoneyButtons[1])
            setValue("gold", std::to_string(std::max(0, intValue("gold") + (sender == mMoneyButtons[1] ? 100 : -100))));
        else
            setValue("xp", std::to_string(std::max(0, intValue("xp") + (sender == mMoneyButtons[3] ? 10 : -10))));
        mGold->setOnlyText(value("gold"));
        mXp->setOnlyText(value("xp"));
    }

    void QuestWizardWindow::onRewardRemove(MyGUI::Widget*)
    {
        collectPage();
        if (mRewardSelected < mDraft.rewardItems.size())
            mDraft.rewardItems.erase(mDraft.rewardItems.begin() + mRewardSelected);
        if (mRewardSelected > 0 && mRewardSelected >= mDraft.rewardItems.size())
            --mRewardSelected;
        loadPage();
    }

    void QuestWizardWindow::onRewardSelected(MyGUI::ListBox*, std::size_t index)
    {
        if (index == MyGUI::ITEM_NONE) return;
        mRewardSelected = index;
        updateCountLabels();
    }

    void QuestWizardWindow::onRewardCount(MyGUI::Widget* sender)
    {
        if (mRewardSelected >= mDraft.rewardItems.size()) return;
        int& count = mDraft.rewardItems[mRewardSelected].count;
        count = std::max(1, std::min(1000, count + (sender == mRewardPlus ? 1 : -1)));
        collectPage();
        loadPage();
    }

    void QuestWizardWindow::onSave(MyGUI::Widget* sender)
    {
        collectPage();
        const std::vector<std::string> problems = draftProblems();
        if (!problems.empty())
        {
            setNotice(problems.front());
            loadPage();
            return;
        }
        std::vector<std::string> fields = { "wizard_save", mDraft.questId, sender == mSavePublish ? "1" : "0" };
        const std::vector<std::string> data = serializeDraft();
        fields.insert(fields.end(), data.begin(), data.end());
        setNotice(tr("questwizard.notice.saving"));
        sendCommand(fields);
    }

    void QuestWizardWindow::onInvSearch(MyGUI::EditBox*)
    {
        rebuildInventory();
    }

    void QuestWizardWindow::onInvAccept(MyGUI::ListBox*, std::size_t)
    {
        onInvAdd(nullptr);
    }

    void QuestWizardWindow::onInvAdd(MyGUI::Widget*)
    {
        const std::size_t index = mInvList->getIndexSelected();
        if (index == MyGUI::ITEM_NONE || index >= mInventoryVisible.size())
        {
            setNotice(tr("questwizard.inv.select_first"));
            return;
        }
        const Item& source = mInventory[mInventoryVisible[index]];
        std::vector<Item>& items = mInventoryTarget == "reward" ? mDraft.rewardItems : mDraft.fetchItems;
        std::size_t& selected = mInventoryTarget == "reward" ? mRewardSelected : mFetchSelected;
        bool merged = false;
        for (std::size_t i = 0; i < items.size(); ++i)
            if (lowerAscii(items[i].refId) == lowerAscii(source.refId))
            {
                selected = i;
                merged = true;
            }
        if (!merged)
        {
            if (items.size() >= MaxItems)
            {
                setNotice(tr("questwizard.inv.full"));
                return;
            }
            items.push_back({ source.refId, source.name, 1 });
            selected = items.size() - 1;
        }
        setNotice(tr("questwizard.notice.picked") + " " + source.name);
        showPage(mPickReturnPage);
    }

    void QuestWizardWindow::onInvCancel(MyGUI::Widget*)
    {
        showPage(mPickReturnPage);
    }
}
