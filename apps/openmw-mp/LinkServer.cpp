// ArenaLink transport. The host must supply thread-safe account callbacks;
// LinkAccountStore supplies the native server's JSON account adapter.
#ifdef _WIN32
#  define _CRT_RAND_S
#endif
#include "LinkServer.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <cstdlib>
#include <cerrno>
#include <limits>
#include <utility>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
   using socklen_t = int;
#  define ARENA_CLOSESOCKET closesocket
#else
#  include <arpa/inet.h>
#  include <fcntl.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <sys/select.h>
#  include <sys/socket.h>
#  include <unistd.h>
#  define ARENA_CLOSESOCKET ::close
#endif

using namespace mwmp;
using namespace ArenaLink;

namespace
{
    std::uint64_t nowSec()
    {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    }

    bool randomBytes(std::uint8_t* out, std::size_t size)
    {
#ifdef _WIN32
        for (std::size_t i = 0; i < size; ++i)
        {
            unsigned int value = 0;
            if (rand_s(&value) != 0) return false;
            out[i] = static_cast<std::uint8_t>(value);
        }
        return true;
#else
        std::ifstream entropy("/dev/urandom", std::ios::binary);
        return static_cast<bool>(entropy.read(reinterpret_cast<char*>(out), size));
#endif
    }

    bool nonblocking(std::intptr_t socket)
    {
#ifdef _WIN32
        u_long enabled = 1;
        return ioctlsocket(socket, FIONBIO, &enabled) == 0;
#else
        const int flags = fcntl(socket, F_GETFL, 0);
        return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
    }

    int socketError()
    {
#ifdef _WIN32
        return WSAGetLastError();
#else
        return errno;
#endif
    }

    bool wouldBlock()
    {
#ifdef _WIN32
        const int error = WSAGetLastError();
        return error == WSAEWOULDBLOCK || error == WSAEINTR;
#else
        return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
    }

}

struct LinkServer::Client
{
    std::uint64_t id = 0;
    std::intptr_t socket = -1;
    std::string address;
    std::string inBuffer;
    std::string outBuffer;
    std::uint8_t nonce[sNonceSize] = {};
    bool helloDone = false;
    bool authAttempted = false;
    LinkAccount account;
    bool accountFound = false;
    std::string requestedName;
    bool authorized = false;
    std::uint32_t userId = 0;
    std::string name;
    std::uint16_t level = 0;
    std::uint32_t color = 0xC8C8C8u;
    std::uint16_t channel = 0;
    std::uint64_t lastSeen = 0;
    std::uint64_t minuteStart = 0;
    unsigned sentThisMinute = 0;
};

LinkServer::LinkServer() = default;

LinkServer::~LinkServer()
{
    stop();
}

void LinkServer::configure(const LinkConfig& config, const LinkCallbacks& callbacks)
{
    mConfig = config;
    mCallbacks = callbacks;
}

void LinkServer::diagnose(const std::string& event) const
{
    try { if (mCallbacks.diagnostic) mCallbacks.diagnostic(event); }
    catch (...) { /* A diagnostic sink must not interrupt networking. */ }
}

