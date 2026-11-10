// ArenaMP U025 — скелет страницы «Чат и голос».
//
// Подключение в maindialog.cpp:
//   sNavigationItems: 4 -> 5
//   createIcons():  пятый QListWidgetItem, иконка "chat" из arenaicons.qrc
//   createPages():  mChatPage = new ChatPage(mLauncherSettings, this);
//                   pagesWidget->addWidget(mChatPage);   // пятым: порядок
//                                                        // страниц обязан
//                                                        // совпадать с иконками
//   mChatPage->setServerEndpoint(addr, port.toUShort());
//   в play()/serverRunningChanged(): mChatPage->setGameRunning(...)
// CMakeLists.txt: chatpage.cpp, arenalinkclient.cpp, voice/voicepanel.cpp
//   в LAUNCHER; заголовки — в LAUNCHER_HEADER и LAUNCHER_HEADER_MOC.

#include "chatpage.hpp"
#include "voice/voicepanel.hpp"

#include <QApplication>
#include <QBrush>
#include <QDateTime>
#include <QFile>
#include <QSaveFile>
#include <QStringList>
#include <QFileInfo>
#include <QDir>
#include <components/settings/settings.hpp>
#include <QFrame>
#include <QFont>
#include <QDesktopServices>
#include <QUrl>
#include <QMessageBox>
#include <QTextStream>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QPainter>
#include <QPixmap>
#include <QIcon>
#include <QPolygon>
#include <QSize>
#include <QColor>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStackedLayout>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>

#include <components/config/launchersettings.hpp>

using namespace Launcher;

ChatPage::ChatPage(Config::LauncherSettings& launcherSettings, QWidget* parent)
    : QWidget(parent)
    , mLauncherSettings(launcherSettings)
    , mClient(new ArenaLinkClient(this))
    , mStack(new QStackedLayout())
{
    setObjectName(QStringLiteral("chatPage"));
    buildLoginCard();
    buildChatView();
    QVBoxLayout* pageLayout = new QVBoxLayout(this);
    pageLayout->addLayout(mStack, 1);
    mVoicePanel = new VoicePanel(this);
    pageLayout->addWidget(mVoicePanel);
    QPushButton* logButton = new QPushButton(tr("Open Chat.log"), this);
    pageLayout->addWidget(logButton);
    connect(logButton, &QPushButton::clicked, this, [this]() {
        const QString path = mClient->chatLogPath();
        if (path.isEmpty() || !QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
            QMessageBox::warning(this, tr("Chat log"), tr("Could not open Chat.log: %1").arg(path));
    });

    connect(mClient, &ArenaLinkClient::loggedIn, this, &ChatPage::slotLoggedIn);
    connect(mClient, &ArenaLinkClient::loginFailed, this, &ChatPage::slotLoginFailed);
    connect(mClient, &ArenaLinkClient::disconnected, this, &ChatPage::slotDisconnected);
    connect(mClient, &ArenaLinkClient::channelsReceived, this, &ChatPage::slotChannelsReceived);
    connect(mClient, &ArenaLinkClient::messagesReceived, this, &ChatPage::slotMessagesReceived);
    connect(mClient, &ArenaLinkClient::messageReceived, this, &ChatPage::slotMessageReceived);
    connect(mClient, &ArenaLinkClient::presenceReceived, this, &ChatPage::slotPresenceReceived);
    connect(mClient, &ArenaLinkClient::notice, this, &ChatPage::slotNotice);
    connect(mClient, &ArenaLinkClient::voiceTicketReceived, this,
        [this](const QByteArray& ticket, quint16 port, int ttl)
        {
            emit voiceTicketReady(ticket, port, ttl);
        });

    loadSettings();
}

void ChatPage::buildLoginCard()
{
    QWidget* page = new QWidget(this);
    QVBoxLayout* center = new QVBoxLayout(page);
    center->setContentsMargins(16, 16, 16, 16);
    center->addStretch(1);
    QFrame* card = new QFrame(page);
    card->setObjectName(QStringLiteral("chatLoginCard"));
    card->setMaximumWidth(480);
    card->setStyleSheet(QStringLiteral("QFrame#chatLoginCard { background: rgba(28,29,33,235); border: 1px solid rgba(217,181,108,100); border-radius: 12px; }"));
    QVBoxLayout* layout = new QVBoxLayout(card);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);

    QLabel* title = new QLabel(tr("ArenaMP chat sign-in"), card);
    title->setProperty("arenaTitle", true);
    title->setAlignment(Qt::AlignHCenter);

    QLabel* hint = new QLabel(tr(
        "The character name and password of the game server, the same as in game.\n"
        "A character is created in game only: press Play and join the server."), card);
    hint->setAlignment(Qt::AlignHCenter);
    hint->setProperty("arenaMuted", true);

    mNameEdit = new QLineEdit(card);
    mNameEdit->setPlaceholderText(tr("Character name"));
    mNameEdit->setMaxLength(32);

    mSecretEdit = new QLineEdit(card);
    mSecretEdit->setPlaceholderText(tr("Password"));
    mSecretEdit->setEchoMode(QLineEdit::Password);
    mSecretEdit->setMaxLength(64);

    mLoginButton = new QPushButton(tr("Sign in"), card);
    mLoginButton->setDefault(true);
    mLoginButton->setMinimumHeight(42);
    mUseCodeButton = new QPushButton(tr("Sign in with an in-game code (/chatlink)"), card);
    mUseCodeButton->setProperty("arenaQuiet", true);
    mUseCodeButton->hide();
    mLoginStatus = new QLabel(card);
    mLoginStatus->setAlignment(Qt::AlignHCenter);
    mLoginStatus->setWordWrap(true);

    for (QWidget* widget : { static_cast<QWidget*>(title), static_cast<QWidget*>(hint),
        static_cast<QWidget*>(mNameEdit), static_cast<QWidget*>(mSecretEdit),
        static_cast<QWidget*>(mLoginButton), static_cast<QWidget*>(mUseCodeButton),
        static_cast<QWidget*>(mLoginStatus) })
    {
        widget->setMaximumWidth(420);
        layout->addWidget(widget, 0, Qt::AlignHCenter);
    }
    center->addWidget(card, 0, Qt::AlignHCenter);
    center->addStretch(1);

    connect(mLoginButton, &QPushButton::clicked, this, &ChatPage::slotLoginClicked);
    connect(mSecretEdit, &QLineEdit::returnPressed, this, &ChatPage::slotLoginClicked);
    connect(mUseCodeButton, &QPushButton::clicked, this, &ChatPage::slotToggleCodeMode);

    mStack->addWidget(page);
}

