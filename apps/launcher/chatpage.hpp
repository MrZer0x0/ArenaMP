#ifndef ARENAMP_CHATPAGE_HPP
#define ARENAMP_CHATPAGE_HPP

// ArenaMP U025 — пятая страница лаунчера: «Чат и голос».
//
// Два состояния в одном QStackedLayout:
//   0 — карточка входа (имя персонажа + пароль, либо код из /chatlink).
//       Регистрации здесь нет: аккаунт и персонаж создаются только
//       на игровом сервере.
//   1 — чат: каналы слева, лента справа, поле ввода снизу,
//       панель управления голосом под списком каналов.
//
// Ник красится тем же цветом, что в игре, перед ником — бейдж уровня.
// Связь — постоянный TCP к игровому серверу (ArenaLink), без веба.

#include <QHash>
#include <QVector>
#include <QString>
#include <QWidget>

#include "arenalinkclient.hpp"

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedLayout;
class QTextBrowser;

namespace Config { class LauncherSettings; }

namespace Launcher
{
    class VoicePanel;

    class ChatPage : public QWidget
    {
        Q_OBJECT

    public:
        explicit ChatPage(Config::LauncherSettings& launcherSettings, QWidget* parent = nullptr);

        void loadSettings(const QString& gameSettingsPath = QString());
        void saveSettings();
        void refreshLoginFromGameSettings();

        /// MainDialog сообщает адрес выбранного сервера: чат и голос ходят
        /// на тот же хост, порты игровой+2 и игровой+1.
        void setServerEndpoint(const QString& host, quint16 gamePort);

        /// Игра запущена/остановлена: лобби-голос выключается, коммутатор
        /// уходит в режим proximity.
        void setGameRunning(bool running);

        VoicePanel* voicePanel() const { return mVoicePanel; }
        bool voiceEnabled() const;
        QString pushToTalkKey() const;
        QString captureDevice() const;

    signals:
        /// Тикет получен и его можно отдать коммутатору.
        void voiceTicketReady(const QByteArray& ticket, quint16 port, int ttlSeconds);

    private slots:
        void slotLoginClicked();
        void slotToggleCodeMode();
        void slotLogoutClicked();
        void slotSendClicked();
        void slotChannelChanged(int row);

        void slotLoggedIn(const LinkProfile& profile);
        void slotLoginFailed(quint8 reason, const QString& text);
        void slotDisconnected(const QString& reason);
        void slotChannelsReceived(const QVector<LinkChannel>& channels);
        void slotMessagesReceived(quint16 channel, const QVector<LinkMessage>& messages);
        void slotMessageReceived(const LinkMessage& message);
        void slotPresenceReceived(const QVector<LinkPresence>& players);
        void slotNotice(quint8 severity, const QString& text);

    private:
        void buildLoginCard();
        void buildChatView();
        void appendMessage(const LinkMessage& message);
        /// Единственное место, где текст превращается в HTML.
        QString renderMessageHtml(const LinkMessage& message) const;
        bool isMentionForMe(const LinkMessage& message) const;
        void updateChannelIndicator(quint16 channel);
        void updateMentionSummary();
        void tryAutoLogin();
        void scheduleAutoLogin();
        static QString safeColor(const QString& value);
        void setStatus(const QString& text, bool error = false);

        Config::LauncherSettings& mLauncherSettings;
        QString mGameSettingsPath;
        QString mGamePassword;
        QString mSubmittedName, mSubmittedPassword, mSubmittedSettingsPath;
        bool savePrimaryLogin();
        ArenaLinkClient* mClient;
        VoicePanel* mVoicePanel = nullptr;
        QStackedLayout* mStack;

        QLineEdit* mNameEdit = nullptr;
        QLineEdit* mSecretEdit = nullptr;
        QPushButton* mLoginButton = nullptr;
        QPushButton* mUseCodeButton = nullptr;
        QLabel* mLoginStatus = nullptr;
        bool mCodeMode = false;

        QListWidget* mChannels = nullptr;
        QListWidget* mPlayers = nullptr;
        QTextBrowser* mHistory = nullptr;
        QLineEdit* mInput = nullptr;
        QPushButton* mSendButton = nullptr;
        QLabel* mProfileLabel = nullptr;
        QLabel* mMentionLabel = nullptr;
        QPushButton* mLogoutButton = nullptr;

        QVector<LinkChannel> mChannelList;
        QHash<quint16, int> mUnreadByChannel;
        QHash<quint16, int> mMentionsByChannel;
        QString mServerHost;
        quint16 mGamePort = 0;
        quint16 mCurrentChannel = 0;
        bool mGameRunning = false;
        bool mLoginInProgress = false;
        bool mManualLogout = false;
        bool mAutoLoginBlocked = false;
        bool mReconnectScheduled = false;
    };
}
#endif