bool LinkServer::start(unsigned short gamePort)
{
    diagnose("START build=U032 protocol=" + std::to_string(sProtocol)
        + " game_port=" + std::to_string(gamePort) + " bind=" + mConfig.bindAddress);
    if (!mConfig.enabled || mRunning.load())
    {
        diagnose("START_SKIPPED disabled_or_already_running");
        return false;
    }

    if (mCallbacks.findAccount == nullptr
        || ((mConfig.authMode == AUTH_PROOF || mConfig.authMode == AUTH_TES3MP_PROOF) && mCallbacks.verifyProof == nullptr)
        || (mConfig.authMode == AUTH_CODE && mCallbacks.verifyLinkCode == nullptr)
        || (mConfig.authMode != AUTH_PROOF && mConfig.authMode != AUTH_TES3MP_PROOF && mConfig.authMode != AUTH_CODE)
        || (mConfig.port == 0 && gamePort > 65533))
    {
        diagnose("START_FAILED invalid_callbacks_auth_mode_or_port");
        return false;
    }

    mPort = mConfig.port != 0 ? mConfig.port : static_cast<unsigned short>(gamePort + 2);

    mListenSocket = static_cast<std::intptr_t>(::socket(AF_INET, SOCK_STREAM, 0));
    if (mListenSocket < 0)
    {
        diagnose("SOCKET_CREATE_FAILED error=" + std::to_string(socketError()));
        return false;
    }

    if (!nonblocking(mListenSocket))
    {
        diagnose("NONBLOCKING_FAILED error=" + std::to_string(socketError()));
        ARENA_CLOSESOCKET(mListenSocket);
        mListenSocket = -1;
        return false;
    }
#ifndef _WIN32
    if (mListenSocket >= FD_SETSIZE)
    {
        ARENA_CLOSESOCKET(mListenSocket);
        mListenSocket = -1;
        return false;
    }
#endif
    int reuse = 1;
    ::setsockopt(mListenSocket, SOL_SOCKET, SO_REUSEADDR,
        reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(mPort);
    address.sin_addr.s_addr = mConfig.bindAddress == "0.0.0.0"
        ? htonl(INADDR_ANY) : inet_addr(mConfig.bindAddress.c_str());

    if (::bind(mListenSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0
        || ::listen(mListenSocket, 16) != 0)
    {
        diagnose("BIND_OR_LISTEN_FAILED tcp_port=" + std::to_string(mPort)
            + " error=" + std::to_string(socketError()));
        ARENA_CLOSESOCKET(mListenSocket);
        mListenSocket = -1;
        return false;
    }

    // Каналы по умолчанию. Первый зеркалит внутриигровой чат, поэтому
    // написанное в лаунчере видно в игре и наоборот; остальные живут
    // только в лаунчере.
    mChannels = {
        { 1, "Общий",        static_cast<std::uint8_t>(CHANNEL_WRITABLE | (mConfig.mirrorGameChat ? CHANNEL_MIRRORS_GAME : 0)) },
        { 2, "Поиск группы", CHANNEL_WRITABLE },
        { 3, "Торговля",     CHANNEL_WRITABLE },
        { 4, "Объявления",   static_cast<std::uint8_t>(CHANNEL_ADMIN_ONLY) },
    };
    loadHistory();

    diagnose("LISTENING tcp_port=" + std::to_string(mPort));
    mRunning.store(true);
    mThread = std::thread(&LinkServer::threadMain, this);
    return true;
}

void LinkServer::stop()
{
    if (!mRunning.exchange(false))
        return;
    diagnose("STOP_REQUEST");
    if (mThread.joinable())
        mThread.join();
    if (mListenSocket >= 0)
    {
        ARENA_CLOSESOCKET(mListenSocket);
        mListenSocket = -1;
    }

    std::lock_guard<std::mutex> lock(mMutex);
    for (Client* client : mClients)
    {
        if (client->socket >= 0)
            ARENA_CLOSESOCKET(client->socket);
        delete client;
    }
    mClients.clear();
}

LinkServer::Stats LinkServer::stats() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    Stats copy = mStats;
    copy.connections = static_cast<unsigned>(mClients.size());
    copy.authorized = static_cast<unsigned>(std::count_if(mClients.begin(), mClients.end(),
        [](const Client* client) { return client->authorized; }));
    return copy;
}

void LinkServer::threadMain()
{
    // Один поток, select по всем сокетам: соединений десятки, не тысячи,
    // и отдельный поток на клиента здесь не нужен.
    while (mRunning.load())
    {
        fd_set reads, writes;
        FD_ZERO(&reads);
        FD_ZERO(&writes);
        FD_SET(mListenSocket, &reads);
        std::intptr_t largest = mListenSocket;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            for (Client* client : mClients)
            {
                if (client->socket < 0) continue;
                FD_SET(client->socket, &reads);
                if (!client->outBuffer.empty()) FD_SET(client->socket, &writes);
                largest = std::max(largest, client->socket);
            }
        }
        timeval timeout{0, 200000};
        const int ready = ::select(
#ifdef _WIN32
            0,
#else
            static_cast<int>(largest + 1),
#endif
            &reads, &writes, nullptr, &timeout);
        if (!mRunning.load()) break;
        if (ready > 0 && FD_ISSET(mListenSocket, &reads)) acceptPending();

        std::lock_guard<std::mutex> lock(mMutex);
        const std::uint64_t now = nowSec();
        for (auto it = mClients.begin(); it != mClients.end();)
        {
            Client* client = *it;
            if (ready > 0 && client->socket >= 0 && FD_ISSET(client->socket, &reads))
                serviceClient(*client);
            if (client->socket >= 0) flushOutput(*client);
            // 90 с без единого кадра — клиент мёртв. Лаунчер шлёт PING
            // раз в 30 с, так что живое соединение сюда не попадает.
            if (client->socket < 0 || now - client->lastSeen > 90)
            {
                if (client->socket >= 0)
                    ARENA_CLOSESOCKET(client->socket);
                delete client;
                it = mClients.erase(it);
            }
            else
                ++it;
        }
    }
}