void ChatPage::buildChatView()
{
    QWidget* view = new QWidget(this);
    QVBoxLayout* outer = new QVBoxLayout(view);

    QHBoxLayout* header = new QHBoxLayout();
    mProfileLabel = new QLabel(view);
    mMentionLabel = new QLabel(view);
    mMentionLabel->setStyleSheet(QStringLiteral("color:#E8C87A;font-weight:bold"));
    mMentionLabel->hide();
    mLogoutButton = new QPushButton(tr("Sign out"), view);
    mLogoutButton->setProperty("arenaQuiet", true);
    header->addWidget(mProfileLabel, 1);
    header->addWidget(mMentionLabel);
    header->addWidget(mLogoutButton);
    outer->addLayout(header);

    QHBoxLayout* body = new QHBoxLayout();

    // Левая колонка: каналы, затем управление голосом.
    QWidget* left = new QWidget(view);
    left->setFixedWidth(200);
    QVBoxLayout* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    mChannels = new QListWidget(left);
    leftLayout->addWidget(mChannels, 1);

    mHistory = new QTextBrowser(view);
    mHistory->setOpenExternalLinks(true);

    mPlayers = new QListWidget(view);
    mPlayers->setFixedWidth(170);

    body->addWidget(left);
    body->addWidget(mHistory, 1);
    body->addWidget(mPlayers);
    outer->addLayout(body, 1);

    QHBoxLayout* footer = new QHBoxLayout();
    mInput = new QLineEdit(view);
    mInput->setPlaceholderText(tr("Message…"));
    mInput->setMaxLength(static_cast<int>(ArenaLink::sMaxText));
    // A compact Play arrow uses the existing launcher button theme; the
    // accessible name and tooltip keep the action clear in RU/EN.
    mSendButton = new QPushButton(view);
    QPixmap sendPixmap(24, 24);
    sendPixmap.fill(Qt::transparent);
    {
        QPainter painter(&sendPixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(QStringLiteral("#D9B56C")));
        QPolygon arrow;
        arrow << QPoint(6, 4) << QPoint(19, 12) << QPoint(6, 20);
        painter.drawPolygon(arrow);
    }
    mSendButton->setIcon(QIcon(sendPixmap));
    mSendButton->setIconSize(QSize(22, 22));
    mSendButton->setFixedSize(44, 38);
    mSendButton->setToolTip(tr("Send"));
    mSendButton->setAccessibleName(tr("Send"));
    mInput->setMinimumHeight(38);
    footer->setSpacing(8);
    footer->addWidget(mInput, 1);
    footer->addWidget(mSendButton);
    outer->addLayout(footer);

    connect(mSendButton, &QPushButton::clicked, this, &ChatPage::slotSendClicked);
    connect(mInput, &QLineEdit::returnPressed, this, &ChatPage::slotSendClicked);
    connect(mLogoutButton, &QPushButton::clicked, this, &ChatPage::slotLogoutClicked);
    connect(mChannels, &QListWidget::currentRowChanged, this, &ChatPage::slotChannelChanged);

    mStack->addWidget(view);
}

