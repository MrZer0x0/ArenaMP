#include "questmanager.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <regex>
#include <sstream>
#include <utility>

#include <MyGUI_Button.h>
#include <MyGUI_ComboBox.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_ImageBox.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_LanguageManager.h>
#include <MyGUI_ListBox.h>
#include <MyGUI_TabControl.h>
#include <MyGUI_TabItem.h>
#include <MyGUI_TextBox.h>
#include <MyGUI_Window.h>
#include <MyGUI_RenderManager.h>

#include <components/debug/debuglog.hpp>
#include <components/esm/loaddial.hpp>
#include <components/esm/loadinfo.hpp>
#include <components/esm/loadfact.hpp>
#include <components/esm/loadnpc.hpp>
#include <components/misc/stringops.hpp>
#include <components/settings/settings.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/journal.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwdialogue/quest.hpp"
#include "../mwdialogue/topic.hpp"
#include "../mwdialogue/topicrecovery.hpp"
#include "../mwmechanics/npcstats.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwmp/ServerQuestRegistry.hpp"
#include "../mwmp/LocalPlayer.hpp"
#include "../mwmp/Main.hpp"
#include "../mwmp/Networking.hpp"
#include "../mwdialogue/journalimp.hpp"
#include "../mwmechanics/actorutil.hpp"

namespace
{
    std::string trim(const std::string& value)
    {
        std::size_t first = 0;
        while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])))
            ++first;
        std::size_t last = value.size();
        while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])))
            --last;
        return value.substr(first, last - first);
    }

    bool starts(const std::string& value, const std::string& prefix)
    {
        return value.compare(0, prefix.size(), prefix) == 0;
    }

    const long long sQuestOrderStride = 1000000000000LL;

    long long serverQuestOrder(const mwmp::ServerQuestJournalEntry& entry, long long nativeCount, long long fallbackSerial)
    {
        const long long fallbackAnchor = nativeCount > 0 ? nativeCount - 1 : 0;
        const long long anchor = entry.vanillaAnchor >= 0 ? entry.vanillaAnchor : fallbackAnchor;
        long long subOrder = entry.epoch > 0 ? entry.epoch : fallbackSerial;
        subOrder = std::max(1LL, std::min(sQuestOrderStride - 1, subOrder));
        return anchor * sQuestOrderStride + subOrder;
    }

    bool inSet(const std::string& value, std::initializer_list<const char*> values)
    {
        for (const char* candidate : values)
            if (value == candidate)
                return true;
        return false;
    }

    std::string questNameFromEsm(const std::string& id)
    {
        // TES3 has no separate quest-title field. The canonical localized
        // title is the Journal INFO marked QS_Name, so read that directly
        // from the loaded ESM store and never expose an editor id as a title.
        // This mirrors the original Questman: unnamed journals are Records.
        try
        {
            const MWWorld::ESMStore& store = MWBase::Environment::get().getWorld()->getStore();
            const ESM::Dialogue* dialogue = store.get<ESM::Dialogue>().search(id);
            if (!dialogue || dialogue->mType != ESM::Dialogue::Journal)
                return std::string();

            for (const ESM::DialInfo& info : dialogue->mInfo)
            {
                if (info.mQuestStatus != ESM::DialInfo::QS_Name)
                    continue;
                const std::string title = trim(info.mResponse);
                if (!title.empty())
                    return title;
            }
        }
        catch (...)
        {
        }
        return std::string();
    }

    std::string sanitizeJournalText(const std::string& value)
    {
        std::string out;
        out.reserve(value.size());
        for (char c : value)
            if (c != '@' && c != '#')
                out.push_back(c);
        return out;
    }

    bool titleLike(const std::string& value)
    {
        const std::string text = trim(sanitizeJournalText(value));
        if (text.empty() || text.size() > 72)
            return false;
        const char last = text.back();
        return last != '.' && last != '!' && last != '?' && last != ';' && last != ',';
    }

    std::string recordTitle(const std::string& id, const std::string& fallback)
    {
        try
        {
            const MWWorld::ESMStore& store = MWBase::Environment::get().getWorld()->getStore();
            const ESM::Dialogue* record = store.get<ESM::Dialogue>().search(id);
            if (record && record->mType == ESM::Dialogue::Journal)
            {
                const ESM::DialInfo* best = nullptr;
                for (const ESM::DialInfo& info : record->mInfo)
                    if ((!best || info.mData.mJournalIndex < best->mData.mJournalIndex)
                        && !info.mResponse.empty())
                        best = &info;
                if (best && titleLike(best->mResponse))
                    return trim(sanitizeJournalText(best->mResponse));
            }
        }
        catch (...) {}

        std::string text = trim(sanitizeJournalText(fallback));
        if (text.size() > 42)
            text = text.substr(0, 39) + "...";
        return text.empty() ? id : text;
    }

    std::string fallbackTopicEntryText(const std::string& topicId, const std::string& infoId)
    {
        return MWDialogue::TopicRecovery::responseFor(topicId, infoId);
    }
}