void LinkServer::acceptPending()
{
    sockaddr_in from{};
    socklen_t fromSize = sizeof(from);
    const std::intptr_t socket = static_cast<std::intptr_t>(::accept(mListenSocket,
        reinterpret_cast<sockaddr*>(&from), &fromSize));
    if (socket < 0)
        return;

    std::lock_guard<std::mutex> lock(mMutex);
    if (mClients.size() >= std::min<unsigned>(mConfig.maxConnections, FD_SETSIZE - 1)
#ifndef _WIN32
        || socket >= FD_SETSIZE
#endif
        )
    {
        diagnose("ACCEPT_REJECTED connection_or_descriptor_limit");
        ARENA_CLOSESOCKET(socket);
        return;
    }

    const std::string address = inet_ntoa(from.sin_addr);
    if (mCallbacks.isAddressBanned && mCallbacks.isAddressBanned(address))
    {
        diagnose("ACCEPT_REJECTED address_banned_or_banlist_unreadable peer=" + address);
        ARENA_CLOSESOCKET(socket);
        return;
    }
    const unsigned fromAddress = static_cast<unsigned>(std::count_if(mClients.begin(), mClients.end(),
        [&address](const Client* client) { return client->address == address; }));
    if (fromAddress >= mConfig.maxConnectionsPerAddress)
    {
        diagnose("ACCEPT_REJECTED per_address_limit peer=" + address);
        ARENA_CLOSESOCKET(socket);
        return;
    }

    if (!nonblocking(socket))
    {
        ARENA_CLOSESOCKET(socket);
        return;
    }
    int noDelay = 1;
    ::setsockopt(socket, IPPROTO_TCP, TCP_NODELAY,
        reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));

    Client* client = new Client();
    client->id = mNextConnectionId++;
    client->socket = socket;
    diagnose("ACCEPT conn=" + std::to_string(client->id) + " peer=" + address);
    client->address = address;
    client->lastSeen = nowSec();
    client->minuteStart = client->lastSeen;
    if (!randomBytes(client->nonce, sNonceSize))
    {
        ARENA_CLOSESOCKET(socket);
        delete client;
        return;
    }
    mClients.push_back(client);

    // Protocol 2 waits for HELLO(name) so the challenge can carry that account's salt.

}

void LinkServer::serviceClient(Client& client)
{
    char buffer[4096];
    for (;;)
    {
        const auto received = ::recv(client.socket, buffer, sizeof(buffer), 0);
        if (received > 0)
        {
            diagnose("RECV conn=" + std::to_string(client.id) + " bytes=" + std::to_string(received));
            client.inBuffer.append(buffer, static_cast<std::size_t>(received));
            client.lastSeen = nowSec();
            if (client.inBuffer.size() > sMaxPayload * 4)
            {
                closeClient(client, "переполнение буфера");
                return;
            }
            continue;
        }
        if (received == 0)
        {
            closeClient(client, "клиент закрыл соединение");
            return;
        }
        if (!wouldBlock()) closeClient(client, "socket read failed");
        break;
    }

    if (client.socket < 0) return;
    for (;;)
    {
        FrameHeader header;
        std::string payload;
        std::size_t consumed = 0;
        bool fatal = false;
        if (!nextFrame(client.inBuffer.data(), client.inBuffer.size(), header, payload, consumed, fatal))
        {
            if (fatal)
                closeClient(client, "битый кадр");
            return;
        }
        client.inBuffer.erase(0, consumed);
        handleFrame(client, header, payload);
        if (client.socket < 0)
            return;
    }

}