void ChatPage::loadSettings(const QString& gameSettingsPath)
{
    if (!gameSettingsPath.trimmed().isEmpty())
        mGameSettingsPath = gameSettingsPath;

    // Launcher/Chat/lastName remains only as a fallback for old configurations.
    // The authoritative ArenaMP account is [Login] name/password in settings.cfg.
    mNameEdit->setText(mLauncherSettings.value(QStringLiteral("Chat/lastName")));
    refreshLoginFromGameSettings();

    if (mVoicePanel != nullptr)
        mVoicePanel->loadSettings(mLauncherSettings);
}

void ChatPage::refreshLoginFromGameSettings()
{
    if (mGameSettingsPath.trimmed().isEmpty())
        return;

    mClient->configureChatLog(mGameSettingsPath);
    QFile file(mGameSettingsPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        mClient->logCredentialSource(false, false, false);
        tryAutoLogin();
        return;
    }

    QString accountName;
    QString accountPassword;
    QString section;
    QTextStream input(&file);
    input.setCodec("UTF-8");

    while (!input.atEnd())
    {
        QString line = input.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char(';')))
            continue;

        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']')))
        {
            section = line.mid(1, line.size() - 2).trimmed();
            continue;
        }
        if (section.compare(QStringLiteral("Login"), Qt::CaseInsensitive) != 0)
            continue;

        const int equals = line.indexOf(QLatin1Char('='));
        if (equals <= 0)
            continue;

        const QString key = line.left(equals).trimmed();
        const QString value = line.mid(equals + 1).trimmed();
        if (key.compare(QStringLiteral("name"), Qt::CaseInsensitive) == 0)
            accountName = value;
        else if (key.compare(QStringLiteral("password"), Qt::CaseInsensitive) == 0)
            accountPassword = value;
    }

    if (!accountName.isEmpty())
        mNameEdit->setText(accountName);

    // Never copy the password into launcher.cfg. Keep it only in memory and
    // show it masked in the normal password mode. /chatlink code mode keeps
    // its own temporary value untouched until the user switches back.
    mClient->logCredentialSource(true, !accountName.isEmpty(), !accountPassword.isEmpty());
    mGamePassword = accountPassword;
    if (!mCodeMode)
        mSecretEdit->setText(mGamePassword);

    // U034: launcher chat follows the same settings.cfg identity as the game.
    // Once both credentials and endpoint are known, no extra Sign in click is
    // required. Password still never gets persisted in launcher.cfg.
    mAutoLoginBlocked = false;
    tryAutoLogin();
}

void ChatPage::saveSettings()
{
    mLauncherSettings.setValue(QStringLiteral("Chat/lastName"), mNameEdit->text().trimmed());
    if (mVoicePanel != nullptr)
        mVoicePanel->saveSettings(mLauncherSettings);
}


bool ChatPage::voiceEnabled() const
{
    return mVoicePanel != nullptr && mVoicePanel->voiceEnabled();
}

QString ChatPage::pushToTalkKey() const
{
    return mVoicePanel != nullptr ? mVoicePanel->pushToTalkKey() : QStringLiteral("V");
}

QString ChatPage::captureDevice() const
{
    return mVoicePanel != nullptr ? mVoicePanel->captureDevice() : QString();
}

