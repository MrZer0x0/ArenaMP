#ifndef ARENAMP_LINKSERVER_HPP
#define ARENAMP_LINKSERVER_HPP

// ArenaMP U025 — ArenaLink: чат лаунчера прямо в процессе tes3mp-server.
//
// Отдельный TCP-порт и обычные сокеты (select/poll). RakNet не используется:
// игровой протокол 806 не меняется, чат не занимает игровой слот и не может
// уронить игровой цикл. Внешней БД и веб-обвязки нет — каналы, история и
// присутствие живут здесь же.
//
// Хранилище намеренно простое: кольцо последних сообщений в памяти плюс
// дописываемый JSONL-файл на канал. Никаких новых зависимостей; при старте
// читается хвост файла.

#include <atomic>
#include <functional>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <components/openmw-mp/arenalink.hpp>

namespace mwmp
{
    struct LinkConfig
    {
        bool enabled = true;
        unsigned short port = 0;                 // 0 = игровой порт + 2
        std::string bindAddress = "0.0.0.0";
        std::uint8_t authMode = ArenaLink::AUTH_PROOF;
        unsigned historyPerChannel = 500;        // сколько держать в памяти
        unsigned historyOnJoin = 50;             // сколько отдавать при входе
        unsigned maxConnections = 128;
        unsigned maxConnectionsPerAddress = 4;
        unsigned sendRatePerMinute = 30;
        unsigned authTriesBeforeBlock = 5;
        unsigned authBlockSeconds = 300;
        bool mirrorGameChat = true;                 // channel 1 <-> in-game ///
        std::string storageDir;                  // server/data/chat
    };

    /// Учётка, как её видит ArenaLink. Заполняет скриптовый слой сервера:
    /// у ArenaLink нет собственной базы игроков, он спрашивает ту же,
    /// через которую игрок входит в игру.
    struct LinkAccount
    {
        std::uint32_t userId = 0;
        std::string name;
        std::uint16_t level = 0;
        std::uint32_t color = 0xC8C8C8u;
        std::string className;
        bool banned = false;
        bool online = false;                     // сейчас в мире
        /// Для AUTH_PROOF: sha256(пароль) из хранилища сервера.
        /// Пустая строка — режим proof для этой учётки недоступен.
        std::string passwordSha256;
        std::string passwordSalt;
    };

    /// Колбэки в остальной сервер. Реализуются там, где живёт существующая
    /// логика входа: подменять или дублировать её ArenaLink не должен.
    struct LinkCallbacks
    {
        std::function<void(const std::string&)> diagnostic;
        /// Найти учётку по имени. false — персонажа нет (его создают только
        /// в игре), лаунчер покажет «зайдите на сервер и создайте персонажа».
        std::function<bool(const std::string&)> isAddressBanned;
        std::function<bool(const std::string&, LinkAccount&)> findAccount;
        /// Запасной путь, когда пароли в bcrypt и proof не посчитать.
        bool (*verifyPassword)(const std::string& name, const std::string& password) = nullptr;
        /// Код из внутриигровой команды /chatlink.
        bool (*verifyLinkCode)(const std::string& name, const std::string& code) = nullptr;
        /// Verify HMAC-SHA256 with the actual account verifier, in constant time.
        /// Must be thread-safe; never call Lua directly from the network thread.
        bool (*verifyProof)(const LinkAccount& account, const std::string& nonce,
            const std::string& proof) = nullptr;
        /// Отдать сообщение во внутриигровой чат (каналы с CHANNEL_MIRRORS_GAME).
        void (*pushToGameChat)(const std::string& author, const std::string& text) = nullptr;
        /// Выдать голосовой тикет: реализуется через VoiceServer::issueTicket.
        bool (*issueVoiceTicket)(std::uint32_t userId, std::uint8_t scope,
            std::uint8_t ticketOut[ArenaLink::sTicketSize], std::uint16_t& portOut) = nullptr;
    };

    class LinkServer
    {
    public:
        LinkServer();
        ~LinkServer();

        void configure(const LinkConfig& config, const LinkCallbacks& callbacks);
        bool start(unsigned short gamePort);
        void stop();
        bool running() const { return mRunning.load(); }
        unsigned short port() const { return mPort; }

        /// Из игрового потока: сообщение из внутриигрового чата уходит
        /// в зеркалящие каналы лаунчера.
        void publishFromGame(const std::string& author, std::uint32_t userId,
            std::uint16_t level, std::uint32_t color, const std::string& text);
        void publishFromGame(const std::string& author, const std::string& text);

        struct PendingGameChat
        {
            std::string author;
            std::uint32_t userId = 0;
            std::uint16_t level = 0;
            std::uint32_t color = 0xC8C8C8u;
            std::string text;
        };

        /// The TCP thread only queues mirrored launcher messages. The server
        /// main thread drains them and calls Lua, so Lua is never touched from
        /// LinkServer::threadMain().
        bool popPendingGameChat(PendingGameChat& message);

        /// Из игрового потока: игрок вошёл в мир / вышел / взял уровень /
        /// сменил цвет через /chatcolor. Рассылается как PRESENCE_DELTA,
        /// и лаунчер сразу перекрашивает ник.
        void publishPresence(const LinkAccount& account, bool online);

        /// Модерация: бан или mute из игры действует и в чате лаунчера.
        void disconnectUser(std::uint32_t userId, const std::string& reason);

        struct Stats
        {
            unsigned connections = 0;
            unsigned authorized = 0;
            unsigned long long messages = 0;
            unsigned long long rejectedAuth = 0;
            unsigned long long rejectedRate = 0;
        };
        Stats stats() const;

    private:
        struct Client;

        void diagnose(const std::string& event) const;
        void threadMain();
        void acceptPending();
        void serviceClient(Client& client);
        void handleFrame(Client& client, const ArenaLink::FrameHeader& header, const std::string& payload);
        void handleAuth(Client& client, ArenaLink::Reader& reader);
        void handleSend(Client& client, ArenaLink::Reader& reader);
        void handleHistory(Client& client, ArenaLink::Reader& reader);
        void handleVoiceTicket(Client& client, ArenaLink::Reader& reader);

        void flushOutput(Client& client);
        void sendTo(Client& client, const std::string& frame);
        void broadcast(std::uint16_t channel, const std::string& frame);
        void closeClient(Client& client, const std::string& reason);

        void loadHistory();
        void appendHistory(const ArenaLink::Message& message);

        bool throttleAuth(const std::string& address);
        bool throttleSend(Client& client);

        LinkConfig mConfig;
        LinkCallbacks mCallbacks{};
        std::atomic<bool> mRunning{false};
        std::thread mThread;
        std::intptr_t mListenSocket = -1;
        unsigned short mPort = 0;

        mutable std::mutex mMutex;
        std::vector<Client*> mClients;
        std::vector<ArenaLink::Channel> mChannels;
        std::map<std::uint16_t, std::deque<ArenaLink::Message>> mHistory;
        std::deque<PendingGameChat> mPendingGameChat;
        std::map<std::string, std::pair<unsigned, std::uint64_t>> mAuthAttempts;
        std::uint64_t mNextMessageId = 1;
        Stats mStats;
        std::uint64_t mNextConnectionId = 1;
    };
}
#endif