void LinkServer::handleFrame(Client& client, const FrameHeader& header, const std::string& payload)
{
    diagnose("FRAME conn=" + std::to_string(client.id) + " type=" + std::to_string(header.type)
        + " bytes=" + std::to_string(payload.size()));
    Reader reader(payload);

    // До авторизации принимаем ровно два типа кадров: всё остальное —
    // повод закрыть соединение, а не молча проигнорировать.
    if (!client.authorized && header.type != TYPE_HELLO && header.type != TYPE_AUTH
        && header.type != TYPE_PING)
    {
        closeClient(client, "кадр до авторизации");
        return;
    }

    switch (header.type)
    {
        case TYPE_HELLO:
        {
            const std::uint16_t protocol = reader.u16();
            reader.u8();                          // вид клиента: ПК/Android
            reader.text(32);                      // version
            const std::string name = reader.text(sMaxNick);
            diagnose("HELLO conn=" + std::to_string(client.id) + " protocol=" + std::to_string(protocol)
                + " parsed=" + std::to_string(reader.ok()));
            if (!reader.ok() || protocol != sProtocol || client.helloDone || name.empty())
            {
                sendTo(client, makeAuthFail(FAIL_VERSION, "Версия лаунчера не совпадает с сервером"));
                closeClient(client, "версия протокола");
                return;
            }
            if (!throttleAuth(client.address))
            {
                diagnose("AUTH_RATE_LIMIT conn=" + std::to_string(client.id));
                sendTo(client, makeAuthFail(FAIL_RATE, "Слишком много попыток, подождите"));
                return;
            }
            client.helloDone = true;
            client.requestedName = name;
            client.accountFound = mCallbacks.findAccount(name, client.account);
            // Missing users receive a syntactically valid challenge too.
            const std::string salt = client.accountFound ? client.account.passwordSalt : std::string(64, '0');
            diagnose("CHALLENGE conn=" + std::to_string(client.id) + " mode=" + std::to_string(mConfig.authMode)
                + " account_found=" + std::to_string(client.accountFound));
            sendTo(client, makeChallenge(client.nonce, mConfig.authMode, salt));
            break;
        }
        case TYPE_AUTH:      handleAuth(client, reader); break;
        case TYPE_SEND:      handleSend(client, reader); break;
        case TYPE_HISTORY_REQ: handleHistory(client, reader); break;
        case TYPE_JOIN_CHANNEL:
        {
            const std::uint16_t channel = reader.u16();
            const auto found = std::find_if(mChannels.begin(), mChannels.end(),
                [channel](const Channel& c) { return c.id == channel; });
            if (found != mChannels.end())
            {
                client.channel = channel;
                // TODO: отдать последние mConfig.historyOnJoin сообщений канала.
            }
            break;
        }
        case TYPE_VOICE_TICKET_REQ: handleVoiceTicket(client, reader); break;
        case TYPE_PING:
        {
            Writer writer;
            writer.u32(reader.u32());
            sendTo(client, frame(TYPE_PONG, writer.data()));
            break;
        }
        default:
            break;
    }
}

