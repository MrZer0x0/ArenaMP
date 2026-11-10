// ArenaMP U025 — клиент ArenaLink.
//
// Сборка: arenalinkclient.cpp в LAUNCHER, .hpp в LAUNCHER_HEADER и
// LAUNCHER_HEADER_MOC. Qt5::Network уже подключён — новых зависимостей нет.

#include "arenalinkclient.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QMessageAuthenticationCode>
#include <QTcpSocket>
#include <QSignalBlocker>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QDebug>
#include <QHostAddress>

using namespace Launcher;
using namespace ArenaLink;

namespace
{
    const char* socketErrorName(QAbstractSocket::SocketError error)
    {
        switch (error)
        {
            case QAbstractSocket::ConnectionRefusedError: return "CONNECTION_REFUSED";
            case QAbstractSocket::RemoteHostClosedError: return "REMOTE_CLOSED";
            case QAbstractSocket::HostNotFoundError: return "HOST_NOT_FOUND";
            case QAbstractSocket::SocketTimeoutError: return "TIMEOUT";
            case QAbstractSocket::NetworkError: return "NETWORK_ERROR";
            case QAbstractSocket::SocketAccessError: return "ACCESS_DENIED";
            default: return "OTHER";
        }
    }
    constexpr int sPingIntervalMs = 30000;
    constexpr int sConnectTimeoutMs = 8000;
}

ArenaLinkClient::ArenaLinkClient(QObject* parent)
    : QObject(parent)
    , mSocket(new QTcpSocket(this))
    , mPingTimer(new QTimer(this))
{
    connect(mSocket, &QTcpSocket::connected, this, &ArenaLinkClient::slotConnected);
    connect(mSocket, &QTcpSocket::readyRead, this, &ArenaLinkClient::slotReadyRead);
    connect(mSocket, &QTcpSocket::disconnected, this, &ArenaLinkClient::slotDisconnected);
    connect(mSocket, &QAbstractSocket::errorOccurred, this, &ArenaLinkClient::slotError);

    configureChatLog(QString());
    connect(mSocket, &QTcpSocket::hostFound, this, [this]() {
        logEvent(QStringLiteral("DNS_RESOLVED"));
    });
    mPingTimer->setInterval(sPingIntervalMs);
    connect(mPingTimer, &QTimer::timeout, this, &ArenaLinkClient::slotPing);
}

ArenaLinkClient::~ArenaLinkClient()
{
    disconnectFromServer();
}

void ArenaLinkClient::configureChatLog(const QString& settingsPath)
{
    QString fallback = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (fallback.isEmpty()) fallback = QDir::homePath() + QStringLiteral("/ArenaMP");
    const QString preferred = settingsPath.isEmpty() ? fallback : QFileInfo(settingsPath).absolutePath();
    for (const QString& directory : { preferred, fallback })
    {
        if (!QDir().mkpath(directory)) continue;
        QFile file(QDir(directory).filePath(QStringLiteral("Chat.log")));
        if (file.open(QIODevice::WriteOnly | QIODevice::Append))
        {
            mLogPath = file.fileName();
            return;
        }
    }
    mLogPath.clear();
    qWarning("ArenaLink: cannot create Chat.log in the config or app data directory");
}

void ArenaLinkClient::logEvent(const QString& event)
{
    if (mLogPath.isEmpty()) return;
    // This function only receives allowlisted metadata, never network payloads,
    // account names, passwords, proofs, nonces, salts, messages or exception dumps.
    QString line = event.left(1024);
    line.replace(QLatin1Char('\n'), QLatin1Char(' '));
    line.replace(QLatin1Char('\r'), QLatin1Char(' '));
    if (QFileInfo(mLogPath).size() >= 1024 * 1024)
    {
        QFile::remove(mLogPath + QStringLiteral(".1"));
        if (!QFile::rename(mLogPath, mLogPath + QStringLiteral(".1"))) return;
    }
    QFile file(mLogPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) return;
    const QString record = QStringLiteral("%1 pid=%2 attempt=%3 elapsed_ms=%4 stage=%5 %6\n")
        .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs))
        .arg(QCoreApplication::applicationPid()).arg(mAttempt)
        .arg(mAttemptClock.isValid() ? mAttemptClock.elapsed() : 0).arg(mStage, line);
    const QByteArray bytes = record.toUtf8();
    file.write(bytes);
    file.flush();
}