void ChatPage::setServerEndpoint(const QString& host, quint16 gamePort)
{
    // Порты: игровой (RakNet) не трогаем, голос = +1, чат = +2.
    const QString normalizedHost = host.trimmed();
    const bool changed = normalizedHost != mServerHost || gamePort != mGamePort;
    if (changed && (mClient->authorized() || mLoginInProgress))
        mClient->disconnectFromServer();

    mServerHost = normalizedHost;
    mGamePort = gamePort;
    mClient->setServer(normalizedHost, gamePort);
    if (changed)
    {
        mAutoLoginBlocked = false;
        mReconnectScheduled = false;
    }
    tryAutoLogin();
}

void ChatPage::setGameRunning(bool running)
{
    mGameRunning = running;
    if (mVoicePanel != nullptr)
        mVoicePanel->setGameRunning(running);
}

void ChatPage::tryAutoLogin()
{
    if (mManualLogout || mAutoLoginBlocked || mLoginInProgress || mClient->authorized()
        || mCodeMode || mServerHost.isEmpty() || mGamePort == 0)
        return;

    const QString name = mNameEdit != nullptr ? mNameEdit->text().trimmed() : QString();
    if (name.isEmpty() || mGamePassword.isEmpty())
        return;

    mSecretEdit->setText(mGamePassword);
    mLoginInProgress = true;
    mLoginButton->setEnabled(false);
    setStatus(tr("Connecting to the game server…"));
    mSubmittedName = name;
    mSubmittedPassword = mGamePassword;
    mSubmittedSettingsPath = mGameSettingsPath;
    mClient->connectAndLogin(name, mGamePassword);
}

void ChatPage::scheduleAutoLogin()
{
    if (mManualLogout || mAutoLoginBlocked || mReconnectScheduled)
        return;
    mReconnectScheduled = true;
    QTimer::singleShot(4000, this, [this]()
    {
        mReconnectScheduled = false;
        tryAutoLogin();
    });
}

void ChatPage::slotLoginClicked()
{
    mManualLogout = false;
    mAutoLoginBlocked = false;
    const QString name = mNameEdit->text().trimmed();
    const QString secret = mSecretEdit->text();
    if (name.isEmpty() || secret.isEmpty())
    {
        mClient->logLoginValidation(!name.isEmpty(), !secret.isEmpty());
        setStatus(tr("Enter the character name and password"), true);
        return;
    }
    mLoginInProgress = true;
    mLoginButton->setEnabled(false);
    setStatus(tr("Connecting to the game server…"));
    mSubmittedName = name;
    mSubmittedPassword = mCodeMode ? QString() : secret;
    mSubmittedSettingsPath = mGameSettingsPath;
    if (mCodeMode)
        mClient->connectAndLoginWithCode(name, secret);
    else
        mClient->connectAndLogin(name, secret);
}

void ChatPage::slotToggleCodeMode()
{
    mCodeMode = !mCodeMode;
    mSecretEdit->setEchoMode(mCodeMode ? QLineEdit::Normal : QLineEdit::Password);
    mSecretEdit->setPlaceholderText(mCodeMode ? tr("In-game code") : tr("Password"));
    if (mCodeMode)
        mSecretEdit->clear();
    else
    {
        refreshLoginFromGameSettings();
        mSecretEdit->setText(mGamePassword);
    }
    mUseCodeButton->setText(mCodeMode ? tr("Sign in with a password")
                                      : tr("Sign in with an in-game code (/chatlink)"));
}

void ChatPage::slotLoggedIn(const LinkProfile& profile)
{
    mLoginInProgress = false;
    mManualLogout = false;
    mAutoLoginBlocked = false;
    mLoginButton->setEnabled(true);
    const bool saved = savePrimaryLogin();
    mSubmittedPassword.clear();
    mSecretEdit->clear();
    saveSettings();
    if (!saved)
        QMessageBox::warning(this, tr("Settings"), tr("Signed in, but the primary login could not be saved to settings.cfg."));

    mProfileLabel->setText(tr("%1 · level %2").arg(profile.name).arg(profile.level));
    mStack->setCurrentIndex(1);
    mHistory->clear();
    mUnreadByChannel.clear();
    mMentionsByChannel.clear();
    updateMentionSummary();

}