void LinkServer::handleAuth(Client& client, Reader& reader)
{
    if (!client.helloDone || client.authorized || client.authAttempted)
    {
        closeClient(client, "unexpected AUTH");
        return;
    }
    client.authAttempted = true;
    const std::string name = reader.text(sMaxNick);
    const std::uint8_t mode = reader.u8();
    const std::string secret = reader.text(128);
    diagnose("AUTH_RECEIVED conn=" + std::to_string(client.id) + " mode=" + std::to_string(mode)
        + " parsed=" + std::to_string(reader.ok()));
    if (!reader.ok() || name != client.requestedName || mode != mConfig.authMode)
    {
        diagnose("AUTH_INVALID_FIELDS conn=" + std::to_string(client.id));
        closeClient(client, "некорректный AUTH");
        return;
    }

    LinkAccount account;
    if (!client.accountFound || !mCallbacks.findAccount(name, account)
        || account.passwordSha256 != client.account.passwordSha256
        || account.passwordSalt != client.account.passwordSalt)
    {
        diagnose("AUTH_FAIL conn=" + std::to_string(client.id) + " cause=ACCOUNT_MISSING_CHANGED_OR_UNREADABLE");
        sendTo(client, makeAuthFail(FAIL_BAD_CREDENTIALS, "Неверное имя или пароль"));
        return;
    }
    if (account.banned)
    {
        diagnose("AUTH_FAIL conn=" + std::to_string(client.id) + " cause=ACCOUNT_BANNED");
        sendTo(client, makeAuthFail(FAIL_BANNED, "Учётная запись заблокирована"));
        return;
    }

    bool verified = false;
    switch (mode)
    {
        case AUTH_PROOF:
        case AUTH_TES3MP_PROOF:
            verified = secret.size() == sProofSize && mCallbacks.verifyProof != nullptr
                && mCallbacks.verifyProof(account,
                    std::string(reinterpret_cast<const char*>(client.nonce), sNonceSize), secret);
            break;
        case AUTH_PLAIN:
            verified = mCallbacks.verifyPassword != nullptr
                && mCallbacks.verifyPassword(name, secret);
            break;
        case AUTH_CODE:
            verified = mCallbacks.verifyLinkCode != nullptr
                && mCallbacks.verifyLinkCode(name, secret);
            break;
        default:
            break;
    }

    if (!verified)
    {
        diagnose("AUTH_FAIL conn=" + std::to_string(client.id) + " cause=PROOF_MISMATCH");
        ++mStats.rejectedAuth;
        sendTo(client, makeAuthFail(FAIL_BAD_CREDENTIALS, "Неверный пароль"));
        return;
    }

    diagnose("AUTH_OK conn=" + std::to_string(client.id));
    mAuthAttempts.erase(client.address);
    client.authorized = true;
    client.userId = account.userId;
    client.name = account.name;
    client.level = account.level;
    client.color = account.color;
    client.channel = mChannels.empty() ? 0 : mChannels.front().id;

    AuthResult result;
    result.userId = account.userId;
    result.name = account.name;
    result.level = account.level;
    result.color = account.color;
    result.className = account.className;
    result.voicePort = 0;                 // TODO: VoiceServer::port(), 0 если голос выключен
    sendTo(client, makeAuthOk(result));
    sendTo(client, makeChannels(mChannels));
    // TODO: PRESENCE всем, история текущего канала этому клиенту.
}

void LinkServer::handleSend(Client& client, Reader& reader)
{
    LinkAccount currentAccount;
    if (!mCallbacks.findAccount(client.name, currentAccount) || currentAccount.banned
        || (mCallbacks.isAddressBanned && mCallbacks.isAddressBanned(client.address))
        || currentAccount.passwordSha256 != client.account.passwordSha256)
    {
        closeClient(client, "account revoked");
        return;
    }
    const std::uint16_t channelId = reader.u16();
    const std::string text = reader.text16(sMaxText);
    if (!reader.ok() || text.empty())
        return;

    if (!throttleSend(client))
    {
        ++mStats.rejectedRate;
        sendTo(client, makeNotice(1, "Слишком часто. Подождите немного."));
        return;
    }

    const auto found = std::find_if(mChannels.begin(), mChannels.end(),
        [channelId](const Channel& c) { return c.id == channelId; });
    if (found == mChannels.end() || (found->flags & CHANNEL_WRITABLE) == 0)
    {
        sendTo(client, makeNotice(1, "В этот канал писать нельзя"));
        return;
    }
    // TODO: проверка админского канала и серверного mute — та же, что в игре.

    Message message;
    message.id = mNextMessageId++;
    message.timestamp = static_cast<std::uint32_t>(nowSec());
    message.channel = channelId;
    message.userId = client.userId;
    message.author = client.name;
    // Уровень и цвет пишутся снимком: история не перекрашивается, когда
    // игрок сменил цвет через /chatcolor или взял уровень.
    message.level = client.level;
    message.color = client.color;
    message.text = text;

    mHistory[channelId].push_back(message);
    while (mHistory[channelId].size() > mConfig.historyPerChannel)
        mHistory[channelId].pop_front();
    appendHistory(message);
    ++mStats.messages;

    broadcast(channelId, makeMessage(message));

    if ((found->flags & CHANNEL_MIRRORS_GAME) != 0 && mConfig.mirrorGameChat)
    {
        PendingGameChat pending;
        pending.author = client.name;
        pending.userId = client.userId;
        pending.level = client.level;
        pending.color = client.color;
        pending.text = text;
        // threadMain already owns mMutex while handleSend() runs.
        mPendingGameChat.push_back(std::move(pending));
        while (mPendingGameChat.size() > 256)
            mPendingGameChat.pop_front();
    }
}

