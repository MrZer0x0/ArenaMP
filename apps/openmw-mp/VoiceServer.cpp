// ArenaMP U025 — скелет голосового сервера.
//
// Реализовано по-настоящему: маршрутизация, слышимость, спатиальные
// коэффициенты, лимиты. Помечено TODO то, что зависит от конкретных
// структур дерева (Players, Cell, группы) и от выбранной библиотеки сокетов.

#include "VoiceServer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <random>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
   using socklen_t = int;
#  define ARENA_CLOSESOCKET closesocket
#else
#  include <arpa/inet.h>
#  include <fcntl.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
#  define ARENA_CLOSESOCKET ::close
#endif

using namespace mwmp;
using namespace ArenaVoice;

namespace
{
    std::uint64_t nowMs()
    {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    std::string randomBytes(std::size_t size)
    {
        static thread_local std::mt19937_64 engine{std::random_device{}()};
        std::string out(size, '\0');
        for (std::size_t i = 0; i < size; ++i)
            out[i] = static_cast<char>(engine() & 0xFF);
        return out;
    }
}

struct VoiceServer::Ticket
{
    std::string bytes;
    std::string name;
    std::uint64_t expiresMs = 0;
};

struct VoiceServer::Session
{
    std::uint32_t id = 0;
    std::uint32_t ssrc = 0;
    std::string name;
    sockaddr_storage address{};
    socklen_t addressSize = 0;
    std::uint16_t caps = 0;
    std::uint64_t lastSeenMs = 0;
    std::uint64_t lastAudioMs = 0;
    std::uint16_t outSeq = 0;
    State state;                      // последнее STATE от клиента
    unsigned framesThisSecond = 0;
    std::uint64_t secondStartMs = 0;
    bool forceMuted = false;
};

VoiceServer::VoiceServer() = default;

VoiceServer::~VoiceServer()
{
    stop();
}

bool VoiceServer::start(unsigned short gamePort)
{
    if (!mConfig.enabled || mRunning.load())
        return false;

    mPort = mConfig.port != 0 ? mConfig.port : static_cast<unsigned short>(gamePort + 1);

    mSocket = static_cast<int>(::socket(AF_INET, SOCK_DGRAM, 0));
    if (mSocket < 0)
        return false;   // TODO: LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, ...)

    sockaddr_in bindAddress{};
    bindAddress.sin_family = AF_INET;
    bindAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    bindAddress.sin_port = htons(mPort);
    if (::bind(mSocket, reinterpret_cast<sockaddr*>(&bindAddress), sizeof(bindAddress)) != 0)
    {
        ARENA_CLOSESOCKET(mSocket);
        mSocket = -1;
        return false;
    }

    mRunning.store(true);
    mThread = std::thread(&VoiceServer::threadMain, this);
    return true;
}

void VoiceServer::stop()
{
    if (!mRunning.exchange(false))
        return;
    if (mSocket >= 0)
    {
        ARENA_CLOSESOCKET(mSocket);
        mSocket = -1;
    }
    if (mThread.joinable())
        mThread.join();
}

std::string VoiceServer::issueTicket(std::uint32_t ssrc, const std::string& name)
{
    Ticket ticket;
    ticket.bytes = randomBytes(sTicketSize);
    ticket.name = name;
    ticket.expiresMs = nowMs() + static_cast<std::uint64_t>(mConfig.ticketTtlSeconds) * 1000ull;

    std::lock_guard<std::mutex> lock(mMutex);
    mTickets[ssrc] = ticket;
    return ticket.bytes;
}

void VoiceServer::publishSnapshot(std::vector<VoiceSnapshotEntry> snapshot)
{
    std::lock_guard<std::mutex> lock(mMutex);
    mSnapshot = std::move(snapshot);
}

void VoiceServer::setMuted(std::uint32_t ssrc, bool muted)
{
    std::lock_guard<std::mutex> lock(mMutex);
    for (auto& entry : mSessions)
    {
        if (entry.second.ssrc != ssrc)
            continue;
        entry.second.forceMuted = muted;
        sendTo(entry.second, makeControl(entry.first, CONTROL_MUTE, muted ? 1u : 0u));
    }
}

void VoiceServer::kick(std::uint32_t ssrc)
{
    std::lock_guard<std::mutex> lock(mMutex);
    for (auto it = mSessions.begin(); it != mSessions.end();)
    {
        if (it->second.ssrc != ssrc) { ++it; continue; }
        sendTo(it->second, makeControl(it->first, CONTROL_KICK, 0));
        it = mSessions.erase(it);
    }
}

VoiceServer::Stats VoiceServer::stats() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    Stats copy = mStats;
    copy.sessions = static_cast<unsigned>(mSessions.size());
    return copy;
}