void ChatPage::slotLoginFailed(quint8 reason, const QString& text)
{
    mSubmittedPassword.clear();
    mLoginInProgress = false;
    mAutoLoginBlocked = true;
    mLoginButton->setEnabled(true);
    if (!text.isEmpty())
    {
        setStatus(text, true);
        return;
    }
    switch (reason)
    {
        case ArenaLink::FAIL_NO_ACCOUNT:
            setStatus(tr("Character not found. Join the server and create it in game."), true);
            break;
        case ArenaLink::FAIL_BANNED:
            setStatus(tr("This account is blocked"), true);
            break;
        case ArenaLink::FAIL_RATE:
            setStatus(tr("Too many attempts, please wait"), true);
            break;
        case ArenaLink::FAIL_CODE_EXPIRED:
            setStatus(tr("The code expired, get a new one with /chatlink"), true);
            break;
        case ArenaLink::FAIL_VERSION:
            setStatus(tr("Launcher version does not match the server"), true);
            break;
        default:
            setStatus(tr("Wrong name or password"), true);
            break;
    }
}

void ChatPage::slotDisconnected(const QString& reason)
{
    mLoginInProgress = false;
    mLoginButton->setEnabled(true);
    mStack->setCurrentIndex(0);
    setStatus(reason, !reason.isEmpty());
    // Native in-game voice does not depend on the ArenaLink connection.
    // ArenaLink itself reconnects automatically after a transient server restart.
    if (!mManualLogout)
        scheduleAutoLogin();
}

void ChatPage::slotLogoutClicked()
{
    mManualLogout = true;
    mLoginInProgress = false;
    mReconnectScheduled = false;
    mClient->disconnectFromServer();
    mStack->setCurrentIndex(0);
    refreshLoginFromGameSettings();
    setStatus(QString());
}

void ChatPage::slotChannelsReceived(const QVector<LinkChannel>& channels)
{
    mChannelList = channels;
    mChannels->clear();
    for (const LinkChannel& channel : channels)
    {
        // Канал-зеркало помечаем: игрок должен понимать, что его увидят
        // и в игре тоже.
        mChannels->addItem(channel.mirrorsGame
            ? QStringLiteral("%1  ↔").arg(channel.name) : channel.name);
    }
    mUnreadByChannel.clear();
    mMentionsByChannel.clear();
    updateMentionSummary();
    if (!channels.isEmpty())
        mChannels->setCurrentRow(0);
}

void ChatPage::slotChannelChanged(int row)
{
    if (row < 0 || row >= mChannelList.size())
        return;
    mCurrentChannel = mChannelList[row].id;
    mUnreadByChannel[mCurrentChannel] = 0;
    mMentionsByChannel[mCurrentChannel] = 0;
    updateChannelIndicator(mCurrentChannel);
    updateMentionSummary();
    mHistory->clear();
    mSendButton->setEnabled(mChannelList[row].writable);
    mInput->setEnabled(mChannelList[row].writable);
    mClient->joinChannel(mCurrentChannel);
    mClient->requestHistory(mCurrentChannel, 0, 50);
}

void ChatPage::slotMessagesReceived(quint16 channel, const QVector<LinkMessage>& messages)
{
    if (channel != mCurrentChannel)
        return;
    for (const LinkMessage& message : messages)
        appendMessage(message);
}

void ChatPage::slotMessageReceived(const LinkMessage& message)
{
    const bool mention = isMentionForMe(message);
    if (message.channel == mCurrentChannel)
        appendMessage(message);
    else
        ++mUnreadByChannel[message.channel];

    if (mention)
    {
        ++mMentionsByChannel[message.channel];
        // Flashes the Windows taskbar entry even when the launcher is minimized.
        QApplication::alert(window(), 5000);
    }
    updateChannelIndicator(message.channel);
    updateMentionSummary();
}

void ChatPage::slotPresenceReceived(const QVector<LinkPresence>& players)
{
    mPlayers->clear();
    for (const LinkPresence& player : players)
    {
        QListWidgetItem* item = new QListWidgetItem(
            QStringLiteral("%1  %2").arg(player.level).arg(player.name), mPlayers);
        item->setForeground(QColor(safeColor(player.color)));
        if (player.inGame)
            item->setToolTip(tr("In game"));
    }
}

void ChatPage::slotNotice(quint8 severity, const QString& text)
{
    LinkMessage message;
    message.system = true;
    message.text = text;
    message.timestamp = QDateTime::currentSecsSinceEpoch();
    message.channel = mCurrentChannel;
    appendMessage(message);
    if (severity >= 2)
        setStatus(text, true);
}