void LinkServer::handleHistory(Client& client, Reader& reader)
{
    const std::uint16_t channelId = reader.u16();
    const std::uint64_t beforeId = reader.u64();
    const std::uint16_t limit = reader.u16();
    if (!reader.ok())
        return;

    const auto found = mHistory.find(channelId);
    if (found == mHistory.end())
        return;

    std::vector<Message> batch;
    const unsigned wanted = std::min<unsigned>(limit == 0 ? mConfig.historyOnJoin : limit, 100);
    for (auto it = found->second.rbegin(); it != found->second.rend(); ++it)
    {
        if (beforeId != 0 && it->id >= beforeId)
            continue;
        batch.push_back(*it);
        if (batch.size() >= wanted)
            break;
    }
    std::reverse(batch.begin(), batch.end());

    // Кадр ограничен sMaxPayload, а сообщение может быть на 4000 символов,
    // поэтому отдаём порциями, а не одним куском.
    std::vector<Message> chunk;
    std::size_t chunkBytes = 0;
    for (const Message& message : batch)
    {
        const std::size_t size = message.text.size() + message.author.size() + 32;
        if (chunkBytes + size > sMaxPayload - 64 || chunk.size() >= 255)
        {
            sendTo(client, makeMessages(channelId, chunk));
            chunk.clear();
            chunkBytes = 0;
        }
        chunk.push_back(message);
        chunkBytes += size;
    }
    if (!chunk.empty())
        sendTo(client, makeMessages(channelId, chunk));
}

void LinkServer::handleVoiceTicket(Client& client, Reader& reader)
{
    const std::uint8_t scope = reader.u8();
    if (!reader.ok() || mCallbacks.issueVoiceTicket == nullptr)
        return;

    std::uint8_t ticket[sTicketSize] = {};
    std::uint16_t port = 0;
    if (!mCallbacks.issueVoiceTicket(client.userId, scope, ticket, port))
    {
        sendTo(client, makeNotice(1, "Голосовой сервер недоступен"));
        return;
    }
    sendTo(client, makeVoiceTicket(ticket, port, 120));
}

void LinkServer::publishFromGame(const std::string& author, std::uint32_t userId,
    std::uint16_t level, std::uint32_t color, const std::string& text)
{
    std::lock_guard<std::mutex> lock(mMutex);
    for (const Channel& channel : mChannels)
    {
        if ((channel.flags & CHANNEL_MIRRORS_GAME) == 0)
            continue;

        Message message;
        message.id = mNextMessageId++;
        message.timestamp = static_cast<std::uint32_t>(nowSec());
        message.channel = channel.id;
        message.userId = userId;
        message.author = author;
        message.level = level;
        message.color = color;
        message.flags = MESSAGE_FROM_GAME;
        message.text = text;

        mHistory[channel.id].push_back(message);
        while (mHistory[channel.id].size() > mConfig.historyPerChannel)
            mHistory[channel.id].pop_front();
        appendHistory(message);
        broadcast(channel.id, makeMessage(message));
    }
}

void LinkServer::publishFromGame(const std::string& author, const std::string& text)
{
    LinkAccount account;
    if (mCallbacks.findAccount == nullptr || !mCallbacks.findAccount(author, account))
        return;
    publishFromGame(account.name.empty() ? author : account.name, account.userId,
        account.level, account.color, text);
}

bool LinkServer::popPendingGameChat(PendingGameChat& message)
{
    std::lock_guard<std::mutex> lock(mMutex);
    if (mPendingGameChat.empty())
        return false;
    message = std::move(mPendingGameChat.front());
    mPendingGameChat.pop_front();
    return true;
}

void LinkServer::publishPresence(const LinkAccount& account, bool online)
{
    std::lock_guard<std::mutex> lock(mMutex);
    Presence presence;
    presence.userId = account.userId;
    presence.name = account.name;
    presence.level = account.level;
    presence.color = account.color;
    presence.flags = static_cast<std::uint8_t>(online ? (PRESENCE_ONLINE | PRESENCE_INGAME) : 0);

    // Уровень и цвет живого игрока обновляются здесь же: лаунчер
    // перекрашивает ник, не переспрашивая.
    for (Client* client : mClients)
    {
        if (!client->authorized)
            continue;
        if (client->userId == account.userId)
        {
            client->level = account.level;
            client->color = account.color;
        }
        sendTo(*client, frame(TYPE_PRESENCE_DELTA, makePresence({presence}).substr(sHeaderSize)));
    }
}