void ArenaLinkClient::logCredentialSource(bool readable, bool hasName, bool hasPassword)
{
    logEvent(QStringLiteral("CONFIG_SOURCE settings_readable=%1 name_present=%2 password_present=%3")
        .arg(readable).arg(hasName).arg(hasPassword));
}

void ArenaLinkClient::setServer(const QString& host, quint16 gamePort, quint16 linkPort)
{
    mHost = host.trimmed();
    // ArenaLink TCP defaults to game port + 2.
    mLinkPort = linkPort != 0 ? linkPort : static_cast<quint16>(gamePort + 2);
}

void ArenaLinkClient::logLoginValidation(bool hasName, bool hasPassword)
{
    logEvent(QStringLiteral("LOCAL_VALIDATION_FAILED name_present=%1 password_present=%2")
        .arg(hasName).arg(hasPassword));
}

void ArenaLinkClient::connectAndLogin(const QString& name, const QString& password)
{
    disconnectFromServer();
    mSocket->abort();
    mBuffer.clear();
    if (mHost.isEmpty() || mLinkPort < 3 || name.toUtf8().size() > static_cast<int>(sMaxNick)
        || password.toUtf8().size() > 128)
    {
        logEvent(QStringLiteral("LOCAL_VALIDATION_FAILED"));
        emit loginFailed(FAIL_BAD_CREDENTIALS, tr("Invalid endpoint or credentials exceed protocol limits"));
        return;
    }
    const quint64 generation = ++mGeneration;
    mAwaitingChallenge = true;
    ++mAttempt;
    mAttemptClock.start();
    mStage = QStringLiteral("connecting");
    logEvent(QStringLiteral("CONNECT build=U035i protocol=%1 host=%2 tcp_port=%3 name_present=%4 password_present=%5")
        .arg(sProtocol).arg(mHost).arg(mLinkPort).arg(!name.isEmpty()).arg(!password.isEmpty()));
    mPendingName = name;
    mPendingSecret = password;
    mPendingMode = AUTH_PROOF;
    emit connecting();
    mSocket->connectToHost(mHost, mLinkPort);
    QTimer::singleShot(sConnectTimeoutMs, this, [this, generation]()
    {
        if (generation == mGeneration && !mProfile.authorized)
        {
            logEvent(QStringLiteral("TIMEOUT socket_state=%1 bytes_pending=%2")
                .arg(static_cast<int>(mSocket->state())).arg(mSocket->bytesToWrite()));
            disconnectFromServer();
            mSocket->abort();
            emit disconnected(tr("Chat sign-in timed out at %1:%2. Check the ArenaLink service.").arg(mHost).arg(mLinkPort));
        }
    });
}

void ArenaLinkClient::connectAndLoginWithCode(const QString& name, const QString& code)
{
    connectAndLogin(name, code);
    mPendingMode = AUTH_CODE;
}

void ArenaLinkClient::disconnectFromServer()
{
    logEvent(QStringLiteral("LOCAL_DISCONNECT"));
    ++mGeneration;
    mAwaitingChallenge = false;
    mPingTimer->stop();
    mProfile = LinkProfile();
    mBuffer.clear();
    mPendingSecret.clear();
    if (mSocket->state() != QAbstractSocket::UnconnectedState)
        mSocket->disconnectFromHost();
}

