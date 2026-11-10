#include <cctype>
#include "apps/openmw/mwbase/mechanicsmanager.hpp"
#include "apps/openmw/mwmechanics/creaturestats.hpp"
#include "apps/openmw/mwmechanics/actorutil.hpp"
#include "apps/openmw/mwmechanics/npcstats.hpp"
#include "apps/openmw/mwmechanics/aisequence.hpp"
#include "apps/openmw/mwworld/class.hpp"
#include "GUIChat.hpp"
#include "apps/openmw/mwworld/actionteleport.hpp"
#include "apps/openmw/mwworld/cellstore.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <sstream>

#include <MyGUI_Button.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_FontManager.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_LanguageManager.h>
#include <MyGUI_ListBox.h>
#include <MyGUI_RenderManager.h>
#include <MyGUI_ResourceManager.h>
#include <MyGUI_ScrollBar.h>
#include <MyGUI_TextBox.h>
#include <MyGUI_UString.h>
#include <MyGUI_Widget.h>

#include "apps/openmw/mwbase/environment.hpp"
#include "apps/openmw/mwbase/world.hpp"
#include "apps/openmw/mwgui/windowmanagerimp.hpp"
#include "apps/openmw/mwgui/trainingwindow.hpp"
#include "apps/openmw/mwinput/inputmanagerimp.hpp"
#include "apps/openmw/mwmechanics/xpserverbridge.hpp"
#include <components/openmw-mp/TimedLog.hpp>
#include <components/misc/constants.hpp>
#include <components/settings/settings.hpp>

#include "../Networking.hpp"
#include "../Main.hpp"
#include "../ArenaHousing.hpp"
#include "../VoiceChat.hpp"
#include "../LocalPlayer.hpp"
#include "../GUIController.hpp"

namespace
{
    constexpr float sFullyVisibleAlpha = 1.f;
    constexpr float sThirtyPercentTransparentAlpha = 0.7f;
    constexpr float sSixtyPercentTransparentAlpha = 0.4f;
    constexpr float sFadeSpeed = 4.f;
    constexpr float sGeometrySaveDebounce = 0.45f;
    constexpr float sMenuBackgroundAlpha = 0.92f;
    constexpr int sMinimumPanelWidth = 800;
    constexpr int sMinimumPanelHeight = 500;
    constexpr int sButtonPadding = 22;
    constexpr int sMinimumButtonWidth = 34;
    constexpr int sRowGap = 6;
    constexpr int sToolbarTop = 44;
    // X054: the toolbar carries coreChat's five channels and four styles now,
    // so it is two rows tall. sToolbarHeight is one row; the container gets
    // sToolbarRows of them.
    constexpr int sToolbarHeight = 30;
    constexpr int sToolbarRows = 2;
    constexpr int sDrawerTop = sToolbarTop + sToolbarHeight * sToolbarRows + 4;
    constexpr int sDrawerHeight = 68;
    constexpr int sColorDrawerHeight = 96;
    constexpr int sSideMargin = 10;
    constexpr const char* sHudFont = "Russo";
    constexpr const char* sMenuFont = "DejaVuLGCSansMono";
    constexpr const char* sChatFontResource = "ArenaMPChatColor.xml";

    std::string localizeArena(const std::string& key)
    {
        return MyGUI::LanguageManager::getInstance().replaceTags("#{arenamp=" + key + "}");
    }

    // X052: quick-insert palettes.
    //
    // The Unicode column only ever reaches the screen when the player opted in
    // to a font that actually carries those glyphs (see [Chat] emoji font).
    // The ASCII column is the default and is guaranteed to render in every
    // font OpenMW ships, which is what removes the empty "tofu" boxes.
    struct EmojiSlot
    {
        const char* unicode;
        const char* ascii;
    };

    const EmojiSlot sEmojiPalette[] = {
        { "\xE2\x98\xBA", ":)"   }, { "\xE2\x98\xB9", ":("   },
        { "\xF0\x9F\x98\x8A", ":D" }, { "\xF0\x9F\x98\x89", ";)" },
        { "\xF0\x9F\x98\x8B", ":P" }, { "\xF0\x9F\x98\x82", "xD" },
        { "\xF0\x9F\x98\xA2", ":'(" }, { "\xF0\x9F\x98\xA1", ">:(" },
        { "\xE2\x99\xA5", "<3"   }, { "\xF0\x9F\x92\x94", "</3" },
        { "\xF0\x9F\x91\x8D", "(y)" }, { "\xF0\x9F\x91\x8E", "(n)" },
        { "\xE2\x9C\x94", "(v)"  }, { "\xE2\x9D\x8C", "(x)"  },
        { "\xE2\xAD\x90", "*"    }, { "\xE2\x9A\x94", "><>"  },
        { "\xF0\x9F\x8D\xBA", "[o]" }, { "\xF0\x9F\x94\xA5", "(f)" },
        { "\xF0\x9F\x8E\xB5", "(m)" }, { "\xF0\x9F\x92\xA4", "zZz" }
    };

    std::size_t utf8Length(const std::string& text)
    {
        std::size_t count = 0;
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            const unsigned char c = static_cast<unsigned char>(text[i]);
            if (c < 0x80 || c >= 0xC0)
                ++count;
        }
        return count;
    }

    std::string utf8Truncate(const std::string& text, std::size_t codePoints)
    {
        std::size_t count = 0;
        std::size_t i = 0;
        while (i < text.size() && count < codePoints)
        {
            const unsigned char c = static_cast<unsigned char>(text[i]);
            std::size_t length = 1;
            if (c >= 0xF0) length = 4;
            else if (c >= 0xE0) length = 3;
            else if (c >= 0xC0) length = 2;
            i += length;
            ++count;
        }
        return text.substr(0, std::min(i, text.size()));
    }

    int measuredCaptionWidth(MyGUI::Widget* widget)
    {
        if (widget == nullptr)
            return 0;
        MyGUI::TextBox* text = widget->castType<MyGUI::TextBox>(false);
        return text != nullptr ? text->getTextSize().width : 0;
    }

    void setWidgetCaption(MyGUI::Widget* widget, const std::string& caption)
    {
        if (widget == nullptr)
            return;

        if (MyGUI::Button* button = widget->castType<MyGUI::Button>(false))
        {
            button->setCaption(caption);
            return;
        }
        if (MyGUI::TextBox* text = widget->castType<MyGUI::TextBox>(false))
        {
            text->setCaption(caption);
            return;
        }
        if (MyGUI::EditBox* edit = widget->castType<MyGUI::EditBox>(false))
            edit->setCaption(caption);
    }

    // "#RRGGBB" as used by the server's color table.
    bool parseHexColour(const std::string& value, unsigned int& out)
    {
        const std::size_t offset = (!value.empty() && value[0] == '#') ? 1u : 0u;
        if (value.size() < offset + 6)
            return false;
        unsigned int result = 0;
        for (std::size_t i = 0; i < 6; ++i)
        {
            const char c = value[offset + i];
            unsigned int digit = 0;
            if (c >= '0' && c <= '9') digit = static_cast<unsigned int>(c - '0');
            else if (c >= 'a' && c <= 'f') digit = static_cast<unsigned int>(c - 'a') + 10u;
            else if (c >= 'A' && c <= 'F') digit = static_cast<unsigned int>(c - 'A') + 10u;
            else return false;
            result = (result << 4) | digit;
        }
        out = result;
        return true;
    }

    MyGUI::Colour toMyGuiColour(unsigned int rgb)
    {
        return MyGUI::Colour(
            static_cast<float>((rgb >> 16) & 0xFF) / 255.f,
            static_cast<float>((rgb >> 8) & 0xFF) / 255.f,
            static_cast<float>(rgb & 0xFF) / 255.f);
    }

    std::vector<std::string> splitControlFields(const std::string& value, char delimiter)
    {
        std::vector<std::string> result;
        std::string current;
        for (char c : value)
        {
            if (c == delimiter)
            {
                result.push_back(current);
                current.clear();
            }
            else if (c != '\r' && c != '\n')
                current.push_back(c);
        }
        result.push_back(current);
        return result;
    }

    std::string unescapeControlField(const std::string& value)
    {
        std::string result;
        result.reserve(value.size());
        for (std::size_t i = 0; i < value.size(); ++i)
        {
            if (value[i] != '\\' || i + 1 >= value.size())
            {
                result.push_back(value[i]);
                continue;
            }
            const char next = value[++i];
            if (next == 'n') result.push_back('\n');
            else if (next == 't') result.push_back('\t');
            else result.push_back(next);
        }
        return result;
    }

    bool controlBool(const std::vector<std::string>& fields, std::size_t index)
    {
        return index < fields.size() && fields[index] == "1";
    }

    float moveTowards(float value, float target, float maximumDelta)
    {
        if (value < target)
            return std::min(value + maximumDelta, target);
        return std::max(value - maximumDelta, target);
    }
}

namespace mwmp
{
    GUIChat::GUIChat(int x, int y, int w, int h)
        : WindowBase("tes3mp_chat.layout")
        , mCommandLine(nullptr)
        , mHistory(nullptr)
        , mHistoryScroll(nullptr)
        , mCommandScroll(nullptr)
        , mPanelBackground(nullptr)
        , mDragHandle(nullptr)
        , mLayoutFrame(nullptr)
        , mLayoutDrag(nullptr)
        , mLayoutResize(nullptr)
        , mChatToolbar(nullptr)
        , mEmojiBar(nullptr)
        , mColorBar(nullptr)
        , mGroupPane(nullptr)
        , mPlayersPane(nullptr)
        , mProfilePane(nullptr)
        , mPlayersActionRow(nullptr)
        , mProfileActionRow(nullptr)
        , mProfileActionRow2(nullptr)
        , mGroupActionRow1(nullptr)
        , mGroupActionRow2(nullptr)
        , mGroupActionRow3(nullptr)
        , mGroupInfo(nullptr)
        , mPlayersInfo(nullptr)
        , mProfileInfo(nullptr)
        , mGroupRoster(nullptr)
        , mPlayersList(nullptr)
        , mGroupRosterLabel(nullptr)
        , mGroupNameLabel(nullptr)
        , mGroupTargetLabel(nullptr)
        , mColorBarLabel(nullptr)
        , mGroupNameEdit(nullptr)
        , mGroupTargetEdit(nullptr)
        , mTabChat(nullptr)
        , mTabGroup(nullptr)
        , mTabPlayers(nullptr)
        , mTabStats(nullptr)
        , mPlayersRefreshButton(nullptr)
        , mPlayersOpenListButton(nullptr)
        , mPlayersInviteButton(nullptr)
        , mProfileRefreshButton(nullptr)
        , mProfileFixButton(nullptr)
        , mProfileReturnsButton(nullptr)
        , mProfileDeleteButton(nullptr)
        , mModeOoc(nullptr)
        , mModeRp(nullptr)
        , mChannelSay(nullptr)
        , mChannelWhisper(nullptr)
        , mChannelShout(nullptr)
        , mChannelLocalOoc(nullptr)
        , mChannelGlobalOoc(nullptr)
        , mStylePlain(nullptr)
        , mStyleMe(nullptr)
        , mStyleDo(nullptr)
        , mStyleTry(nullptr)
        , mStayOpenButton(nullptr)
        , mSendButton(nullptr)
        , mReturnButton(nullptr)
        , mEmojiToggleButton(nullptr)
        , mColorToggleButton(nullptr)
        , mEmojiButtons{}
        , mColorButtons{}
        , mGroupCreateButton(nullptr)
        , mGroupRefreshButton(nullptr)
        , mGroupInviteButton(nullptr)
        , mGroupLeaveButton(nullptr)
        , mGroupDisbandButton(nullptr)
        , mGroupKickButton(nullptr)
        , mGroupLeaderButton(nullptr)
        , mGroupJournalButton(nullptr)
        , mGroupTopicsButton(nullptr)
        , mGroupAcceptButton(nullptr)
        , mGroupDeclineButton(nullptr)
        , windowState(CHAT_TRANSPARENT_30)
        , chatChannel(CHANNEL_SAY)
        , chatStyle(STYLE_PLAIN)
        , rememberedPrefix()
        , activeTab(TAB_CHAT)
        , rpMode(false)
        , stayOpenAfterSend(false)
        , activeDrawer(DRAWER_NONE)
        , emojiFontName()
        , menuFontName()
        , groupRosterNames()
        , playerListNames()
        , playerListDetails()
        , colorPalette()
        , colorNames()
        , selectedColorIndex(-1)
        , menuCaptions()
        , groupInGroup(false)
        , groupIsLeader(false)
        , groupJournalSync(true)
        , groupTopicSync(true)
        , groupPendingInvite(false)
        , editState(false)
        , menuState(false)
        , sayKeyHeld(false)
        , applyingRememberedPrefix(false)
        , historyReviewState(false)
        , mainMenuOpen(false)
        , historyDisplayEnabled(true)
        , externalHistoryDisplayEnabled(true)
        , hideAfterFade(false)
        , geometryDirty(false)
        , geometrySaveDelay(0.f)
        , delay(3.f)
        , revealTime(0.f)
        , currentAlpha(sThirtyPercentTransparentAlpha)
        , targetAlpha(sThirtyPercentTransparentAlpha)
        , geometryDragMode(GEOMETRY_DRAG_NONE)
        , dragStartMouse(0, 0)
        , dragStartWindow(0, 0)
        , dragStartSize(0, 0)
        , hudCoord(x, y, std::max(240, w), std::max(120, h))
        , panelCoord(Settings::Manager::getInt("menu x", "Chat"), Settings::Manager::getInt("menu y", "Chat"),
            std::max(sMinimumPanelWidth, Settings::Manager::getInt("menu w", "Chat")),
            std::max(sMinimumPanelHeight, Settings::Manager::getInt("menu h", "Chat")))
    {
        // HUD and expanded player menu have independent saved rectangles.
        setCoord(hudCoord.left, hudCoord.top, hudCoord.width, hudCoord.height);

        getWidget(mCommandLine, "edit_Command");
        getWidget(mHistory, "list_History");

        for (size_t i = 0; i < mHistory->getChildCount(); ++i)
        {
            if (MyGUI::ScrollBar* scroll = mHistory->getChildAt(i)->castType<MyGUI::ScrollBar>(false))
            {
                mHistoryScroll = scroll;
                break;
            }
        }
        if (mHistoryScroll)
        {
            mHistoryScroll->setVisible(false);
            mHistoryScroll->setNeedMouseFocus(false);
        }

        for (size_t i = 0; i < mCommandLine->getChildCount(); ++i)
        {
            if (MyGUI::ScrollBar* scroll = mCommandLine->getChildAt(i)->castType<MyGUI::ScrollBar>(false))
            {
                mCommandScroll = scroll;
                break;
            }
        }
        if (mCommandScroll)
        {
            mCommandScroll->setVisible(false);
            mCommandScroll->setNeedMouseFocus(false);
        }

        mCommandLine->eventEditSelectAccept += MyGUI::newDelegate(this, &GUIChat::acceptCommand);
        mCommandLine->eventKeyButtonPressed += MyGUI::newDelegate(this, &GUIChat::keyPress);
        mCommandLine->eventEditTextChange += MyGUI::newDelegate(this, &GUIChat::commandTextChanged);
        mCommandLine->setEditMultiLine(true);
        mCommandLine->setEditWordWrap(true);
        mCommandLine->setOverflowToTheLeft(false);

        setTitle("ArenaMP");

        // Keep the full session; EditBox defaults must not truncate the log.
        mHistory->setMaxTextLength(std::numeric_limits<size_t>::max());
        mHistory->setOverflowToTheLeft(false);
        mHistory->setEditWordWrap(true);
        mHistory->setEditReadOnly(true);
        mHistory->setTextShadow(true);
        mHistory->setTextShadowColour(MyGUI::Colour::Black);
        mHistory->setNeedKeyFocus(false);
        mHistory->setFontName(sHudFont);
        mCommandLine->setFontName(sMenuFont);
        // Arena Y011: the text the local player is typing must stay more
        // legible than the passive history on both desktop and Android.
        mCommandLine->setFontHeight(14);
        mCommandLine->setTextColour(MyGUI::Colour::White);

        setupPlayerMenu();
        syncSettings();

        const MyGUI::IntSize view = MyGUI::RenderManager::getInstance().getViewSize();
        applyHudGeometry(view.width, view.height);

        mCommandLine->setVisible(false);
        updateCommandLineLayout();
        refreshPlayerMenu();
        applyAlpha(currentAlpha);
    }