void LinkServer::disconnectUser(std::uint32_t userId, const std::string& reason)
{
    std::lock_guard<std::mutex> lock(mMutex);
    for (Client* client : mClients)
    {
        if (client->userId != userId)
            continue;
        sendTo(*client, makeNotice(2, reason));
        closeClient(*client, reason);
    }
}

void LinkServer::flushOutput(Client& client)
{
    while (client.socket >= 0 && !client.outBuffer.empty())
    {
        const int count = static_cast<int>(client.outBuffer.size());
        const auto sent = ::send(client.socket, client.outBuffer.data(), count,
#ifdef MSG_NOSIGNAL
            MSG_NOSIGNAL
#else
            0
#endif
        );
        if (sent > 0)
        {
            diagnose("SENT conn=" + std::to_string(client.id) + " bytes=" + std::to_string(sent));
            client.outBuffer.erase(0, static_cast<std::size_t>(sent));
        }
        else
        {
            if (sent == 0 || !wouldBlock()) closeClient(client, "socket write failed");
            return;
        }
    }
}

void LinkServer::sendTo(Client& client, const std::string& data)
{
    if (client.socket < 0) return;
    if (client.outBuffer.size() + data.size() > sMaxPayload * 64)
    {
        closeClient(client, "slow consumer");
        return;
    }
    client.outBuffer += data;
    flushOutput(client);
}

void LinkServer::broadcast(std::uint16_t channel, const std::string& data)
{
    for (Client* client : mClients)
    {
        // Сообщение уходит всем авторизованным, а не только тем, кто
        // сейчас смотрит канал: иначе непрочитанное не посчитать.
        if (client->authorized)
            sendTo(*client, data);
    }
    (void)channel;
}

void LinkServer::closeClient(Client& client, const std::string&)
{
    diagnose("CLOSE conn=" + std::to_string(client.id) + " authorized=" + std::to_string(client.authorized)
        + " hello=" + std::to_string(client.helloDone) + " buffered=" + std::to_string(client.inBuffer.size())
        + " pending=" + std::to_string(client.outBuffer.size()));
    if (client.socket >= 0)
        ARENA_CLOSESOCKET(client.socket);
    client.socket = -1;
    client.authorized = false;
}

void LinkServer::loadHistory()
{
    if (mConfig.storageDir.empty())
        return;
    // TODO: прочитать хвост server/data/chat/<channel>.jsonl (последние
    // historyPerChannel строк) и восстановить mNextMessageId.
}

void LinkServer::appendHistory(const Message& message)
{
    if (mConfig.storageDir.empty())
        return;
    std::ofstream file(mConfig.storageDir + "/" + std::to_string(message.channel) + ".jsonl",
        std::ios::app | std::ios::binary);
    if (!file)
        return;
    // TODO: экранирование кавычек и управляющих символов в author/text.
    file << "{\"id\":" << message.id
         << ",\"ts\":" << message.timestamp
         << ",\"uid\":" << message.userId
         << ",\"lvl\":" << message.level
         << ",\"color\":" << message.color
         << ",\"flags\":" << static_cast<unsigned>(message.flags)
         << ",\"name\":\"" << message.author
         << "\",\"text\":\"" << message.text << "\"}\n";
}

bool LinkServer::throttleAuth(const std::string& address)
{
    const std::uint64_t now = nowSec();
    for (auto it = mAuthAttempts.begin(); it != mAuthAttempts.end();)
        if (it->second.second <= now) it = mAuthAttempts.erase(it);
        else ++it;
    auto& entry = mAuthAttempts[address];
    if (entry.second == 0) entry.second = now + mConfig.authBlockSeconds;
    if (entry.first >= mConfig.authTriesBeforeBlock) return false;
    ++entry.first;
    return true;
}

bool LinkServer::throttleSend(Client& client)
{
    const std::uint64_t now = nowSec();
    if (now - client.minuteStart >= 60)
    {
        client.minuteStart = now;
        client.sentThisMinute = 0;
    }
    return ++client.sentThisMinute <= mConfig.sendRatePerMinute;
}