void ChatPage::appendMessage(const LinkMessage& message)
{
    QScrollBar* scrollBar = mHistory->verticalScrollBar();
    mHistory->append(renderMessageHtml(message));
    if (scrollBar != nullptr)
    {
        // Do not move the QTextBrowser cursor: it steals focus from the
        // message field on some Qt/platform combinations.
        QTimer::singleShot(0, mHistory, [scrollBar]() {
            scrollBar->setValue(scrollBar->maximum());
        });
    }
}

QString ChatPage::safeColor(const QString& value)
{
    // Цвет приходит от сервера и попадает прямо в style="color:...",
    // поэтому пропускаем только #RRGGBB.
    static const QRegularExpression pattern(QStringLiteral("^#[0-9A-Fa-f]{6}$"));
    return pattern.match(value).hasMatch() ? value.toUpper() : QStringLiteral("#C8C8C8");
}

QString ChatPage::renderMessageHtml(const LinkMessage& message) const
{
    // Ничего из сети не попадает в HTML без экранирования.
    const QString author = message.author.toHtmlEscaped();
    const QString text = message.text.toHtmlEscaped();
    const QString time = QDateTime::fromSecsSinceEpoch(message.timestamp)
        .toString(QStringLiteral("HH:mm"));

    if (message.system)
        return QStringLiteral("<div style='color:#9A8F7A'><i>%1</i></div>").arg(text);

    const bool mention = isMentionForMe(message);
    const QString badge = message.level > 0
        ? QStringLiteral("<span style='background:#2B2318;color:#E8C87A;"
                         "padding:0 4px;border-radius:3px'>%1</span> ").arg(message.level)
        : QString();
    const QString mentionBadge = mention
        ? QStringLiteral("<span style='background:#6B4F1D;color:#FFE6A0;font-weight:bold;"
                         "padding:0 4px'>@</span> ") : QString();
    const QString containerStyle = mention
        ? QStringLiteral("margin-bottom:4px;background:#352C20;border-left:3px solid #D7B45A;padding:3px")
        : QStringLiteral("margin-bottom:4px");

    // U034: mirrored game and launcher messages deliberately share one visual
    // stream. MESSAGE_FROM_GAME stays in the protocol for diagnostics/dedupe,
    // but is no longer shown as a second pseudo-channel to the player.
    return QStringLiteral(
        "<div style='%1'>%2%3"
        "<span style='color:%4;font-weight:bold'>%5</span>"
        "<span style='color:#6E6A62'> %6</span><br>"
        "<span style='color:#DCD6CC'>%7</span></div>")
        .arg(containerStyle, mentionBadge, badge, safeColor(message.color), author, time, text);
}

bool ChatPage::isMentionForMe(const LinkMessage& message) const
{
    if (message.system || !mClient->authorized())
        return false;
    const QString name = mClient->profile().name.trimmed();
    if (name.isEmpty() || message.author.compare(name, Qt::CaseInsensitive) == 0)
        return false;

    // @Name is the explicit form. A full standalone character name is accepted
    // too, which is useful for names containing spaces copied from the roster.
    const QString escaped = QRegularExpression::escape(name);
    const QRegularExpression explicitMention(QStringLiteral("(^|[^\\p{L}\\p{N}_])@%1(?=$|[^\\p{L}\\p{N}_])").arg(escaped),
        QRegularExpression::CaseInsensitiveOption);
    if (explicitMention.match(message.text).hasMatch())
        return true;
    const QRegularExpression plainMention(QStringLiteral("(^|[^\\p{L}\\p{N}_])%1(?=$|[^\\p{L}\\p{N}_])").arg(escaped),
        QRegularExpression::CaseInsensitiveOption);
    return plainMention.match(message.text).hasMatch();
}

void ChatPage::updateChannelIndicator(quint16 channel)
{
    for (int row = 0; row < mChannelList.size(); ++row)
    {
        if (mChannelList[row].id != channel)
            continue;
        QListWidgetItem* item = mChannels->item(row);
        if (item == nullptr)
            return;
        QString caption = mChannelList[row].mirrorsGame
            ? QStringLiteral("%1  ↔").arg(mChannelList[row].name) : mChannelList[row].name;
        const int mentions = mMentionsByChannel.value(channel);
        const int unread = mUnreadByChannel.value(channel);
        if (mentions > 0)
            caption += QStringLiteral("   @%1").arg(mentions);
        else if (unread > 0)
            caption += QStringLiteral("   • %1").arg(unread);
        item->setText(caption);
        QFont font = item->font();
        font.setBold(mentions > 0 || unread > 0);
        item->setFont(font);
        if (mentions > 0)
            item->setForeground(QBrush(QColor(QStringLiteral("#E8C87A"))));
        else
            item->setForeground(QBrush());
        return;
    }
}