void ArenaLinkClient::slotConnected()
{
    logEvent(QStringLiteral("TCP_CONNECTED peer=%1 peer_port=%2")
        .arg(mSocket->peerAddress().toString()).arg(mSocket->peerPort()));
    mStage = QStringLiteral("waiting_challenge");
    send(makeHello(0, "ArenaMP U035i", mPendingName.toStdString()));
    // Дальше ждём CHALLENGE: без nonce отвечать нечем.
}

QByteArray ArenaLinkClient::makeProof(const QString& password, const QByteArray& nonce)
{
    const QByteArray key = QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Sha256);
    return QMessageAuthenticationCode::hash(nonce, key, QCryptographicHash::Sha256);
}

QString ArenaLinkClient::colorToHex(quint32 color)
{
    return QStringLiteral("#%1").arg(color & 0x00FFFFFFu, 6, 16, QLatin1Char('0')).toUpper();
}

void ArenaLinkClient::slotReadyRead()
{
    mBuffer.append(mSocket->readAll());

    for (;;)
    {
        FrameHeader header;
        std::string payload;
        std::size_t consumed = 0;
        bool fatal = false;
        if (!nextFrame(mBuffer.constData(), static_cast<std::size_t>(mBuffer.size()),
                header, payload, consumed, fatal))
        {
            if (fatal)
            {
                logEvent(QStringLiteral("MALFORMED_FRAME buffered_bytes=%1").arg(mBuffer.size()));
                mSocket->abort();
                emit disconnected(tr("The server sent malformed data"));
            }
            return;
        }
        const QByteArray frameData(payload.data(), static_cast<int>(payload.size()));
        mBuffer.remove(0, static_cast<int>(consumed));
        handleFrame(header, frameData);
    }
}