void VoiceServer::threadMain()
{
    // TODO: приём с таймаутом (select/poll) + тик 20 мс для PEERS и протухания.
    // Голосовой поток не должен брать блокировку игровых структур: он работает
    // только со снимком, который публикует игровой цикл.
    std::vector<char> buffer(sMaxDatagram);
    std::uint64_t lastTick = nowMs();

    while (mRunning.load())
    {
        sockaddr_storage from{};
        socklen_t fromSize = sizeof(from);
        const auto received = ::recvfrom(mSocket, buffer.data(), static_cast<int>(buffer.size()), 0,
            reinterpret_cast<sockaddr*>(&from), &fromSize);
        if (received > 0)
            handleDatagram(buffer.data(), static_cast<std::size_t>(received), &from, fromSize);

        const std::uint64_t now = nowMs();
        if (now - lastTick >= static_cast<std::uint64_t>(sFrameMs))
        {
            lastTick = now;
            expireSessions(now);
            // TODO: разослать PEERS (кто сейчас говорит и с какой громкостью),
            // движок рисует по ним индикатор в HUD.
        }
    }
}

void VoiceServer::handleDatagram(const char* data, std::size_t size, const void* fromAddr, std::size_t addrSize)
{
    Reader reader(data, size);
    Header header;
    if (!readHeader(reader, header))
        return;                                   // мусор — молча выбрасываем

    if (header.type == TYPE_HELLO)
    {
        handleHello(header, reader, fromAddr, addrSize);
        return;
    }

    std::lock_guard<std::mutex> lock(mMutex);
    auto it = mSessions.find(header.session);
    if (it == mSessions.end())
        return;

    // Смена адреса без нового HELLO запрещена: телефон, переехавший
    // с Wi-Fi на LTE, обязан перездороваться тем же тикетом.
    if (std::memcmp(&it->second.address, fromAddr, addrSize) != 0)
        return;

    it->second.lastSeenMs = nowMs();
    ++mStats.packetsIn;

    switch (header.type)
    {
        case TYPE_AUDIO_UP: handleAudioUp(it->second, reader); break;
        case TYPE_STATE:    handleState(it->second, reader); break;
        case TYPE_PING:
        {
            const std::uint32_t timestamp = reader.u32();
            Writer writer;
            writeHeader(writer, TYPE_PONG, it->first);
            writer.u32(timestamp);
            sendTo(it->second, writer.data());
            break;
        }
        case TYPE_BYE: mSessions.erase(it); break;
        default: break;
    }
}

void VoiceServer::handleHello(const Header&, Reader& reader, const void* fromAddr, std::size_t addrSize)
{
    Hello hello;
    if (!parseHello(reader, hello))
        return;

    std::lock_guard<std::mutex> lock(mMutex);

    if (hello.protocol != sProtocol)
    {
        // TODO: ответить REJECT_VERSION на адрес отправителя.
        return;
    }

    const std::string ticket(reinterpret_cast<const char*>(hello.ticket), sTicketSize);
    const std::uint64_t now = nowMs();

    std::uint32_t ssrc = 0;
    std::string name;
    for (auto it = mTickets.begin(); it != mTickets.end();)
    {
        if (it->second.expiresMs < now) { it = mTickets.erase(it); continue; }
        if (it->second.bytes == ticket) { ssrc = it->first; name = it->second.name; }
        ++it;
    }

    if (ssrc == 0)
    {
        ++mStats.droppedBadTicket;
        // TODO: ответить REJECT_BAD_TICKET, ограничить 4 HELLO/мин с адреса.
        return;
    }

    mTickets.erase(ssrc);          // тикет одноразовый

    Session session;
    session.id = static_cast<std::uint32_t>(std::hash<std::string>{}(ticket) & 0x7FFFFFFFu) | 1u;
    session.ssrc = ssrc;
    session.name = name;
    std::memcpy(&session.address, fromAddr, addrSize);
    session.addressSize = static_cast<socklen_t>(addrSize);
    session.caps = hello.caps;
    session.lastSeenMs = now;
    session.secondStartMs = now;

    const bool serverMix = mConfig.serverMix || (hello.caps & CAP_CLIENT_MIX) == 0;

    Welcome welcome;
    welcome.ssrc = ssrc;
    welcome.mode = serverMix ? 1 : 0;
    welcome.maxSpeakers = static_cast<std::uint8_t>(mConfig.maxAudibleSpeakers);
    welcome.rangeUnits = static_cast<std::uint32_t>(metersToUnits(mConfig.shoutRangeMeters));

    sendTo(session, makeWelcome(session.id, welcome));
    mSessions[session.id] = session;
}

void VoiceServer::handleState(Session& session, Reader& reader)
{
    State state;
    if (!parseState(reader, state))
        return;
    // Позиция из STATE НЕ используется для маршрутизации — только поворот
    // головы для интерполяции. Координаты берутся из снимка игрового мира,
    // иначе клиент смог бы «телепортировать» свой голос.
    session.state.yaw = state.yaw;
    session.state.pitch = state.pitch;
    session.state.channel = state.channel;
    session.state.ptt = state.ptt;
    session.state.muted = state.muted;
}