void ChatPage::updateMentionSummary()
{
    if (mMentionLabel == nullptr)
        return;
    int total = 0;
    for (auto it = mMentionsByChannel.constBegin(); it != mMentionsByChannel.constEnd(); ++it)
        total += it.value();
    mMentionLabel->setVisible(total > 0);
    mMentionLabel->setText(total > 0 ? tr("Mentions: %1").arg(total) : QString());
}

void ChatPage::slotSendClicked()
{
    const QString text = mInput->text().trimmed();
    if (text.isEmpty() || mCurrentChannel == 0 || !mClient->authorized())
        return;
    mInput->clear();
    mUnreadByChannel[mCurrentChannel] = 0;
    mMentionsByChannel[mCurrentChannel] = 0;
    updateChannelIndicator(mCurrentChannel);
    updateMentionSummary();
    // Эхо не рисуем: сервер вернёт сообщение пушем всем, включая автора,
    // иначе порядок в ленте разъедется.
    mClient->sendMessage(mCurrentChannel, text);
    // Keep the newest message visible even if the input currently has focus.
    if (QScrollBar* scrollBar = mHistory->verticalScrollBar())
        QTimer::singleShot(0, mHistory, [scrollBar]() { scrollBar->setValue(scrollBar->maximum()); });
}

void ChatPage::setStatus(const QString& text, bool error)
{
    if (mLoginStatus == nullptr)
        return;
    mLoginStatus->setText(text);
    mLoginStatus->setStyleSheet(error ? QStringLiteral("color:#E06A6A") : QString());
}

// Update only [Login], atomically, preserving unrelated settings and comments.
bool ChatPage::savePrimaryLogin()
{
    if (mSubmittedPassword.isEmpty()) return true; // One-time code is never a password.
    if (mSubmittedSettingsPath.isEmpty() || mSubmittedSettingsPath != mGameSettingsPath)
        return false;
    if (mSubmittedName.contains('\n') || mSubmittedName.contains('\r')
        || mSubmittedPassword.contains('\n') || mSubmittedPassword.contains('\r')) return false;
    QFile source(mSubmittedSettingsPath);
    QString content;
    if (source.exists()) {
        if (!source.open(QIODevice::ReadOnly)) return false;
        content = QString::fromUtf8(source.readAll());
        source.close();
    }
    QStringList output;
    bool inLogin = false, wroteLogin = false;
    const auto lines = content.split('\n');
    for (const QString& line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith('[') && trimmed.contains(']')) {
            inLogin = trimmed.mid(1, trimmed.indexOf(']') - 1).trimmed().compare("Login", Qt::CaseInsensitive) == 0;
            if (inLogin) {
                output << "[Login]";
                if (!wroteLogin) {
                    output << "name = " + mSubmittedName << "password = " + mSubmittedPassword;
                    wroteLogin = true;
                }
                continue;
            }
        }
        if (inLogin && trimmed.contains('=')) {
            const QString key = trimmed.left(trimmed.indexOf('=')).trimmed().toLower();
            if (key == "name" || key == "password") continue;
        }
        output << line;
    }
    if (!wroteLogin) output << "" << "[Login]" << "name = " + mSubmittedName << "password = " + mSubmittedPassword;
    if (!QDir().mkpath(QFileInfo(mSubmittedSettingsPath).absolutePath())) return false;
    QSaveFile target(mSubmittedSettingsPath);
    if (!target.open(QIODevice::WriteOnly)) return false;
    while (!output.isEmpty() && output.last().isEmpty()) output.removeLast();
    const QByteArray bytes = (output.join('\n') + '\n').toUtf8();
    if (target.write(bytes) != bytes.size() || !target.commit()) return false;
    mGamePassword = mSubmittedPassword;
    mNameEdit->setText(mSubmittedName);
    // Keep launcher in-memory settings in sync for subsequent Apply/Play saves.
    Settings::Manager::setString("name", "Login", mSubmittedName.toStdString());
    Settings::Manager::setString("password", "Login", mSubmittedPassword.toStdString());
    return true;
}