    void GUIChat::setupPlayerMenu()
    {
        getWidget(mPanelBackground, "PanelBackground");
        getWidget(mDragHandle, "DragHandle");
        getWidget(mLayoutFrame, "LayoutFrame");
        getWidget(mLayoutDrag, "LayoutDrag");
        getWidget(mLayoutResize, "LayoutResize");
        MyGUI::TextBox* layoutTitle;
        getWidget(layoutTitle, "LayoutTitle");
        layoutTitle->setCaption(localizeArena("chat.layout.edit"));
        // U024e: use dedicated, unmistakable mouse surfaces instead of the
        // decorative frame itself. A MyGUI Button reliably receives press and
        // capture events across the desktop forks; the frame is visual only.
        for (MyGUI::Widget* handle : {mLayoutDrag, mLayoutResize})
        {
            handle->eventMouseButtonPressed += MyGUI::newDelegate(this, &GUIChat::onDragStart);
            handle->eventMouseDrag += MyGUI::newDelegate(this, &GUIChat::onDrag);
            handle->eventMouseButtonReleased += MyGUI::newDelegate(this, &GUIChat::onGeometryReleased);
        }
        mHistory->eventKeyButtonPressed += MyGUI::newDelegate(this, &GUIChat::keyPress);
        getWidget(mChatToolbar, "ChatToolbar");
        getWidget(mEmojiBar, "EmojiBar");
        getWidget(mColorBar, "ColorBar");
        getWidget(mColorBarLabel, "ColorBarLabel");
        getWidget(mGroupPane, "GroupPane");
        getWidget(mPlayersPane, "PlayersPane");
        getWidget(mProfilePane, "ProfilePane");
        // Z005: housing page widgets.
        getWidget(mHomePane, "HomePane");
        getWidget(mHomeTitle, "HomeTitle");
        getWidget(mHomeInfo, "HomeInfo");
        getWidget(mHomeActionRow1, "HomeActionRow1");
        getWidget(mHomeActionRow2, "HomeActionRow2");
        getWidget(mHomeActionRow3, "HomeActionRow3");
        getWidget(mHomeActionRow4, "HomeActionRow4");
        getWidget(mHomeRenameButton, "HomeRenameButton");
        getWidget(mHomeLockButton, "HomeLockButton");
        getWidget(mHomeTakeButton, "HomeTakeButton");
        getWidget(mHomeKindHouseButton, "HomeKindHouseButton");
        getWidget(mHomeKindTavernButton, "HomeKindTavernButton");
        getWidget(mHomeKindShopButton, "HomeKindShopButton");
        getWidget(mHomeKindGuildButton, "HomeKindGuildButton");
        getWidget(mHomeCoownerButton, "HomeCoownerButton");
        getWidget(mHomeFriendButton, "HomeFriendButton");
        getWidget(mHomeRevokeButton, "HomeRevokeButton");
        getWidget(mHomeResetButton, "HomeResetButton");
        getWidget(mHomeGiftButton, "HomeGiftButton");
        getWidget(mHomeSellButton, "HomeSellButton");
        getWidget(mHomeMarketButton, "HomeMarketButton");
        getWidget(mPlayersActionRow, "PlayersActionRow");
        getWidget(mProfileActionRow, "ProfileActionRow");
        getWidget(mProfileActionRow2, "ProfileActionRow2");
        getWidget(mPlayersList, "PlayersList");
        getWidget(mPlayersInfo, "PlayersInfo");
        getWidget(mProfileInfo, "ProfileInfo");
        getWidget(mPlayersRefreshButton, "PlayersRefreshButton");
        getWidget(mPlayersOpenListButton, "PlayersOpenListButton");
        getWidget(mPlayersInviteButton, "PlayersInviteButton");
        getWidget(mProfileRefreshButton, "ProfileRefreshButton");
        getWidget(mProfileFixButton, "ProfileFixButton");
        getWidget(mProfileReturnsButton, "ProfileReturnsButton");
        getWidget(mProfileDeleteButton, "ProfileDeleteButton");
        getWidget(mGroupActionRow1, "GroupActionRow1");
        getWidget(mGroupActionRow2, "GroupActionRow2");
        getWidget(mGroupActionRow3, "GroupActionRow3");
        getWidget(mGroupRoster, "GroupRoster");
        getWidget(mGroupRosterLabel, "GroupRosterLabel");

        getWidget(mTabChat, "TabChat");
        getWidget(mTabGroup, "TabGroup");
        getWidget(mTabPlayers, "TabPlayers");
        getWidget(mTabStats, "TabStats");
        getWidget(mTabHome, "TabHome");
        getWidget(mTabDecor, "TabDecor");
        getWidget(mDecorPane, "DecorPane");
        getWidget(mDecorTitle, "DecorTitle");
        getWidget(mDecorList, "DecorList");
        getWidget(mDecorBack, "DecorBack");
        getWidget(mDecorOpen, "DecorOpen");
        getWidget(mDecorCapture, "DecorCapture");
        getWidget(mDecorRefresh, "DecorRefresh");
        getWidget(mModeOoc, "ModeOoc");
        getWidget(mModeRp, "ModeRp");
        // X054: the three layout slots become Say / local OOC / global OOC;
        // whisper, shout and /try are created below so the .layout file does
        // not have to grow a widget per coreChat feature.
        getWidget(mChannelSay, "ChannelDefault");
        getWidget(mChannelLocalOoc, "ChannelLocal");
        getWidget(mChannelGlobalOoc, "ChannelGlobal");
        getWidget(mStylePlain, "StylePlain");
        getWidget(mStyleMe, "StyleMe");
        getWidget(mStyleDo, "StyleDo");
        getWidget(mStayOpenButton, "StayOpenButton");
        getWidget(mSendButton, "SendButton");
        getWidget(mReturnButton, "ReturnButton");
        getWidget(mEmojiToggleButton, "EmojiToggle");
        getWidget(mColorToggleButton, "ColorToggle");
        for (int i = 0; i < sEmojiSlotCount; ++i)
            getWidget(mEmojiButtons[i], "Emoji" + std::to_string(i + 1));

        // The layout ships sLayoutColorSlotCount swatches; coreChat has 40, so
        // the remainder are created here. Widgets made in code sit in the same
        // parent and take part in applyMenuLayout() exactly like XML ones.
        // Note the split: the first block must go through getWidget(), because
        // WindowBase prefixes layout widget names at load time.
        for (int i = 0; i < sLayoutColorSlotCount; ++i)
            getWidget(mColorButtons[i], "Color" + std::to_string(i + 1));
        for (int i = sLayoutColorSlotCount; i < sColorSlotCount; ++i)
        {
            mColorButtons[i] = mColorBar->createWidget<MyGUI::Button>("MW_Button",
                MyGUI::IntCoord(6, 28, 38, 26), MyGUI::Align::Default);
        }

        // Whisper, shout and /try have no slot in the .layout file. They are
        // created as ordinary children of the toolbar, which is enough for
        // applyMenuLayout() to place them like any XML-declared button.
        mChannelWhisper = mChatToolbar->createWidget<MyGUI::Button>("MW_Button",
            MyGUI::IntCoord(0, 2, 60, 26), MyGUI::Align::Default);
        mChannelShout = mChatToolbar->createWidget<MyGUI::Button>("MW_Button",
            MyGUI::IntCoord(0, 2, 60, 26), MyGUI::Align::Default);
        mStyleTry = mChatToolbar->createWidget<MyGUI::Button>("MW_Button",
            MyGUI::IntCoord(0, 32, 60, 26), MyGUI::Align::Default);

        getWidget(mGroupInfo, "GroupInfo");
        getWidget(mGroupNameLabel, "GroupNameLabel");
        getWidget(mGroupTargetLabel, "GroupTargetLabel");
        getWidget(mGroupNameEdit, "GroupNameEdit");
        getWidget(mGroupTargetEdit, "GroupTargetEdit");
        getWidget(mGroupCreateButton, "GroupCreateButton");
        getWidget(mGroupRefreshButton, "GroupRefreshButton");
        getWidget(mGroupVoiceButton, "GroupVoiceButton");
        getWidget(mGroupInviteButton, "GroupInviteButton");
        getWidget(mGroupLeaveButton, "GroupLeaveButton");
        getWidget(mGroupDisbandButton, "GroupDisbandButton");
        getWidget(mGroupKickButton, "GroupKickButton");
        getWidget(mGroupLeaderButton, "GroupLeaderButton");
        getWidget(mGroupJournalButton, "GroupJournalButton");
        getWidget(mGroupTopicsButton, "GroupTopicsButton");
        getWidget(mGroupAcceptButton, "GroupAcceptButton");
        getWidget(mGroupDeclineButton, "GroupDeclineButton");

        MyGUI::TextBox* title = nullptr;
        MyGUI::TextBox* groupTitle = nullptr;
        MyGUI::TextBox* playersTitle = nullptr;
        MyGUI::TextBox* profileTitle = nullptr;
        getWidget(title, "PlayerMenuTitle");
        getWidget(groupTitle, "GroupTitle");
        getWidget(playersTitle, "PlayersTitle");
        getWidget(profileTitle, "ProfileTitle");

        title->setCaption(localizeArena("chat.menu.title"));
        setMenuCaption(mTabChat, localizeArena("chat.tab.chat"));
        setMenuCaption(mTabGroup, localizeArena("chat.tab.group"));
        setMenuCaption(mTabPlayers, localizeArena("chat.tab.players"));
        setMenuCaption(mTabStats, localizeArena("chat.tab.stats"));
        setMenuCaption(mReturnButton, "X");
        mReturnButton->setTextColour(MyGUI::Colour(1.f, 0.15f, 0.15f));
        setMenuCaption(mModeOoc, "OOC");
        setMenuCaption(mModeRp, "RP");
        setMenuCaption(mChannelSay, localizeArena("chat.channel.say"));
        setMenuCaption(mChannelWhisper, localizeArena("chat.channel.whisper"));
        setMenuCaption(mChannelShout, localizeArena("chat.channel.shout"));
        setMenuCaption(mChannelLocalOoc, localizeArena("chat.channel.local_ooc"));
        setMenuCaption(mChannelGlobalOoc, localizeArena("chat.channel.global_ooc"));
        setMenuCaption(mStylePlain, localizeArena("chat.style.plain"));
        setMenuCaption(mStyleMe, "/me");
        setMenuCaption(mStyleDo, "/do");
        setMenuCaption(mStyleTry, "/try");
        setMenuCaption(mEmojiToggleButton, localizeArena("chat.emoji"));
        setMenuCaption(mColorToggleButton, localizeArena("chat.color.button"));
        setMenuCaption(mStayOpenButton, localizeArena("chat.stay_after_send"));
        setMenuCaption(mSendButton, localizeArena("chat.send"));
        mColorBarLabel->setCaption(localizeArena("chat.color.hint"));

        groupTitle->setCaption(localizeArena("chat.group.title"));
        mGroupInfo->setCaption(localizeArena("chat.group.loading"));
        mGroupRosterLabel->setCaption(localizeArena("chat.group.roster"));
        mGroupNameLabel->setCaption(localizeArena("chat.group.name"));
        mGroupTargetLabel->setCaption(localizeArena("chat.group.target"));
        setMenuCaption(mGroupCreateButton, localizeArena("chat.group.create"));
        setMenuCaption(mGroupRefreshButton, localizeArena("chat.group.refresh"));
        setMenuCaption(mGroupInviteButton, localizeArena("chat.group.invite"));
        setMenuCaption(mGroupLeaveButton, localizeArena("chat.group.leave"));
        setMenuCaption(mGroupDisbandButton, localizeArena("chat.group.disband"));
        setMenuCaption(mGroupKickButton, localizeArena("chat.group.kick"));
        setMenuCaption(mGroupLeaderButton, localizeArena("chat.group.leader"));
        setMenuCaption(mGroupAcceptButton, localizeArena("chat.group.accept"));
        setMenuCaption(mGroupDeclineButton, localizeArena("chat.group.decline"));
        playersTitle->setCaption(localizeArena("chat.players.title"));
        mPlayersInfo->setCaption(localizeArena("chat.players.loading"));
        setMenuCaption(mPlayersRefreshButton, localizeArena("chat.group.refresh"));
        setMenuCaption(mPlayersOpenListButton, localizeArena("chat.players.open_list"));
        setMenuCaption(mPlayersInviteButton, localizeArena("chat.group.invite"));

        profileTitle->setCaption(localizeArena("chat.profile.title"));
        mProfileInfo->setCaption(localizeArena("chat.profile.loading"));
        setMenuCaption(mProfileRefreshButton, localizeArena("chat.profile.refresh"));
        setMenuCaption(mProfileFixButton, "FIX");
        setMenuCaption(mProfileReturnsButton, localizeArena("chat.profile.returns"));
        setMenuCaption(mProfileDeleteButton, localizeArena("chat.profile.delete"));
        mProfileDeleteButton->setTextColour(MyGUI::Colour(1.f, 0.25f, 0.25f));

        emojiFontName = Settings::Manager::getString("emoji font", "Chat");
        menuFontName = Settings::Manager::getString("menu font", "Chat");
        if (menuFontName.empty())
            menuFontName = sMenuFont;
        ensureChatFontLoaded();
        if (MyGUI::FontManager::getInstance().getByName(menuFontName) == nullptr)
            menuFontName = sMenuFont;
        applyEmojiPalette();

        mTabChat->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onTabClicked);
        mTabGroup->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onTabClicked);
        mTabPlayers->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onTabClicked);
        mTabStats->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onTabClicked);
        mTabHome->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onTabClicked);
        mTabDecor->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onTabClicked);
        // One mouse/touch click opens the selected entry, while keyboard/controller
        // navigation can move the highlight freely and confirms with E/Accept.
        mDecorList->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onDecorClick);
        mDecorList->eventListSelectAccept += MyGUI::newDelegate(this, &GUIChat::onDecorSelect);
        mDecorBack->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onDecorBack);
        mDecorOpen->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onDecorOpen);
        mDecorRefresh->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onDecorRefresh);
        mDecorCapture->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onDecorCapture);
        setMenuCaption(mTabDecor, localizeArena("furniture.decorate"));
        setMenuCaption(mDecorBack, localizeArena("furniture.back"));
        setMenuCaption(mDecorOpen, localizeArena("furniture.shop"));
        setMenuCaption(mDecorCapture, localizeArena("furniture.capture"));
        setMenuCaption(mDecorRefresh, localizeArena("furniture.refresh"));
        setMenuCaption(mTabHome, MWBase::Environment::get().getWindowManager()->getArenaLanguage() == "ru"
            ? "Мой дом" : "My home");
        mModeOoc->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onModeClicked);
        mModeRp->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onModeClicked);
        mChannelSay->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onChannelClicked);
        mChannelWhisper->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onChannelClicked);
        mChannelShout->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onChannelClicked);
        mChannelLocalOoc->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onChannelClicked);
        mChannelGlobalOoc->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onChannelClicked);
        mStylePlain->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onStyleClicked);
        mStyleMe->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onStyleClicked);
        mStyleDo->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onStyleClicked);
        mStyleTry->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onStyleClicked);
        mStayOpenButton->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onStayOpenClicked);
        mSendButton->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onSendClicked);
        mReturnButton->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onReturnClicked);
        mEmojiToggleButton->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onEmojiToggleClicked);
        mColorToggleButton->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onColorToggleClicked);
        for (int i = 0; i < sEmojiSlotCount; ++i)
            mEmojiButtons[i]->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onEmojiClicked);
        for (int i = 0; i < sColorSlotCount; ++i)
        {
            if (mColorButtons[i] == nullptr)
                continue;
            mColorButtons[i]->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onColorClicked);
            mColorButtons[i]->setVisible(false);
        }
        mGroupRoster->eventListChangePosition += MyGUI::newDelegate(this, &GUIChat::onRosterSelected);
        mGroupRoster->eventListSelectAccept += MyGUI::newDelegate(this, &GUIChat::onRosterAccepted);
        mPlayersList->eventListChangePosition += MyGUI::newDelegate(this, &GUIChat::onPlayerListSelected);
        mPlayersRefreshButton->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onPlayersButtonClicked);
        mPlayersOpenListButton->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onPlayersButtonClicked);
        mPlayersInviteButton->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onPlayersButtonClicked);
        mProfileRefreshButton->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onProfileButtonClicked);
        mProfileFixButton->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onProfileButtonClicked);
        mProfileReturnsButton->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onProfileButtonClicked);
        mProfileDeleteButton->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onProfileButtonClicked);

        // Z005: every housing action is a button on the page. Only the three
        // that need a name or a title still open a server input dialog, and the
        // server closes it straight back into this page.
        const bool ruHome = MWBase::Environment::get().getWindowManager()->getArenaLanguage() == "ru";
        mHomeTitle->setCaption(ruHome ? "Мой дом" : "My home");
        mHomeInfo->setCaption("");
        // Actions are grouped under short headers rather than an unbroken grid.
        const char* homeSectionIds[] = {"HomeSettingsLabel", "HomeTypeLabel", "HomeAccessLabel", "HomeManageLabel"};
        const char* homeSectionRu[] = {"НАСТРОЙКИ", "ТИП ПОМЕЩЕНИЯ", "ДОСТУП", "УПРАВЛЕНИЕ"};
        const char* homeSectionEn[] = {"SETTINGS", "PROPERTY TYPE", "ACCESS", "MANAGEMENT"};
        MyGUI::Widget* homeRows[] = {mHomeActionRow1, mHomeActionRow2, mHomeActionRow3, mHomeActionRow4};
        for (int i = 0; i < 4; ++i)
        {
            // Widgets loaded from a MyGUI layout are prefixed by MWGui::Layout.
            // Searching a child by the raw XML name returns nullptr; use getWidget()
            // so the prefix is applied before casting.
            MyGUI::TextBox* heading = nullptr;
            getWidget(heading, homeSectionIds[i]);
            heading->setCaption(ruHome ? homeSectionRu[i] : homeSectionEn[i]);
        }
        setMenuCaption(mHomeRenameButton, ruHome ? "Название" : "Rename");
        setMenuCaption(mHomeLockButton, ruHome ? "Замок" : "Lock");
        setMenuCaption(mHomeTakeButton, ruHome ? "Вещи гостям" : "Guest items");
        setMenuCaption(mHomeKindHouseButton, ruHome ? "Дом" : "Home");
        setMenuCaption(mHomeKindTavernButton, ruHome ? "Таверна" : "Tavern");
        setMenuCaption(mHomeKindShopButton, ruHome ? "Магазин" : "Shop");
        setMenuCaption(mHomeKindGuildButton, ruHome ? "Гильдия" : "Guild");
        setMenuCaption(mHomeCoownerButton, ruHome ? "Совладелец +" : "Co-owner +");
        setMenuCaption(mHomeFriendButton, ruHome ? "Друг +" : "Friend +");
        setMenuCaption(mHomeRevokeButton, ruHome ? "Убрать доступ" : "Revoke access");
        setMenuCaption(mHomeResetButton, ruHome ? "Очистить дом" : "Reset home");
        setMenuCaption(mHomeGiftButton, ruHome ? "Подарить" : "Gift");
        setMenuCaption(mHomeSellButton, ruHome ? "Продать" : "Sell");
        setMenuCaption(mHomeMarketButton, ruHome ? "Выручка" : "Proceeds");
        mHomeResetButton->setTextColour(MyGUI::Colour(1.f, 0.52f, 0.42f));
        mHomeGiftButton->setTextColour(MyGUI::Colour(0.95f, 0.78f, 0.42f));
        mHomeSellButton->setTextColour(MyGUI::Colour(1.f, 0.52f, 0.42f));
        for (MyGUI::Button* homeButton : {mHomeRenameButton, mHomeLockButton, mHomeTakeButton,
                 mHomeKindHouseButton, mHomeKindTavernButton, mHomeKindShopButton,
                 mHomeKindGuildButton, mHomeCoownerButton, mHomeFriendButton,
                 mHomeRevokeButton, mHomeResetButton, mHomeGiftButton, mHomeSellButton, mHomeMarketButton})
            homeButton->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onHomeButton);

        mGroupCreateButton->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onGroupButtonClicked);
        mGroupRefreshButton->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onGroupButtonClicked);
        mGroupVoiceButton->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onGroupVoiceClicked);
        mGroupInviteButton->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onGroupButtonClicked);
        mGroupLeaveButton->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onGroupButtonClicked);
        mGroupDisbandButton->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onGroupButtonClicked);
        mGroupKickButton->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onGroupButtonClicked);
        mGroupLeaderButton->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onGroupButtonClicked);
        mGroupJournalButton->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onGroupButtonClicked);
        mGroupTopicsButton->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onGroupButtonClicked);
        mGroupAcceptButton->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onGroupButtonClicked);
        mGroupDeclineButton->eventMouseButtonClick += MyGUI::newDelegate(this, &GUIChat::onGroupButtonClicked);

        mDragHandle->eventMouseButtonPressed += MyGUI::newDelegate(this, &GUIChat::onDragStart);
        mDragHandle->eventMouseDrag += MyGUI::newDelegate(this, &GUIChat::onDrag);
        mDragHandle->eventMouseButtonReleased += MyGUI::newDelegate(this, &GUIChat::onGeometryReleased);

        // Keep the title bar draggable even though its label occupies part of it.
        title->setNeedMouseFocus(false);
        mGroupInfo->setEditReadOnly(true);
        mProfileInfo->setEditReadOnly(true);
        updateGroupControls();
    }

    void GUIChat::onOpen()
    {
        applyAlpha(currentAlpha);
    }

    void GUIChat::onClose()
    {
        if (geometryDirty)
            persistGeometry();
    }

    bool GUIChat::exit()
    {
        if (historyReviewState)
        {
            setHistoryReviewState(false);
            return false;
        }
        if (editState)
        {
            setEditState(false);
            return false;
        }
        return true;
    }

    bool GUIChat::getEditState()
    {
        return editState;
    }

    void GUIChat::acceptCommand(MyGUI::EditBox*)
    {
        if (!editState || activeTab != TAB_CHAT)
            return;

        const std::string cm = mCommandLine->getOnlyText();

        // With "stay in menu" enabled, an empty Enter is harmless. Without it,
        // retain the historical quick-cancel behaviour.
        if (cm.empty())
        {
            if (!stayOpenAfterSend)
                setEditState(false);
            return;
        }

        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Player: %s", cm.c_str());

        if (mCommandHistory.empty() || mCommandHistory.back() != cm)
            mCommandHistory.push_back(cm);
        mCurrent = mCommandHistory.end();
        mEditString.clear();

        // Remember exactly the established prefix that was sent. This makes
        // repeated RP/OOC messages fast without changing the meaning of any
        // existing slash command. If the prefix was manually removed while
        // editing, commandTextChanged() has already cleared the saved value.
        const std::string explicitPrefix = detectRememberedPrefix(cm);
        if (!explicitPrefix.empty())
            setRememberedPrefix(explicitPrefix);

        // Clearing after Send is an internal editor reset, not the player's
        // request to forget the prefix. Suppress commandTextChanged() for it.
        applyingRememberedPrefix = true;
        mCommandLine->setCaption("");
        applyingRememberedPrefix = false;
        const std::string outgoing = buildOutgoingMessage(cm);
        send(outgoing);

        if (stayOpenAfterSend)
        {
            selectTab(TAB_CHAT, false);
            refillRememberedPrefix();
            MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mCommandLine);
            updateCommandLineLayout();
        }
        else
            setEditState(false);
    }

    void GUIChat::onResChange(int width, int height)
    {
        if (editState && menuState)
        {
            clampToViewport(width, height);
            panelCoord = mMainWidget->getCoord();
        }
        else
            applyHudGeometry(width, height);
        updateCommandLineLayout();
    }

    void GUIChat::setFont(const std::string &fntName)
    {
        // Kept for API compatibility. The HUD intentionally uses Russo while
        // the interactive player-menu chat uses the Unicode/emoji-capable font.
        mCommandLine->setFontName(fntName);
        if (editState)
            mHistory->setFontName(fntName);
    }

    void GUIChat::print(const std::string &msg, const std::string &color)
    {
        if (msg.empty())
        {
            // An empty/failed network payload is not a request to erase the session.
            return;
        }
        else
        {
            // Y024: the gameplay HUD is a live feed, not a history browser.
            // Only the full Player Menu may keep a manually reviewed scroll
            // position. When the player is already at the bottom, keep following
            // new messages there as well.
            const size_t previousRange = mHistory->getVScrollRange();
            const size_t previousPosition = mHistory->getVScrollPosition();
            const bool playerMenuHistory = editState && menuState && activeTab == TAB_CHAT && !mainMenuOpen;
            const bool wasAtBottom = previousRange == 0 || previousPosition + 1 >= previousRange;

            mHistory->addText(color + msg);
            if (!playerMenuHistory || wasAtBottom)
                scrollHistoryToBottom();
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "%s", msg.c_str());

            if (historyDisplayEnabled && windowState != CHAT_HIDDEN && !mainMenuOpen)
                revealTemporarily();
        }
    }

    void GUIChat::printOK(const std::string &msg)
    {
        print(msg + "\n", "#FF00FF");
    }

    void GUIChat::printError(const std::string &msg)
    {
        print(msg + "\n", "#FF2222");
    }

    bool GUIChat::requestGroupRecall()
    {
        if (!mGroupRecallSupported)
            return false; // Unmodified servers retain vanilla Recall.
        const MWWorld::Ptr player = MWMechanics::getPlayer();
        mRecallRequest = std::to_string(++mRecallSerial);
        mRecallTimeLeft = 45.f;
        mRecallOriginCell = player.getCell()->getCell()->getDescription();
        send("@@AMP_RECALL@@OPEN\t" + mRecallRequest);
        return true;
    }

    bool GUIChat::handleServerControlMessage(const std::string& msg)
    {
        static const std::string recallPrefix = "@@AMP_RECALL@@";
        if (msg.compare(0, recallPrefix.size(), recallPrefix) == 0)
        {
            const auto fields = splitControlFields(msg.substr(recallPrefix.size()), '\t');
            if (fields.size() == 2 && fields[0] == "READY")
                mGroupRecallSupported = fields[1] == "1";
            else if (fields.size() >= 2 && !mRecallRequest.empty() && fields[1] == mRecallRequest)
            {
                if (fields[0] == "CANCEL")
                    mRecallRequest.clear();
                else if (fields[0] == "GO" && fields.size() == 8)
                {
                    // Only a live, successful local Recall may consume a server choice.
                    // Replayed controls and manually typed OPEN messages cannot teleport.
                    mRecallRequest.clear();
                    MWBase::World* world = MWBase::Environment::get().getWorld();
                    const MWWorld::Ptr player = MWMechanics::getPlayer();
                    if (player.getClass().getCreatureStats(player).isDead()
                        || player.getCell()->getCell()->getDescription() != mRecallOriginCell)
                        return true;
                    if (!world->isTeleportingEnabled())
                    {
                        MWBase::Environment::get().getWindowManager()->messageBox("#{sTeleportDisabled}");
                        return true;
                    }
                    ESM::Position position{};
                    float values[5];
                    for (std::size_t i = 0; i < 5; ++i)
                    {
                        char* end = nullptr;
                        values[i] = std::strtof(fields[i + 3].c_str(), &end);
                        if (end == fields[i + 3].c_str() || *end != '\0' || !std::isfinite(values[i]))
                            return true;
                    }
                    for (int i = 0; i < 3; ++i) position.pos[i] = values[i];
                    position.rot[0] = values[3];
                    position.rot[2] = values[4];
                    std::string cell = unescapeControlField(fields[2]);
                    if (cell.empty()) return true;
                    // Saved exterior descriptions use grid coordinates; ActionTeleport
                    // expects an empty cell name and resolves the exterior from XYZ.
                    std::istringstream grid(cell);
                    int gridX, gridY;
                    char comma;
                    if ((grid >> gridX >> comma >> gridY) && comma == ',' && (grid >> std::ws).eof())
                        cell.clear();
                    MWWorld::ActionTeleport action(cell, position, false);
                    action.execute(player);
                }
            }
            return true;
        }
        static const std::string groupPrefix = "@@AMP_GROUP@@";
        static const std::string xpPrefix = "@@AMP_XP@@";
        static const std::string colorPrefix = "@@AMP_COLOR@@";
        static const std::string playersPrefix = "@@AMP_PLAYERS@@";
        static const std::string placementDenyPrefix = "@@AMP_PLACE_DENY@@";
        static const std::string personalMapPrefix = "@@AMP_PMARK@@";
        static const std::string groupMapPrefix = "@@AMP_GMARK@@";
        static const std::string profilePrefix = "@@AMP_PROFILE@@";
        static const std::string housePrefix = "@@AMP_HOUSE@@";
        static const std::string trainingPrefix = "@@AMP_TRAIN@@";
        static const std::string revivePrefix = "@@AMP_REVIVE@@";

        if (msg.compare(0, groupPrefix.size(), groupPrefix) == 0)
        {
            // X054: do NOT unescape here. Fields 8 and 11 are themselves lists
            // that still have to be split on ';' and '^', and an early unescape
            // turned every "\n" inside a record into a real newline that the
            // splitter then dropped - which is what glued the detail lines
            // together with no separator. Escape sequences are resolved on the
            // leaves instead, right before they reach a caption.
            std::vector<std::string> fields = splitControlFields(msg.substr(groupPrefix.size()), '\t');
            if (!fields.empty() && fields[0] == "STATE")
                rebuildGroupInfo(fields);
            return true;
        }

        if (msg.compare(0, colorPrefix.size(), colorPrefix) == 0)
        {
            std::vector<std::string> fields = splitControlFields(msg.substr(colorPrefix.size()), '\t');
            if (!fields.empty() && fields[0] == "STATE")
                rebuildColorPalette(fields);
            return true;
        }

        if (msg.compare(0, playersPrefix.size(), playersPrefix) == 0)
        {
            std::vector<std::string> fields = splitControlFields(msg.substr(playersPrefix.size()), '\t');
            if (!fields.empty() && fields[0] == "STATE")
                rebuildPlayerList(fields);
            return true;
        }



        if (msg.compare(0, revivePrefix.size(), revivePrefix) == 0)
        {
            // U035: server -> client death-recovery state. STATE <OPEN|DENY|LOCK> <seconds>
            std::vector<std::string> fields = splitControlFields(msg.substr(revivePrefix.size()), '\t');
            if (fields.size() >= 3 && fields[0] == "BOUNTY")
            {
                auto* local = mwmp::Main::isInitialized() ? mwmp::Main::get().getLocalPlayer() : nullptr;
                if (local) local->applyBountyDeathState(std::strtof(fields[1].c_str(), nullptr),
                    std::strtof(fields[2].c_str(), nullptr));
            }
            else if (fields.size() >= 2 && fields[0] == "RESPAWN")
            {
                auto* local = mwmp::Main::isInitialized() ? mwmp::Main::get().getLocalPlayer() : nullptr;
                if (local) local->applyRespawnWait(std::strtof(fields[1].c_str(), nullptr));
            }
            else if (fields.size() >= 3 && fields[0] == "STATE")
            {
                mwmp::LocalPlayer* localPlayer = mwmp::Main::isInitialized()
                    ? mwmp::Main::get().getLocalPlayer() : nullptr;
                if (localPlayer != nullptr)
                    localPlayer->applyDeathRecoveryState(fields[1], std::strtof(fields[2].c_str(), nullptr));
            }
            return true;
        }

        if (msg.compare(0, trainingPrefix.size(), trainingPrefix) == 0)
        {
            std::vector<std::string> fields = splitControlFields(msg.substr(trainingPrefix.size()), '\t');
            if (fields.size() >= 4 && fields[0] == "STATE")
                MWGui::TrainingWindow::setServerTrainingState(fields[1],
                    std::atoi(fields[2].c_str()), std::atoi(fields[3].c_str()));
            return true;
        }

        if (msg.compare(0, housePrefix.size(), housePrefix) == 0)
        {
            // Z001: housing traffic is machine-readable state, never chat. The
            // payload keeps its tabs and carets intact - ArenaHousing does its
            // own splitting, and splitControlFields() here would destroy the
            // record separators it needs.
            std::string payload = msg.substr(housePrefix.size());
            while (!payload.empty() && (payload.back() == '\n' || payload.back() == '\r'))
                payload.pop_back();
            // Z005: the "My home" page is fed by the same channel, but it is a
            // menu page, not housing runtime state, so it is handled here.
            if (payload.compare(0, 5, "HOME\t") == 0 || payload == "HOME")
            {
                rebuildHomeInfo(splitControlFields(payload, '\t'));
                return true;
            }

            // Z006: the door menu's "manage" entry. The server has just sent
            // the page its state; this opens the Player Menu on it, so the
            // door and the menu are the same interface rather than two.
            if (payload == "MENU")
            {
                setEditState(true);
                setMenuState(true);
                selectTab(TAB_HOME);
                return true;
            }
            if (payload.compare(0, 3, "FS_") == 0)
            {
                std::vector<std::string> fs = splitControlFields(payload, '\t');
                if (fs.size() >= 3 && fs[0] == "FS_ROLE")
                {
                    mDecorIsStaff = fs[1] == "1";
                    mDecorAllowed = fs[2] == "1";
                    if (!mDecorAllowed && activeTab == TAB_DECOR) activeTab = TAB_CHAT;
                    refreshPlayerMenu();
                    return true;
                }
                if (!fs.empty() && fs[0] == "FS_MENU_BEGIN")
                {
                    mDecorShowItems = fs.size() > 1 && fs[1] == "items";
                    mDecorPage = fs.size() > 2 ? std::max(1, std::atoi(fs[2].c_str())) : 1;
                    mDecorPages = fs.size() > 3 ? std::max(1, std::atoi(fs[3].c_str())) : 1;
                    mDecorValues.clear();mDecorList->removeAllItems();
                    return true;
                }
                if (fs.size() >= 3 && fs[0] == "FS_MENU_ITEM")
                {
                    mDecorValues.push_back(fs[1]);mDecorList->addItem(fs[2]);
                    return true;
                }
                if (!fs.empty() && fs[0] == "FS_MENU_END")
                {
                    if (mDecorShowItems)
                        mDecorTitle->setCaption(mDecorCategory + " (" + std::to_string(mDecorPage)
                            + "/" + std::to_string(mDecorPages) + ")");
                    else mDecorTitle->setCaption(localizeArena("furniture.title"));
                    MyGUI::TextBox* hint = nullptr;
                    getWidget(hint, "DecorHint");
                    {
                        const bool ru=MWBase::Environment::get().getWindowManager()->getArenaLanguage()=="ru";
                        hint->setCaption(ru ? (mDecorShowItems ? "Выберите предмет для просмотра" : "Выберите категорию одним нажатием")
                            : (mDecorShowItems ? "Select an item to preview" : "Tap a category once to open"));
                    }
                    return true;
                }
            }
            ArenaHousing::get().handleControlMessage(payload);
            return true;
        }

        if (msg.compare(0, profilePrefix.size(), profilePrefix) == 0)
        {
            const std::string action = msg.substr(profilePrefix.size());
            if (action.rfind("DELETED", 0) == 0)
            {
                // A deleted server profile must return to a genuinely fresh
                // registration flow. Do not silently pre-fill the old password.
                Settings::Manager::setString("password", "Login", "");
                Settings::Manager::saveUser();
            }
            else
            {
                std::vector<std::string> fields = splitControlFields(action, '\t');
                if (!fields.empty() && fields[0] == "STATE")
                    rebuildProfileState(fields);
            }
            return true;
        }

        if (msg.compare(0, personalMapPrefix.size(), personalMapPrefix) == 0)
        {
            std::vector<std::string> fields = splitControlFields(msg.substr(personalMapPrefix.size()), '\t');
            MWGui::WindowManager* windowManager = dynamic_cast<MWGui::WindowManager*>(
                MWBase::Environment::get().getWindowManager());
            if (!windowManager || fields.empty())
                return true;

            MWGui::CustomMarkerCollection& markers = windowManager->getCustomMarkerCollection();
            if (fields[0] == "CLEAR")
            {
                // In multiplayer the server is authoritative for personal map
                // notes. Clearing the shared collection also removes stale local
                // save remnants before the current profile is replayed.
                markers.clear();
                mPersonalMapMarkers.clear();
                return true;
            }

            if (fields[0] == "ADD" && fields.size() >= 12)
            {
                ESM::CustomMarker marker;
                const std::string id = fields[1];
                const bool paged = fields[2] == "1";
                const std::string cell = unescapeControlField(fields[3]);
                marker.mCell.mPaged = paged;
                marker.mCell.mWorldspace = paged ? ESM::CellId::sDefaultWorldspace : cell;
                marker.mCell.mIndex.mX = std::atoi(fields[4].c_str());
                marker.mCell.mIndex.mY = std::atoi(fields[5].c_str());
                marker.mWorldX = std::strtof(fields[6].c_str(), nullptr);
                marker.mWorldY = std::strtof(fields[7].c_str(), nullptr);
                const std::string kind = unescapeControlField(fields[8]);
                const std::string colour = unescapeControlField(fields[9]);
                const bool group = fields[10] == "1";
                const std::string text = unescapeControlField(fields[11]);
                marker.mNote = "@AMP_MARK@|" + kind + "|" + colour + "|" + (group ? "1" : "0") + "|" + text;
                markers.addMarker(marker);
                mPersonalMapMarkers[id] = marker;
                return true;
            }
            return true;
        }

        if (msg.compare(0, groupMapPrefix.size(), groupMapPrefix) == 0)
        {
            std::vector<std::string> fields = splitControlFields(msg.substr(groupMapPrefix.size()), '\t');
            GUIController* gui = Main::get().getGUIController();
            if (!gui || fields.empty())
                return true;

            if (fields[0] == "CLEAR")
            {
                for (const auto& entry : mGroupMapMarkers)
                {
                    if (gui->mPlayerMarkers.contains(entry.second))
                        gui->mPlayerMarkers.deleteMarker(entry.second);
                }
                mGroupMapMarkers.clear();
                return true;
            }

            if (fields[0] == "ADD" && fields.size() >= 10)
            {
                const std::string id = fields[1];
                auto old = mGroupMapMarkers.find(id);
                if (old != mGroupMapMarkers.end())
                {
                    if (gui->mPlayerMarkers.contains(old->second))
                        gui->mPlayerMarkers.deleteMarker(old->second);
                    mGroupMapMarkers.erase(old);
                }

                ESM::CustomMarker marker;
                const bool paged = fields[2] == "1";
                const std::string cell = unescapeControlField(fields[3]);
                std::string kind, colour, text, owner;
                if (fields.size() >= 12)
                {
                    marker.mCell.mIndex.mX = std::atoi(fields[4].c_str());
                    marker.mCell.mIndex.mY = std::atoi(fields[5].c_str());
                    marker.mWorldX = std::strtof(fields[6].c_str(), nullptr);
                    marker.mWorldY = std::strtof(fields[7].c_str(), nullptr);
                    kind = unescapeControlField(fields[8]);
                    colour = unescapeControlField(fields[9]);
                    text = unescapeControlField(fields[10]);
                    owner = unescapeControlField(fields[11]);
                }
                else
                {
                    // Compatibility with the first Y049 draft protocol.
                    marker.mWorldX = std::strtof(fields[4].c_str(), nullptr);
                    marker.mWorldY = std::strtof(fields[5].c_str(), nullptr);
                    kind = unescapeControlField(fields[6]);
                    colour = unescapeControlField(fields[7]);
                    text = unescapeControlField(fields[8]);
                    owner = fields.size() > 9 ? unescapeControlField(fields[9]) : std::string();
                    if (paged)
                    {
                        marker.mCell.mIndex.mX = static_cast<int>(std::floor(marker.mWorldX / Constants::CellSizeInUnits));
                        marker.mCell.mIndex.mY = static_cast<int>(std::floor(marker.mWorldY / Constants::CellSizeInUnits));
                    }
                }

                // The owner's editable personal copy is already restored through
                // @@AMP_PMARK@@, so do not draw a second marker on top of it.
                const std::string localAccountName = Settings::Manager::getString("name", "Login");
                if (!owner.empty() && owner == localAccountName)
                    return true;

                marker.mNote = "@AMP_GMARK@|" + kind + "|" + colour + "|" + text;
                marker.mCell.mPaged = paged;
                marker.mCell.mWorldspace = paged ? ESM::CellId::sDefaultWorldspace : cell;

                mGroupMapMarkers[id] = marker;
                gui->mPlayerMarkers.addMarker(marker);
                return true;
            }
            return true;
        }

        if (msg.compare(0, placementDenyPrefix.size(), placementDenyPrefix) == 0)
        {
            // Arena Y020: the server rejected a protected world-object grab. Roll
            // back to the grab-start transform without echoing that rollback as a
            // new ObjectMove/ObjectRotate packet, then explain the policy locally.
            //
            // Y013-fix-01: two changes. The rollback now also works when the
            // rejection lands after the object was released, and the explanation is
            // shown only when the server flags this as a new attempt - a held object
            // resyncs ~20 times a second and used to produce a wall of message boxes.
            std::vector<std::string> fields = splitControlFields(msg.substr(placementDenyPrefix.size()), '\t');

            const bool rollback = fields.empty() || fields[0] != "0";
            const bool explain = fields.size() < 2 || fields[1] != "0";

            if (rollback)
                MWBase::Environment::get().getWorld()->revertLastPlacement();
            if (explain)
                MWBase::Environment::get().getWindowManager()->messageBox(localizeArena("placement.denied"));
            return true;
        }

        if (msg.compare(0, xpPrefix.size(), xpPrefix) == 0)
        {
            std::vector<std::string> fields = splitControlFields(msg.substr(xpPrefix.size()), '\t');
            if (fields.size() >= 2)
            {
                char* end = nullptr;
                const float amount = std::strtof(fields[0].c_str(), &end);
                const bool scaled = fields[1] == "1";
                const std::string kind = fields.size() > 2 ? fields[2] : std::string();
                std::string reason;
                // Arena Y012: known server event kinds are localized client-side
                // even if an older server accidentally supplied raw English text.
                if (kind == "kill")
                    reason = localizeArena("xp.reason.defeated_enemy");
                else if (kind == "quest")
                    reason = localizeArena("xp.reason.quest_progress");
                else if (kind == "death")
                    reason = localizeArena("xp.msg.death");
                else if (kind == "jail")
                    reason = localizeArena("xp.msg.jail");
                else if (fields.size() > 3)
                    reason = unescapeControlField(fields[3]);

                if (end != fields[0].c_str() && amount > 0.f)
                {
                    // ArenaMP Y088: the amp-xp key is only a request/nonce used to
                    // validate a kill/quest on the server.  Remove it BEFORE the XP
                    // mutation makes LocalPlayer send PlayerLevel again; otherwise
                    // the already-consumed key can be submitted a second time and a
                    // later kill may accidentally match it.
                    MWWorld::Ptr player = MWMechanics::getPlayer();
                    if (!player.isEmpty() && player.getClass().isNpc())
                        player.getClass().getNpcStats(player).removeXpRewardKeysWithPrefix("amp-xp|");
                    MWMechanics::XPLeveling::awardServer(amount, scaled, reason);
                }
                else if (end != fields[0].c_str() && amount < 0.f)
                    MWMechanics::XPLeveling::notifyServerAdjustment(amount, reason);
            }
            return true;
        }

        return false;
    }

    void GUIChat::send(const std::string &str)
    {
        LocalPlayer *localPlayer = Main::get().getLocalPlayer();
        Networking *networking = Main::get().getNetworking();

        std::istringstream commandStream(str);
        std::string command;
        commandStream >> command;
        std::transform(command.begin(), command.end(), command.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (command == "/fix" || command == "/fixme")
        {
            const MWWorld::Ptr player = localPlayer->getPlayerPtr();
            if (!player.isEmpty() &&
                (player.getClass().getCreatureStats(player).getAiSequence().isInCombat()
                 || !MWBase::Environment::get().getMechanicsManager()->getActorsFighting(player).empty()))
            {
                MWBase::Environment::get().getWindowManager()->messageBox(
                    MyGUI::LanguageManager::getInstance().replaceTags("#{arenamp=fix.combat_blocked}"));
                return;
            }
        }

        localPlayer->chatMessage = str;
        networking->getPlayerPacket(ID_CHAT_MESSAGE)->setPlayer(localPlayer);
        networking->getPlayerPacket(ID_CHAT_MESSAGE)->Send();
    }

    std::string GUIChat::detectRememberedPrefix(const std::string& text)
    {
        // Longest prefixes first: /// must never be mistaken for //.
        static const std::array<const char*, 6> prefixes = {"///", "/try", "/do", "/me", "/s", "//"};
        for (const char* prefix : prefixes)
        {
            const std::size_t length = std::strlen(prefix);
            if (text.compare(0, length, prefix) == 0
                && (text.size() == length || text[length] == ' '))
                return prefix;
        }
        return std::string();
    }

    void GUIChat::setRememberedPrefix(const std::string& prefix, bool refillEditor)
    {
        if (rememberedPrefix == prefix)
        {
            if (refillEditor)
                refillRememberedPrefix();
            return;
        }

        rememberedPrefix = prefix;
        Settings::Manager::setString("input prefix", "Chat", rememberedPrefix);
        Settings::Manager::saveUser();
        if (refillEditor)
            refillRememberedPrefix(true);
    }

    void GUIChat::refillRememberedPrefix(bool prependExisting)
    {
        if (mCommandLine == nullptr || rememberedPrefix.empty())
            return;

        const std::string current = mCommandLine->getOnlyText();
        std::string text;
        if (current.empty())
            text = rememberedPrefix + " ";
        else if (prependExisting && current[0] != '/')
            text = rememberedPrefix + " " + current;
        else
            return;

        applyingRememberedPrefix = true;
        mCommandLine->setCaption(text);
        mCommandLine->setTextCursor(text.size());
        applyingRememberedPrefix = false;
    }

    std::string GUIChat::buildOutgoingMessage(const std::string& text) const
    {
        // User-entered slash commands always bypass the visual mode selector.
        // This keeps /help and every existing server command 100% compatible.
        if (!text.empty() && text[0] == '/')
            return text;

        // X054: the menu no longer speaks its own /ampchat dialect. X052 sent a
        // structured envelope that eventHandler formatted on its own, which is
        // why /me from the toolbar and /me typed by hand looked different, and
        // why the toolbar ignored RP mode, popup mode and whisper/shout ranges
        // entirely. Now the toolbar emits the exact command a player would
        // type, so coreChat is the single formatter and there is nothing left
        // to keep in sync.
        //
        // Style wins over channel because coreChat's /me, /do and /try are
        // always cell-local actions, exactly as they are from the keyboard.
        switch (chatStyle)
        {
            case STYLE_ME: return "/me " + text;
            case STYLE_DO: return "/do " + text;
            case STYLE_TRY: return "/try " + text;
            default: break;
        }

        switch (chatChannel)
        {
            case CHANNEL_WHISPER: return "/w " + text;
            case CHANNEL_SHOUT: return "/sh " + text;
            case CHANNEL_LOCAL_OOC: return "// " + text;
            case CHANNEL_GLOBAL_OOC: return "/// " + text;
            case CHANNEL_SAY:
            default: return "/s " + text;
        }
    }

    void GUIChat::clean()
    {
        mHistory->setCaption("");
        scrollHistoryToBottom();
    }

    void GUIChat::pressedChatMode()
    {
        if (!historyDisplayEnabled)
        {
            MWBase::Environment::get().getWindowManager()->messageBox(localizeArena("chat.history_hidden"));
            return;
        }

        // Keep the historic visibility cycle, inserting a transient layout mode
        // between fully visible and 30% transparent. Only that mode has a frame.
        if (windowState == CHAT_VISIBLE && !historyReviewState)
        {
            setHistoryReviewState(true);
            MWBase::Environment::get().getWindowManager()->messageBox(localizeArena("chat.mode.layout"));
            return;
        }

        windowState = historyReviewState ? CHAT_TRANSPARENT_30
            : static_cast<ChatWindowState>((static_cast<int>(windowState) + 1) % CHAT_STATE_COUNT);
        Settings::Manager::setString("mode", "Chat", getModeSetting());
        Settings::Manager::saveUser();
        setHistoryReviewState(false);
        revealTime = windowState == CHAT_AUTOHIDE ? delay : 0.f;
        if (windowState == CHAT_HIDDEN && editState)
            setEditState(false);

        const std::string chatMode = getModeMessage();
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE, "Switch chat mode to %s", chatMode.c_str());
        MWBase::Environment::get().getWindowManager()->messageBox(chatMode);
        refreshPresentation();
    }

    void GUIChat::setEditState(bool state)
    {
        if (state && historyReviewState)
            setHistoryReviewState(false);

        if (editState == state)
        {
            if (state && activeTab == TAB_CHAT)
            {
                syncInteractiveInputMode();
                MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mCommandLine);
            }
            return;
        }

        if (!state && editState)
        {
            if (menuState)
                panelCoord = mMainWidget->getCoord();
            if (geometryDirty)
                persistGeometry();
        }

        editState = state;
        activeDrawer = DRAWER_NONE;
        // X054: leaving the input state always drops the expanded menu too, so
        // the next tap of the Say key starts from the compact HUD again.
        if (!editState)
            menuState = false;

        if (editState)
        {
            applyStateGeometry();
            mHistory->setFontName(menuState ? menuFontName : sHudFont);
            mCommandLine->setFontName(menuState ? menuFontName : sHudFont);
            selectTab(TAB_CHAT, false);
            revealTime = 0.f;
            mMainWidget->setNeedMouseFocus(menuState);
            syncInteractiveInputMode();
            MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mCommandLine);
        }
        else
        {
            MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(nullptr);
            mMainWidget->setNeedMouseFocus(false);
            syncInteractiveInputMode();
            mHistory->setFontName(sHudFont);
            mCommandLine->setFontName(sHudFont);
            applyStateGeometry();
            if (windowState == CHAT_AUTOHIDE)
                revealTime = delay;
        }

        refreshPlayerMenu();
        updateCommandLineLayout();
        refreshPresentation();
    }

    // X054: the second tier of the chat window. editState alone is the historic
    // HUD caret - no cursor, no mouse capture, camera untouched. menuState is
    // what turns the strip into the real Player Menu, and only holding the Say
    // key (or clicking a tab) gets you there.
    void GUIChat::setMenuState(bool state)
    {
        if (!editState)
            state = false;
        if (menuState == state)
            return;

        if (!state)
        {
            panelCoord = mMainWidget->getCoord();
            if (geometryDirty)
                persistGeometry();
        }

        menuState = state;
        activeDrawer = DRAWER_NONE;

        mHistory->setFontName(menuState ? menuFontName : sHudFont);
        mCommandLine->setFontName(menuState ? menuFontName : sHudFont);
        mMainWidget->setNeedMouseFocus(menuState);

        applyStateGeometry();
        syncInteractiveInputMode();
        if (activeTab == TAB_CHAT)
            MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mCommandLine);

        refreshPlayerMenu();
        updateCommandLineLayout();
        refreshPresentation();
    }

    bool GUIChat::getMenuState() const
    {
        return menuState;
    }

    void GUIChat::setSayKeyHeld(bool held)
    {
        sayKeyHeld = held;
    }

    void GUIChat::commandTextChanged(MyGUI::EditBox*)
    {
        if (!applyingRememberedPrefix && !rememberedPrefix.empty() && mCommandLine != nullptr)
        {
            const std::string text = mCommandLine->getOnlyText();
            const std::size_t length = rememberedPrefix.size();
            const bool stillHasPrefix = text.compare(0, length, rememberedPrefix) == 0
                && (text.size() == length || (text.size() > length && text[length] == ' '));
            if (!stillHasPrefix)
                setRememberedPrefix(std::string());
        }

        // X054: while the Say key is still physically down we are inside the
        // tap-or-hold window. SDL key auto-repeat would otherwise stream the
        // bound character into the freshly focused editor, so anything typed by
        // the key that opened the chat is discarded until it is released.
        if (sayKeyHeld && mCommandLine != nullptr && !mCommandLine->getOnlyText().empty())
        {
            // U034: keep the remembered prefix that was inserted by pressedSay(),
            // but discard SDL TEXTINPUT/autorepeat generated by the same physical
            // key that opened chat. Previously the whole editor was cleared, which
            // also erased the newly restored /s, //, ///, /me, /do or /try prefix.
            applyingRememberedPrefix = true;
            if (!rememberedPrefix.empty())
            {
                const std::string prefixText = rememberedPrefix + " ";
                mCommandLine->setCaption(prefixText);
                mCommandLine->setTextCursor(prefixText.size());
            }
            else
            {
                mCommandLine->setCaption(std::string());
                mCommandLine->setTextCursor(0);
            }
            applyingRememberedPrefix = false;
        }

        updateCommandLineLayout();
    }

    void GUIChat::updateCommandLineLayout()
    {
        if (!mMainWidget || !mCommandLine || !mHistory)
            return;

        const int mainWidth = mMainWidget->getWidth();
        const int mainHeight = mMainWidget->getHeight();

        if (!editState)
        {
            const int top = historyReviewState ? 42 : 8;
            const int bottom = historyReviewState ? 38 : 8;
            mHistory->setCoord(8, top, std::max(32, mainWidth - 16), std::max(20, mainHeight - top - bottom));
            mCommandLine->setVisible(false);
            return;
        }

        if (menuState && activeTab != TAB_CHAT)
        {
            mCommandLine->setVisible(false);
            return;
        }

        constexpr int lineHeight = 20;
        constexpr int maxVisibleLines = 5;
        constexpr int inputPadding = 8;
        constexpr int sideMargin = 10;
        constexpr int bottomFooterHeight = 46;
        constexpr int gap = 8;

        const std::string text = mCommandLine->getOnlyText();
        int explicitLines = 1 + static_cast<int>(std::count(text.begin(), text.end(), '\n'));
        const int measuredHeight = std::max(lineHeight, mCommandLine->getTextSize().height);
        const int measuredLines = std::max(1, (measuredHeight + lineHeight - 1) / lineHeight);
        const int contentLines = std::max(explicitLines, measuredLines);
        const int visibleLines = std::min(maxVisibleLines, contentLines);
        const int commandHeight = inputPadding + visibleLines * lineHeight;

        // X054: the compact HUD editor has no toolbar, no footer and no tab
        // strip to dodge, so it pins the input to the bottom of the small
        // rectangle and gives every remaining pixel to the history.
        if (!menuState)
        {
            const int hudMargin = 6;
            const int hudCommandHeight = std::min(std::max(24, commandHeight), std::max(24, mainHeight / 2));
            const int hudCommandTop = std::max(hudMargin, mainHeight - hudMargin - hudCommandHeight);
            const int hudInnerWidth = std::max(32, mainWidth - hudMargin * 2);

            mCommandLine->setCoord(hudMargin, hudCommandTop, hudInnerWidth, hudCommandHeight);
            mCommandLine->setEditMultiLine(true);
            mCommandLine->setEditWordWrap(true);
            mCommandLine->setOverflowToTheLeft(false);
            mHistory->setCoord(hudMargin, hudMargin, hudInnerWidth,
                std::max(20, hudCommandTop - hudMargin * 2));

            if (mCommandScroll)
            {
                const bool hudOverflow = contentLines > maxVisibleLines;
                mCommandScroll->setVisible(hudOverflow);
                mCommandScroll->setNeedMouseFocus(hudOverflow);
            }
            return;
        }

        const int drawerHeight = activeDrawer == DRAWER_COLOR ? sColorDrawerHeight : sDrawerHeight;
        const int historyTop = activeDrawer != DRAWER_NONE ? sDrawerTop + drawerHeight + 6 : sDrawerTop;

        const int commandBottom = std::max(historyTop + 80, mainHeight - bottomFooterHeight);
        const int commandTop = std::max(historyTop + 40, commandBottom - commandHeight);

        mCommandLine->setCoord(sideMargin, commandTop,
            std::max(32, mainWidth - sideMargin * 2), std::max(28, commandBottom - commandTop));
        mCommandLine->setEditMultiLine(true);
        mCommandLine->setEditWordWrap(true);
        mCommandLine->setOverflowToTheLeft(false);

        const int historyHeight = std::max(40, commandTop - gap - historyTop);
        mHistory->setCoord(sideMargin, historyTop,
            std::max(32, mainWidth - sideMargin * 2), historyHeight);

        if (mCommandScroll)
        {
            const bool overflow = contentLines > maxVisibleLines;
            mCommandScroll->setVisible(overflow);
            mCommandScroll->setNeedMouseFocus(overflow);
        }
    }

    void GUIChat::scrollHistoryToBottom()
    {
        const size_t range = mHistory->getVScrollRange();
        if (range > 0)
            mHistory->setVScrollPosition(range - 1);
        mHistory->setTextCursor(mHistory->getCaption().size());
    }

    void GUIChat::setHistoryReviewState(bool state)
    {
        if (historyReviewState == state)
            return;

        if (state && editState)
            setEditState(false);

        if (!state && geometryDirty)
            persistGeometry();
        if (!state)
            geometryDragMode = GEOMETRY_DRAG_NONE;
        historyReviewState = state;
        applyStateGeometry();
        if (state)
        {
            mMainWidget->setNeedMouseFocus(true);
            // U024d: layout-edit mode owns the mouse completely. Keeping the
            // history mouse-active allowed the history/scrollbar to win hit
            // testing over the visible frame on some MyGUI builds. Keyboard
            // focus remains on history so Esc/F2 handling is unchanged.
            mHistory->setNeedMouseFocus(false);
            mHistory->setNeedKeyFocus(true);
            if (mHistoryScroll)
            {
                mHistoryScroll->setVisible(false);
                mHistoryScroll->setNeedMouseFocus(false);
            }
            syncInteractiveInputMode();
            MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mHistory);
        }
        else
        {
            mMainWidget->setNeedMouseFocus(false);
            mHistory->setNeedMouseFocus(false);
            mHistory->setNeedKeyFocus(false);
            if (mHistoryScroll)
            {
                mHistoryScroll->setVisible(false);
                mHistoryScroll->setNeedMouseFocus(false);
            }
            MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(nullptr);
            syncInteractiveInputMode();
            scrollHistoryToBottom();
            if (windowState == CHAT_AUTOHIDE)
                revealTime = delay;
        }

        updateCommandLineLayout();
        refreshPresentation();
    }

    std::string GUIChat::getHistoryText() const
    {
        return mHistory->getCaption().asUTF8();
    }

    void GUIChat::setMainMenuOpen(bool state)
    {
        if (mainMenuOpen == state)
            return;

        if (state && historyReviewState)
            setHistoryReviewState(false);
        mainMenuOpen = state;
        if (state)
        {
            if (editState && menuState)
            {
                panelCoord = mMainWidget->getCoord();
                if (geometryDirty)
                    persistGeometry();
            }
            editState = false;
            activeDrawer = DRAWER_NONE;
            mCommandLine->setVisible(false);
            historyReviewState = false;
            syncInteractiveInputMode();
            const MyGUI::IntSize view = MyGUI::RenderManager::getInstance().getViewSize();
            mHistory->setFontName(sHudFont);
            applyHudGeometry(view.width, view.height);
            mMainWidget->setNeedMouseFocus(false);
            mHistory->setNeedMouseFocus(false);
            mHistory->setNeedKeyFocus(false);
            if (mHistoryScroll)
            {
                mHistoryScroll->setVisible(false);
                mHistoryScroll->setNeedMouseFocus(false);
            }
            currentAlpha = 0.f;
            targetAlpha = 0.f;
            hideAfterFade = false;
            refreshPlayerMenu();
            applyAlpha(0.f);
            setVisible(false);
        }
        else
        {
            updateCommandLineLayout();
            refreshPlayerMenu();
            refreshPresentation();
        }
    }

    void GUIChat::pressedSay()
    {
        if (!editState)
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE, "Opening ArenaMP HUD chat input.");

        // X054: a tap restores the pre-X049 behaviour exactly - the caret lands
        // in the compact HUD strip and nothing else about the game changes.
        setEditState(true);
        selectTab(TAB_CHAT, false);
        mCommandLine->setVisible(true);
        refillRememberedPrefix();
        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mCommandLine);
    }

    void GUIChat::openPlayerMenu()
    {
        if (!editState)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE, "Opening ArenaMP player menu.");
            setEditState(true);
        }
        setMenuState(true);
        mCommandLine->setVisible(activeTab == TAB_CHAT);
        // RP mode and the channel may have been changed from the keyboard since
        // the menu was last open.
        requestChatState();
        // Don't trust a cached staff flag from a previous session or another room.
        mDecorAllowed = false;
        mDecorIsStaff = false;
        refreshPlayerMenu();
        // Discover staff/ownership on every open: the server is authoritative.
        ArenaHousing::get().sendFurnitureAction("ROLE", "_");
    }

    void GUIChat::openDecorationsPage(bool requestCatalog)
    {
        openPlayerMenu();
        // Optimistically expose the page for this local navigation request;
        // ROLE is already sent by openPlayerMenu() and will hide it again if the
        // server says this account/cell cannot use decorations.
        mDecorAllowed = true;
        if (requestCatalog)
            selectTab(TAB_DECOR);
        else
        {
            // FS_BEGIN already *is* the catalogue response. Re-open its host
            // without sending a second OPEN request back to the server. This is
            // important after admin world capture: the player menu was closed to
            // target an object, and the response must return here rather than
            // resurrecting the retired standalone furniture window.
            activeTab = TAB_DECOR;
            activeDrawer = DRAWER_NONE;
            refreshPlayerMenu();
            updateCommandLineLayout();
            MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(nullptr);
        }
    }

    void GUIChat::closeForWorldAction()
    {
        setEditState(false);
    }

    void GUIChat::keyPress(MyGUI::Widget*, MyGUI::KeyCode key, MyGUI::Char)
    {
        if (key == MyGUI::KeyCode::Escape)
        {
            setHistoryReviewState(false);
            setEditState(false);
            return;
        }

        if (key == MyGUI::KeyCode::Return || key == MyGUI::KeyCode::NumpadEnter)
        {
            acceptCommand(mCommandLine);
            return;
        }

        updateCommandLineLayout();

        const bool multiRow = mCommandLine->getOnlyText().find('\n') != std::string::npos
            || mCommandLine->getTextSize().height > 24;
        if (multiRow && (key == MyGUI::KeyCode::ArrowUp || key == MyGUI::KeyCode::ArrowDown))
            return;

        if (mCommandHistory.empty())
            return;

        if (key == MyGUI::KeyCode::ArrowUp)
        {
            if (mCurrent == mCommandHistory.end())
                mEditString = mCommandLine->getOnlyText();

            if (mCurrent != mCommandHistory.begin())
            {
                --mCurrent;
                mCommandLine->setCaption(*mCurrent);
            }
        }
        else if (key == MyGUI::KeyCode::ArrowDown)
        {
            if (mCurrent != mCommandHistory.end())
            {
                ++mCurrent;
                if (mCurrent != mCommandHistory.end())
                    mCommandLine->setCaption(*mCurrent);
                else
                    mCommandLine->setCaption(mEditString);
            }
        }
    }

    void GUIChat::update(float dt)
    {
        if (!mRecallRequest.empty())
        {
            mRecallTimeLeft -= std::max(0.f, dt);
            if (mRecallTimeLeft <= 0.f)
            {
                mRecallRequest.clear();
                MWBase::Environment::get().getWindowManager()->messageBox(
                    "#{arenamp=recall.expired}");
            }
        }
        syncSettings();

        // U024e: eventMouseDrag is not dependable on every MyGUI/OpenMW
        // combination used by ArenaMP. Once the title bar or resize grip has
        // captured the mouse, update directly from InputManager every frame.
        // This makes movement deterministic even when no drag event is emitted.
        if (geometryDragMode != GEOMETRY_DRAG_NONE)
        {
            if (MyGUI::InputManager::getInstance().isCaptureMouse())
                updateGeometryDrag();
            else
            {
                geometryDragMode = GEOMETRY_DRAG_NONE;
                if (geometryDirty)
                    persistGeometry();
            }
        }

        if (editState && activeTab == TAB_CHAT)
            updateCommandLineLayout();

        if (geometryDirty)
        {
            geometrySaveDelay = std::max(0.f, geometrySaveDelay - std::max(0.f, dt));
            if (geometrySaveDelay <= 0.f)
                persistGeometry();
        }

        if (mainMenuOpen)
            return;

        // Y024: never let a plain gameplay cursor turn the HUD feed into a
        // scrollable history view. The compact HUD always tracks the newest
        // line; manual scrolling exists only in the Player Menu (and the pause
        // menu has its own history widget).
        if (!menuState && !historyReviewState)
        {
            const size_t range = mHistory->getVScrollRange();
            if (range > 0 && mHistory->getVScrollPosition() + 1 < range)
                scrollHistoryToBottom();
        }

        if (revealTime > 0.f && !editState && !historyReviewState)
        {
            revealTime = std::max(0.f, revealTime - dt);
            if (revealTime == 0.f)
                refreshPresentation();
        }

        if (!isVisible())
            return;

        currentAlpha = moveTowards(currentAlpha, targetAlpha, std::max(0.f, dt) * sFadeSpeed);
        applyAlpha(currentAlpha);

        if (hideAfterFade && currentAlpha <= 0.001f)
        {
            currentAlpha = 0.f;
            targetAlpha = 0.f;
            hideAfterFade = false;
            applyAlpha(0.f);
            setVisible(false);
        }
    }

    void GUIChat::refreshPresentation()
    {
        if (mainMenuOpen)
            return;

        refreshPlayerMenu();

        // Input/player-menu mode is always fully opaque regardless of HUD opacity.
        if (editState)
        {
            showSmoothly(sFullyVisibleAlpha);
            return;
        }

        if (!historyDisplayEnabled)
        {
            hideSmoothly();
            return;
        }

        if (historyReviewState)
        {
            showSmoothly(sFullyVisibleAlpha);
            return;
        }

        if (revealTime > 0.f && windowState != CHAT_HIDDEN)
        {
            showSmoothly(sFullyVisibleAlpha);
            return;
        }

        const float restingAlpha = getRestingAlpha();
        if (restingAlpha > 0.f)
            showSmoothly(restingAlpha);
        else
            hideSmoothly();
    }

    void GUIChat::refreshPlayerMenu()
    {
        // Z005: sell the house while standing on its own page and the page has
        // to go with it, otherwise the menu keeps a tab nothing can reach.
        if (activeTab == TAB_DECOR && !mDecorAllowed) activeTab = TAB_CHAT;
        if (activeTab == TAB_HOME && !ArenaHousing::get().ownsHome() && !ArenaHousing::get().hasItemWallet())
            activeTab = TAB_CHAT;
        // X054: menuVisible is the expanded Player Menu; editorVisible is the
        // plain HUD caret, which must keep working with no menu chrome at all.
        const bool menuVisible = editState && menuState && !mainMenuOpen;
        const bool chatVisible = menuVisible && activeTab == TAB_CHAT;
        const bool editorVisible = editState && !mainMenuOpen && (!menuState || activeTab == TAB_CHAT);

        const bool layoutVisible = historyReviewState && !mainMenuOpen;
        mLayoutFrame->setVisible(layoutVisible);
        // Do not rely only on the .layout NeedMouse flag: older MyGUI/OpenMW
        // combinations can retain a stale focus mask when a hidden overlay is
        // shown again. Force all three editor surfaces into the active state.
        // MyGUI stops child hit-testing when a parent has NeedMouse=false.
        // Keep the frame pickable so the Button children can receive capture;
        // only the title bar and resize grip have drag delegates attached.
        mLayoutFrame->setNeedMouseFocus(layoutVisible);
        mLayoutDrag->setNeedMouseFocus(layoutVisible);
        mLayoutResize->setNeedMouseFocus(layoutVisible);
        mPanelBackground->setVisible(menuVisible);
        mDragHandle->setVisible(menuVisible);
        mTabHome->setVisible(ArenaHousing::get().ownsHome() || ArenaHousing::get().hasItemWallet());
        mTabDecor->setVisible(mDecorAllowed);
        mChatToolbar->setVisible(chatVisible);
        mStayOpenButton->setVisible(chatVisible);
        mSendButton->setVisible(chatVisible);
        mEmojiBar->setVisible(chatVisible && activeDrawer == DRAWER_EMOJI);
        mColorBar->setVisible(chatVisible && activeDrawer == DRAWER_COLOR);
        mGroupPane->setVisible(menuVisible && activeTab == TAB_GROUP);
        mPlayersPane->setVisible(menuVisible && activeTab == TAB_PLAYERS);
        mProfilePane->setVisible(menuVisible && activeTab == TAB_STATS);
        mHomePane->setVisible(menuVisible && activeTab == TAB_HOME);
        mDecorPane->setVisible(menuVisible && activeTab == TAB_DECOR);
        // The Decorations tab is now only a host for the native furniture catalogue.
        // Never flash the retired category landing page while waiting for FS_BEGIN.
        if (menuVisible && activeTab == TAB_DECOR)
        {
            mDecorTitle->setVisible(false);
            mDecorList->setVisible(false);
            mDecorBack->setVisible(false);
            mDecorOpen->setVisible(false);
            mDecorRefresh->setVisible(false);
            mDecorCapture->setVisible(false);
            getWidget("DecorHint")->setVisible(false);
        }

        mCommandLine->setVisible(editorVisible);
        mHistory->setVisible(historyDisplayEnabled && (!menuVisible || chatVisible));

        // Y024: the history scrollbar is an explicit Player Menu feature.
        // Showing the ordinary game cursor must leave the HUD chat transparent
        // to the mouse and must not expose a scrollbar.
        mHistory->setNeedMouseFocus(chatVisible);
        if (mHistoryScroll)
        {
            mHistoryScroll->setVisible(chatVisible);
            mHistoryScroll->setNeedMouseFocus(chatVisible);
        }

        if (!menuVisible && !historyReviewState)
            scrollHistoryToBottom();

        updateToggleButtons();
        updateGroupControls();
        updateColorButtons();
        if (menuVisible)
            applyMenuLayout();
    }

    void GUIChat::selectTab(PlayerMenuTab tab, bool persist)
    {
        activeTab = tab;
        if (persist)
        {
            Settings::Manager::setInt("player menu tab", "Chat", static_cast<int>(activeTab));
            Settings::Manager::saveUser();
        }

        activeDrawer = DRAWER_NONE;
        refreshPlayerMenu();
        updateCommandLineLayout();

        if (!editState)
            return;

        if (activeTab == TAB_CHAT)
            MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mCommandLine);
        else
        {
            MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(nullptr);
            if (activeTab == TAB_GROUP)
                requestGroupState();
            else if (activeTab == TAB_PLAYERS)
                requestPlayerList();
            else if (activeTab == TAB_STATS)
                requestProfileState();
            else if (activeTab == TAB_HOME)
                requestHomeState();
            else if (activeTab == TAB_DECOR)
            {
                // Open the full catalogue immediately inside the Player Menu.
                // The old MENU/category landing page is no longer part of the flow.
                for (MyGUI::Widget* child : {static_cast<MyGUI::Widget*>(mDecorTitle),
                         static_cast<MyGUI::Widget*>(mDecorList), static_cast<MyGUI::Widget*>(mDecorBack),
                         static_cast<MyGUI::Widget*>(mDecorOpen), static_cast<MyGUI::Widget*>(mDecorCapture),
                         static_cast<MyGUI::Widget*>(mDecorRefresh)})
                    if (child) child->setVisible(false);
                getWidget("DecorHint")->setVisible(false);
                ArenaHousing::get().requestFurnitureMenu();
            }
        }
    }

    void GUIChat::updateToggleButtons()
    {
        mTabChat->setStateSelected(activeTab == TAB_CHAT);
        mTabGroup->setStateSelected(activeTab == TAB_GROUP);
        mTabPlayers->setStateSelected(activeTab == TAB_PLAYERS);
        mTabStats->setStateSelected(activeTab == TAB_STATS);
        mTabHome->setStateSelected(activeTab == TAB_HOME);
        mTabDecor->setStateSelected(activeTab == TAB_DECOR);
        mModeOoc->setStateSelected(!rpMode);
        mModeRp->setStateSelected(rpMode);
        mChannelSay->setStateSelected(chatChannel == CHANNEL_SAY);
        mChannelWhisper->setStateSelected(chatChannel == CHANNEL_WHISPER);
        mChannelShout->setStateSelected(chatChannel == CHANNEL_SHOUT);
        mChannelLocalOoc->setStateSelected(chatChannel == CHANNEL_LOCAL_OOC);
        mChannelGlobalOoc->setStateSelected(chatChannel == CHANNEL_GLOBAL_OOC);
        mStylePlain->setStateSelected(chatStyle == STYLE_PLAIN);
        mStyleMe->setStateSelected(chatStyle == STYLE_ME);
        mStyleDo->setStateSelected(chatStyle == STYLE_DO);
        mStyleTry->setStateSelected(chatStyle == STYLE_TRY);
        mStayOpenButton->setStateSelected(stayOpenAfterSend);
        mEmojiToggleButton->setStateSelected(activeDrawer == DRAWER_EMOJI);
        mColorToggleButton->setStateSelected(activeDrawer == DRAWER_COLOR);
    }

    void GUIChat::requestChatState()
    {
        send("/chatcolor state");
    }

    // X054: coreChat owns RP status and the speech channel, so the toolbar
    // shows what the server reports rather than a local guess. Fields 4..6 are
    // appended by X054's chatColorHelper; an older server simply omits them and
    // the buttons keep their last value.
    void GUIChat::applyChatState(const std::vector<std::string>& fields)
    {
        if (fields.size() > 3)
        {
            const bool serverRp = fields[3] == "1";
            if (serverRp != rpMode)
                setRpMode(serverRp);
        }

        if (fields.size() > 4)
        {
            // coreChat channel ids: 1 global OOC, 2 speak, 3 whisper,
            // 4 shout, 5 local OOC.
            switch (std::atoi(fields[4].c_str()))
            {
                case 1: chatChannel = CHANNEL_GLOBAL_OOC; break;
                case 3: chatChannel = CHANNEL_WHISPER; break;
                case 4: chatChannel = CHANNEL_SHOUT; break;
                case 5: chatChannel = CHANNEL_LOCAL_OOC; break;
                case 2:
                default: chatChannel = CHANNEL_SAY; break;
            }
            updateToggleButtons();
        }
    }

    void GUIChat::setChatChannel(ChatChannel channel)
    {
        chatChannel = channel;
        const char* setting = "say";
        switch (chatChannel)
        {
            case CHANNEL_WHISPER: setting = "whisper"; break;
            case CHANNEL_SHOUT: setting = "shout"; break;
            case CHANNEL_LOCAL_OOC: setting = "local"; break;
            case CHANNEL_GLOBAL_OOC: setting = "global"; break;
            default: break;
        }
        Settings::Manager::setString("send channel", "Chat", setting);
        Settings::Manager::saveUser();
        if (chatStyle == STYLE_PLAIN)
        {
            if (chatChannel == CHANNEL_GLOBAL_OOC) setRememberedPrefix("///", true);
            else if (chatChannel == CHANNEL_LOCAL_OOC) setRememberedPrefix("//", true);
            else if (chatChannel == CHANNEL_SAY) setRememberedPrefix("/s", true);
        }
        updateToggleButtons();
    }

    void GUIChat::setChatStyle(ChatStyle style)
    {
        chatStyle = style;
        const char* setting = chatStyle == STYLE_ME ? "me"
            : (chatStyle == STYLE_DO ? "do" : (chatStyle == STYLE_TRY ? "try" : "plain"));
        Settings::Manager::setString("send style", "Chat", setting);
        Settings::Manager::saveUser();
        if (chatStyle == STYLE_ME) setRememberedPrefix("/me", true);
        else if (chatStyle == STYLE_DO) setRememberedPrefix("/do", true);
        else if (chatStyle == STYLE_TRY) setRememberedPrefix("/try", true);
        else if (chatChannel == CHANNEL_GLOBAL_OOC) setRememberedPrefix("///", true);
        else if (chatChannel == CHANNEL_LOCAL_OOC) setRememberedPrefix("//", true);
        else if (chatChannel == CHANNEL_SAY) setRememberedPrefix("/s", true);
        updateToggleButtons();
    }

    void GUIChat::setRpMode(bool enabled)
    {
        rpMode = enabled;
        Settings::Manager::setBool("rp mode", "Chat", rpMode);
        Settings::Manager::saveUser();
        updateToggleButtons();
    }

    void GUIChat::setStayOpenAfterSend(bool enabled)
    {
        stayOpenAfterSend = enabled;
        Settings::Manager::setBool("stay after send", "Chat", stayOpenAfterSend);
        Settings::Manager::saveUser();
        updateToggleButtons();
    }

    void GUIChat::requestGroupState()
    {
        send("/groupui state");
    }

    void GUIChat::sendGroupAction(const std::string& action, const std::string& argument)
    {
        std::string command = "/groupui " + action;
        if (!argument.empty())
            command += " " + argument;
        send(command);
    }

    void GUIChat::updateGroupControls()
    {
        if (mGroupCreateButton == nullptr)
            return;

        const bool leaderTools = groupInGroup && groupIsLeader;
        mGroupNameLabel->setVisible(!groupInGroup);
        mGroupNameEdit->setVisible(!groupInGroup);
        mGroupCreateButton->setVisible(!groupInGroup);

        mGroupTargetLabel->setVisible(leaderTools);
        mGroupTargetEdit->setVisible(leaderTools);
        mGroupInviteButton->setVisible(leaderTools);
        mGroupKickButton->setVisible(leaderTools);
        mGroupLeaderButton->setVisible(leaderTools);

        mGroupJournalButton->setVisible(groupInGroup);
        mGroupTopicsButton->setVisible(groupInGroup);
        mGroupLeaveButton->setVisible(groupInGroup);
        mGroupDisbandButton->setVisible(leaderTools);

        mGroupAcceptButton->setVisible(!groupInGroup && groupPendingInvite);
        mGroupDeclineButton->setVisible(!groupInGroup && groupPendingInvite);
        mGroupRefreshButton->setVisible(true);
        mGroupRoster->setVisible(true);
        mGroupRosterLabel->setVisible(true);
        mGroupTargetLabel->setVisible(leaderTools);
        mGroupTargetEdit->setVisible(leaderTools);

        // Z005: party voice. The button exists only while you are in a group,
        // and it says what it will do, not what it is.
        VoiceChat* voice = Main::isInitialized() ? Main::get().getVoiceChat() : nullptr;
        if (voice != nullptr)
            voice->setInGroup(groupInGroup);
        // Z007: the caption states what the latch IS, not what pressing it
        // would do. A button that reads "turn it on" while it is already on is
        // exactly the confusion this replaces, and the state is remembered
        // between sessions, so it has to be readable at a glance.
        const bool groupVoiceOn = voice != nullptr && voice->isGroupTalk();
        mGroupVoiceButton->setVisible(groupInGroup);
        mGroupVoiceButton->setStateSelected(groupVoiceOn);
        setMenuCaption(mGroupVoiceButton,
            groupVoiceOn ? "Голос группы ВКЛ" : "Голос группы ВЫКЛ");

        mGroupJournalButton->setStateSelected(groupJournalSync);
        mGroupTopicsButton->setStateSelected(groupTopicSync);
        mGroupJournalButton->setCaption(localizeArena("chat.group.sync_journal") + ": "
            + localizeArena(groupJournalSync ? "chat.group.on" : "chat.group.off"));
        mGroupTopicsButton->setCaption(localizeArena("chat.group.sync_topics") + ": "
            + localizeArena(groupTopicSync ? "chat.group.on" : "chat.group.off"));

        // Z006: this function is also called on its own - when the server's
        // group state arrives, and when the voice latch is flipped - and each
        // of those changes WHICH buttons are visible. layoutRow only places the
        // visible ones, so without a re-layout the newly shown button kept the
        // position from the .layout file and landed on top of a neighbour that
        // had already been stretched to fill the row.
        if (menuState && editState && !mainMenuOpen)
            applyMenuLayout();
    }

    void GUIChat::rebuildGroupInfo(const std::vector<std::string>& fields)
    {
        groupInGroup = controlBool(fields, 1);
        groupIsLeader = controlBool(fields, 2);
        groupJournalSync = controlBool(fields, 3);
        groupTopicSync = controlBool(fields, 4);
        groupPendingInvite = controlBool(fields, 5);

        const std::string groupName = fields.size() > 6 ? unescapeControlField(fields[6]) : std::string();
        const std::string leaderName = fields.size() > 7 ? unescapeControlField(fields[7]) : std::string();
        const std::string membersField = fields.size() > 8 ? fields[8] : std::string();
        const std::string inviteFrom = fields.size() > 9 ? unescapeControlField(fields[9]) : std::string();
        const std::string inviteGroup = fields.size() > 10 ? unescapeControlField(fields[10]) : std::string();

        std::ostringstream info;
        if (!groupInGroup)
        {
            info << localizeArena("chat.group.not_member");
            if (groupPendingInvite)
            {
                info << "\n\n" << localizeArena("chat.group.invite_pending") << "\n"
                     << localizeArena("chat.group.inviter") << ": " << inviteFrom << "\n"
                     << localizeArena("chat.group.group") << ": " << inviteGroup;
            }
        }
        else
        {
            info << localizeArena("chat.group.group") << ": " << groupName << "\n"
                 << localizeArena("chat.group.role") << ": "
                 << localizeArena(groupIsLeader ? "chat.group.role_leader" : "chat.group.role_member") << "\n"
                 << localizeArena("chat.group.leader_name") << ": " << leaderName << "\n"
                 << localizeArena("chat.group.xp_rule") << "\n\n"
                 << localizeArena("chat.group.members") << ":";

            const std::vector<std::string> members = splitControlFields(membersField, ';');
            for (const std::string& encoded : members)
            {
                if (encoded.empty())
                    continue;
                const std::vector<std::string> member = splitControlFields(encoded, '^');
                const std::string name = !member.empty() ? unescapeControlField(member[0]) : "?";
                const bool online = member.size() > 1 && member[1] == "1";
                const bool sameCell = member.size() > 2 && member[2] == "1";
                const bool leader = member.size() > 3 && member[3] == "1";
                info << "\n" << (leader ? "* " : "- ") << name << " ["
                     << localizeArena(!online ? "chat.group.offline" : (sameCell ? "chat.group.same_cell" : "chat.group.other_cell"))
                     << "]";
            }
        }

        mGroupInfo->setCaption(info.str());
        // X052 appends the online roster as field 11. Older servers simply omit
        // it, in which case the list stays empty instead of breaking the page.
        rebuildGroupRoster(fields.size() > 11 ? fields[11] : std::string());
        updateGroupControls();
    }

    void GUIChat::onGroupVoiceClicked(MyGUI::Widget* sender)
    {
        if (sender != mGroupVoiceButton || !Main::isInitialized())
            return;
        VoiceChat* voice = Main::get().getVoiceChat();
        if (voice == nullptr)
            return;
        // World voice on V is untouched: this latch is the group channel, and
        // it is the only way into it.
        voice->setGroupTalk(!voice->isGroupTalk());
        updateGroupControls();
    }

    void GUIChat::onTabClicked(MyGUI::Widget* sender)
    {
        // Tabs only exist while the expanded menu is on screen, but keep this
        // defensive: any tab interaction implies the menu tier.
        setMenuState(true);
        if (sender == mTabGroup)
            selectTab(TAB_GROUP);
        else if (sender == mTabPlayers)
            selectTab(TAB_PLAYERS);
        else if (sender == mTabStats)
            selectTab(TAB_STATS);
        else if (sender == mTabHome)
            selectTab(TAB_HOME);
        else if (sender == mTabDecor && mDecorAllowed)
            selectTab(TAB_DECOR);
        else
            selectTab(TAB_CHAT);
    }

    void GUIChat::requestDecorMenu(const std::string& category, int page)
    {
        if (!mDecorAllowed) return;
        mDecorCategory = category;
        ArenaHousing::get().sendFurnitureAction("MENU", category.empty()?"_":category, page);
    }
    void GUIChat::onDecorClick(MyGUI::Widget*)
    {
        const std::size_t index = mDecorList->getIndexSelected();
        if (index == MyGUI::ITEM_NONE)
            return;
        onDecorSelect(mDecorList, index);
    }

    void GUIChat::onDecorSelect(MyGUI::ListBox*, size_t index)
    {
        if (index >= mDecorValues.size()) return;
        if (mDecorValues[index] == "@empty") return;
        if (!mDecorShowItems)
        {
            requestDecorMenu(mDecorValues[index]);return;
        }
        if (mDecorValues[index] == "@prev") { requestDecorMenu(mDecorCategory, mDecorPage - 1);return; }
        if (mDecorValues[index] == "@next") { requestDecorMenu(mDecorCategory, mDecorPage + 1);return; }
        // Open the same catalogue *inside* the Player Menu with this item selected.
        ArenaHousing::get().sendFurnitureAction("VIEW", mDecorValues[index]);
    }
    void GUIChat::onDecorBack(MyGUI::Widget*)
    {
        if (mDecorShowItems) requestDecorMenu();
        else selectTab(TAB_CHAT);
    }
    void GUIChat::onDecorOpen(MyGUI::Widget*)
    {
        ArenaHousing::get().requestFurnitureMenu();
    }
    void GUIChat::onDecorRefresh(MyGUI::Widget*)
    {
        if (mDecorIsStaff) ArenaHousing::get().sendFurnitureAction("REFRESH", "_");
    }
    void GUIChat::onDecorCapture(MyGUI::Widget*)
    {
        if (!mDecorIsStaff) return;
        // Arm one-use capture and allow the administrator to aim in the world.
        setEditState(false);
        ArenaHousing::get().armFurnitureCapture();
    }

    void GUIChat::onModeClicked(MyGUI::Widget* sender)
    {
        // X054: RP mode belongs to coreChat (customVariables.RPStatus), not to
        // a client setting. /rp is a toggle, so only send it when the requested
        // state actually differs, then redraw from the server's reply.
        const bool wanted = (sender == mModeRp);
        if (wanted == rpMode)
            return;

        send("/rp");
        setRpMode(wanted);
        requestChatState();
    }

    void GUIChat::onChannelClicked(MyGUI::Widget* sender)
    {
        if (sender == mChannelWhisper)
            setChatChannel(CHANNEL_WHISPER);
        else if (sender == mChannelShout)
            setChatChannel(CHANNEL_SHOUT);
        else if (sender == mChannelLocalOoc)
            setChatChannel(CHANNEL_LOCAL_OOC);
        else if (sender == mChannelGlobalOoc)
            setChatChannel(CHANNEL_GLOBAL_OOC);
        else
            setChatChannel(CHANNEL_SAY);
    }

    void GUIChat::onStyleClicked(MyGUI::Widget* sender)
    {
        if (sender == mStyleMe)
            setChatStyle(STYLE_ME);
        else if (sender == mStyleDo)
            setChatStyle(STYLE_DO);
        else if (sender == mStyleTry)
            setChatStyle(STYLE_TRY);
        else
            setChatStyle(STYLE_PLAIN);
    }

    void GUIChat::onStayOpenClicked(MyGUI::Widget*)
    {
        setStayOpenAfterSend(!stayOpenAfterSend);
    }

    void GUIChat::onSendClicked(MyGUI::Widget*)
    {
        acceptCommand(mCommandLine);
    }

    void GUIChat::onReturnClicked(MyGUI::Widget*)
    {
        setEditState(false);
    }

    void GUIChat::onEmojiToggleClicked(MyGUI::Widget*)
    {
        setDrawer(activeDrawer == DRAWER_EMOJI ? DRAWER_NONE : DRAWER_EMOJI);
    }

    void GUIChat::onColorToggleClicked(MyGUI::Widget*)
    {
        setDrawer(activeDrawer == DRAWER_COLOR ? DRAWER_NONE : DRAWER_COLOR);
    }

    void GUIChat::onEmojiClicked(MyGUI::Widget* sender)
    {
        for (int i = 0; i < sEmojiSlotCount; ++i)
        {
            if (sender != mEmojiButtons[i])
                continue;

            // X052: insert at the caret instead of blindly appending, and keep
            // the caret behind the inserted token so typing continues normally.
            MyGUI::UString token(emojiSymbol(i));
            MyGUI::UString text = mCommandLine->getOnlyText();
            std::size_t caret = mCommandLine->getTextCursor();
            if (caret > text.size())
                caret = text.size();
            text.insert(caret, token);
            mCommandLine->setCaption(text);
            mCommandLine->setTextCursor(caret + token.size());
            MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mCommandLine);
            updateCommandLineLayout();
            break;
        }
    }

    void GUIChat::onColorClicked(MyGUI::Widget* sender)
    {
        for (int i = 0; i < sColorSlotCount; ++i)
        {
            if (sender != mColorButtons[i] || static_cast<std::size_t>(i) >= colorPalette.size())
                continue;
            // /chatcolor now writes customVariables.chatColor, the same
            // variable /color writes, so the pick is live everywhere at once.
            send("/chatcolor " + std::to_string(i + 1));
            selectedColorIndex = i;
            updateColorButtons();
            break;
        }
    }

    void GUIChat::onRosterSelected(MyGUI::ListBox*, size_t index)
    {
        if (index >= groupRosterNames.size())
            return;
        mGroupTargetEdit->setCaption(groupRosterNames[index]);
    }

    void GUIChat::onRosterAccepted(MyGUI::ListBox*, size_t index)
    {
        if (index >= groupRosterNames.size())
            return;
        mGroupTargetEdit->setCaption(groupRosterNames[index]);
        // Double click is the natural "do the obvious thing" shortcut: invite a
        // stranger, kick a member. Both are leader-only server side anyway.
        if (groupInGroup && groupIsLeader)
            sendGroupAction("invite", groupRosterNames[index]);
    }

    void GUIChat::onGroupButtonClicked(MyGUI::Widget* sender)
    {
        if (sender == mGroupRefreshButton)
            requestGroupState();
        else if (sender == mGroupCreateButton)
        {
            sendGroupAction("create", mGroupNameEdit->getOnlyText());
            mGroupNameEdit->setCaption("");
        }
        else if (sender == mGroupInviteButton || sender == mGroupKickButton || sender == mGroupLeaderButton)
        {
            // The typed name wins; an empty field falls back to whatever is
            // highlighted in the roster, so invites need no typing at all.
            std::string target = mGroupTargetEdit->getOnlyText();
            if (target.empty())
                target = selectedRosterName();
            if (target.empty())
                return;

            if (sender == mGroupInviteButton)
                sendGroupAction("invite", target);
            else if (sender == mGroupKickButton)
                sendGroupAction("kick", target);
            else
                sendGroupAction("leader", target);
        }
        else if (sender == mGroupJournalButton)
            sendGroupAction("journal");
        else if (sender == mGroupTopicsButton)
            sendGroupAction("topics");
        else if (sender == mGroupLeaveButton)
            sendGroupAction("leave");
        else if (sender == mGroupDisbandButton)
            sendGroupAction("disband");
        else if (sender == mGroupAcceptButton)
            sendGroupAction("accept");
        else if (sender == mGroupDeclineButton)
            sendGroupAction("decline");
    }

    void GUIChat::onDragStart(MyGUI::Widget* sender, int, int, MyGUI::MouseButton id)
    {
        if (id != MyGUI::MouseButton::Left)
            return;

        if (historyReviewState)
        {
            if (sender == mLayoutResize)
                geometryDragMode = GEOMETRY_DRAG_HUD_RESIZE;
            else if (sender == mLayoutDrag)
                geometryDragMode = GEOMETRY_DRAG_HUD_MOVE;
            else
                return;
        }
        else if (editState && menuState && sender == mDragHandle)
            geometryDragMode = GEOMETRY_DRAG_MENU_MOVE;
        else
            return;

        dragStartMouse = MyGUI::InputManager::getInstance().getMousePosition();
        dragStartWindow = mMainWidget->getPosition();
        dragStartSize = mMainWidget->getSize();

        // Apply immediately as well as from update(). This also proves the
        // actual _Main widget, not merely the decorative frame, owns geometry.
        updateGeometryDrag();
    }

    void GUIChat::onDrag(MyGUI::Widget*, int, int, MyGUI::MouseButton id)
    {
        if (id == MyGUI::MouseButton::Left && geometryDragMode != GEOMETRY_DRAG_NONE)
            updateGeometryDrag();
    }

    void GUIChat::updateGeometryDrag()
    {
        if (geometryDragMode == GEOMETRY_DRAG_NONE || !mMainWidget)
            return;

        const MyGUI::IntPoint delta = MyGUI::InputManager::getInstance().getMousePosition() - dragStartMouse;
        const MyGUI::IntSize view = MyGUI::RenderManager::getInstance().getViewSize();

        if (geometryDragMode == GEOMETRY_DRAG_HUD_RESIZE)
        {
            hudCoord = MyGUI::IntCoord(dragStartWindow.left, dragStartWindow.top,
                std::max(240, dragStartSize.width + delta.left),
                std::max(120, dragStartSize.height + delta.top));
            if (view.width > 0 && view.height > 0)
            {
                hudCoord.width = std::min(hudCoord.width, std::max(1, view.width - hudCoord.left));
                hudCoord.height = std::min(hudCoord.height, std::max(1, view.height - hudCoord.top));
            }
            applyHudGeometry(view.width, view.height);
            hudCoord = mMainWidget->getCoord();
            updateCommandLineLayout();
        }
        else if (geometryDragMode == GEOMETRY_DRAG_HUD_MOVE)
        {
            hudCoord = MyGUI::IntCoord(dragStartWindow.left + delta.left, dragStartWindow.top + delta.top,
                dragStartSize.width, dragStartSize.height);
            applyHudGeometry(view.width, view.height);
            hudCoord = mMainWidget->getCoord();
            updateCommandLineLayout();
        }
        else if (geometryDragMode == GEOMETRY_DRAG_MENU_MOVE)
        {
            mMainWidget->setPosition(dragStartWindow + delta);
            clampToViewport(view.width, view.height);
            panelCoord = mMainWidget->getCoord();
            applyMenuLayout();
        }

        markGeometryDirty();
    }

    void GUIChat::onGeometryReleased(MyGUI::Widget*, int, int, MyGUI::MouseButton id)
    {
        if (id != MyGUI::MouseButton::Left)
            return;

        if (geometryDragMode != GEOMETRY_DRAG_NONE)
            updateGeometryDrag();
        geometryDragMode = GEOMETRY_DRAG_NONE;
        if (geometryDirty)
            persistGeometry();
    }

    void GUIChat::applyHudGeometry(int width, int height)
    {
        if (!mMainWidget)
            return;
        MyGUI::IntCoord visible = hudCoord;
        if (width > 0 && height > 0)
        {
            visible.width = std::min(std::max(240, visible.width), width);
            visible.height = std::min(std::max(120, visible.height), height);
            visible.left = std::max(0, std::min(visible.left, width - visible.width));
            visible.top = std::max(0, std::min(visible.top, height - visible.height));
        }
        mMainWidget->setCoord(visible);
    }

    void GUIChat::applyPanelGeometry(int width, int height)
    {
        if (!mMainWidget)
            return;
        mMainWidget->setCoord(panelCoord);
        clampToViewport(width, height);
        panelCoord = mMainWidget->getCoord();
    }

    void GUIChat::applyStateGeometry()
    {
        const MyGUI::IntSize view = MyGUI::RenderManager::getInstance().getViewSize();
        if (editState && menuState)
            applyPanelGeometry(view.width, view.height);
        else
            applyHudGeometry(view.width, view.height);
    }

    void GUIChat::syncInteractiveInputMode()
    {
        MWBase::WindowManager* windowManager = MWBase::Environment::get().getWindowManager();
        const MWGui::GuiMode playerMenuMode = static_cast<MWGui::GuiMode>(GUIController::GM_ARENAMP_PlayerMenu);
        // X054: the compact HUD caret deliberately does NOT enter GUI mode, so
        // mouse-look stays live exactly as it did before the player menu existed.
        const bool interactive = !mainMenuOpen && ((editState && menuState) || historyReviewState);

        if (interactive)
        {
            if (!windowManager->containsMode(playerMenuMode))
                windowManager->pushGuiMode(playerMenuMode);
            // Fallback for forks that defer WindowManager::updateVisible().
            MWBase::Environment::get().getInputManager()->changeInputMode(true);
        }
        else if (windowManager->containsMode(playerMenuMode))
            windowManager->removeGuiMode(playerMenuMode);
    }

    void GUIChat::clampToViewport(int width, int height)
    {
        if (!mMainWidget || width <= 0 || height <= 0)
            return;

        MyGUI::IntSize size = mMainWidget->getSize();
        size.width = std::min(std::max(sMinimumPanelWidth, size.width), std::max(sMinimumPanelWidth, width));
        size.height = std::min(std::max(sMinimumPanelHeight, size.height), std::max(sMinimumPanelHeight, height));
        if (width < sMinimumPanelWidth)
            size.width = width;
        if (height < sMinimumPanelHeight)
            size.height = height;
        mMainWidget->setSize(size);

        MyGUI::IntPoint pos = mMainWidget->getPosition();
        pos.left = std::max(0, std::min(pos.left, std::max(0, width - size.width)));
        pos.top = std::max(0, std::min(pos.top, std::max(0, height - size.height)));
        mMainWidget->setPosition(pos);
    }

    void GUIChat::markGeometryDirty()
    {
        geometryDirty = true;
        geometrySaveDelay = sGeometrySaveDebounce;
    }

    void GUIChat::persistGeometry()
    {
        if (!mMainWidget)
            return;

        if (editState && menuState)
            panelCoord = mMainWidget->getCoord();
        Settings::Manager::setInt("x", "Chat", hudCoord.left);
        Settings::Manager::setInt("y", "Chat", hudCoord.top);
        Settings::Manager::setInt("w", "Chat", hudCoord.width);
        Settings::Manager::setInt("h", "Chat", hudCoord.height);
        Settings::Manager::setInt("menu x", "Chat", panelCoord.left);
        Settings::Manager::setInt("menu y", "Chat", panelCoord.top);
        Settings::Manager::setInt("menu w", "Chat", panelCoord.width);
        Settings::Manager::setInt("menu h", "Chat", panelCoord.height);
        Settings::Manager::saveUser();
        geometryDirty = false;
        geometrySaveDelay = 0.f;
    }

    void GUIChat::revealTemporarily()
    {
        revealTime = delay;
        refreshPresentation();
    }

    void GUIChat::showSmoothly(float alpha)
    {
        targetAlpha = std::max(0.f, std::min(1.f, alpha));
        hideAfterFade = false;

        if (!isVisible())
        {
            currentAlpha = 0.f;
            applyAlpha(0.f);
            setVisible(true);
        }
    }

    void GUIChat::hideSmoothly()
    {
        targetAlpha = 0.f;
        hideAfterFade = isVisible();

        if (!isVisible())
        {
            currentAlpha = 0.f;
            hideAfterFade = false;
        }
    }

    void GUIChat::applyAlpha(float alpha)
    {
        // Player-menu chrome stays readable while the passive history still obeys
        // the legacy opacity/autohide state machine.
        if (editState)
        {
            mHistory->setAlpha(1.f);
            mCommandLine->setAlpha(1.f);
            mPanelBackground->setAlpha(sMenuBackgroundAlpha);
            mDragHandle->setAlpha(1.f);
            mChatToolbar->setAlpha(1.f);
            mGroupPane->setAlpha(1.f);
        }
        else
        {
            mHistory->setAlpha(alpha);
            mCommandLine->setAlpha(alpha);
        }
    }

    float GUIChat::getRestingAlpha() const
    {
        switch (windowState)
        {
            case CHAT_VISIBLE: return sFullyVisibleAlpha;
            case CHAT_TRANSPARENT_30: return sThirtyPercentTransparentAlpha;
            case CHAT_TRANSPARENT_60: return sSixtyPercentTransparentAlpha;
            default: return 0.f;
        }
    }

    std::string GUIChat::getModeMessage() const
    {
        switch (windowState)
        {
            case CHAT_VISIBLE: return localizeArena("chat.mode.visible");
            case CHAT_TRANSPARENT_30: return localizeArena("chat.mode.opacity_30");
            case CHAT_TRANSPARENT_60: return localizeArena("chat.mode.opacity_60");
            case CHAT_AUTOHIDE: return localizeArena("chat.mode.autohide");
            case CHAT_HIDDEN: return localizeArena("chat.mode.hidden");
            default: return localizeArena("chat.mode.visible");
        }
    }

    std::string GUIChat::getModeSetting() const
    {
        switch (windowState)
        {
            case CHAT_VISIBLE: return "visible";
            case CHAT_TRANSPARENT_30: return "transparent30";
            case CHAT_TRANSPARENT_60: return "transparent60";
            case CHAT_AUTOHIDE: return "autohide";
            case CHAT_HIDDEN: return "hidden";
            default: return "transparent30";
        }
    }

    void GUIChat::setModeFromSetting(const std::string& mode)
    {
        if (mode == "visible")
            windowState = CHAT_VISIBLE;
        else if (mode == "transparent60")
            windowState = CHAT_TRANSPARENT_60;
        else if (mode == "autohide")
            windowState = CHAT_AUTOHIDE;
        else if (mode == "hidden")
            windowState = CHAT_HIDDEN;
        else
            windowState = CHAT_TRANSPARENT_30;
    }

    void GUIChat::syncSettings()
    {
        const bool enabled = externalHistoryDisplayEnabled && Settings::Manager::getBool("enabled", "Chat");
        const float configuredDelay = std::max(0.f, Settings::Manager::getFloat("delay", "Chat"));
        const std::string configuredMode = Settings::Manager::getString("mode", "Chat");

        bool changed = false;
        if (historyDisplayEnabled != enabled)
        {
            historyDisplayEnabled = enabled;
            if (!enabled && historyReviewState)
                setHistoryReviewState(false);
            changed = true;
        }
        if (std::abs(delay - configuredDelay) > 0.001f)
        {
            delay = configuredDelay;
            revealTime = std::min(revealTime, delay);
            changed = true;
        }

        const ChatWindowState previousState = windowState;
        setModeFromSetting(configuredMode);
        if (windowState != previousState)
        {
            if (windowState == CHAT_HIDDEN && !editState)
            {
                if (historyReviewState)
                    setHistoryReviewState(false);
            }
            revealTime = windowState == CHAT_AUTOHIDE ? delay : 0.f;
            changed = true;
        }

        const std::string channel = Settings::Manager::getString("send channel", "Chat");
        ChatChannel configuredChannel = CHANNEL_SAY;
        if (channel == "whisper") configuredChannel = CHANNEL_WHISPER;
        else if (channel == "shout") configuredChannel = CHANNEL_SHOUT;
        else if (channel == "local") configuredChannel = CHANNEL_LOCAL_OOC;
        else if (channel == "global") configuredChannel = CHANNEL_GLOBAL_OOC;
        if (chatChannel != configuredChannel)
        {
            chatChannel = configuredChannel;
            changed = true;
        }

        const std::string configuredPrefix = Settings::Manager::getString("input prefix", "Chat");
        if (rememberedPrefix != configuredPrefix)
        {
            rememberedPrefix = detectRememberedPrefix(configuredPrefix);
            changed = true;
        }

        const std::string style = Settings::Manager::getString("send style", "Chat");
        const ChatStyle configuredStyle = style == "me" ? STYLE_ME
            : (style == "do" ? STYLE_DO : (style == "try" ? STYLE_TRY : STYLE_PLAIN));
        if (chatStyle != configuredStyle)
        {
            chatStyle = configuredStyle;
            changed = true;
        }

        const bool configuredRp = Settings::Manager::getBool("rp mode", "Chat");
        if (rpMode != configuredRp)
        {
            rpMode = configuredRp;
            changed = true;
        }

        const bool configuredStay = Settings::Manager::getBool("stay after send", "Chat");
        if (stayOpenAfterSend != configuredStay)
        {
            stayOpenAfterSend = configuredStay;
            changed = true;
        }

        if (changed)
        {
            updateToggleButtons();
            refreshPresentation();
        }
    }

    void GUIChat::setHistoryDisplayEnabled(bool enabled)
    {
        externalHistoryDisplayEnabled = enabled;
        syncSettings();
    }


    // ------------------------------------------------------------------
    // X052: caption-aware layout
    //
    // Every hardcoded pixel width in the layout file is only a starting
    // rectangle. Buttons are re-sized here from the width their localized
    // caption actually measures, so a long Russian label can never be clipped
    // and a short English one does not leave a hole in the row.
    // ------------------------------------------------------------------

    void GUIChat::setMenuCaption(MyGUI::Widget* widget, const std::string& caption)
    {
        if (widget == nullptr)
            return;
        menuCaptions[widget] = caption;
        setWidgetCaption(widget, caption);
    }

    void GUIChat::refitCaption(MyGUI::Widget* widget)
    {
        if (widget == nullptr)
            return;

        const std::map<MyGUI::Widget*, std::string>::const_iterator entry = menuCaptions.find(widget);
        if (entry == menuCaptions.end())
            return;

        const std::string& full = entry->second;
        setWidgetCaption(widget, full);

        const int available = widget->getWidth() - 10;
        if (available <= 0 || measuredCaptionWidth(widget) <= available)
            return;

        // Only reached when even the proportional row pass could not give the
        // button enough room. Cut on a UTF-8 boundary so a multi-byte Cyrillic
        // character is never split into an invalid sequence.
        std::size_t length = utf8Length(full);
        while (length > 1)
        {
            --length;
            setWidgetCaption(widget, utf8Truncate(full, length) + "...");
            if (measuredCaptionWidth(widget) <= available)
                return;
        }
    }

    void GUIChat::layoutRow(const std::vector<MyGUI::Widget*>& row, int left, int top, int width,
        int height, int gap)
    {
        std::vector<MyGUI::Widget*> visible;
        for (std::size_t i = 0; i < row.size(); ++i)
        {
            if (row[i] != nullptr && row[i]->getVisible())
                visible.push_back(row[i]);
        }
        if (visible.empty())
            return;

        const int count = static_cast<int>(visible.size());
        std::vector<int> preferred(visible.size(), 0);
        int total = 0;
        for (std::size_t i = 0; i < visible.size(); ++i)
        {
            const std::map<MyGUI::Widget*, std::string>::const_iterator entry = menuCaptions.find(visible[i]);
            if (entry != menuCaptions.end())
                setWidgetCaption(visible[i], entry->second);

            int wanted = measuredCaptionWidth(visible[i]) + sButtonPadding;
            if (wanted < sMinimumButtonWidth)
                wanted = sMinimumButtonWidth;
            preferred[i] = wanted;
            total += wanted;
        }

        const int gaps = (count - 1) * gap;
        int usable = width - gaps;
        if (usable < count * sMinimumButtonWidth)
            usable = count * sMinimumButtonWidth;

        std::vector<int> resolved(visible.size(), 0);
        int assigned = 0;
        for (std::size_t i = 0; i < visible.size(); ++i)
        {
            const long long scaled = static_cast<long long>(preferred[i]) * usable / std::max(1, total);
            resolved[i] = std::max(sMinimumButtonWidth, static_cast<int>(scaled));
            assigned += resolved[i];
        }

        // Hand the rounding remainder to the widest button so the row ends
        // exactly on the right margin instead of a pixel short of it.
        int remainder = usable - assigned;
        if (remainder != 0)
        {
            std::size_t widest = 0;
            for (std::size_t i = 1; i < resolved.size(); ++i)
            {
                if (resolved[i] > resolved[widest])
                    widest = i;
            }
            resolved[widest] = std::max(sMinimumButtonWidth, resolved[widest] + remainder);
        }

        int x = left;
        for (std::size_t i = 0; i < visible.size(); ++i)
        {
            visible[i]->setCoord(x, top, resolved[i], height);
            x += resolved[i] + gap;
            refitCaption(visible[i]);
        }
    }

    void GUIChat::layoutGrid(const std::vector<MyGUI::Widget*>& cells, int left, int top, int width,
        int rowHeight, int columns, int gap)
    {
        if (columns <= 0 || cells.empty())
            return;

        const int cellWidth = std::max(sMinimumButtonWidth,
            (width - (columns - 1) * gap) / columns);
        for (std::size_t i = 0; i < cells.size(); ++i)
        {
            if (cells[i] == nullptr)
                continue;
            const int column = static_cast<int>(i) % columns;
            const int row = static_cast<int>(i) / columns;
            cells[i]->setCoord(left + column * (cellWidth + gap), top + row * (rowHeight + gap),
                cellWidth, rowHeight);
        }
    }

    void GUIChat::applyMenuLayout()
    {
        if (!mMainWidget || mDragHandle == nullptr)
            return;

        const int mainWidth = mMainWidget->getWidth();
        const int mainHeight = mMainWidget->getHeight();
        const int inner = std::max(120, mainWidth - sSideMargin * 2);

        // Title strip: the label keeps a fixed share, the tabs and the
        // "back to game" button share the rest.
        mDragHandle->setCoord(6, 6, std::max(80, mainWidth - 12), 32);
        const int titleWidth = std::max(90, std::min(190, mainWidth / 5));
        getWidget("PlayerMenuTitle")->setCoord(10, 5, titleWidth - 14, 22);

        // Y052: keep the close button compact and unmistakable. The four tabs
        // share the remaining title strip, while the red X always stays square.
        const int closeWidth = 30;
        const int tabsWidth = std::max(160, mDragHandle->getWidth() - titleWidth - closeWidth - sRowGap - 10);
        std::vector<MyGUI::Widget*> tabs;
        tabs.push_back(mTabChat);
        tabs.push_back(mTabGroup);
        tabs.push_back(mTabPlayers);
        tabs.push_back(mTabStats);
        if (ArenaHousing::get().ownsHome()) tabs.push_back(mTabHome);
        if (mDecorAllowed) tabs.push_back(mTabDecor);
        layoutRow(tabs, titleWidth, 4, tabsWidth, 24, sRowGap);
        mReturnButton->setCoord(titleWidth + tabsWidth + sRowGap, 4, closeWidth, 24);

        // X054: one container, two rows. Row 1 is the RP switch plus coreChat's
        // five speech channels; row 2 is the roleplay action styles and the two
        // drawers. Nine buttons on one line was already tight in RU, and
        // thirteen simply do not fit.
        mChatToolbar->setCoord(sSideMargin, sToolbarTop, inner, sToolbarHeight * sToolbarRows);
        std::vector<MyGUI::Widget*> toolbar;
        toolbar.push_back(mModeOoc);
        toolbar.push_back(mModeRp);
        toolbar.push_back(mChannelSay);
        toolbar.push_back(mChannelWhisper);
        toolbar.push_back(mChannelShout);
        toolbar.push_back(mChannelLocalOoc);
        toolbar.push_back(mChannelGlobalOoc);
        layoutRow(toolbar, 0, 2, inner, 26, sRowGap);

        std::vector<MyGUI::Widget*> toolbar2;
        toolbar2.push_back(mStylePlain);
        toolbar2.push_back(mStyleMe);
        toolbar2.push_back(mStyleDo);
        toolbar2.push_back(mStyleTry);
        toolbar2.push_back(mEmojiToggleButton);
        toolbar2.push_back(mColorToggleButton);
        layoutRow(toolbar2, 0, sToolbarHeight + 2, inner, 26, sRowGap);

        mEmojiBar->setCoord(sSideMargin, sDrawerTop, inner, sDrawerHeight);
        std::vector<MyGUI::Widget*> emoji;
        for (int i = 0; i < sEmojiSlotCount; ++i)
            emoji.push_back(mEmojiButtons[i]);
        layoutGrid(emoji, 6, 6, inner - 12, 26, 10, 4);

        // 40 swatches over two rows of 20.
        mColorBar->setCoord(sSideMargin, sDrawerTop, inner, sColorDrawerHeight);
        mColorBarLabel->setCoord(8, 4, inner - 16, 20);
        std::vector<MyGUI::Widget*> swatches;
        for (int i = 0; i < sColorSlotCount; ++i)
            swatches.push_back(mColorButtons[i]);
        layoutGrid(swatches, 6, 28, inner - 12, 26, 20, 4);

        const int footerTop = std::max(120, mainHeight - 36);
        const int sendWidth = std::max(90, std::min(160, inner / 5));
        mStayOpenButton->setCoord(sSideMargin, footerTop, std::max(120, inner - sendWidth - sRowGap), 28);
        mSendButton->setCoord(sSideMargin + inner - sendWidth, footerTop, sendWidth, 28);
        refitCaption(mStayOpenButton);
        refitCaption(mSendButton);

        const int paneTop = 46;
        const int paneHeight = std::max(200, mainHeight - paneTop - sSideMargin);
        mGroupPane->setCoord(sSideMargin, paneTop, inner, paneHeight);
        mPlayersPane->setCoord(sSideMargin, paneTop, inner, paneHeight);

        // Group page: info on the left, live roster on the right, three
        // action rows pinned to the bottom.
        const int paneInner = std::max(200, inner - 28);
        const int listWidth = std::max(180, paneInner / 3);
        const int infoWidth = paneInner - listWidth - 10;
        const int topBlockHeight = std::max(110, paneHeight - 218);

        getWidget("GroupTitle")->setCoord(14, 8, infoWidth, 24);
        mGroupInfo->setCoord(14, 36, infoWidth, topBlockHeight);
        mGroupRosterLabel->setCoord(24 + infoWidth, 8, listWidth, 24);
        mGroupRoster->setCoord(24 + infoWidth, 36, listWidth, topBlockHeight);

        int rowTop = 46 + topBlockHeight;
        const int labelWidth = std::max(80, paneInner / 6);
        mGroupNameLabel->setCoord(14, rowTop + 4, labelWidth, 24);
        mGroupNameEdit->setCoord(18 + labelWidth, rowTop, std::max(120, paneInner - labelWidth - 150), 28);
        mGroupCreateButton->setCoord(paneInner - 118, rowTop, 132, 28);
        refitCaption(mGroupCreateButton);

        rowTop += 34;
        mGroupTargetLabel->setCoord(14, rowTop + 4, labelWidth, 24);
        mGroupTargetEdit->setCoord(18 + labelWidth, rowTop, std::max(120, paneInner - labelWidth - 20), 28);

        rowTop += 36;
        mGroupActionRow1->setCoord(14, rowTop, paneInner, 30);
        std::vector<MyGUI::Widget*> actions1;
        actions1.push_back(mGroupInviteButton);
        actions1.push_back(mGroupKickButton);
        actions1.push_back(mGroupLeaderButton);
        layoutRow(actions1, 0, 2, paneInner, 26, sRowGap);

        rowTop += 34;
        mGroupActionRow2->setCoord(14, rowTop, paneInner, 30);
        std::vector<MyGUI::Widget*> actions2;
        actions2.push_back(mGroupJournalButton);
        actions2.push_back(mGroupTopicsButton);
        actions2.push_back(mGroupLeaveButton);
        actions2.push_back(mGroupDisbandButton);
        layoutRow(actions2, 0, 2, paneInner, 26, sRowGap);

        rowTop += 34;
        mGroupActionRow3->setCoord(14, rowTop, paneInner, 30);
        std::vector<MyGUI::Widget*> actions3;
        actions3.push_back(mGroupAcceptButton);
        actions3.push_back(mGroupDeclineButton);
        actions3.push_back(mGroupRefreshButton);
        actions3.push_back(mGroupVoiceButton);
        layoutRow(actions3, 0, 2, paneInner, 26, sRowGap);

        // Players page.
        // Z005: the roster is one full-width column now. Every row already
        // carries rank, name, level, race and location, so the old narrow list
        // plus a side card wasted two thirds of the pane on a name the row was
        // already showing. The card stays, but as a detail strip underneath.
        const int playersBodyHeight = std::max(120, paneHeight - 110);
        const int playersDetailHeight = std::max(90, playersBodyHeight / 3);
        const int playersRowsHeight = std::max(90, playersBodyHeight - playersDetailHeight - 8);
        getWidget("PlayersTitle")->setCoord(14, 8, paneInner, 24);
        mPlayersList->setCoord(14, 36, paneInner, playersRowsHeight);
        mPlayersInfo->setCoord(14, 44 + playersRowsHeight, paneInner, playersDetailHeight);
        mPlayersActionRow->setCoord(14, 46 + playersBodyHeight, paneInner, 30);
        std::vector<MyGUI::Widget*> playerActions;
        playerActions.push_back(mPlayersRefreshButton);
        playerActions.push_back(mPlayersOpenListButton);
        playerActions.push_back(mPlayersInviteButton);
        layoutRow(playerActions, 0, 2, paneInner, 26, sRowGap);

        // Housing: a scrollable summary, then four labelled action groups.
        // Reserve the bottom rows first so even a small menu cannot clip Sell.
        if (mHomePane != nullptr)
        {
            const int homeRows = 4;
            const int sectionHeight = 46;
            const int homeInfoHeight = std::max(72, paneHeight - 36 - homeRows * sectionHeight - 12);
            mHomeTitle->setCoord(14, 8, paneInner, 24);
            mHomeInfo->setCoord(14, 36, paneInner, homeInfoHeight);
            int homeTop = 43 + homeInfoHeight;
            MyGUI::Widget* homeRowWidgets[] = {mHomeActionRow1, mHomeActionRow2, mHomeActionRow3, mHomeActionRow4};
            std::vector<std::vector<MyGUI::Widget*>> homeButtons = {
                {mHomeRenameButton, mHomeLockButton, mHomeTakeButton},
                {mHomeKindHouseButton, mHomeKindTavernButton, mHomeKindShopButton, mHomeKindGuildButton},
                {mHomeCoownerButton, mHomeFriendButton, mHomeRevokeButton},
                {mHomeResetButton, mHomeGiftButton, mHomeSellButton, mHomeMarketButton}};
            for (int row = 0; row < homeRows; ++row)
            {
                homeRowWidgets[row]->setCoord(14, homeTop, paneInner, sectionHeight);
                if (homeRowWidgets[row]->getChildCount() > 0)
                {
                    const char* names[] = {"HomeSettingsLabel", "HomeTypeLabel", "HomeAccessLabel", "HomeManageLabel"};
                    getWidget(names[row])->setCoord(2, 0, paneInner - 4, 16);
                }
                layoutRow(homeButtons[row], 0, 18, paneInner, 26, sRowGap);
                homeTop += sectionHeight;
            }
        }

        // Decorations: concise hint, tall list, and full-width two-button rows.
        // Admin actions get a separate row instead of four squeezed captions.
        mDecorPane->setCoord(sSideMargin,paneTop,inner,paneHeight);
        mDecorTitle->setCoord(14,8,paneInner,24);
        MyGUI::TextBox* hint = nullptr;
        getWidget(hint, "DecorHint");
        {
            const bool ru = MWBase::Environment::get().getWindowManager()->getArenaLanguage() == "ru";
            hint->setCaption(ru ? (mDecorShowItems ? "Выберите предмет для просмотра" : "Выберите категорию одним нажатием")
                : (mDecorShowItems ? "Select an item to preview" : "Tap a category once to open"));
            hint->setCoord(14,34,paneInner,18);
        }
        const int footerRows = mDecorIsStaff ? 2 : 1;
        const int decorFooter = paneHeight - footerRows * 32 - 8;
        mDecorList->setCoord(14,58,paneInner,std::max(48,decorFooter - 64));
        const int decorGap = 8;
        const int decorWidth = (paneInner - decorGap) / 2;
        mDecorBack->setCoord(14,decorFooter,decorWidth,28);
        mDecorOpen->setCoord(14+decorWidth+decorGap,decorFooter,decorWidth,28);
        if (mDecorIsStaff)
        {
            mDecorCapture->setCoord(14,decorFooter+32,decorWidth,28);
            mDecorRefresh->setCoord(14+decorWidth+decorGap,decorFooter+32,decorWidth,28);
        }
        for (MyGUI::Widget* button : {mDecorBack,mDecorOpen,mDecorCapture,mDecorRefresh})
            refitCaption(button);

        // Y052 profile/statistics page.
        mProfilePane->setCoord(sSideMargin, paneTop, inner, paneHeight);
        getWidget("ProfileTitle")->setCoord(14, 8, paneInner, 24);
        // Z001: the four profile actions used to share one row. layoutRow only
        // guarantees a minimum width per button, so on a narrow panel the row
        // grew past the pane and the last button - "Delete profile" - ended up
        // clipped and unclickable. Two rows of two always fit.
        const int profileBodyHeight = std::max(120, paneHeight - 144);
        mProfileInfo->setCoord(14, 36, paneInner, profileBodyHeight);

        int profileRowTop = 46 + profileBodyHeight;
        mProfileActionRow->setCoord(14, profileRowTop, paneInner, 30);
        std::vector<MyGUI::Widget*> profileActions;
        profileActions.push_back(mProfileRefreshButton);
        profileActions.push_back(mProfileFixButton);
        layoutRow(profileActions, 0, 2, paneInner, 26, sRowGap);

        profileRowTop += 34;
        mProfileActionRow2->setCoord(14, profileRowTop, paneInner, 30);
        std::vector<MyGUI::Widget*> profileActions2;
        profileActions2.push_back(mProfileReturnsButton);
        profileActions2.push_back(mProfileDeleteButton);
        layoutRow(profileActions2, 0, 2, paneInner, 26, sRowGap);
    }

    // ------------------------------------------------------------------
    // X052: quick-insert strip
    // ------------------------------------------------------------------

    void GUIChat::setDrawer(ChatDrawer drawer)
    {
        activeDrawer = drawer;
        if (activeDrawer == DRAWER_COLOR)
            requestColorState();
        refreshPlayerMenu();
        updateCommandLineLayout();
        if (editState && activeTab == TAB_CHAT)
            MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mCommandLine);
    }

    void GUIChat::ensureChatFontLoaded()
    {
        // The colour atlas is a MyGUI ResourceManualFont: a texture plus glyph
        // rectangles. It is loaded here rather than declared in core.xml so a
        // missing or malformed atlas degrades to the stock font instead of
        // taking the whole UI down at startup.
        const std::string wanted = !emojiFontName.empty() ? emojiFontName : menuFontName;
        if (wanted.empty() || MyGUI::FontManager::getInstance().getByName(wanted) != nullptr)
            return;

        std::string resource = Settings::Manager::getString("chat font resource", "Chat");
        if (resource.empty())
            resource = sChatFontResource;

        try
        {
            MyGUI::ResourceManager::getInstance().load(resource);
        }
        catch (const std::exception& e)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                "Could not load chat font resource %s: %s", resource.c_str(), e.what());
        }
    }

    bool GUIChat::unicodeEmojiEnabled() const
    {
        // Opt-in only. OpenMW's bundled DejaVu LGC face carries no dingbat or
        // emoji glyphs, so asking MyGUI to draw them is what produced the empty
        // boxes. A server or player who installs a real symbol font names it in
        // [Chat] emoji font and gets the Unicode palette instead.
        if (emojiFontName.empty())
            return false;
        return MyGUI::FontManager::getInstance().getByName(emojiFontName) != nullptr;
    }

    const char* GUIChat::emojiSymbol(int index) const
    {
        if (index < 0 || index >= sEmojiSlotCount)
            return "";
        return unicodeEmojiEnabled() ? sEmojiPalette[index].unicode : sEmojiPalette[index].ascii;
    }

    void GUIChat::applyEmojiPalette()
    {
        const bool unicode = unicodeEmojiEnabled();
        for (int i = 0; i < sEmojiSlotCount; ++i)
        {
            if (mEmojiButtons[i] == nullptr)
                continue;
            if (unicode)
                mEmojiButtons[i]->setFontName(emojiFontName);
            setMenuCaption(mEmojiButtons[i], emojiSymbol(i));
        }
    }

    // ------------------------------------------------------------------
    // X052: nickname colour
    // ------------------------------------------------------------------

    void GUIChat::requestColorState()
    {
        requestChatState();
    }

    void GUIChat::rebuildColorPalette(const std::vector<std::string>& fields)
    {
        selectedColorIndex = -1;
        if (fields.size() > 1)
        {
            char* end = nullptr;
            const long parsed = std::strtol(fields[1].c_str(), &end, 10);
            if (end != fields[1].c_str())
                selectedColorIndex = static_cast<int>(parsed) - 1;
        }

        colorPalette.clear();
        colorNames.clear();
        if (fields.size() > 2)
        {
            const std::vector<std::string> entries = splitControlFields(fields[2], ';');
            for (std::size_t i = 0; i < entries.size(); ++i)
            {
                if (entries[i].empty())
                    continue;

                // X054: each record is "#RRGGBB^localized name". The name is
                // produced by coreChat's own dictionary, so the swatch tooltip
                // matches what /color prints in the player's language.
                const std::vector<std::string> parts = splitControlFields(entries[i], '^');
                if (parts.empty())
                    continue;

                const std::string hex = unescapeControlField(parts[0]);
                unsigned int rgb = 0;
                if (!parseHexColour(hex, rgb))
                    continue;
                colorPalette.push_back(std::make_pair(rgb, hex));
                colorNames.push_back(parts.size() > 1 ? unescapeControlField(parts[1]) : std::string());
            }
        }

        applyChatState(fields);
        updateColorButtons();
    }

    void GUIChat::updateColorButtons()
    {
        for (int i = 0; i < sColorSlotCount; ++i)
        {
            if (mColorButtons[i] == nullptr)
                continue;

            const bool used = static_cast<std::size_t>(i) < colorPalette.size();
            mColorButtons[i]->setVisible(used && activeDrawer == DRAWER_COLOR
                && editState && activeTab == TAB_CHAT);
            if (!used)
                continue;

            // The swatch is drawn with the palette colour itself, which needs
            // no glyph coverage and therefore cannot end up as a box.
            setMenuCaption(mColorButtons[i], std::to_string(i + 1));
            mColorButtons[i]->setTextColour(toMyGuiColour(colorPalette[i].first));
            mColorButtons[i]->setStateSelected(i == selectedColorIndex);
            if (static_cast<std::size_t>(i) < colorNames.size() && !colorNames[i].empty())
            {
                mColorButtons[i]->setUserString("ToolTipType", "Layout");
                mColorButtons[i]->setUserString("ToolTipLayout", "TextToolTip");
                mColorButtons[i]->setUserString("Caption_Text", colorNames[i]);
            }
        }
    }

    // ------------------------------------------------------------------
    // X052: group roster and server player list
    // ------------------------------------------------------------------

    void GUIChat::rebuildGroupRoster(const std::string& rosterField)
    {
        groupRosterNames.clear();
        mGroupRoster->removeAllItems();

        const std::vector<std::string> entries = splitControlFields(rosterField, ';');
        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            if (entries[i].empty())
                continue;
            const std::vector<std::string> parts = splitControlFields(entries[i], '^');
            if (parts.empty() || parts[0].empty())
                continue;

            const bool hasGroup = parts.size() > 1 && parts[1] == "1";
            const bool inMyGroup = parts.size() > 2 && parts[2] == "1";
            const bool sameCell = parts.size() > 3 && parts[3] == "1";

            std::string label = unescapeControlField(parts[0]);
            if (inMyGroup)
                label += " [" + localizeArena("chat.group.tag.member") + "]";
            else if (hasGroup)
                label += " [" + localizeArena("chat.group.tag.busy") + "]";
            if (sameCell)
                label += " *";

            groupRosterNames.push_back(unescapeControlField(parts[0]));
            mGroupRoster->addItem(label);
        }

        if (groupRosterNames.empty())
            mGroupRoster->addItem(localizeArena("chat.group.roster_empty"));
    }

    std::string GUIChat::selectedRosterName() const
    {
        const std::size_t index = mGroupRoster->getIndexSelected();
        if (index == MyGUI::ITEM_NONE || index >= groupRosterNames.size())
            return std::string();
        return groupRosterNames[index];
    }

    void GUIChat::requestPlayerList()
    {
        send("/playerlistui state");
    }

    void GUIChat::rebuildPlayerList(const std::vector<std::string>& fields)
    {
        playerListNames.clear();
        playerListDetails.clear();
        mPlayersList->removeAllItems();

        const std::string header = fields.size() > 1 ? unescapeControlField(fields[1]) : std::string();
        const std::string body = fields.size() > 2 ? fields[2] : std::string();

        const std::vector<std::string> entries = splitControlFields(body, ';');
        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            if (entries[i].empty())
                continue;
            const std::vector<std::string> parts = splitControlFields(entries[i], '^');
            if (parts.empty() || parts[0].empty())
                continue;

            // X054: the record separators are gone by now, so it is finally safe
            // to turn "\n" back into the real line breaks the detail card needs.
            playerListNames.push_back(unescapeControlField(parts[0]));
            playerListDetails.push_back(parts.size() > 1 ? unescapeControlField(parts[1]) : std::string());
            mPlayersList->addItem(unescapeControlField(
                parts.size() > 2 && !parts[2].empty() ? parts[2] : parts[0]));
        }

        if (playerListNames.empty())
        {
            mPlayersList->addItem(localizeArena("chat.players.empty"));
            mPlayersInfo->setCaption(header.empty() ? localizeArena("chat.players.empty") : header);
            return;
        }

        mPlayersList->setIndexSelected(0);
        updatePlayerDetails();
        if (!header.empty())
            mPlayersInfo->setCaption(header + "\n\n" + mPlayersInfo->getOnlyText());
    }

    void GUIChat::updatePlayerDetails()
    {
        const std::size_t index = mPlayersList->getIndexSelected();
        if (index == MyGUI::ITEM_NONE || index >= playerListDetails.size())
            return;
        mPlayersInfo->setCaption(playerListDetails[index]);
    }

    void GUIChat::onPlayerListSelected(MyGUI::ListBox*, size_t)
    {
        updatePlayerDetails();
    }

    void GUIChat::onPlayersButtonClicked(MyGUI::Widget* sender)
    {
        if (sender == mPlayersRefreshButton)
        {
            requestPlayerList();
            return;
        }

        if (sender == mPlayersOpenListButton)
        {
            // Exactly what typing /list does: the server answers with its own
            // native dialog, so both entry points stay in sync by construction.
            send("/list");
            return;
        }

        if (sender == mPlayersInviteButton)
        {
            const std::size_t index = mPlayersList->getIndexSelected();
            if (index == MyGUI::ITEM_NONE || index >= playerListNames.size())
                return;
            mGroupTargetEdit->setCaption(playerListNames[index]);
            sendGroupAction("invite", playerListNames[index]);
        }
    }

    void GUIChat::requestProfileState()
    {
        send("/profileui state");
    }

    void GUIChat::requestHomeState()
    {
        send("/house state");
    }

    // Z005: the page mirrors what the server knows, including the two toggles,
    // so the buttons say what they will do rather than what the state is.
    void GUIChat::rebuildHomeInfo(const std::vector<std::string>& fields)
    {
        if (mHomeInfo == nullptr)
            return;

        homeOwned = fields.size() > 1 && fields[1] == "1";
        if (!homeOwned)
        {
            const bool ru=MWBase::Environment::get().getWindowManager()->getArenaLanguage()=="ru";
            mHomeTitle->setCaption(ru ? "Мой дом" : "My home");
            mHomeInfo->setCaption(ru ? "У вас нет недвижимости. Подойдите к двери свободного дома "
                                  "и удерживайте «Взять»." :
                                  "You do not own a home. Approach the door of a vacant house and hold Activate.");
            homeLocked = false;
            homeAllowTake = false;
            refreshPlayerMenu();
            return;
        }

        const std::string title = fields.size() > 2 ? unescapeControlField(fields[2]) : "";
        const std::string kind = fields.size() > 3 ? unescapeControlField(fields[3]) : "";
        homeLocked = fields.size() > 6 && fields[6] == "1";
        homeAllowTake = fields.size() > 7 && fields[7] == "1";

        const bool ru=MWBase::Environment::get().getWindowManager()->getArenaLanguage()=="ru";
        std::string header = title.empty() ? std::string(ru ? "Мой дом" : "My home") : title;
        if (!kind.empty())
            header += "  (" + kind + ")";
        mHomeTitle->setCaption(header);
        mHomeInfo->setCaption(fields.size() > 10 ? unescapeControlField(fields[10]) : std::string());

        setMenuCaption(mHomeLockButton, homeLocked ? (ru ? "Открыть всем" : "Unlock")
            : (ru ? "Закрыть замок" : "Lock"));
        setMenuCaption(mHomeTakeButton, homeAllowTake ? (ru ? "Запретить брать" : "Deny taking")
            : (ru ? "Разрешить брать" : "Allow taking"));
        // The server sends the property type already localised for this player.
        // Accept both languages so the active type remains highlighted after a
        // live language change or a state packet already in flight.
        mHomeKindHouseButton->setStateSelected(kind == "house" || kind == "дом");
        mHomeKindTavernButton->setStateSelected(kind == "tavern" || kind == "таверна");
        mHomeKindShopButton->setStateSelected(kind == "shop" || kind == "магазин");
        mHomeKindGuildButton->setStateSelected(kind == "guild hall" || kind == "гильдейский зал");
        refreshPlayerMenu();
    }

    void GUIChat::onHomeButton(MyGUI::Widget* sender)
    {
        // Every action is an ordinary server command: the page never mutates
        // housing state by itself, it only asks and then redraws from the reply.
        std::string command;
        if (sender == mHomeMarketButton)
        {
            closeForServerDialog();
            ArenaHousing::get().requestItemWallet();
            return;
        }
        if (sender == mHomeRenameButton)          command = "/house rename";
        else if (sender == mHomeLockButton)       command = "/house togglelock";
        else if (sender == mHomeTakeButton)       command = "/house take";
        else if (sender == mHomeKindHouseButton)  command = "/house kind house";
        else if (sender == mHomeKindTavernButton) command = "/house kind tavern";
        else if (sender == mHomeKindShopButton)   command = "/house kind shop";
        else if (sender == mHomeKindGuildButton)  command = "/house kind guild";
        else if (sender == mHomeCoownerButton)    command = "/house addcoowner";
        else if (sender == mHomeFriendButton)     command = "/house addfriend";
        else if (sender == mHomeRevokeButton)     command = "/house revoke";
        else if (sender == mHomeResetButton)      command = "/house reset";
        else if (sender == mHomeGiftButton)       command = "/house gift";
        else if (sender == mHomeSellButton)       command = "/house sell";
        else
            return;

        // The ones that ask for a name or a title open a server input dialog,
        // which needs the menu out of the way first - exactly like profile
        // deletion does.
        const bool opensDialog = sender == mHomeRenameButton || sender == mHomeCoownerButton
            || sender == mHomeFriendButton || sender == mHomeRevokeButton
            || sender == mHomeGiftButton;
        if (opensDialog)
            closeForServerDialog();

        send(command);
        if (!opensDialog)
            requestHomeState();
    }

    void GUIChat::rebuildProfileState(const std::vector<std::string>& fields)
    {
        if (mProfileInfo == nullptr || fields.empty())
            return;

        const std::string played = fields.size() > 1 ? unescapeControlField(fields[1]) : "-";
        const std::string deaths = fields.size() > 2 ? fields[2] : "0";
        const std::string arrests = fields.size() > 3 ? fields[3] : "0";
        const std::string level = fields.size() > 4 ? fields[4] : "1";
        const bool unlocked = fields.size() > 5 && fields[5] == "1";
        const std::string unlockLevel = fields.size() > 6 ? fields[6] : "25";
        const std::string slotA = fields.size() > 7 ? unescapeControlField(fields[7]) : "-";
        const std::string slotB = fields.size() > 8 ? unescapeControlField(fields[8]) : "-";
        const std::string slotC = fields.size() > 9 ? unescapeControlField(fields[9]) : "-";

        std::ostringstream info;
        info << localizeArena("chat.profile.time") << ": " << played << "\n"
             << localizeArena("chat.profile.deaths") << ": " << deaths << "\n"
             << localizeArena("chat.profile.arrests") << ": " << arrests << "\n"
             << localizeArena("chat.profile.level") << ": " << level << "\n\n"
             << localizeArena("chat.profile.return_points") << ":\n"
             << "A: " << slotA << "\n"
             << "B: " << slotB << "\n"
             << "C: " << slotC;

        if (!unlocked)
            info << "\n\n" << localizeArena("chat.profile.unlock_hint") << " " << unlockLevel << ".";

        mProfileInfo->setCaption(info.str());
    }

    void GUIChat::onProfileButtonClicked(MyGUI::Widget* sender)
    {
        if (sender == mProfileRefreshButton)
        {
            requestProfileState();
            return;
        }
        if (sender == mProfileFixButton)
        {
            send("/fix");
            return;
        }
        if (sender == mProfileReturnsButton)
        {
            // Z001: both of these make the server answer with its own modal
            // dialog. Leaving the Player Menu open meant that modal had to
            // arrive on top of a GUI mode this window owns, and a server
            // message box that loses that race is simply never seen - which is
            // exactly how "Delete profile" came to look like a dead button.
            // Close the menu first; the server dialog then opens into a clean
            // gameplay GUI state.
            closeForServerDialog();
            send("/profile returns");
            return;
        }
        if (sender == mProfileDeleteButton)
        {
            closeForServerDialog();
            send("/profile delete");
            return;
        }
    }

    // Z001: drop the Player Menu (and its GUI mode) before a server-side modal
    // is requested. setEditState(false) already clears menuState and calls
    // syncInteractiveInputMode(), which removes GM_ARENAMP_PlayerMenu from the
    // mode stack, so the incoming InteractiveMessageBox is the only modal on
    // screen and its reply cannot be swallowed.
    void GUIChat::closeForServerDialog()
    {
        if (!editState)
            return;
        setEditState(false);
    }

    void GUIChat::setDelay(float newDelay)
    {
        delay = std::max(0.f, newDelay);
        if (revealTime > delay)
            revealTime = delay;
    }
}