void ArenaLinkClient::handleFrame(const FrameHeader& header, const QByteArray& payload)
{
    logEvent(QStringLiteral("RX type=%1 bytes=%2").arg(static_cast<int>(header.type)).arg(payload.size()));
    Reader reader(payload.constData(), static_cast<std::size_t>(payload.size()));

    switch (header.type)
    {
        case TYPE_CHALLENGE:
        {
            QByteArray nonce(static_cast<int>(sNonceSize), '\0');
            if (!mAwaitingChallenge || !reader.raw(nonce.data(), sNonceSize))
            {
                logEvent(QStringLiteral("CHALLENGE_INVALID_OR_DUPLICATE"));
                return;
            }
            const quint8 serverMode = reader.u8();
            const QByteArray salt = QByteArray::fromStdString(reader.text(128));

            logEvent(QStringLiteral("CHALLENGE mode=%1 salt_bytes=%2 valid=%3")
                .arg(serverMode).arg(salt.size()).arg(reader.ok()));
            quint8 mode = mPendingMode == AUTH_CODE ? static_cast<quint8>(AUTH_CODE) : serverMode;
            if (!reader.ok() || (mode != AUTH_PROOF && mode != AUTH_TES3MP_PROOF && mode != AUTH_CODE))
            {
                logEvent(QStringLiteral("AUTH_METHOD_UNSUPPORTED_OR_PROTOCOL_MISMATCH"));
                disconnectFromServer();
                emit loginFailed(FAIL_BAD_CREDENTIALS, tr("Chat server needs a supported secure sign-in method"));
                return;
            }
            mAwaitingChallenge = false;
            std::string secret;
            if (mode == AUTH_TES3MP_PROOF)
            {
                const auto hexHash = [](const QByteArray& bytes) {
                    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
                };
                const QByteArray first = hexHash(mPendingSecret.toUtf8());
                const QByteArray gameHash = hexHash(first + hexHash(hexHash(first)));
                const QByteArray key = QCryptographicHash::hash(gameHash + salt, QCryptographicHash::Sha256);
                const QByteArray proof = QMessageAuthenticationCode::hash(nonce, key, QCryptographicHash::Sha256);
                secret.assign(proof.constData(), static_cast<std::size_t>(proof.size()));
            }
            else if (mode == AUTH_PROOF)
            {
                const QByteArray proof = makeProof(mPendingSecret, nonce);
                secret.assign(proof.constData(), static_cast<std::size_t>(proof.size()));
            }
            else
            {
                secret = mPendingSecret.toStdString();
            }

            mStage = QStringLiteral("waiting_auth_result");
            logEvent(QStringLiteral("AUTH_SEND mode=%1").arg(mode));
            send(makeAuth(mPendingName.toStdString(), mode, secret));
            mPendingSecret.clear();      // пароль в памяти не держим
            break;
        }
        case TYPE_AUTH_OK:
        {
            AuthResult result;
            if (!parseAuthOk(reader, result))
            {
                logEvent(QStringLiteral("AUTH_OK_MALFORMED"));
                return;
            }
            mStage = QStringLiteral("authorized");
            logEvent(QStringLiteral("AUTH_OK"));
            mProfile.userId = result.userId;
            mProfile.name = QString::fromStdString(result.name);
            mProfile.level = result.level;
            mProfile.color = colorToHex(result.color);
            mProfile.className = QString::fromStdString(result.className);
            mProfile.voicePort = result.voicePort;
            mProfile.authorized = true;
            mPingTimer->start();
            emit loggedIn(mProfile);
            break;
        }
        case TYPE_AUTH_FAIL:
        {
            const quint8 reason = reader.u8();
            const QString text = QString::fromStdString(reader.text16(256));
            logEvent(QStringLiteral("AUTH_FAIL reason=%1 valid=%2").arg(reason).arg(reader.ok()));
            // Preserve the server's authentication error instead of replacing
            // it with a generic socket disconnect notification.
            const QSignalBlocker blocker(mSocket);
            disconnectFromServer();
            mSocket->abort();
            emit loginFailed(reason, text);
            break;
        }
        case TYPE_CHANNELS:
        {
            std::vector<Channel> channels;
            if (!parseChannels(reader, channels))
                return;
            QVector<LinkChannel> out;
            out.reserve(static_cast<int>(channels.size()));
            for (const Channel& channel : channels)
            {
                LinkChannel item;
                item.id = channel.id;
                item.name = QString::fromStdString(channel.name);
                item.writable = (channel.flags & CHANNEL_WRITABLE) != 0;
                item.mirrorsGame = (channel.flags & CHANNEL_MIRRORS_GAME) != 0;
                out.push_back(item);
            }
            emit channelsReceived(out);
            break;
        }
        case TYPE_MESSAGES:
        {
            quint16 channel = 0;
            std::vector<Message> messages;
            if (!parseMessages(reader, channel, messages))
                return;
            QVector<LinkMessage> out;
            out.reserve(static_cast<int>(messages.size()));
            for (const Message& message : messages)
            {
                LinkMessage item;
                item.id = message.id;
                item.channel = message.channel;
                item.userId = message.userId;
                item.author = QString::fromStdString(message.author);
                item.level = message.level;
                item.color = colorToHex(message.color);
                item.text = QString::fromStdString(message.text);
                item.timestamp = message.timestamp;
                item.system = (message.flags & MESSAGE_SYSTEM) != 0;
                item.fromGame = (message.flags & MESSAGE_FROM_GAME) != 0;
                out.push_back(item);
            }
            emit messagesReceived(channel, out);
            break;
        }
        case TYPE_MESSAGE:
        {
            Message message;
            if (!readMessage(reader, message))
                return;
            LinkMessage item;
            item.id = message.id;
            item.channel = message.channel;
            item.userId = message.userId;
            item.author = QString::fromStdString(message.author);
            item.level = message.level;
            item.color = colorToHex(message.color);
            item.text = QString::fromStdString(message.text);
            item.timestamp = message.timestamp;
            item.system = (message.flags & MESSAGE_SYSTEM) != 0;
            item.fromGame = (message.flags & MESSAGE_FROM_GAME) != 0;
            emit messageReceived(item);
            break;
        }
        case TYPE_PRESENCE:
        case TYPE_PRESENCE_DELTA:
        {
            std::vector<Presence> players;
            if (!parsePresence(reader, players))
                return;
            QVector<LinkPresence> out;
            out.reserve(static_cast<int>(players.size()));
            for (const Presence& player : players)
            {
                LinkPresence item;
                item.userId = player.userId;
                item.name = QString::fromStdString(player.name);
                item.level = player.level;
                item.color = colorToHex(player.color);
                item.inGame = (player.flags & PRESENCE_INGAME) != 0;
                item.inVoice = (player.flags & PRESENCE_VOICE) != 0;
                out.push_back(item);
            }
            emit presenceReceived(out);
            break;
        }
        case TYPE_VOICE_TICKET:
        {
            QByteArray ticket(static_cast<int>(sTicketSize), '\0');
            if (!reader.raw(ticket.data(), sTicketSize))
                return;
            const quint16 port = reader.u16();
            const quint16 ttl = reader.u16();
            emit voiceTicketReceived(ticket, port, ttl);
            break;
        }
        case TYPE_NOTICE:
        {
            const quint8 severity = reader.u8();
            emit notice(severity, QString::fromStdString(reader.text16(512)));
            break;
        }
        case TYPE_PONG:
        default:
            break;
    }
}