namespace MWGui
{
    QuestManagerWindow::QuestManagerWindow(std::function<void()> returnToJournal)
        : WindowModal("openmw_questmanager.layout")
        , mReturnToJournal(std::move(returnToJournal))
    {
        getWidget(mTitle, "QuestmanTitle");
        getWidget(mTabs, "QuestmanTabs");
        getWidget(mQuestSearch, "QuestSearch");
        getWidget(mQuestFilter, "QuestFilter");
        getWidget(mShowCompleted, "ShowCompleted");
        getWidget(mShowHidden, "ShowHidden");
        getWidget(mQuestList, "QuestList");
        getWidget(mQuestCounter, "QuestCounter");
        getWidget(mQuestIcon, "QuestIcon");
        getWidget(mQuestHeading, "QuestHeading");
        getWidget(mQuestDetail, "QuestDetail");
        getWidget(mPinButton, "PinButton");
        getWidget(mHideButton, "HideButton");
        getWidget(mQuestRelatedLabel, "QuestRelatedLabel");
        getWidget(mQuestRelatedList, "QuestRelatedList");
        getWidget(mTopicSearch, "TopicSearch");
        getWidget(mTopicList, "TopicList");
        getWidget(mTopicCounter, "TopicCounter");
        getWidget(mTopicHeading, "TopicHeading");
        getWidget(mTopicDetail, "TopicDetail");
        getWidget(mTopicRelatedLabel, "TopicRelatedLabel");
        getWidget(mTopicRelatedList, "TopicRelatedList");
        getWidget(mRecordSearch, "RecordSearch");
        getWidget(mRecordList, "RecordList");
        getWidget(mRecordCounter, "RecordCounter");
        getWidget(mRecordHeading, "RecordHeading");
        getWidget(mRecordDetail, "RecordDetail");
        getWidget(mStatsDetail, "StatsDetail");
        getWidget(mCloseButton, "CloseButton");
        getWidget(mSkipButton, "SkipQuestButton");
        getWidget(mResetButton, "ResetQuestButton");
        getWidget(mStatusText, "QuestmanStatus");
        getWidget(mServicesPoints, "ServicesPoints");
        getWidget(mServicesInfo, "ServicesInfo");
        getWidget(mClearJournalButton, "ClearJournalButton");
        getWidget(mFactionLabel, "FactionLabel");
        getWidget(mFactionList, "FactionList");
        getWidget(mLeaveFactionButton, "LeaveFactionButton");
        sInstance = this;

        mTitle->setCaption(tr("questman.title"));
        mTabs->getItemAt(0)->setCaption(tr("questman.tab.quests"));
        mTabs->getItemAt(1)->setCaption(tr("questman.tab.topics"));
        mTabs->getItemAt(2)->setCaption(tr("questman.tab.records"));
        mTabs->getItemAt(3)->setCaption(tr("questman.tab.stats"));
        mTabs->getItemAt(4)->setCaption(tr("questman.tab.services"));
        mFactionLabel->setCaption(tr("questman.svc.factions"));
        mServicesInfo->setEditStatic(true);
        mServicesInfo->setEditReadOnly(true);
        mServicesInfo->setEditMultiLine(true);
        mServicesInfo->setEditWordWrap(true);
        mStatusText->setCaption("");
        mQuestSearch->setCaption("");
        mTopicSearch->setCaption("");
        mRecordSearch->setCaption("");
        mShowCompleted->setCaption(tr("questman.show_completed"));
        mShowHidden->setCaption(tr("questman.show_hidden"));
        mQuestRelatedLabel->setCaption(tr("questman.related_topics"));
        mTopicRelatedLabel->setCaption(tr("questman.related_topics"));
        mCloseButton->setCaption(tr("questman.close"));

        mQuestDetail->setEditStatic(true);
        mQuestDetail->setEditReadOnly(true);
        mQuestDetail->setEditMultiLine(true);
        mQuestDetail->setEditWordWrap(true);
        mTopicDetail->setEditStatic(true);
        mTopicDetail->setEditReadOnly(true);
        mTopicDetail->setEditMultiLine(true);
        mTopicDetail->setEditWordWrap(true);
        mRecordDetail->setEditStatic(true);
        mRecordDetail->setEditReadOnly(true);
        mRecordDetail->setEditMultiLine(true);
        mRecordDetail->setEditWordWrap(true);
        mStatsDetail->setEditStatic(true);
        mStatsDetail->setEditReadOnly(true);
        mStatsDetail->setEditMultiLine(true);
        mStatsDetail->setEditWordWrap(true);

        mCloseButton->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestManagerWindow::notifyClose);
        mTabs->eventTabChangeSelect += MyGUI::newDelegate(this, &QuestManagerWindow::notifyTabChanged);
        mQuestSearch->eventEditTextChange += MyGUI::newDelegate(this, &QuestManagerWindow::notifySearchChanged);
        mTopicSearch->eventEditTextChange += MyGUI::newDelegate(this, &QuestManagerWindow::notifySearchChanged);
        mRecordSearch->eventEditTextChange += MyGUI::newDelegate(this, &QuestManagerWindow::notifySearchChanged);
        mQuestFilter->eventComboChangePosition += MyGUI::newDelegate(this, &QuestManagerWindow::notifyFilterChanged);
        mShowCompleted->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestManagerWindow::notifyShowCompleted);
        mShowHidden->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestManagerWindow::notifyShowHidden);
        mQuestList->eventListChangePosition += MyGUI::newDelegate(this, &QuestManagerWindow::notifyQuestSelected);
        mTopicList->eventListChangePosition += MyGUI::newDelegate(this, &QuestManagerWindow::notifyTopicSelected);
        mRecordList->eventListChangePosition += MyGUI::newDelegate(this, &QuestManagerWindow::notifyRecordSelected);
        mPinButton->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestManagerWindow::notifyPin);
        mHideButton->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestManagerWindow::notifyHide);
        mSkipButton->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestManagerWindow::notifySkipQuest);
        mResetButton->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestManagerWindow::notifyResetQuest);
        mClearJournalButton->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestManagerWindow::notifyClearJournal);
        mLeaveFactionButton->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestManagerWindow::notifyLeaveFaction);
        mFactionList->eventListChangePosition += MyGUI::newDelegate(this, &QuestManagerWindow::notifyFactionSelected);
        mQuestRelatedList->eventListChangePosition += MyGUI::newDelegate(this, &QuestManagerWindow::notifyQuestRelatedSelected);
        mTopicRelatedList->eventListChangePosition += MyGUI::newDelegate(this, &QuestManagerWindow::notifyTopicRelatedSelected);
        mMainWidget->castType<MyGUI::Window>()->eventWindowChangeCoord += MyGUI::newDelegate(this, &QuestManagerWindow::notifyWindowChanged);

        center();
    }

    void QuestManagerWindow::onOpen()
    {
        WindowModal::onOpen();
        restoreWindowGeometry();
        rebuildData();

        int tab = 0;
        try { tab = Settings::Manager::getInt("last tab", "Questman"); }
        catch (...) {}
        tab = std::max(0, std::min(tab, 4));
        mTabs->setIndexSelected(static_cast<std::size_t>(tab));
        applyResponsiveLayout();
        mArmedAction.clear();
        mStatusText->setCaption("");
        // QM1: prices come from the server; ask again on every open (cheap).
        sendServiceCommand({ "config" });
        refreshCurrentTab();
        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mCloseButton);
    }

    bool QuestManagerWindow::exit()
    {
        notifyClose(nullptr);
        return false;
    }

    void QuestManagerWindow::clear()
    {
        mQuests.clear();
        mTopics.clear();
        mRecords.clear();
        mQuestGivers.clear();
        mQuestGiverStages.clear();
        mVisibleQuests.clear();
        mVisibleTopics.clear();
        mVisibleRecords.clear();
        mQuestRelatedTopics.clear();
        mTopicRelatedTopics.clear();
    }

    void QuestManagerWindow::onResChange(int, int)
    {
        applyResponsiveLayout();
    }

    void QuestManagerWindow::restoreWindowGeometry()
    {
        const MyGUI::IntSize view = MyGUI::RenderManager::getInstance().getViewSize();
        const int w = 790;
        const int h = 500;
        int x = std::max(0, (view.width - w) / 2);
        int y = std::max(0, (view.height - h) / 2);

        int layoutVersion = 0;
        try { layoutVersion = Settings::Manager::getInt("layout version", "Questman"); }
        catch (...) {}

        if (layoutVersion < 2)
        {
            // Preserve the old combined visibility preference while migrating
            // to independent Completed/Hidden toggles.
            const bool legacyShow = getBool("show completed hidden", false);
            Settings::Manager::setBool("show completed", "Questman", legacyShow);
            Settings::Manager::setBool("show hidden", "Questman", legacyShow);
        }

        // Version 4 resets previously saved Questman size once and keeps
        // Questman at a fixed layout size so the lower content always fits.
        if (layoutVersion >= 5)
        {
            try
            {
                const float sx = Settings::Manager::getFloat("window x", "Questman");
                const float sy = Settings::Manager::getFloat("window y", "Questman");
                x = std::max(0, std::min(static_cast<int>(sx * view.width), view.width - w));
                y = std::max(0, std::min(static_cast<int>(sy * view.height), view.height - h));
            }
            catch (...) {}
        }

        mRestoringGeometry = true;
        mMainWidget->setCoord(MyGUI::IntCoord(x, y, w, h));
        mRestoringGeometry = false;
        if (layoutVersion < 5)
        {
            Settings::Manager::setFloat("window w", "Questman", -1.f);
            Settings::Manager::setFloat("window h", "Questman", -1.f);
            Settings::Manager::setInt("layout version", "Questman", 5);
        }
    }

    void QuestManagerWindow::saveWindowGeometry()
    {
        if (mRestoringGeometry)
            return;
        const MyGUI::IntSize view = MyGUI::RenderManager::getInstance().getViewSize();
        if (view.width <= 0 || view.height <= 0)
            return;
        const MyGUI::IntCoord c = mMainWidget->getCoord();
        Settings::Manager::setFloat("window x", "Questman", c.left / static_cast<float>(view.width));
        Settings::Manager::setFloat("window y", "Questman", c.top / static_cast<float>(view.height));
        // Questman keeps a fixed size; only the on-screen position is remembered.
    }

    void QuestManagerWindow::applyResponsiveLayout()
    {
        const int w = 790;
        const int h = 500;
        if (mMainWidget->getSize().width != w || mMainWidget->getSize().height != h)
        {
            const MyGUI::IntCoord c = mMainWidget->getCoord();
            mRestoringGeometry = true;
            mMainWidget->setCoord(MyGUI::IntCoord(c.left, c.top, w, h));
            mRestoringGeometry = false;
        }

        // Fixed compact layout. MW_Window_NoCaption gives us the full client
        // area, so every child frame is now placed explicitly inside it. The
        // footer is reserved for Exit inside the MW_Window_NoCaption client area;
        // the skin reserves 28 px at the top and 8 px at the bottom, so child
        // controls must not be positioned as if all 500 window pixels were client space.
        mTitle->setCoord(MyGUI::IntCoord(14, 6, 762, 24));
        mCloseButton->setCoord(MyGUI::IntCoord(674, 430, 88, 26));
        mTabs->setCoord(MyGUI::IntCoord(10, 36, 770, 388));

        // Quests tab. Keep the left list and the related-topics frame aligned
        // to the same lower baseline, close to the bottom of the tab frame.
        mQuestSearch->setCoord(MyGUI::IntCoord(6, 6, 282, 24));
        mQuestFilter->setCoord(MyGUI::IntCoord(304, 6, 214, 24));
        mShowCompleted->setCoord(MyGUI::IntCoord(528, 6, 104, 24));
        mShowHidden->setCoord(MyGUI::IntCoord(640, 6, 104, 24));
        mQuestList->setCoord(MyGUI::IntCoord(6, 38, 282, 284));
        mQuestCounter->setCoord(MyGUI::IntCoord(6, 326, 282, 20));
        mQuestIcon->setCoord(MyGUI::IntCoord(304, 40, 44, 44));
        mQuestHeading->setCoord(MyGUI::IntCoord(358, 42, 386, 40));
        mQuestDetail->setCoord(MyGUI::IntCoord(304, 92, 440, 146));
        mPinButton->setCoord(MyGUI::IntCoord(304, 246, 120, 28));
        mHideButton->setCoord(MyGUI::IntCoord(432, 246, 120, 28));
        mSkipButton->setCoord(MyGUI::IntCoord(560, 246, 88, 28));
        mResetButton->setCoord(MyGUI::IntCoord(656, 246, 88, 28));
        mStatusText->setCoord(MyGUI::IntCoord(14, 430, 650, 26));
        mQuestRelatedLabel->setCoord(MyGUI::IntCoord(304, 282, 440, 18));
        mQuestRelatedList->setCoord(MyGUI::IntCoord(304, 304, 440, 44));

        // Topics tab
        mTopicSearch->setCoord(MyGUI::IntCoord(6, 6, 220, 24));
        mTopicList->setCoord(MyGUI::IntCoord(6, 38, 220, 284));
        mTopicCounter->setCoord(MyGUI::IntCoord(6, 326, 220, 20));
        mTopicHeading->setCoord(MyGUI::IntCoord(242, 8, 502, 26));
        mTopicDetail->setCoord(MyGUI::IntCoord(242, 38, 502, 238));
        mTopicRelatedLabel->setCoord(MyGUI::IntCoord(242, 284, 502, 18));
        mTopicRelatedList->setCoord(MyGUI::IntCoord(242, 306, 502, 42));

        // Records tab
        mRecordSearch->setCoord(MyGUI::IntCoord(6, 6, 220, 24));
        mRecordList->setCoord(MyGUI::IntCoord(6, 38, 220, 284));
        mRecordCounter->setCoord(MyGUI::IntCoord(6, 326, 220, 20));
        mRecordHeading->setCoord(MyGUI::IntCoord(242, 8, 502, 26));
        mRecordDetail->setCoord(MyGUI::IntCoord(242, 38, 502, 310));

        // Progress tab
        mStatsDetail->setCoord(MyGUI::IntCoord(6, 6, 738, 342));

        // QM1: Services tab
        mServicesPoints->setCoord(MyGUI::IntCoord(6, 6, 738, 22));
        mServicesInfo->setCoord(MyGUI::IntCoord(6, 32, 738, 118));
        mClearJournalButton->setCoord(MyGUI::IntCoord(6, 158, 300, 28));
        mFactionLabel->setCoord(MyGUI::IntCoord(6, 196, 420, 20));
        mFactionList->setCoord(MyGUI::IntCoord(6, 218, 420, 130));
        mLeaveFactionButton->setCoord(MyGUI::IntCoord(440, 218, 304, 28));
    }

    std::string QuestManagerWindow::tr(const std::string& key)
    {
        return MyGUI::LanguageManager::getInstance().replaceTags("#{arenamp=" + key + "}");
    }

    std::string QuestManagerWindow::lower(const std::string& value)
    {
        return Misc::StringUtils::lowerCaseUtf8(value);
    }

    bool QuestManagerWindow::containsSearch(const std::string& haystack, const std::string& needle)
    {
        if (needle.empty())
            return true;
        return lower(haystack).find(needle) != std::string::npos;
    }

    bool QuestManagerWindow::getBool(const char* setting, bool fallback)
    {
        try { return Settings::Manager::getBool(setting, "Questman"); }
        catch (...) { return fallback; }
    }

    std::set<std::string> QuestManagerWindow::readIdSet(const char* setting)
    {
        std::set<std::string> result;
        std::string data;
        try { data = Settings::Manager::getString(setting, "Questman"); }
        catch (...) { return result; }
        std::stringstream stream(data);
        std::string item;
        while (std::getline(stream, item, ';'))
        {
            item = trim(item);
            if (!item.empty())
                result.insert(lower(item));
        }
        return result;
    }

    void QuestManagerWindow::writeIdSet(const char* setting, const std::set<std::string>& values)
    {
        std::string data;
        for (const std::string& value : values)
        {
            if (!data.empty())
                data += ';';
            data += value;
        }
        Settings::Manager::setString(setting, "Questman", data);
    }

    std::string QuestManagerWindow::truncate(const std::string& value, std::size_t maxChars)
    {
        if (value.size() <= maxChars)
            return value;
        return value.substr(0, maxChars > 3 ? maxChars - 3 : maxChars) + (maxChars > 3 ? "..." : "");
    }

    std::string QuestManagerWindow::join(const std::vector<std::string>& values, const std::string& separator)
    {
        std::string result;
        for (const std::string& value : values)
        {
            if (!result.empty())
                result += separator;
            result += value;
        }
        return result;
    }

    std::string QuestManagerWindow::labelForAddon(const std::string& value)
    {
        return tr("questman.addon." + value);
    }

    std::string QuestManagerWindow::labelForCategory(const std::string& value)
    {
        return tr("questman.category." + value);
    }

    std::string QuestManagerWindow::labelForFaction(const std::string& value)
    {
        if (value.empty())
            return tr("questman.none");
        return tr("questman.faction." + value);
    }

    bool QuestManagerWindow::isGenericTopic(const std::string& value)
    {
        const std::string key = lower(trim(value));
        static const std::set<std::string> generic = {
            "background", "latest rumors", "my trade", "little advice", "little secret",
            "someone in particular", "duties", "services", "specific place", "advancement", "orders",
            "свежие сплетни", "небольшой совет", "маленький секрет", "мое занятие",
            "кто-то особенный", "определенное место", "задания", "задание", "приказы",
            "повышение", "услуги", "услуга", "биография", "работа", "дела", "пойти вместе",
            "направление", "поговорить", "долг", "мудрость морроувинда", "крепость",
            "некромантия", "загадка", "рабство", "вампиры"
        };
        return generic.count(key) != 0;
    }

    bool QuestManagerWindow::isMainQuestFinal(const std::string& addon, const std::vector<std::string>& ids)
    {
        std::set<std::string> finals;
        if (addon == "morrowind") finals = { "c3_destroydagoth", "cx_backpath" };
        else if (addon == "tribunal") finals = { "tr_sothasil" };
        else if (addon == "bloodmoon") finals = { "bm_wildhunt" };
        else if (addon == "tamriel_rebuilt") finals = { "tr_necmq_3", "tr_necmq_3tribunal" };
        for (const std::string& id : ids)
            if (finals.count(lower(id)) != 0)
                return true;
        return false;
    }

    std::vector<std::size_t> QuestManagerWindow::relatedTopics(
        const std::vector<EntryData>& entries, const std::string& excludeId) const
    {
        std::set<std::string> explicitLinks;
        std::string blob;
        const std::regex marker("@([^#]+)#");
        for (const EntryData& entry : entries)
        {
            if (!blob.empty()) blob += '\n';
            blob += lower(sanitizeJournalText(entry.mText));
            for (std::sregex_iterator it(entry.mText.begin(), entry.mText.end(), marker), end; it != end; ++it)
                explicitLinks.insert(lower(trim((*it)[1].str())));
        }

        const std::string excluded = lower(excludeId);
        std::vector<std::size_t> result;
        for (std::size_t i = 0; i < mTopics.size(); ++i)
        {
            const TopicData& topic = mTopics[i];
            if (lower(topic.mId) == excluded || isGenericTopic(topic.mId) || isGenericTopic(topic.mName))
                continue;
            const std::string id = lower(trim(topic.mId));
            const std::string name = lower(trim(topic.mName));
            bool match = explicitLinks.count(id) != 0 || explicitLinks.count(name) != 0;

            // The English journal often relies on automatic keyword links and
            // contains no @keyword# markup. Mirror that behaviour conservatively
            // for Latin topic names, while Russian keeps using explicit markers
            // to avoid false matches caused by grammatical declension.
            if (!match && !name.empty() && static_cast<unsigned char>(name[0]) < 0x80)
            {
                std::size_t pos = blob.find(name);
                while (pos != std::string::npos)
                {
                    const auto wordByte = [](unsigned char c)
                    {
                        return std::isalnum(c) != 0 || c >= 0x80;
                    };
                    const bool leftOk = pos == 0 || !wordByte(static_cast<unsigned char>(blob[pos - 1]));
                    const std::size_t after = pos + name.size();
                    const bool rightOk = after >= blob.size() || !wordByte(static_cast<unsigned char>(blob[after]));
                    if (leftOk && rightOk) { match = true; break; }
                    pos = blob.find(name, pos + 1);
                }
            }
            if (match) result.push_back(i);
        }
        return result;
    }

    void QuestManagerWindow::refreshQuestRelatedTopics()
    {
        mQuestRelatedTopics.clear();
        mQuestRelatedList->removeAllItems();
        const std::size_t selected = mQuestList->getIndexSelected();
        if (selected == MyGUI::ITEM_NONE || selected >= mVisibleQuests.size())
            return;
        const QuestData& quest = mQuests[mVisibleQuests[selected]];
        mQuestRelatedTopics = relatedTopics(quest.mEntries, std::string());
        for (std::size_t topicIndex : mQuestRelatedTopics)
            mQuestRelatedList->addItem(mTopics[topicIndex].mName);
    }

    void QuestManagerWindow::refreshTopicRelatedTopics()
    {
        mTopicRelatedTopics.clear();
        mTopicRelatedList->removeAllItems();
        const std::size_t selected = mTopicList->getIndexSelected();
        if (selected == MyGUI::ITEM_NONE || selected >= mVisibleTopics.size())
            return;
        const TopicData& topic = mTopics[mVisibleTopics[selected]];
        mTopicRelatedTopics = relatedTopics(topic.mEntries, topic.mId);
        for (std::size_t topicIndex : mTopicRelatedTopics)
            mTopicRelatedList->addItem(mTopics[topicIndex].mName);
    }

    void QuestManagerWindow::selectTopic(std::size_t topicIndex)
    {
        if (topicIndex >= mTopics.size())
            return;
        mLastTopicId = lower(mTopics[topicIndex].mId);
        Settings::Manager::setString("last topic", "Questman", mLastTopicId);
        mTabs->setIndexSelected(1);
        refreshTopics();
        for (std::size_t row = 0; row < mVisibleTopics.size(); ++row)
        {
            if (mVisibleTopics[row] == topicIndex)
            {
                mTopicList->setIndexSelected(row);
                updateTopicDetail();
                break;
            }
        }
    }

    std::string QuestManagerWindow::questIconFor(const Metadata& meta)
    {
        if (!meta.mIcon.empty())
            return "icons/questman/" + meta.mIcon;
        if (!meta.mFaction.empty())
            return "icons/questman/fa_shared.dds";
        return "icons/questman/cat_misc.dds";
    }

    QuestManagerWindow::Metadata QuestManagerWindow::classifyQuest(const std::string& rawId)
    {
        const std::string id = lower(rawId);
        Metadata out;
        out.mAddon = "unknown";
        out.mCategory = "unknown";

        const auto isDigitAt = [&id](std::size_t pos)
        {
            return pos < id.size() && std::isdigit(static_cast<unsigned char>(id[pos]));
        };
        const auto hasToken = [&id](const std::string& token)
        {
            std::size_t pos = id.find(token);
            while (pos != std::string::npos)
            {
                const std::size_t after = pos + token.size();
                if (after < id.size() && (id[after] == '_' || std::isdigit(static_cast<unsigned char>(id[after]))))
                    return true;
                pos = id.find(token, pos + 1);
            }
            return false;
        };
        const auto hasTagNumber = [&id](const std::string& tag)
        {
            std::size_t pos = id.find(tag);
            while (pos != std::string::npos)
            {
                const std::size_t after = pos + tag.size();
                if (after < id.size() && std::isdigit(static_cast<unsigned char>(id[after])))
                    return true;
                pos = id.find(tag, pos + 1);
            }
            return false;
        };
        const auto setFaction = [&out](const char* faction)
        {
            out.mFaction = faction;
            out.mCategory = inSet(faction, {"hlaalu", "redoran", "telvanni"}) ? "great_house" : "faction";
        };

        // Stage 1: identify the province/add-on exactly like Questman's gated
        // rule sets. This deliberately does NOT treat every TR_ id as Tamriel
        // Rebuilt, because vanilla Tribunal also owns TR_ records.
        const bool isTamrielRebuilt = (starts(id, "tr_m") && isDigitAt(4))
            || starts(id, "tr_necmq") || id == "tr_tt_q1";
        const bool isProjectCyrodiil = starts(id, "pc_m") && isDigitAt(4);
        const bool isProvinceSkyrim = starts(id, "sky_");

        if (isTamrielRebuilt)
        {
            out.mAddon = "tamriel_rebuilt";
            out.mCategory = "misc";
            if (id.find("necmq") != std::string::npos) out.mCategory = "main";
            else if (hasToken("_hh")) setFaction("hlaalu");
            else if (hasToken("_hr")) setFaction("redoran");
            else if (hasToken("_ht")) setFaction("telvanni");
            else if (hasToken("_eec")) setFaction("east_empire");
            else if (hasToken("_fg")) setFaction("fighters");
            else if (hasToken("_ic")) setFaction("imperial_cult");
            else if (hasToken("_il")) setFaction("imperial_legion");
            else if (hasToken("_jns")) setFaction("janatta");
            else if (hasToken("_mg")) setFaction("mages");
            else if (hasToken("_mt")) setFaction("morag_tong");
            else if (hasToken("_tg")) setFaction("thieves");
            else if (hasToken("_tt")) setFaction("temple");
            else if (hasToken("_da")) out.mCategory = "daedric";
            else if (hasToken("_va") || hasToken("_bal")) out.mCategory = "vampire";
            else if (hasToken("_arena")) out.mCategory = "narsis_arena";
            else if (id.find("_and_bounty_") != std::string::npos
                || hasToken("_b") || id.find("_uman_b") != std::string::npos)
                out.mCategory = "bounty";

            // Questman's explicit TR outliers.
            if (starts(id, "tr_m1_rr_mq_") || id == "tr_m1_rr_mq_2.5" || id == "tr_m1_rr_mq_6.5")
                setFaction("telvanni");
            else if (id == "tr_m4_and_missingmerchant") setFaction("hlaalu");
            else if (id == "tr_m3_at_mgbonus") setFaction("mages");
            else if (id == "tr_m0_siegeatfiremoth") setFaction("imperial_legion");
            else if (id == "tr_tt_q1") setFaction("temple");
        }
        else if (isProjectCyrodiil)
        {
            out.mAddon = "project_cyrodiil";
            out.mCategory = "misc";
            if (hasToken("_mg")) setFaction("mages");
            else if (hasToken("_tg")) setFaction("thieves");
            else if (hasToken("_fg")) setFaction("fighters");
            else if (hasToken("_k1")) setFaction("kingdom_anvil");
            else if (hasToken("_ip")) setFaction("itinerant_priests");
            else if (hasToken("_bounty")) out.mCategory = "bounty";
            else if (id.find("_afp") != std::string::npos) out.mCategory = "abecette";

            if (id == "pc_m0_fw_colstalker") setFaction("fighters");
            else if (id == "pc_m1_anv_goldenrod") setFaction("kingdom_anvil");
        }
        else if (isProvinceSkyrim)
        {
            out.mAddon = "province_skyrim";
            out.mCategory = "misc";
            if (hasTagNumber("tg")) setFaction("thieves");
            else if (hasTagNumber("mg")) setFaction("mages");
            else if (hasTagNumber("fg")) setFaction("fighters");
            else if (hasTagNumber("b")) out.mCategory = "bounty";
        }
        else
        {
            // Vanilla mainland fallback. Order matters: faction/house prefixes
            // precede broad Tribunal/Bloodmoon main-quest prefixes.
            out.mAddon = "unknown";
            out.mCategory = "unknown";
            if (starts(id, "fg_")) { out.mAddon = "morrowind"; setFaction("fighters"); }
            else if (starts(id, "mg_")) { out.mAddon = "morrowind"; setFaction("mages"); }
            else if (starts(id, "tg_")) { out.mAddon = "morrowind"; setFaction("thieves"); }
            else if (starts(id, "mt_")) { out.mAddon = "morrowind"; setFaction("morag_tong"); }
            else if (starts(id, "il_")) { out.mAddon = "morrowind"; setFaction("imperial_legion"); }
            else if (starts(id, "ic")) { out.mAddon = "morrowind"; setFaction("imperial_cult"); }
            else if (starts(id, "tt_")) { out.mAddon = "morrowind"; setFaction("temple"); }
            else if (starts(id, "co_")) { out.mAddon = "bloodmoon"; setFaction("east_empire"); }
            else if (starts(id, "hh_")) { out.mAddon = "morrowind"; setFaction("hlaalu"); }
            else if (starts(id, "hr_")) { out.mAddon = "morrowind"; setFaction("redoran"); }
            else if (starts(id, "ht_")) { out.mAddon = "morrowind"; setFaction("telvanni"); }
            else if (starts(id, "da_")) { out.mAddon = "morrowind"; out.mCategory = "daedric"; }
            else if (starts(id, "va_")) { out.mAddon = "morrowind"; out.mCategory = "vampire"; }
            else if (id.size() >= 2 && (id[0] == 'a' || id[0] == 'b' || id[0] == 'c') && isDigitAt(1))
                { out.mAddon = "morrowind"; out.mCategory = "main"; }
            else if (starts(id, "cx_")) { out.mAddon = "morrowind"; out.mCategory = "main"; }
            else if (starts(id, "tr")) { out.mAddon = "tribunal"; out.mCategory = "main"; }
            else if (starts(id, "bm_")) { out.mAddon = "bloodmoon"; out.mCategory = "main"; }
            else if (starts(id, "ms_")) { out.mAddon = "tribunal"; out.mCategory = "misc"; }
            else if (starts(id, "mv_") || starts(id, "eb") || starts(id, "blades_")
                || starts(id, "town_") || starts(id, "romance_"))
                { out.mAddon = "morrowind"; out.mCategory = "misc"; }

            // Exact vanilla exceptions from Questman 1.4.
            if (id == "a1_sleeperdreamer02" || id == "a2_1_kurapli_zallay")
                out.mCategory = "misc";
            if (inSet(id, {"bm_airship", "bm_airship_a", "bm_airship_c", "bm_brodirgrove",
                    "bm_cursedcaptain", "bm_cursedcaptain_a", "bm_falmer", "bm_ingmar",
                    "bm_meadhall", "bm_meadhall_a", "bm_meadhall_b", "bm_meadhall_c",
                    "bm_missionary", "bm_moonsugar", "bm_retribution", "bm_sadseer",
                    "bm_tymvaul", "bm_womanscorned"}))
                out.mCategory = "misc";
            if (inSet(id, {"ms_apologies", "ms_arenimtomb", "ms_fargothring", "ms_firemoth",
                    "ms_gold_kanet_flower", "ms_hannat", "ms_hatandskirt", "ms_hentuspants",
                    "ms_jobashaabolitionist", "ms_lookout", "ms_master_index", "ms_nord_burial",
                    "ms_nuccius", "ms_piernette", "ms_trerayna_bounty", "ms_umbra",
                    "ms_vampirecure", "ms_vassirdidanat", "ms_whiteguar"}))
                out.mAddon = "morrowind";
        }

        if (out.mFaction == "hlaalu") out.mIcon = "gh_hlalu.dds";
        else if (out.mFaction == "redoran") out.mIcon = "gh_redoran.dds";
        else if (out.mFaction == "telvanni") out.mIcon = "gh_telvanni.dds";
        else if (out.mFaction == "fighters") out.mIcon = "fa_fighters.dds";
        else if (out.mFaction == "mages") out.mIcon = "fa_mages.dds";
        else if (out.mFaction == "thieves") out.mIcon = "fa_thieves.dds";
        else if (out.mFaction == "east_empire") out.mIcon = "fa_eec.dds";
        else if (out.mFaction == "temple") out.mIcon = "fa_temple.dds";
        else if (out.mFaction == "imperial_legion") out.mIcon = "fa_imp_legion.dds";
        else if (out.mFaction == "imperial_cult") out.mIcon = "fa_imp_cult.dds";
        else if (out.mFaction == "morag_tong") out.mIcon = "fa_morag_tong.dds";
        else if (out.mFaction == "janatta") out.mIcon = "fa_janatta.dds";
        else if (out.mFaction == "kingdom_anvil") out.mIcon = "fa_anvil.dds";
        else if (out.mFaction == "itinerant_priests") out.mIcon = "fa_priests.dds";
        else if (out.mCategory == "vampire") out.mIcon = "cat_vampires.dds";
        else if (out.mCategory == "daedric") out.mIcon = "cat_daedric.dds";
        else if (out.mCategory == "bounty") out.mIcon = "cat_bounty.dds";
        else if (out.mCategory == "narsis_arena") out.mIcon = "cat_narsis_arena.dds";
        else if (out.mCategory == "abecette") out.mIcon = "cat_abecette.dds";
        else if (out.mCategory == "main")
        {
            if (out.mAddon == "tribunal") out.mIcon = "addon_tribunal.dds";
            else if (out.mAddon == "bloodmoon") out.mIcon = "addon_bloodmoon.dds";
            else if (out.mAddon == "tamriel_rebuilt") out.mIcon = "addon_tamriel.dds";
            else if (out.mAddon == "project_cyrodiil") out.mIcon = "addon_cyrodiil.dds";
            else if (out.mAddon == "province_skyrim") out.mIcon = "addon_skyrim.dds";
            else out.mIcon = "addon_morrowind.dds";
        }
        else if (out.mCategory == "great_house") out.mIcon = "gh_shared.dds";
        else if (out.mCategory == "faction") out.mIcon = "fa_shared.dds";
        else out.mIcon = "cat_misc.dds";

        return out;
    }

    void QuestManagerWindow::rebuildQuestGivers()
    {
        mQuestGivers.clear();
        mQuestGiverStages.clear();
        std::map<std::string, int> stages;
        const MWWorld::ESMStore& store = MWBase::Environment::get().getWorld()->getStore();
        const MWWorld::Store<ESM::Dialogue>& dialogues = store.get<ESM::Dialogue>();
        const std::regex quoted("journal[\\t ,]+\\\"([^\\\"]+)\\\"[\\t ,]+(-?[0-9]+)", std::regex_constants::icase);
        const std::regex unquoted("journal[\\t ,]+([A-Za-z][A-Za-z0-9_]+)[\\t ,]+(-?[0-9]+)", std::regex_constants::icase);

        for (auto dialIt = dialogues.begin(); dialIt != dialogues.end(); ++dialIt)
        {
            for (const ESM::DialInfo& info : dialIt->mInfo)
            {
                if (info.mResultScript.empty() || info.mActor.empty())
                    continue;
                std::smatch match;
                std::string::const_iterator begin = info.mResultScript.begin();
                const std::string::const_iterator end = info.mResultScript.end();
                while (begin != end)
                {
                    std::match_results<std::string::const_iterator> result;
                    bool found = std::regex_search(begin, end, result, quoted);
                    if (!found)
                        found = std::regex_search(begin, end, result, unquoted);
                    if (!found)
                        break;
                    const std::string id = lower(result[1].str());
                    const int stage = std::atoi(result[2].str().c_str());
                    const auto current = stages.find(id);
                    if (current == stages.end() || stage < current->second)
                    {
                        stages[id] = stage;
                        const ESM::NPC* npc = store.get<ESM::NPC>().search(info.mActor);
                        mQuestGivers[id] = npc && !npc->mName.empty() ? npc->mName : info.mActor;
                        mQuestGiverStages[id] = stage;
                    }
                    begin = result[0].second;
                }
            }
        }
    }

    void QuestManagerWindow::rebuildData()
    {
        // Rebuild the save-dependent lists on every open, but keep the giver
        // catalog cached: it depends on loaded content, not on journal state,
        // and scanning every INFO record in TR/PC/SHOTN can be expensive.
        mQuests.clear();
        mTopics.clear();
        mRecords.clear();
        mVisibleQuests.clear();
        mVisibleTopics.clear();
        mVisibleRecords.clear();
        mPinned = readIdSet("pinned quests");
        mHidden = readIdSet("hidden quests");
        const bool legacyShow = getBool("show completed hidden", false);
        mShowCompletedState = getBool("show completed", legacyShow);
        mShowHiddenState = getBool("show hidden", legacyShow);
        try { mLastQuestId = lower(Settings::Manager::getString("last quest", "Questman")); }
        catch (...) { mLastQuestId.clear(); }
        try { mLastTopicId = lower(Settings::Manager::getString("last topic", "Questman")); }
        catch (...) { mLastTopicId.clear(); }
        try { mLastRecordTopic = lower(Settings::Manager::getString("last record", "Questman")); }
        catch (...) { mLastRecordTopic.clear(); }
        mShowCompleted->setStateSelected(mShowCompletedState);
        mShowHidden->setStateSelected(mShowHiddenState);
        if (mQuestGivers.empty())
            rebuildQuestGivers();

        MWBase::Journal* journal = MWBase::Environment::get().getJournal();
        if (!journal)
            return;

        struct Group
        {
            QuestData mQuest;
            std::set<std::string> mIdKeys;
        };
        std::map<std::string, Group> groups;
        std::map<std::string, std::string> idToGroup;

        for (auto it = journal->questBegin(); it != journal->questEnd(); ++it)
        {
            const std::string id = it->first;
            const std::string name = questNameFromEsm(id);
            // If the ESM has no QS_Name, this is an unnamed journal record,
            // not a titled quest. Leave it unmapped so its entries appear in
            // the Records tab instead of exposing a raw editor id.
            if (name.empty())
                continue;
            const std::string groupKey = lower(name);
            Group& group = groups[groupKey];
            if (group.mQuest.mId.empty())
            {
                group.mQuest.mId = id;
                group.mQuest.mName = name;
                const Metadata meta = classifyQuest(id);
                group.mQuest.mAddon = meta.mAddon;
                group.mQuest.mCategory = meta.mCategory;
                group.mQuest.mFaction = meta.mFaction;
                group.mQuest.mIcon = questIconFor(meta);
            }
            group.mQuest.mIds.push_back(id);
            group.mIdKeys.insert(lower(id));
            idToGroup[lower(id)] = groupKey;
            group.mQuest.mCompleted = group.mQuest.mCompleted || it->second.isFinished();
            group.mQuest.mStage = std::max(group.mQuest.mStage, it->second.getIndex());
        }

        int order = 0;
        for (auto it = journal->begin(); it != journal->end(); ++it, ++order)
        {
            const std::string topicKey = lower(it->mTopic);
            const auto groupName = idToGroup.find(topicKey);
            std::ostringstream dateStream;
            dateStream << it->mDayOfMonth << " "
                << MWBase::Environment::get().getWorld()->getMonthName(it->mMonth)
                << " " << it->mDay;
            const std::string date = dateStream.str();
            EntryData entry;
            entry.mInfoId = it->mInfoId;
            entry.mText = it->getText();
            entry.mActor = it->mActorName;
            entry.mDate = date;
            entry.mOrder = (static_cast<long long>(order) + 1) * sQuestOrderStride;

            if (groupName != idToGroup.end())
            {
                Group& group = groups[groupName->second];
                group.mQuest.mEntries.push_back(entry);
                group.mQuest.mLastOrder = std::max(group.mQuest.mLastOrder, entry.mOrder);
            }
            else
            {
                // Questman's Records tab is specifically the accumulated JOUR
                // records that are not real/name-grouped quests.
                RecordData record;
                record.mTopic = it->mTopic;
                record.mEntry = entry;
                record.mTitle = recordTitle(it->mTopic, entry.mText);
                mRecords.push_back(record);
            }
        }

        for (auto& groupPair : groups)
        {
            QuestData& quest = groupPair.second.mQuest;
            // A translated quest can be represented by multiple JOUR ids. Keep a
            // deterministic identity so pin/hide/last-viewed state does not jump
            // when the journal progresses into another alias.
            std::sort(quest.mIds.begin(), quest.mIds.end(), [](const std::string& a, const std::string& b)
                { return QuestManagerWindow::lower(a) < QuestManagerWindow::lower(b); });
            if (!quest.mIds.empty())
                quest.mId = quest.mIds.front();

            const std::string key = lower(quest.mId);
            quest.mPinned = mPinned.count(key) != 0;
            quest.mHidden = mHidden.count(key) != 0;
            int bestStage = 0x7fffffff;
            for (const std::string& id : quest.mIds)
            {
                const std::string giverKey = lower(id);
                const auto giver = mQuestGivers.find(giverKey);
                const auto stage = mQuestGiverStages.find(giverKey);
                if (giver == mQuestGivers.end() || stage == mQuestGiverStages.end())
                    continue;
                if (stage->second < bestStage)
                {
                    bestStage = stage->second;
                    quest.mGiver = giver->second;
                }
            }
            mQuests.push_back(quest);
        }

        // X037: merge server-authoritative quests into the same Quest Manager
        // used for vanilla JOUR quests. They remain separate from the ESM Journal
        // storage/save format; the server is authoritative and re-syncs them on login.
        const std::vector<mwmp::ServerQuestState>& serverStates
            = mwmp::ServerQuestRegistry::get().getQuestStates();
        const MWWorld::ESMStore& esmStore = MWBase::Environment::get().getWorld()->getStore();
        long long serverSerial = 1;
        const long long nativeEntryCount = order;
        for (const mwmp::ServerQuestState& state : serverStates)
        {
            if (state.questId.empty() || state.questName.empty())
                continue;

            QuestData quest;
            quest.mId = "server:" + state.questId;
            quest.mIds.push_back(quest.mId);
            quest.mName = state.questName;
            quest.mStage = state.stage;
            quest.mCompleted = state.state == "completed" || state.state == "failed";
            quest.mAddon = "server";
            quest.mCategory = "server";
            quest.mIcon = "icons/questman/cat_misc.dds";
            const std::string key = lower(quest.mId);
            quest.mPinned = mPinned.count(key) != 0;
            quest.mHidden = mHidden.count(key) != 0;

            const ESM::NPC* giver = esmStore.get<ESM::NPC>().search(state.giverRefId);
            quest.mGiver = giver && !giver->mName.empty() ? giver->mName : state.giverRefId;

            for (const mwmp::ServerQuestJournalEntry& serverEntry : state.journal)
            {
                EntryData entry;
                entry.mInfoId = "server:" + state.questId + ":" + std::to_string(serverEntry.stage);
                entry.mText = serverEntry.text;
                entry.mActor = quest.mGiver;
                entry.mDate = serverEntry.date;
                entry.mOrder = serverQuestOrder(serverEntry, nativeEntryCount, serverSerial++);
                quest.mEntries.push_back(entry);
                quest.mLastOrder = std::max(quest.mLastOrder, entry.mOrder);
            }
            mQuests.push_back(quest);
        }

        std::stable_sort(mQuests.begin(), mQuests.end(), [](const QuestData& a, const QuestData& b)
        {
            if (a.mPinned != b.mPinned) return a.mPinned > b.mPinned;
            // Y015: recency is authoritative across vanilla and server quests.
            // Completion state is only a tie-breaker; finishing a newer ordinary
            // quest must be allowed to move ahead of an older active custom quest.
            if (a.mLastOrder != b.mLastOrder) return a.mLastOrder > b.mLastOrder;
            if (a.mCompleted != b.mCompleted) return a.mCompleted < b.mCompleted;
            return QuestManagerWindow::lower(a.mName) < QuestManagerWindow::lower(b.mName);
        });

        for (auto it = journal->topicBegin(); it != journal->topicEnd(); ++it)
        {
            TopicData topic;
            topic.mId = it->first;
            topic.mName = it->second.getName().empty() ? it->first : it->second.getName();
            int topicOrder = 0;
            for (auto entry = it->second.begin(); entry != it->second.end(); ++entry, ++topicOrder)
            {
                EntryData data;
                data.mInfoId = entry->mInfoId;
                data.mText = entry->getText();
                data.mActor = entry->mActorName;
                data.mOrder = topicOrder;
                topic.mEntries.push_back(data);
            }

            if (topic.mEntries.empty())
            {
                // Multiplayer clients are told which topics the player knows, but
                // never receive the responses. Resolve the text from the locally
                // loaded dialogue records so the topic is readable and searchable
                // instead of reporting that it has no entries.
                EntryData data;
                data.mOrder = 0;
                data.mText = MWDialogue::TopicRecovery::responseFor(topic.mId, std::string(), &data.mInfoId);
                if (!data.mText.empty())
                    topic.mEntries.push_back(data);
            }

            mTopics.push_back(topic);
        }
        std::sort(mTopics.begin(), mTopics.end(), [](const TopicData& a, const TopicData& b)
            { return QuestManagerWindow::lower(a.mName) < QuestManagerWindow::lower(b.mName); });

        std::reverse(mRecords.begin(), mRecords.end());
        rebuildFilters();
    }

    void QuestManagerWindow::rebuildFilters()
    {
        mFilters.clear();
        mQuestFilter->removeAllItems();
        FilterData all;
        all.mAxis = FilterData::All;
        all.mLabel = tr("questman.filter.all");
        mFilters.push_back(all);
        mQuestFilter->addItem(all.mLabel);

        std::map<std::string, int> addonCounts, categoryCounts, factionCounts;
        for (const QuestData& quest : mQuests)
        {
            ++addonCounts[quest.mAddon];
            ++categoryCounts[quest.mCategory];
            if (!quest.mFaction.empty())
                ++factionCounts[quest.mFaction];
        }

        std::vector<std::string> addons, categories, factions;
        for (const auto& row : addonCounts) addons.push_back(row.first);
        for (const auto& row : categoryCounts) categories.push_back(row.first);
        for (const auto& row : factionCounts) factions.push_back(row.first);

        // Match the original Questman ordering: vanilla add-ons first, Other
        // last; Main Quest first and Miscellaneous last; factions alphabetical.
        const auto addonRank = [](const std::string& value)
        {
            if (value == "morrowind") return 0;
            if (value == "tribunal") return 1;
            if (value == "bloodmoon") return 2;
            if (value == "server") return 3;
            if (value == "unknown") return 1000;
            return 10;
        };
        std::sort(addons.begin(), addons.end(), [addonRank](const std::string& a, const std::string& b)
        {
            const int ra = addonRank(a), rb = addonRank(b);
            if (ra != rb) return ra < rb;
            return QuestManagerWindow::labelForAddon(a) < QuestManagerWindow::labelForAddon(b);
        });
        const auto categoryRank = [](const std::string& value)
        {
            if (value == "main") return 0;
            if (value == "server") return 5;
            if (value == "misc") return 1000;
            if (value == "unknown") return 999;
            return 10;
        };
        std::sort(categories.begin(), categories.end(), [categoryRank](const std::string& a, const std::string& b)
        {
            const int ra = categoryRank(a), rb = categoryRank(b);
            if (ra != rb) return ra < rb;
            return QuestManagerWindow::labelForCategory(a) < QuestManagerWindow::labelForCategory(b);
        });
        std::sort(factions.begin(), factions.end(), [](const std::string& a, const std::string& b)
            { return QuestManagerWindow::labelForFaction(a) < QuestManagerWindow::labelForFaction(b); });

        const auto addFilter = [this](FilterData::Axis axis, const std::string& value,
            const std::string& prefix, int count)
        {
            FilterData filter;
            filter.mAxis = axis;
            filter.mValue = value;
            std::string label;
            if (axis == FilterData::Addon) label = labelForAddon(value);
            else if (axis == FilterData::Category) label = labelForCategory(value);
            else label = labelForFaction(value);
            std::ostringstream caption;
            caption << prefix << ": " << label << " (" << count << ")";
            filter.mLabel = caption.str();
            mFilters.push_back(filter);
            mQuestFilter->addItem(filter.mLabel);
        };
        for (const std::string& value : addons)
            addFilter(FilterData::Addon, value, tr("questman.filter.addon"), addonCounts[value]);
        for (const std::string& value : categories)
            addFilter(FilterData::Category, value, tr("questman.filter.category"), categoryCounts[value]);
        for (const std::string& value : factions)
            addFilter(FilterData::Faction, value, tr("questman.filter.faction"), factionCounts[value]);

        int savedAxis = static_cast<int>(FilterData::All);
        std::string savedValue;
        try { savedAxis = Settings::Manager::getInt("last filter axis", "Questman"); } catch (...) {}
        try { savedValue = Settings::Manager::getString("last filter value", "Questman"); } catch (...) {}
        std::size_t selected = 0;
        for (std::size_t i = 1; i < mFilters.size(); ++i)
            if (static_cast<int>(mFilters[i].mAxis) == savedAxis && mFilters[i].mValue == savedValue)
            { selected = i; break; }
        mQuestFilter->setIndexSelected(selected);
    }

    void QuestManagerWindow::refreshCurrentTab()
    {
        switch (mTabs->getIndexSelected())
        {
            case 0: refreshQuests(); break;
            case 1: refreshTopics(); break;
            case 2: refreshRecords(); break;
            case 3: refreshStats(); break;
            default: refreshServices(); break;
        }
    }

    void QuestManagerWindow::refreshQuests()
    {
        const std::string needle = lower(mQuestSearch->getCaption());
        const std::size_t filterIndex = mQuestFilter->getIndexSelected();
        const FilterData* filter = filterIndex < mFilters.size() ? &mFilters[filterIndex] : nullptr;
        mVisibleQuests.clear();
        mQuestList->removeAllItems();

        for (std::size_t i = 0; i < mQuests.size(); ++i)
        {
            const QuestData& quest = mQuests[i];
            if (!mShowHiddenState && quest.mHidden)
                continue;
            if (!mShowCompletedState && quest.mCompleted && !quest.mPinned)
                continue;
            if (filter && filter->mAxis != FilterData::All)
            {
                const std::string* field = filter->mAxis == FilterData::Addon ? &quest.mAddon
                    : filter->mAxis == FilterData::Category ? &quest.mCategory : &quest.mFaction;
                if (*field != filter->mValue)
                    continue;
            }
            bool matches = containsSearch(quest.mName, needle) || containsSearch(quest.mId, needle);
            if (!matches)
                for (const EntryData& entry : quest.mEntries)
                    if (containsSearch(entry.mText, needle)) { matches = true; break; }
            if (!matches)
                continue;

            std::string prefix;
            if (quest.mPinned) prefix += "* ";
            if (quest.mCompleted) prefix += "[" + tr("questman.completed_short") + "] ";
            if (quest.mHidden) prefix += "[" + tr("questman.hidden_short") + "] ";
            mQuestList->addItem(prefix + quest.mName);
            mVisibleQuests.push_back(i);
        }

        if (!mVisibleQuests.empty())
        {
            std::size_t select = 0;
            if (!mLastQuestId.empty())
            {
                for (std::size_t row = 0; row < mVisibleQuests.size(); ++row)
                {
                    const QuestData& candidate = mQuests[mVisibleQuests[row]];
                    bool match = lower(candidate.mId) == mLastQuestId;
                    if (!match)
                        for (const std::string& id : candidate.mIds)
                            if (lower(id) == mLastQuestId) { match = true; break; }
                    if (match) { select = row; break; }
                }
            }
            mQuestList->setIndexSelected(select);
        }
        updateCounters();
        updateQuestDetail();
    }

    void QuestManagerWindow::refreshTopics()
    {
        const std::string needle = lower(mTopicSearch->getCaption());
        mVisibleTopics.clear();
        mTopicList->removeAllItems();
        for (std::size_t i = 0; i < mTopics.size(); ++i)
        {
            const TopicData& topic = mTopics[i];
            bool matches = containsSearch(topic.mName, needle);
            if (!matches)
                for (const EntryData& entry : topic.mEntries)
                    if (containsSearch(entry.mText, needle)) { matches = true; break; }
            if (!matches) continue;
            mTopicList->addItem(topic.mName);
            mVisibleTopics.push_back(i);
        }
        if (!mVisibleTopics.empty())
        {
            std::size_t select = 0;
            if (!mLastTopicId.empty())
                for (std::size_t row = 0; row < mVisibleTopics.size(); ++row)
                    if (lower(mTopics[mVisibleTopics[row]].mId) == mLastTopicId)
                    { select = row; break; }
            mTopicList->setIndexSelected(select);
        }
        updateCounters();
        updateTopicDetail();
    }

    void QuestManagerWindow::refreshRecords()
    {
        const std::string needle = lower(mRecordSearch->getCaption());
        mVisibleRecords.clear();
        mRecordList->removeAllItems();
        for (std::size_t i = 0; i < mRecords.size(); ++i)
        {
            const RecordData& record = mRecords[i];
            if (!containsSearch(record.mTitle, needle) && !containsSearch(record.mEntry.mText, needle)
                && !containsSearch(record.mEntry.mActor, needle))
                continue;
            std::string label = record.mEntry.mDate;
            if (!label.empty()) label += " — ";
            label += truncate(record.mTitle, 46);
            mRecordList->addItem(label);
            mVisibleRecords.push_back(i);
        }
        if (!mVisibleRecords.empty())
        {
            std::size_t select = 0;
            if (!mLastRecordTopic.empty())
                for (std::size_t row = 0; row < mVisibleRecords.size(); ++row)
                    if (lower(mRecords[mVisibleRecords[row]].mTopic) == mLastRecordTopic)
                    { select = row; break; }
            mRecordList->setIndexSelected(select);
        }
        updateCounters();
        updateRecordDetail();
    }

    void QuestManagerWindow::updateCounters()
    {
        int hidden = 0;
        for (const QuestData& quest : mQuests) if (quest.mHidden) ++hidden;
        std::ostringstream q;
        q << tr("questman.counter.shown") << " " << mVisibleQuests.size() << " / " << mQuests.size();
        if (hidden > 0) q << "   " << tr("questman.stats.hidden") << ": " << hidden;
        mQuestCounter->setCaption(q.str());

        std::ostringstream t;
        t << tr("questman.counter.shown") << " " << mVisibleTopics.size() << " / " << mTopics.size();
        mTopicCounter->setCaption(t.str());

        std::ostringstream r;
        r << tr("questman.counter.shown") << " " << mVisibleRecords.size() << " / " << mRecords.size();
        mRecordCounter->setCaption(r.str());
    }

    void QuestManagerWindow::refreshStats()
    {
        int active = 0, completed = 0, hidden = 0, pinned = 0;
        std::map<std::string, int> doneCategories, doneFactions;
        std::set<std::string> touchedCategories, touchedFactions;
        for (const QuestData& quest : mQuests)
        {
            if (quest.mCompleted)
            {
                ++completed;
                ++doneCategories[quest.mCategory];
                if (!quest.mFaction.empty()) ++doneFactions[quest.mFaction];
            }
            else ++active;
            if (quest.mHidden) ++hidden;
            if (quest.mPinned) ++pinned;
            touchedCategories.insert(quest.mCategory);
            if (!quest.mFaction.empty()) touchedFactions.insert(quest.mFaction);
        }

        // Original Questman compares completed quests against the entire loaded
        // quest catalog, not only quests already touched by the player.
        struct CatalogQuest { std::string category, faction; };
        std::map<std::string, CatalogQuest> catalog;
        const MWWorld::ESMStore& store = MWBase::Environment::get().getWorld()->getStore();
        const MWWorld::Store<ESM::Dialogue>& dialogues = store.get<ESM::Dialogue>();
        for (auto dial = dialogues.begin(); dial != dialogues.end(); ++dial)
        {
            if (dial->mType != ESM::Dialogue::Journal) continue;
            std::string name;
            for (const ESM::DialInfo& info : dial->mInfo)
            {
                if (info.mQuestStatus == ESM::DialInfo::QS_Name && !trim(info.mResponse).empty())
                {
                    name = trim(info.mResponse);
                    break;
                }
            }
            if (name.empty()) continue;
            const std::string group = lower(name);
            if (catalog.count(group)) continue;
            const Metadata meta = classifyQuest(dial->mId);
            catalog[group] = { meta.mCategory, meta.mFaction };
        }

        std::map<std::string, int> totalCategories, totalFactions;
        for (const auto& row : catalog)
        {
            ++totalCategories[row.second.category];
            if (!row.second.faction.empty()) ++totalFactions[row.second.faction];
        }
        std::size_t serverCatalogCount = 0;
        for (const QuestData& quest : mQuests)
        {
            if (quest.mAddon != "server")
                continue;
            ++serverCatalogCount;
            ++totalCategories[quest.mCategory];
            if (!quest.mFaction.empty())
                ++totalFactions[quest.mFaction];
        }

        const MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
        const MWMechanics::NpcStats& playerStats = player.getClass().getNpcStats(player);
        const auto factionId = [](const std::string& slug) -> std::string
        {
            if (slug == "fighters") return "fighters guild";
            if (slug == "mages") return "mages guild";
            if (slug == "thieves") return "thieves guild";
            if (slug == "morag_tong") return "morag tong";
            if (slug == "imperial_legion") return "imperial legion";
            if (slug == "imperial_cult") return "imperial cult";
            if (slug == "temple") return "temple";
            if (slug == "east_empire") return "east empire company";
            if (slug == "hlaalu") return "hlaalu";
            if (slug == "redoran") return "redoran";
            if (slug == "telvanni") return "telvanni";
            return std::string();
        };
        const auto topRankReached = [&store, &playerStats, &factionId](const std::string& slug)
        {
            const std::string id = factionId(slug);
            if (id.empty()) return false;
            const int rank = playerStats.getFactionRank(id);
            const ESM::Faction* faction = store.get<ESM::Faction>().search(id);
            if (!faction || rank < 0) return false;
            int top = -1;
            for (int i = 0; i < 10; ++i)
                if (!faction->mRanks[i].empty()) top = i;
            return top >= 0 && rank >= top;
        };

        std::string completedHouse;
        for (const char* house : { "hlaalu", "redoran", "telvanni" })
        {
            if (topRankReached(house))
            {
                completedHouse = house;
                break;
            }
        }

        std::ostringstream text;
        text << tr("questman.stats.overview") << "\n";
        text << "  " << tr("questman.stats.active") << ": " << active << "\n";
        text << "  " << tr("questman.stats.completed") << ": " << completed << " / " << (catalog.size() + serverCatalogCount) << "\n";
        text << "  " << tr("questman.stats.pinned") << ": " << pinned << "\n";
        text << "  " << tr("questman.stats.hidden") << ": " << hidden << "\n";
        text << "  " << tr("questman.stats.topics") << ": " << mTopics.size() << "\n";
        text << "  " << tr("questman.stats.records") << ": " << mRecords.size() << "\n\n";

        text << "\n" << tr("questman.stats.by_category") << "\n";
        std::vector<std::string> categoryKeys(touchedCategories.begin(), touchedCategories.end());
        const auto categoryRank = [](const std::string& value)
        {
            if (value == "main") return 0;
            if (value == "server") return 5;
            if (value == "misc") return 1000;
            if (value == "unknown") return 999;
            return 10;
        };
        std::sort(categoryKeys.begin(), categoryKeys.end(), [categoryRank](const std::string& a, const std::string& b)
        {
            const int ra = categoryRank(a), rb = categoryRank(b);
            if (ra != rb) return ra < rb;
            return QuestManagerWindow::labelForCategory(a) < QuestManagerWindow::labelForCategory(b);
        });
        for (const std::string& key : categoryKeys)
            text << "  " << labelForCategory(key) << ": " << doneCategories[key] << " / " << totalCategories[key] << "\n";

        if (!touchedFactions.empty())
        {
            text << "\n" << tr("questman.stats.by_faction") << "\n";
            std::vector<std::string> factionKeys(touchedFactions.begin(), touchedFactions.end());
            std::sort(factionKeys.begin(), factionKeys.end(), [](const std::string& a, const std::string& b)
                { return QuestManagerWindow::labelForFaction(a) < QuestManagerWindow::labelForFaction(b); });
            for (const std::string& key : factionKeys)
            {
                text << "  " << labelForFaction(key) << ": " << doneFactions[key] << " / " << totalFactions[key];
                if (topRankReached(key))
                    text << "  [" << tr("questman.stats.top_rank") << "]";
                else if (!completedHouse.empty() && inSet(key, { "hlaalu", "redoran", "telvanni" }))
                    text << "  [" << tr("questman.stats.other_house") << ": " << labelForFaction(completedHouse) << "]";
                text << "\n";
            }
        }
        mStatsDetail->setCaption(text.str());
    }

    void QuestManagerWindow::updateQuestDetail()
    {
        const std::size_t selected = mQuestList->getIndexSelected();
        if (selected == MyGUI::ITEM_NONE || selected >= mVisibleQuests.size())
        {
            mQuestHeading->setCaption(tr("questman.no_selection"));
            mQuestDetail->setCaption("");
            mQuestIcon->setImageTexture("");
            mPinButton->setEnabled(false);
            mHideButton->setEnabled(false);
            updateServiceButtons();
            mQuestRelatedTopics.clear();
            mQuestRelatedList->removeAllItems();
            return;
        }
        QuestData& quest = mQuests[mVisibleQuests[selected]];
        mQuestHeading->setCaption(quest.mName);
        mQuestIcon->setImageTexture(quest.mIcon);
        std::ostringstream text;
        text << tr("questman.status") << ": " << (quest.mCompleted ? tr("questman.completed") : tr("questman.active")) << "\n";
        text << tr("questman.addon") << ": " << labelForAddon(quest.mAddon) << "\n";
        text << tr("questman.category") << ": " << labelForCategory(quest.mCategory) << "\n";
        if (!quest.mFaction.empty()) text << tr("questman.faction") << ": " << labelForFaction(quest.mFaction) << "\n";
        if (!quest.mGiver.empty()) text << tr("questman.giver") << ": " << quest.mGiver << "\n";
        text << "\n" << tr("questman.journal_entries") << "\n";
        if (quest.mEntries.empty()) text << tr("questman.no_entries") << "\n";
        for (const EntryData& entry : quest.mEntries)
        {
            if (!entry.mDate.empty()) text << "\n[" << entry.mDate << "]";
            if (!entry.mActor.empty()) text << "  " << entry.mActor;
            text << "\n" << sanitizeJournalText(entry.mText) << "\n";
        }
        if (getBool("technical info", false))
        {
            text << "\n" << tr("questman.technical") << "\n";
            text << "ID: " << join(quest.mIds, ", ") << "\n";
            text << "Stage: " << quest.mStage << "\n";
        }
        mQuestDetail->setCaption(text.str());
        updateQuestButtons();
        refreshQuestRelatedTopics();
    }

    void QuestManagerWindow::updateQuestButtons()
    {
        const std::size_t selected = mQuestList->getIndexSelected();
        if (selected == MyGUI::ITEM_NONE || selected >= mVisibleQuests.size()) return;
        const QuestData& quest = mQuests[mVisibleQuests[selected]];
        mPinButton->setEnabled(true);
        mHideButton->setEnabled(true);
        mPinButton->setCaption(quest.mPinned ? tr("questman.unpin") : tr("questman.pin"));
        mHideButton->setCaption(quest.mHidden ? tr("questman.unhide") : tr("questman.hide"));
        updateServiceButtons();
    }

    void QuestManagerWindow::updateTopicDetail()
    {
        const std::size_t selected = mTopicList->getIndexSelected();
        if (selected == MyGUI::ITEM_NONE || selected >= mVisibleTopics.size())
        {
            mTopicHeading->setCaption(tr("questman.no_selection"));
            mTopicDetail->setCaption("");
            mTopicRelatedTopics.clear();
            mTopicRelatedList->removeAllItems();
            return;
        }
        const TopicData& topic = mTopics[mVisibleTopics[selected]];
        mTopicHeading->setCaption(topic.mName);
        std::ostringstream text;
        bool hasVisibleContent = false;
        for (const EntryData& entry : topic.mEntries)
        {
            std::string entryText = entry.mText;
            if (entryText.empty())
                entryText = fallbackTopicEntryText(topic.mId, entry.mInfoId);
            entryText = trim(sanitizeJournalText(entryText));

            if (!entry.mActor.empty())
            {
                text << entry.mActor;
                if (!entryText.empty())
                    text << ":";
                text << "\n";
                hasVisibleContent = true;
            }

            if (!entryText.empty())
            {
                text << entryText << "\n\n";
                hasVisibleContent = true;
            }
        }

        if (!hasVisibleContent)
            text << tr("questman.no_entries");

        mTopicDetail->setCaption(text.str());
        refreshTopicRelatedTopics();
    }

    void QuestManagerWindow::updateRecordDetail()
    {
        const std::size_t selected = mRecordList->getIndexSelected();
        if (selected == MyGUI::ITEM_NONE || selected >= mVisibleRecords.size())
        {
            mRecordHeading->setCaption(tr("questman.no_selection"));
            mRecordDetail->setCaption("");
            return;
        }
        const RecordData& record = mRecords[mVisibleRecords[selected]];
        mRecordHeading->setCaption(record.mTitle);
        std::ostringstream text;
        if (!record.mEntry.mDate.empty()) text << record.mEntry.mDate << "\n";
        if (!record.mEntry.mActor.empty()) text << record.mEntry.mActor << "\n";
        if (!record.mEntry.mDate.empty() || !record.mEntry.mActor.empty()) text << "\n";
        text << sanitizeJournalText(record.mEntry.mText);
        if (getBool("technical info", false)) text << "\n\n" << tr("questman.technical") << "\nTopic: " << record.mTopic;
        mRecordDetail->setCaption(text.str());
    }

    void QuestManagerWindow::notifyClose(MyGUI::Widget*)
    {
        saveWindowGeometry();
        setVisible(false);
        if (mReturnToJournal)
            mReturnToJournal();
    }

    void QuestManagerWindow::notifyTabChanged(MyGUI::TabControl*, std::size_t index)
    {
        Settings::Manager::setInt("last tab", "Questman", static_cast<int>(index));
        refreshCurrentTab();
    }

    void QuestManagerWindow::notifySearchChanged(MyGUI::EditBox*)
    {
        refreshCurrentTab();
    }

    void QuestManagerWindow::notifyFilterChanged(MyGUI::ComboBox*, std::size_t index)
    {
        if (index < mFilters.size())
        {
            Settings::Manager::setInt("last filter axis", "Questman", static_cast<int>(mFilters[index].mAxis));
            Settings::Manager::setString("last filter value", "Questman", mFilters[index].mValue);
        }
        refreshQuests();
    }

    void QuestManagerWindow::notifyShowCompleted(MyGUI::Widget*)
    {
        mShowCompletedState = !mShowCompletedState;
        Settings::Manager::setBool("show completed", "Questman", mShowCompletedState);
        mShowCompleted->setStateSelected(mShowCompletedState);
        refreshQuests();
    }

    void QuestManagerWindow::notifyShowHidden(MyGUI::Widget*)
    {
        mShowHiddenState = !mShowHiddenState;
        Settings::Manager::setBool("show hidden", "Questman", mShowHiddenState);
        mShowHidden->setStateSelected(mShowHiddenState);
        refreshQuests();
    }

    void QuestManagerWindow::notifyQuestSelected(MyGUI::ListBox*, std::size_t)
    {
        const std::size_t selected = mQuestList->getIndexSelected();
        if (selected != MyGUI::ITEM_NONE && selected < mVisibleQuests.size())
        {
            mLastQuestId = lower(mQuests[mVisibleQuests[selected]].mId);
            Settings::Manager::setString("last quest", "Questman", mLastQuestId);
        }
        updateQuestDetail();
    }

    void QuestManagerWindow::notifyTopicSelected(MyGUI::ListBox*, std::size_t)
    {
        const std::size_t selected = mTopicList->getIndexSelected();
        if (selected != MyGUI::ITEM_NONE && selected < mVisibleTopics.size())
        {
            mLastTopicId = lower(mTopics[mVisibleTopics[selected]].mId);
            Settings::Manager::setString("last topic", "Questman", mLastTopicId);
        }
        updateTopicDetail();
    }

    void QuestManagerWindow::notifyRecordSelected(MyGUI::ListBox*, std::size_t)
    {
        const std::size_t selected = mRecordList->getIndexSelected();
        if (selected != MyGUI::ITEM_NONE && selected < mVisibleRecords.size())
        {
            mLastRecordTopic = lower(mRecords[mVisibleRecords[selected]].mTopic);
            Settings::Manager::setString("last record", "Questman", mLastRecordTopic);
        }
        updateRecordDetail();
    }

    void QuestManagerWindow::notifyPin(MyGUI::Widget*)
    {
        const std::size_t selected = mQuestList->getIndexSelected();
        if (selected == MyGUI::ITEM_NONE || selected >= mVisibleQuests.size()) return;
        QuestData& quest = mQuests[mVisibleQuests[selected]];
        const std::string key = lower(quest.mId);
        quest.mPinned = !quest.mPinned;
        if (quest.mPinned) mPinned.insert(key); else mPinned.erase(key);
        writeIdSet("pinned quests", mPinned);
        refreshQuests();
    }

    void QuestManagerWindow::notifyHide(MyGUI::Widget*)
    {
        const std::size_t selected = mQuestList->getIndexSelected();
        if (selected == MyGUI::ITEM_NONE || selected >= mVisibleQuests.size()) return;
        QuestData& quest = mQuests[mVisibleQuests[selected]];
        const std::string key = lower(quest.mId);
        quest.mHidden = !quest.mHidden;
        if (quest.mHidden) mHidden.insert(key); else mHidden.erase(key);
        writeIdSet("hidden quests", mHidden);
        refreshQuests();
    }

    void QuestManagerWindow::notifyQuestRelatedSelected(MyGUI::ListBox*, std::size_t index)
    {
        if (index == MyGUI::ITEM_NONE || index >= mQuestRelatedTopics.size())
            return;
        selectTopic(mQuestRelatedTopics[index]);
    }

    void QuestManagerWindow::notifyTopicRelatedSelected(MyGUI::ListBox*, std::size_t index)
    {
        if (index == MyGUI::ITEM_NONE || index >= mTopicRelatedTopics.size())
            return;
        selectTopic(mTopicRelatedTopics[index]);
    }

    void QuestManagerWindow::notifyWindowChanged(MyGUI::Window*)
    {
        if (mRestoringGeometry)
            return;
        const MyGUI::IntCoord c = mMainWidget->getCoord();
        if (c.width != 790 || c.height != 500)
        {
            mRestoringGeometry = true;
            mMainWidget->setCoord(MyGUI::IntCoord(c.left, c.top, 790, 500));
            mRestoringGeometry = false;
        }
        applyResponsiveLayout();
        saveWindowGeometry();
    }


    // ======================================================================
    // QM1: Quest Manager services paid with Skill Points.
    //
    // The server owns prices, points and persistence (server/scripts/
    // questmanService.lua). The client only offers what makes sense for the
    // selected quest, asks for a second click to confirm, and mirrors the
    // server's answer in the local journal / faction state.
    // ======================================================================

    QuestManagerWindow* QuestManagerWindow::sInstance = nullptr;

    namespace
    {
        struct ServiceConfig
        {
            bool mReceived = false;
            bool mEnabled = false;
            int mResetSmall = 5;
            int mResetLarge = 10;
            int mResetLargeFrom = 3;
            int mSkip = 10;
            int mSkipFaction = 15;
            int mSkipReputation = 2;
            int mClear = 10;
            int mLeave = 10;
            bool mSharedJournal = false;
            bool mSharedFactions = false;
        };

        ServiceConfig sServices;

        double nowSeconds()
        {
            return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        std::string servicePercentEncode(const std::string& value)
        {
            static const char* hex = "0123456789ABCDEF";
            std::string out;
            for (unsigned char c : value)
            {
                if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == ' ')
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

        std::string servicePercentDecode(const std::string& value)
        {
            std::string out;
            for (std::size_t i = 0; i < value.size(); ++i)
            {
                if (value[i] == '%' && i + 2 < value.size()
                    && std::isxdigit(static_cast<unsigned char>(value[i + 1]))
                    && std::isxdigit(static_cast<unsigned char>(value[i + 2])))
                {
                    out.push_back(static_cast<char>(std::strtol(value.substr(i + 1, 2).c_str(), nullptr, 16)));
                    i += 2;
                }
                else
                    out.push_back(value[i]);
            }
            return out;
        }

        std::vector<std::string> splitOn(const std::string& value, char separator)
        {
            std::vector<std::string> parts;
            std::string current;
            for (char c : value)
            {
                if (c == separator)
                {
                    parts.push_back(current);
                    current.clear();
                }
                else
                    current.push_back(c);
            }
            parts.push_back(current);
            return parts;
        }

        int toInt(const std::string& value, int fallback)
        {
            char* end = nullptr;
            const long parsed = std::strtol(value.c_str(), &end, 10);
            return (end != value.c_str()) ? static_cast<int>(parsed) : fallback;
        }

        /// "{0}", "{1}", ... placeholders in l10n strings.
        std::string formatText(std::string text, const std::vector<std::string>& args)
        {
            for (std::size_t i = 0; i < args.size(); ++i)
            {
                const std::string token = "{" + std::to_string(i) + "}";
                std::size_t pos = 0;
                while ((pos = text.find(token, pos)) != std::string::npos)
                {
                    text.replace(pos, token.size(), args[i]);
                    pos += args[i].size();
                }
            }
            return text;
        }

        std::string questDisplayName(const std::string& id)
        {
            const ESM::Dialogue* dialogue
                = MWBase::Environment::get().getWorld()->getStore().get<ESM::Dialogue>().search(id);
            if (dialogue != nullptr)
            {
                for (const ESM::DialInfo& info : dialogue->mInfo)
                    if (info.mQuestStatus == ESM::DialInfo::QS_Name && !info.mResponse.empty())
                        return info.mResponse;
            }
            return id;
        }

        std::string factionDisplayName(const std::string& id)
        {
            const ESM::Faction* faction
                = MWBase::Environment::get().getWorld()->getStore().get<ESM::Faction>().search(id);
            return faction != nullptr && !faction->mName.empty() ? faction->mName : id;
        }
    }

    QuestManagerWindow::~QuestManagerWindow()
    {
        if (sInstance == this)
            sInstance = nullptr;
    }

    int QuestManagerWindow::playerSkillPoints()
    {
        const MWWorld::Ptr player = MWMechanics::getPlayer();
        if (player.isEmpty() || !player.getClass().isNpc())
            return 0;
        return player.getClass().getNpcStats(player).getSkillPoints();
    }

    void QuestManagerWindow::sendServiceCommand(const std::vector<std::string>& fields)
    {
        if (!mwmp::Main::isInitialized())
            return;
        mwmp::LocalPlayer* localPlayer = mwmp::Main::get().getLocalPlayer();
        if (localPlayer == nullptr)
            return;
        std::string data = "QUESTMAN_CMD";
        for (const std::string& field : fields)
        {
            data.push_back('\t');
            data += servicePercentEncode(field);
        }
        localPlayer->guiMessageBox.id = mwmp::ServerQuestRegistry::TransportGuiId;
        localPlayer->guiMessageBox.type = mwmp::BasePlayer::GUIMessageBox::CustomMessageBox;
        localPlayer->guiMessageBox.label.clear();
        localPlayer->guiMessageBox.note.clear();
        localPlayer->guiMessageBox.buttons.clear();
        localPlayer->guiMessageBox.data = data;
        auto* packet = mwmp::Main::get().getNetworking()->getPlayerPacket(ID_GUI_MESSAGEBOX);
        packet->setPlayer(localPlayer);
        packet->Send();
    }

    const QuestManagerWindow::QuestData* QuestManagerWindow::selectedQuest() const
    {
        const std::size_t selected = mQuestList->getIndexSelected();
        if (selected == MyGUI::ITEM_NONE || selected >= mVisibleQuests.size())
            return nullptr;
        return &mQuests[mVisibleQuests[selected]];
    }

    QuestManagerWindow::SkipPlan QuestManagerWindow::planSkip(const QuestData& quest) const
    {
        SkipPlan plan;
        if (quest.mAddon == "server")
        {
            plan.mReason = "questman.svc.no_server";
            return plan;
        }
        if (quest.mCompleted)
        {
            plan.mReason = "questman.svc.no_completed";
            return plan;
        }
        MWBase::Journal* journal = MWBase::Environment::get().getJournal();
        const MWWorld::ESMStore& store = MWBase::Environment::get().getWorld()->getStore();
        // Try the alias that has progressed furthest first: its finishing
        // stage is the one this character is heading for.
        std::vector<std::pair<int, std::string>> ids;
        for (const std::string& id : quest.mIds)
            ids.emplace_back(journal->getJournalIndex(id), id);
        std::sort(ids.begin(), ids.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        for (const auto& candidate : ids)
        {
            const ESM::Dialogue* dialogue = store.get<ESM::Dialogue>().search(candidate.second);
            if (dialogue == nullptr)
                continue;
            int best = -1;
            for (const ESM::DialInfo& info : dialogue->mInfo)
            {
                if (info.mQuestStatus == ESM::DialInfo::QS_Finished && info.mData.mJournalIndex > candidate.first
                    && (best < 0 || info.mData.mJournalIndex < best))
                    best = info.mData.mJournalIndex;
            }
            if (best >= 0)
            {
                plan.mOk = true;
                plan.mId = candidate.second;
                plan.mIndex = best;
                return plan;
            }
        }
        plan.mReason = "questman.svc.no_finish";
        return plan;
    }

    int QuestManagerWindow::skipCost(const QuestData& quest) const
    {
        const bool faction = quest.mCategory == "faction" || quest.mCategory == "great_house";
        return faction ? sServices.mSkipFaction : sServices.mSkip;
    }

    int QuestManagerWindow::resetCost(const QuestData& quest) const
    {
        return static_cast<int>(quest.mEntries.size()) >= sServices.mResetLargeFrom
            ? sServices.mResetLarge : sServices.mResetSmall;
    }

    void QuestManagerWindow::setStatus(const std::string& text, bool error)
    {
        mStatusText->setCaption(text);
        mStatusText->setTextColour(error ? MyGUI::Colour(1.f, 0.55f, 0.45f) : MyGUI::Colour(0.86f, 0.80f, 0.62f));
    }

    bool QuestManagerWindow::confirmAction(const std::string& action, MyGUI::Button* button, int cost)
    {
        const double now = nowSeconds();
        if (mArmedAction == action && now - mArmedAt < 4.0)
        {
            mArmedAction.clear();
            return true;
        }
        mArmedAction = action;
        mArmedAt = now;
        button->setCaption(formatText(tr("questman.svc.confirm"), { std::to_string(cost) }));
        setStatus(formatText(tr("questman.svc.confirm_hint"), { std::to_string(cost), std::to_string(playerSkillPoints()) }), false);
        return false;
    }

    void QuestManagerWindow::onFrame(float)
    {
        if (!mArmedAction.empty() && nowSeconds() - mArmedAt >= 4.0)
        {
            mArmedAction.clear();
            updateServiceButtons();
            refreshServices();
        }
    }

    void QuestManagerWindow::updateServiceButtons()
    {
        const QuestData* quest = selectedQuest();
        const bool available = sServices.mReceived && sServices.mEnabled && !sServices.mSharedJournal;
        if (quest == nullptr)
        {
            mSkipButton->setEnabled(false);
            mResetButton->setEnabled(false);
            mSkipButton->setCaption(tr("questman.svc.skip"));
            mResetButton->setCaption(tr("questman.svc.reset"));
            return;
        }
        const SkipPlan plan = planSkip(*quest);
        const int skip = skipCost(*quest);
        const int reset = resetCost(*quest);
        const bool canReset = quest->mAddon != "server" && !quest->mCompleted;

        if (mArmedAction != "skip:" + quest->mId)
            mSkipButton->setCaption(formatText(tr("questman.svc.skip_cost"), { std::to_string(skip) }));
        if (mArmedAction != "reset:" + quest->mId)
            mResetButton->setCaption(formatText(tr("questman.svc.reset_cost"), { std::to_string(reset) }));
        mSkipButton->setEnabled(available && plan.mOk);
        mResetButton->setEnabled(available && canReset);

        const std::string skipTip = !available ? tr("questman.svc.unavailable")
            : (plan.mOk ? formatText(tr("questman.svc.skip_tip"), { std::to_string(skip), std::to_string(sServices.mSkipReputation) })
                        : tr(plan.mReason));
        const std::string resetTip = !available ? tr("questman.svc.unavailable")
            : (canReset ? formatText(tr("questman.svc.reset_tip"), { std::to_string(reset) })
                        : tr(quest->mAddon == "server" ? "questman.svc.no_server" : "questman.svc.no_reset_completed"));
        for (const auto& pair : { std::make_pair(mSkipButton, skipTip), std::make_pair(mResetButton, resetTip) })
        {
            pair.first->setUserString("ToolTipType", "Layout");
            pair.first->setUserString("ToolTipLayout", "TextToolTip");
            pair.first->setUserString("Caption_Text", pair.second);
        }
    }

    void QuestManagerWindow::refreshServices()
    {
        mServicesPoints->setCaption(formatText(tr("questman.svc.points"), { std::to_string(playerSkillPoints()) }));
        if (!sServices.mReceived || !sServices.mEnabled)
        {
            mServicesInfo->setCaption(tr("questman.svc.unavailable"));
            mClearJournalButton->setEnabled(false);
            mLeaveFactionButton->setEnabled(false);
        }
        else
        {
            mServicesInfo->setCaption(formatText(tr("questman.svc.info"), {
                std::to_string(sServices.mResetSmall), std::to_string(sServices.mResetLarge),
                std::to_string(sServices.mSkip), std::to_string(sServices.mSkipFaction),
                std::to_string(sServices.mSkipReputation), std::to_string(sServices.mClear),
                std::to_string(sServices.mLeave) }));
            mClearJournalButton->setEnabled(!sServices.mSharedJournal);
        }
        if (mArmedAction != "clear")
            mClearJournalButton->setCaption(formatText(tr("questman.svc.clear_cost"), { std::to_string(sServices.mClear) }));

        // Factions the character belongs to.
        const std::size_t previous = mFactionList->getIndexSelected();
        const std::string previousId = previous < mFactionIds.size() ? mFactionIds[previous] : std::string();
        mFactionList->removeAllItems();
        mFactionIds.clear();
        const MWWorld::Ptr player = MWMechanics::getPlayer();
        if (!player.isEmpty() && player.getClass().isNpc())
        {
            const MWWorld::ESMStore& store = MWBase::Environment::get().getWorld()->getStore();
            for (const auto& pair : player.getClass().getNpcStats(player).getFactionRanks())
            {
                std::string label = factionDisplayName(pair.first);
                const ESM::Faction* faction = store.get<ESM::Faction>().search(pair.first);
                if (faction != nullptr && pair.second >= 0 && pair.second < 10 && !faction->mRanks[pair.second].empty())
                    label += " — " + faction->mRanks[pair.second];
                mFactionList->addItem(label);
                mFactionIds.push_back(pair.first);
                if (Misc::StringUtils::ciEqual(pair.first, previousId))
                    mFactionList->setIndexSelected(mFactionIds.size() - 1);
            }
        }
        if (mFactionIds.empty())
            mFactionList->addItem(tr("questman.svc.no_factions"));
        const bool factionSelected = mFactionList->getIndexSelected() < mFactionIds.size();
        mLeaveFactionButton->setEnabled(sServices.mReceived && sServices.mEnabled && !sServices.mSharedFactions
            && factionSelected);
        if (mArmedAction.rfind("leave:", 0) != 0)
            mLeaveFactionButton->setCaption(formatText(tr("questman.svc.leave_cost"), { std::to_string(sServices.mLeave) }));
    }

    void QuestManagerWindow::notifyFactionSelected(MyGUI::ListBox*, std::size_t)
    {
        mArmedAction.clear();
        refreshServices();
    }

    void QuestManagerWindow::notifySkipQuest(MyGUI::Widget*)
    {
        const QuestData* quest = selectedQuest();
        if (quest == nullptr)
            return;
        const SkipPlan plan = planSkip(*quest);
        if (!plan.mOk)
            return;
        const int cost = skipCost(*quest);
        if (playerSkillPoints() < cost)
        {
            setStatus(formatText(tr("questman.svc.err.points"), { std::to_string(cost), std::to_string(playerSkillPoints()) }), true);
            return;
        }
        if (!confirmAction("skip:" + quest->mId, mSkipButton, cost))
            return;
        setStatus(tr("questman.svc.sending"), false);
        sendServiceCommand({ "skip", plan.mId, std::to_string(plan.mIndex) });
        updateServiceButtons();
    }

    void QuestManagerWindow::notifyResetQuest(MyGUI::Widget*)
    {
        const QuestData* quest = selectedQuest();
        if (quest == nullptr || quest->mAddon == "server" || quest->mCompleted)
            return;
        const int cost = resetCost(*quest);
        if (playerSkillPoints() < cost)
        {
            setStatus(formatText(tr("questman.svc.err.points"), { std::to_string(cost), std::to_string(playerSkillPoints()) }), true);
            return;
        }
        if (!confirmAction("reset:" + quest->mId, mResetButton, cost))
            return;
        std::string ids;
        for (const std::string& id : quest->mIds)
        {
            if (!ids.empty())
                ids += ",";
            ids += id;
        }
        setStatus(tr("questman.svc.sending"), false);
        sendServiceCommand({ "reset", ids });
        updateServiceButtons();
    }

    void QuestManagerWindow::notifyClearJournal(MyGUI::Widget*)
    {
        if (playerSkillPoints() < sServices.mClear)
        {
            setStatus(formatText(tr("questman.svc.err.points"), { std::to_string(sServices.mClear), std::to_string(playerSkillPoints()) }), true);
            return;
        }
        if (!confirmAction("clear", mClearJournalButton, sServices.mClear))
            return;
        // Finished quests keep their finishing entry on the server (so they stay
        // completed after a relog); the server hides its text from then on.
        std::string finished;
        MWBase::Journal* journal = MWBase::Environment::get().getJournal();
        for (auto it = journal->questBegin(); it != journal->questEnd(); ++it)
        {
            if (!it->second.isFinished())
                continue;
            if (!finished.empty())
                finished += ";";
            finished += it->first + ":" + std::to_string(it->second.getIndex());
        }
        setStatus(tr("questman.svc.sending"), false);
        sendServiceCommand({ "clear", finished });
        refreshServices();
    }

    void QuestManagerWindow::notifyLeaveFaction(MyGUI::Widget*)
    {
        const std::size_t selected = mFactionList->getIndexSelected();
        if (selected >= mFactionIds.size())
            return;
        if (playerSkillPoints() < sServices.mLeave)
        {
            setStatus(formatText(tr("questman.svc.err.points"), { std::to_string(sServices.mLeave), std::to_string(playerSkillPoints()) }), true);
            return;
        }
        if (!confirmAction("leave:" + mFactionIds[selected], mLeaveFactionButton, sServices.mLeave))
            return;
        setStatus(tr("questman.svc.sending"), false);
        sendServiceCommand({ "leave", mFactionIds[selected] });
    }

    void QuestManagerWindow::handleServiceMessage(const std::string& payload)
    {
        const std::vector<std::string> raw = splitOn(payload, '\t');
        if (raw.size() < 2 || raw[0] != "QUESTMAN")
            return;
        const std::string& kind = raw[1];
        std::vector<std::string> f;
        for (std::size_t i = 2; i < raw.size(); ++i)
            f.push_back(servicePercentDecode(raw[i]));
        const auto field = [&f](std::size_t i) { return i < f.size() ? f[i] : std::string(); };

        MWDialogue::Journal* journal = dynamic_cast<MWDialogue::Journal*>(MWBase::Environment::get().getJournal());
        std::string status;
        bool error = false;
        bool rebuild = false;

        if (kind == "config")
        {
            sServices.mReceived = true;
            sServices.mEnabled = toInt(field(0), 0) != 0;
            sServices.mResetSmall = toInt(field(1), 5);
            sServices.mResetLarge = toInt(field(2), 10);
            sServices.mResetLargeFrom = toInt(field(3), 3);
            sServices.mSkip = toInt(field(4), 10);
            sServices.mSkipFaction = toInt(field(5), 15);
            sServices.mSkipReputation = toInt(field(6), 2);
            sServices.mClear = toInt(field(7), 10);
            sServices.mLeave = toInt(field(8), 10);
            sServices.mSharedJournal = toInt(field(9), 0) != 0;
            sServices.mSharedFactions = toInt(field(10), 0) != 0;
        }
        else if (kind == "hide")
        {
            // Raw field: "quest:index;quest:index" with each quest id encoded.
            if (journal != nullptr && raw.size() > 2)
            {
                for (const std::string& pair : splitOn(raw[2], ';'))
                {
                    const std::size_t colon = pair.rfind(':');
                    if (colon == std::string::npos)
                        continue;
                    journal->hideEntry(servicePercentDecode(pair.substr(0, colon)), toInt(pair.substr(colon + 1), -1));
                }
            }
            rebuild = true;
        }
        else if (kind == "skip_ok")
        {
            const std::string quest = field(0);
            const int index = toInt(field(1), -1);
            const MWWorld::Ptr player = MWMechanics::getPlayer();
            try
            {
                // Same path as finishing the quest in dialogue: local entry
                // (XP for completion) + PlayerJournal packet to the server.
                MWBase::Journal* baseJournal = MWBase::Environment::get().getJournal();
                if (mwmp::Main::isInitialized() && mwmp::Main::get().getLocalPlayer() != nullptr
                    && !baseJournal->hasEntry(quest, index))
                    mwmp::Main::get().getLocalPlayer()->sendJournalEntry(quest, index, player);
                baseJournal->addEntry(quest, index, player);
            }
            catch (const std::exception& e)
            {
                Log(Debug::Warning) << "Questman: could not write the skipped quest's entry: " << e.what();
            }
            status = formatText(tr("questman.svc.ok.skip"), { questDisplayName(quest), field(2), field(3) });
            if (toInt(field(4), 0) > 0)
                status += " " + formatText(tr("questman.svc.ok.reputation"), { field(4), factionDisplayName(field(5)) });
            rebuild = true;
        }
        else if (kind == "reset_ok")
        {
            const std::vector<std::string> ids = splitOn(field(0), ',');
            if (journal != nullptr)
                for (const std::string& id : ids)
                    journal->resetQuest(id);
            status = formatText(tr("questman.svc.ok.reset"), { ids.empty() ? std::string() : questDisplayName(ids.front()),
                field(1), field(2) });
            rebuild = true;
        }
        else if (kind == "clear_ok")
        {
            if (journal != nullptr)
                journal->clearJournalText();
            status = formatText(tr("questman.svc.ok.clear"), { field(0), field(1) });
            rebuild = true;
        }
        else if (kind == "leave_ok")
        {
            const std::string faction = field(0);
            const MWWorld::Ptr player = MWMechanics::getPlayer();
            if (!player.isEmpty() && player.getClass().isNpc())
            {
                MWMechanics::NpcStats& stats = player.getClass().getNpcStats(player);
                std::string key;
                for (const auto& pair : stats.getFactionRanks())
                    if (Misc::StringUtils::ciEqual(pair.first, faction))
                        key = pair.first;
                // NpcStats::lowerRank removes the membership once the rank
                // drops below zero; the faction packet cannot express that.
                for (int guard = 0; !key.empty() && stats.isInFaction(key) && guard < 16; ++guard)
                    stats.lowerRank(key);
                stats.clearExpelled(key.empty() ? faction : key);
                stats.setFactionReputation(key.empty() ? Misc::StringUtils::lowerCase(faction) : key, 0);
            }
            status = formatText(tr("questman.svc.ok.leave"), { factionDisplayName(faction), field(1), field(2) });
        }
        else if (kind == "error")
        {
            error = true;
            const std::string key = field(0);
            if (key == "points")
                status = formatText(tr("questman.svc.err.points"), { field(1), field(2) });
            else
                status = tr("questman.svc.err." + (key.empty() ? std::string("bad") : key));
        }

        if (sInstance == nullptr)
            return;
        sInstance->mArmedAction.clear();
        if (!status.empty())
            sInstance->setStatus(status, error);
        if (sInstance->isVisible())
        {
            if (rebuild)
                sInstance->rebuildData();
            sInstance->refreshCurrentTab();
            sInstance->updateServiceButtons();
        }
    }

}
