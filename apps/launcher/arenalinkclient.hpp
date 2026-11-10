#ifndef ARENAMP_ARENALINKCLIENT_HPP
#define ARENAMP_ARENALINKCLIENT_HPP

// ArenaMP U025 — клиент ArenaLink в лаунчере.
//
// Одно постоянное TCP-соединение прямо с игровым сервером, свой порт.
// Ни RakNet, ни HTTP, ни опроса: сообщения приходят пушем, пока сокет жив.
// Пароль по сети не идёт — сервер присылает nonce, клиент отвечает
// HMAC-SHA256(sha256(пароль), nonce).

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QVector>
#include <string>

#include <components/openmw-mp/arenalink.hpp>

class QTcpSocket;
class QTimer;

namespace Launcher
{
    struct LinkProfile
    {
        quint32 userId = 0;
        QString name;
        int level = 0;
        QString color = QStringLiteral("#C8C8C8");
        QString className;
        quint16 voicePort = 0;
        bool authorized = false;
    };

    struct LinkChannel
    {
        quint16 id = 0;
        QString name;
        bool writable = true;
        bool mirrorsGame = false;
    };

    struct LinkMessage
    {
        quint64 id = 0;
        quint16 channel = 0;
        quint32 userId = 0;
        QString author;
        int level = 0;
        QString color;
        QString text;
        qint64 timestamp = 0;
        bool system = false;
        bool fromGame = false;
    };

    struct LinkPresence
    {
        quint32 userId = 0;
        QString name;
        int level = 0;
        QString color;
        bool inGame = false;
        bool inVoice = false;
    };

    class ArenaLinkClient : public QObject
    {
        Q_OBJECT

    public:
        explicit ArenaLinkClient(QObject* parent = nullptr);
        ~ArenaLinkClient() override;

        /// Адрес игрового сервера и его игровой порт. Порт чата берётся как
        /// игровой + 2, если сервер не переопределил его в конфиге.
        void setServer(const QString& host, quint16 gamePort, quint16 linkPort = 0);

        void connectAndLogin(const QString& name, const QString& password);
        void connectAndLoginWithCode(const QString& name, const QString& code);
        void disconnectFromServer();

        QString chatLogPath() const { return mLogPath; }
        void configureChatLog(const QString& settingsPath);
        void logCredentialSource(bool readable, bool hasName, bool hasPassword);
        void logLoginValidation(bool hasName, bool hasPassword);

        bool authorized() const { return mProfile.authorized; }
        const LinkProfile& profile() const { return mProfile; }

        void joinChannel(quint16 channel);
        void requestHistory(quint16 channel, quint64 beforeId, quint16 limit);
        void sendMessage(quint16 channel, const QString& text);
        /// Тикет для голоса: 0 — лобби, 1 — игровая сессия.
        void requestVoiceTicket(quint8 scope);

    signals:
        void connecting();
        void loggedIn(const LinkProfile& profile);
        void loginFailed(quint8 reason, const QString& text);
        void disconnected(const QString& reason);
        void channelsReceived(const QVector<LinkChannel>& channels);
        void messagesReceived(quint16 channel, const QVector<LinkMessage>& messages);
        void messageReceived(const LinkMessage& message);
        void presenceReceived(const QVector<LinkPresence>& players);
        void voiceTicketReceived(const QByteArray& ticket, quint16 port, int ttlSeconds);
        void notice(quint8 severity, const QString& text);

    private slots:
        void slotConnected();
        void slotReadyRead();
        void slotDisconnected();
        void slotError();
        void slotPing();

    private:
        void logEvent(const QString& event);
        void handleFrame(const ArenaLink::FrameHeader& header, const QByteArray& payload);
        void send(const std::string& frame);
        /// HMAC-SHA256(sha256(пароль), nonce) — пароль остаётся на машине игрока.
        static QByteArray makeProof(const QString& password, const QByteArray& nonce);
        static QString colorToHex(quint32 color);

        QTcpSocket* mSocket;
        QTimer* mPingTimer;
        QByteArray mBuffer;

        QString mHost;
        quint16 mLinkPort = 0;
        QString mPendingName;
        QString mPendingSecret;
        quint8 mPendingMode = ArenaLink::AUTH_PROOF;
        LinkProfile mProfile;
        QString mLogPath;
        QString mStage = QStringLiteral("idle");
        quint64 mAttempt = 0;
        QElapsedTimer mAttemptClock;
        quint64 mGeneration = 0;
        bool mAwaitingChallenge = false;
    };
}
#endif