void VoiceServer::handleAudioUp(Session& session, Reader& reader)
{
    AudioUp audio;
    if (!parseAudioUp(reader, audio))
        return;

    const std::uint64_t now = nowMs();
    if (now - session.secondStartMs >= 1000)
    {
        session.secondStartMs = now;
        session.framesThisSecond = 0;
    }
    if (++session.framesThisSecond > 60)         // 50 кадров/с + запас
    {
        ++mStats.droppedRate;
        return;
    }
    if (session.forceMuted || session.state.muted)
        return;

    session.lastAudioMs = now;
    routeFrame(session, audio);
}

void VoiceServer::routeFrame(const Session& speaker, const AudioUp& audio)
{
    // mMutex уже взят вызывающим.
    const VoiceSnapshotEntry* source = nullptr;
    for (const auto& entry : mSnapshot)
        if (entry.ssrc == speaker.ssrc) { source = &entry; break; }
    if (source == nullptr)
        return;                                   // игрок ещё не в мире

    const bool group = audio.channel == CHANNEL_GROUP;
    if (group && !mConfig.groupRadio)
        return;

    const float rangeMeters = channelRangeMeters(audio.channel,
        mConfig.sayRangeMeters, mConfig.whisperRangeMeters, mConfig.shoutRangeMeters);
    const float minUnits = metersToUnits(mConfig.minRangeMeters);

    for (auto& target : mSessions)
    {
        Session& listener = target.second;
        if (listener.ssrc == speaker.ssrc)
            continue;

        const VoiceSnapshotEntry* ear = nullptr;
        for (const auto& entry : mSnapshot)
            if (entry.ssrc == listener.ssrc) { ear = &entry; break; }
        if (ear == nullptr)
            continue;

        AudioDown down;
        down.ssrc = speaker.ssrc;
        down.seq = audio.seq;
        down.timestamp = audio.timestamp;
        down.opus = audio.opus;
        down.flags = audio.flags;
        if (source->anonymous)
            down.flags |= AUDIO_ANON;

        if (group)
        {
            // Групповая рация: без дистанции и панорамы, ровная громкость.
            if (source->groupId < 0 || source->groupId != ear->groupId)
                continue;
            down.flags |= AUDIO_GROUP;
            down.gain = packGain(0.9f);
            down.azimuth = 0;
            down.distance = 0;
        }
        else
        {
            // Правила слышимости.
            const bool sameCell = source->cell == ear->cell;
            if (source->interior != ear->interior)
                continue;                          // интерьер ↔ экстерьер
            if (source->interior && mConfig.interiorIsolation && !sameCell)
                continue;                          // разные интерьеры
            if (!source->interior && mConfig.requireSameCell && !sameCell)
            {
                // Экстерьер: границы ячеек не должны резать звук, поэтому
                // requireSameCell для улицы игнорируется намеренно.
            }

            const float dx = source->x - ear->x;
            const float dy = source->y - ear->y;
            const float dz = source->z - ear->z;
            const float distanceUnits = std::sqrt(dx * dx + dy * dy + dz * dz);

            float maxUnits = metersToUnits(rangeMeters);
            if (source->interior)
                maxUnits *= mConfig.interiorFactor;

            const float gain = distanceGain(distanceUnits, minUnits, maxUnits, mConfig.rolloff);
            if (gain <= 0.004f)                    // тише 1/255 — не шлём вообще
                continue;

            const float listenerYaw = unpackAngle(listener.state.yaw) != 0.f
                ? unpackAngle(listener.state.yaw) : ear->yaw;

            down.gain = packGain(gain);
            down.azimuth = packAngle(azimuthRadians(dx, dy, listenerYaw));
            down.distance = packDistance(unitsToMeters(distanceUnits));
        }

        // TODO (U025g): в режиме serverMix вместо AUDIO_DOWN копить кадры
        // в микшер слушателя и раз в тик отправлять один MIXDOWN.
        sendTo(listener, makeAudioDown(target.first, down));
        ++mStats.packetsOut;
    }
}

void VoiceServer::trimAudible(std::vector<std::pair<float, std::uint32_t>>& candidates) const
{
    if (candidates.size() <= mConfig.maxAudibleSpeakers)
        return;
    std::partial_sort(candidates.begin(), candidates.begin() + mConfig.maxAudibleSpeakers,
        candidates.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    candidates.resize(mConfig.maxAudibleSpeakers);
}

void VoiceServer::expireSessions(std::uint64_t now)
{
    std::lock_guard<std::mutex> lock(mMutex);
    for (auto it = mSessions.begin(); it != mSessions.end();)
    {
        if (now - it->second.lastSeenMs > 15000)
            it = mSessions.erase(it);
        else
            ++it;
    }
    for (auto it = mTickets.begin(); it != mTickets.end();)
        it = it->second.expiresMs < now ? mTickets.erase(it) : std::next(it);
}

void VoiceServer::sendTo(const Session& session, const std::string& packet)
{
    if (mSocket < 0 || packet.empty() || packet.size() > sMaxDatagram)
        return;
    ::sendto(mSocket, packet.data(), static_cast<int>(packet.size()), 0,
        reinterpret_cast<const sockaddr*>(&session.address), session.addressSize);
}