void ArenaLinkClient::joinChannel(quint16 channel)
{
    Writer writer;
    writer.u16(channel);
    send(frame(TYPE_JOIN_CHANNEL, writer.data()));
}

void ArenaLinkClient::requestHistory(quint16 channel, quint64 beforeId, quint16 limit)
{
    send(makeHistoryRequest(channel, beforeId, limit));
}

void ArenaLinkClient::sendMessage(quint16 channel, const QString& text)
{
    send(makeSend(channel, text.toStdString()));
}

void ArenaLinkClient::requestVoiceTicket(quint8 scope)
{
    send(makeVoiceTicketRequest(scope));
}

void ArenaLinkClient::send(const std::string& data)
{
    if (mSocket->state() != QAbstractSocket::ConnectedState)
    {
        logEvent(QStringLiteral("TX_SKIPPED socket_state=%1").arg(static_cast<int>(mSocket->state())));
        return;
    }
    const qint64 queued = mSocket->write(data.data(), static_cast<qint64>(data.size()));
    const unsigned type = data.size() >= sHeaderSize ? static_cast<unsigned char>(data[4]) : 0;
    logEvent(QStringLiteral("TX type=%1 bytes=%2 queued=%3").arg(type).arg(data.size()).arg(queued));
}

void ArenaLinkClient::slotPing()
{
    Writer writer;
    writer.u32(static_cast<quint32>(QDateTime::currentSecsSinceEpoch()));
    send(frame(TYPE_PING, writer.data()));
}

void ArenaLinkClient::slotDisconnected()
{
    logEvent(QStringLiteral("TCP_DISCONNECTED socket_error=%1").arg(static_cast<int>(mSocket->error())));
    mPingTimer->stop();
    ++mGeneration;
    mPendingSecret.clear();
    mBuffer.clear();
    mProfile.authorized = false;
    emit disconnected(tr("Connection to the server was lost"));
}

void ArenaLinkClient::slotError()
{
    logEvent(QStringLiteral("SOCKET_ERROR code=%1 state=%2 name=%3")
        .arg(static_cast<int>(mSocket->error())).arg(static_cast<int>(mSocket->state()))
        .arg(QString::fromLatin1(socketErrorName(mSocket->error()))));
    mPingTimer->stop();
    ++mGeneration;
    mPendingSecret.clear();
    mBuffer.clear();
    mProfile.authorized = false;
    emit disconnected(mSocket->errorString());
}
