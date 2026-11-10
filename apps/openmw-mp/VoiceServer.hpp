#ifndef ARENAMP_VOICESERVER_HPP
#define ARENAMP_VOICESERVER_HPP

// ArenaMP U025 — голосовой сервер. Отдельный UDP-сокет и отдельный поток
// внутри процесса tes3mp-server: позиции игроков уже здесь, дублировать
// состояние мира в отдельном демоне незачем.
//
// Сервер НЕ декодирует Opus в режиме mode = client: он решает, кто кого
// слышит, считает усиление и азимут и пересылает исходные байты. Режим
// mode = server (микс на сервере) добавляется в U025g и живёт за тем же
// интерфейсом.

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <components/openmw-mp/arenavoice.hpp>

namespace mwmp
{
    struct VoiceConfig
    {
        bool enabled = true;
        unsigned short port = 0;              // 0 = игровой порт + 1
        bool serverMix = false;
        float sayRangeMeters = 20.f;
        float whisperRangeMeters = 5.f;
        float shoutRangeMeters = 50.f;
        float minRangeMeters = 2.f;
        float rolloff = ArenaVoice::sDefaultRolloff;
        float interiorFactor = 0.8f;
        bool requireSameCell = true;
        bool interiorIsolation = true;
        unsigned maxAudibleSpeakers = 6;
        unsigned maxBitrateKbps = 24;
        unsigned ticketTtlSeconds = 120;
        bool groupRadio = true;
    };

    /// Снимок игрока на текущем тике. Заполняется из Players::getPlayers()
    /// один раз за тик, чтобы голосовой поток не держал игровые структуры.
    struct VoiceSnapshotEntry
    {
        std::uint32_t ssrc = 0;
        std::string name;
        float x = 0.f, y = 0.f, z = 0.f;      // юниты
        float yaw = 0.f;                      // радианы
        std::uint32_t cell = 0;
        bool interior = false;
        bool anonymous = false;               // невидимость/хамелеон
        int groupId = -1;
        bool muted = false;                   // серверный mute
    };

    class VoiceServer
    {
    public:
        VoiceServer();
        ~VoiceServer();

        void configure(const VoiceConfig& config) { mConfig = config; }
        const VoiceConfig& config() const { return mConfig; }

        /// gamePort нужен, чтобы вычислить порт по умолчанию (game + 1).
        bool start(unsigned short gamePort);
        void stop();
        bool running() const { return mRunning.load(); }
        unsigned short port() const { return mPort; }

        /// Игровой поток: выдать тикет вошедшему игроку. Тикет уходит клиенту
        /// обычным игровым пакетом, клиент передаёт его коммутатору по мосту.
        std::string issueTicket(std::uint32_t ssrc, const std::string& name);

        /// Игровой поток: заменить снимок мира. Копия, не ссылка.
        void publishSnapshot(std::vector<VoiceSnapshotEntry> snapshot);

        /// Модерация: немедленно действует и на proximity, и на группу.
        void setMuted(std::uint32_t ssrc, bool muted);
        void kick(std::uint32_t ssrc);

        struct Stats
        {
            unsigned sessions = 0;
            unsigned speaking = 0;
            unsigned long long packetsIn = 0;
            unsigned long long packetsOut = 0;
            unsigned long long droppedRate = 0;
            unsigned long long droppedBadTicket = 0;
        };
        Stats stats() const;

    private:
        struct Session;          // адрес, ssrc, seq, лимиты, последнее STATE
        struct Ticket;

        void threadMain();                       // приём + тик 20 мс
        void handleDatagram(const char* data, std::size_t size, const void* fromAddr, std::size_t addrSize);
        void handleHello(const ArenaVoice::Header& header, ArenaVoice::Reader& reader,
            const void* fromAddr, std::size_t addrSize);
        void handleAudioUp(Session& session, ArenaVoice::Reader& reader);
        void handleState(Session& session, ArenaVoice::Reader& reader);

        /// Ядро маршрутизации: для одного говорящего разослать AUDIO_DOWN всем,
        /// кто его слышит, с посчитанными gain/azimuth/distance.
        void routeFrame(const Session& speaker, const ArenaVoice::AudioUp& audio);

        /// Выбрать до maxAudibleSpeakers ближайших, если говорящих больше.
        void trimAudible(std::vector<std::pair<float, std::uint32_t>>& candidates) const;

        void expireSessions(std::uint64_t nowMs);
        void sendTo(const Session& session, const std::string& packet);

        VoiceConfig mConfig;
        std::atomic<bool> mRunning{false};
        std::thread mThread;
        int mSocket = -1;
        unsigned short mPort = 0;

        mutable std::mutex mMutex;               // защищает снимок, тикеты, сессии
        std::vector<VoiceSnapshotEntry> mSnapshot;
        std::map<std::uint32_t, Ticket> mTickets;        // ssrc -> тикет
        std::map<std::uint32_t, Session> mSessions;      // session id -> сессия
        Stats mStats;
    };
}
#endif
