#ifndef ARENAMP_ARENAVOICE_HPP
#define ARENAMP_ARENAVOICE_HPP

// ArenaMP U025 — протокол позиционного голоса ArenaVoice (AVX1).
//
// Header-only и без зависимостей, как components/openmw-mp/serverstatus.hpp:
// один и тот же файл включают сервер (apps/openmw-mp), лаунчер-коммутатор
// (apps/launcher/voice) и движок (apps/openmw/mwmp). Здесь нет ни сокетов,
// ни Opus — только формат пакетов и спатиальная математика, чтобы обе стороны
// физически не могли разойтись в трактовке.
//
// Все целые — big-endian. Максимальный размер датаграммы 1200 байт.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <components/openmw-mp/arenastream.hpp>

namespace ArenaVoice
{
    inline constexpr std::uint32_t sMagic = 0x41565831u;   // 'AVX1'
    inline constexpr std::uint16_t sProtocol = 1;
    inline constexpr std::size_t sMaxDatagram = 1200;
    inline constexpr std::size_t sHeaderSize = 12;
    inline constexpr std::size_t sTicketSize = 16;
    inline constexpr int sFrameMs = 20;
    inline constexpr int sSampleRate = 48000;
    inline constexpr int sFrameSamples = sSampleRate / 1000 * sFrameMs;   // 960
    inline constexpr int sMaxNickLength = 32;

    // 64 юнита Морровинда = 1 ярд = 0.9144 м.
    inline constexpr float sUnitsPerMeter = 64.0f / 0.9144f;              // ~69.99

    inline constexpr float metersToUnits(float meters) { return meters * sUnitsPerMeter; }
    inline constexpr float unitsToMeters(float units) { return units / sUnitsPerMeter; }

    enum Type : std::uint8_t
    {
        TYPE_HELLO      = 0x01,
        TYPE_WELCOME    = 0x02,
        TYPE_REJECT     = 0x03,
        TYPE_BYE        = 0x04,
        TYPE_AUDIO_UP   = 0x10,
        TYPE_AUDIO_DOWN = 0x11,
        TYPE_MIXDOWN    = 0x12,
        TYPE_STATE      = 0x20,
        TYPE_PEERS      = 0x21,
        TYPE_PING       = 0x30,
        TYPE_PONG       = 0x31,
        TYPE_CONTROL    = 0x40,
    };

    enum Reject : std::uint8_t
    {
        REJECT_BAD_TICKET = 1,
        REJECT_BANNED     = 2,
        REJECT_FULL       = 3,
        REJECT_VERSION    = 4,
        REJECT_RATE       = 5,
    };

    enum Channel : std::uint8_t
    {
        // Совпадает с идентификаторами каналов coreChat (GUIChat.cpp: applyChatState).
        CHANNEL_GLOBAL_OOC = 1,
        CHANNEL_SAY        = 2,
        CHANNEL_WHISPER    = 3,
        CHANNEL_SHOUT      = 4,
        CHANNEL_LOCAL_OOC  = 5,
        CHANNEL_GROUP      = 6,   // групповая «рация», дистанция не учитывается
    };

    enum AudioFlag : std::uint8_t
    {
        AUDIO_EOS       = 1 << 0,   // последний пакет фразы, приёмник гасит источник
        AUDIO_ANON      = 1 << 1,   // говорящий скрыт (невидимость/хамелеон): ник не показывать
        AUDIO_GROUP     = 1 << 2,   // пришёл по групповому каналу, без затухания
        AUDIO_FEC       = 1 << 3,   // полезная нагрузка содержит inband FEC
    };

    enum Capability : std::uint16_t
    {
        CAP_CLIENT_MIX  = 1 << 0,   // клиент умеет сводить сам (AUDIO_DOWN)
        CAP_SERVER_MIX  = 1 << 1,   // клиент умеет принимать MIXDOWN
        CAP_HRTF        = 1 << 2,
    };

    enum ControlOp : std::uint8_t
    {
        CONTROL_MUTE        = 1,    // серверный принудительный mute, arg = 1/0
        CONTROL_KICK        = 2,
        CONTROL_RANGE       = 3,    // arg = новый радиус в юнитах
        CONTROL_MODE        = 4,    // arg = 0 client-mix, 1 server-mix
        CONTROL_TICKET_STALE= 5,    // запросить новый тикет через мост
    };

    // ── примитивы записи/чтения ────────────────────────────────────────
    // Общие с ArenaLink: components/openmw-mp/arenastream.hpp.
    using Writer = ArenaNet::Writer;
    using Reader = ArenaNet::Reader;

    // ── заголовок ────────────────────────────────────────────────────────

    struct Header
    {
        std::uint8_t type = 0;
        std::uint8_t flags = 0;
        std::uint32_t session = 0;
    };

    inline void writeHeader(Writer& writer, std::uint8_t type, std::uint32_t session, std::uint8_t flags = 0)
    {
        writer.u32(sMagic);
        writer.u8(type);
        writer.u8(flags);
        writer.u16(0);          // резерв, выравнивание до 12 байт
        writer.u32(session);
    }

    inline bool readHeader(Reader& reader, Header& out)
    {
        if (reader.u32() != sMagic) return false;
        out.type = reader.u8();
        out.flags = reader.u8();
        reader.u16();
        out.session = reader.u32();
        return reader.ok();
    }

    // ── полезные нагрузки ────────────────────────────────────────────────

    struct Hello
    {
        std::uint8_t ticket[sTicketSize] = {};
        std::uint16_t protocol = sProtocol;
        std::uint16_t caps = CAP_CLIENT_MIX;
        std::string nick;
    };

    struct Welcome
    {
        std::uint32_t ssrc = 0;
        std::uint16_t tickMs = sFrameMs;
        std::uint8_t mode = 0;            // 0 = client-mix, 1 = server-mix
        std::uint8_t maxSpeakers = 6;
        std::uint32_t rangeUnits = 0;     // подсказка клиенту для UI/предзагрузки
    };

    struct AudioUp
    {
        std::uint16_t seq = 0;
        std::uint32_t timestamp = 0;
        std::uint8_t channel = CHANNEL_SAY;
        std::uint8_t flags = 0;
        std::string opus;
    };

    struct AudioDown
    {
        std::uint32_t ssrc = 0;
        std::uint16_t seq = 0;
        std::uint32_t timestamp = 0;
        std::uint8_t gain = 255;          // 0..255 → 0..1
        std::int16_t azimuth = 0;         // 1/100 радиана, [-314..314]
        std::uint16_t distance = 0;       // дециметры, 0..6553.5 м
        std::uint8_t flags = 0;
        std::string opus;
    };

    struct State
    {
        std::int32_t x = 0, y = 0, z = 0;     // юниты
        std::int16_t yaw = 0;                 // 1/100 радиана
        std::int16_t pitch = 0;
        std::uint32_t cell = 0;               // хеш имени ячейки
        std::uint8_t channel = CHANNEL_SAY;
        std::uint8_t ptt = 0;
        std::uint8_t muted = 0;
    };

    struct PeerLevel
    {
        std::uint32_t ssrc = 0;
        std::uint8_t level = 0;               // 0..255, для индикатора «говорит»
        std::uint8_t flags = 0;
    };

    inline std::string makeHello(std::uint32_t session, const Hello& hello)
    {
        Writer writer;
        writeHeader(writer, TYPE_HELLO, session);
        writer.raw(hello.ticket, sTicketSize);
        writer.u16(hello.protocol);
        writer.u16(hello.caps);
        writer.text(hello.nick, sMaxNickLength);
        return writer.data();
    }

    inline bool parseHello(Reader& reader, Hello& out)
    {
        if (!reader.raw(out.ticket, sTicketSize)) return false;
        out.protocol = reader.u16();
        out.caps = reader.u16();
        out.nick = reader.text(sMaxNickLength);
        return reader.ok();
    }

    inline std::string makeWelcome(std::uint32_t session, const Welcome& welcome)
    {
        Writer writer;
        writeHeader(writer, TYPE_WELCOME, session);
        writer.u32(welcome.ssrc);
        writer.u16(welcome.tickMs);
        writer.u8(welcome.mode);
        writer.u8(welcome.maxSpeakers);
        writer.u32(welcome.rangeUnits);
        return writer.data();
    }

    inline bool parseWelcome(Reader& reader, Welcome& out)
    {
        out.ssrc = reader.u32();
        out.tickMs = reader.u16();
        out.mode = reader.u8();
        out.maxSpeakers = reader.u8();
        out.rangeUnits = reader.u32();
        return reader.ok();
    }

    inline std::string makeReject(std::uint32_t session, std::uint8_t reason)
    {
        Writer writer;
        writeHeader(writer, TYPE_REJECT, session);
        writer.u8(reason);
        return writer.data();
    }

    inline std::string makeAudioUp(std::uint32_t session, const AudioUp& audio)
    {
        Writer writer(sHeaderSize + 8 + audio.opus.size());
        writeHeader(writer, TYPE_AUDIO_UP, session);
        writer.u16(audio.seq);
        writer.u32(audio.timestamp);
        writer.u8(audio.channel);
        writer.u8(audio.flags);
        writer.raw(audio.opus.data(), audio.opus.size());
        return writer.data();
    }

    inline bool parseAudioUp(Reader& reader, AudioUp& out)
    {
        out.seq = reader.u16();
        out.timestamp = reader.u32();
        out.channel = reader.u8();
        out.flags = reader.u8();
        out.opus = reader.rest();
        return reader.ok() && !out.opus.empty();
    }

    inline std::string makeAudioDown(std::uint32_t session, const AudioDown& audio)
    {
        Writer writer(sHeaderSize + 16 + audio.opus.size());
        writeHeader(writer, TYPE_AUDIO_DOWN, session);
        writer.u32(audio.ssrc);
        writer.u16(audio.seq);
        writer.u32(audio.timestamp);
        writer.u8(audio.gain);
        writer.i16(audio.azimuth);
        writer.u16(audio.distance);
        writer.u8(audio.flags);
        writer.raw(audio.opus.data(), audio.opus.size());
        return writer.data();
    }

    inline bool parseAudioDown(Reader& reader, AudioDown& out)
    {
        out.ssrc = reader.u32();
        out.seq = reader.u16();
        out.timestamp = reader.u32();
        out.gain = reader.u8();
        out.azimuth = reader.i16();
        out.distance = reader.u16();
        out.flags = reader.u8();
        out.opus = reader.rest();
        return reader.ok() && !out.opus.empty();
    }

    inline std::string makeState(std::uint32_t session, const State& state)
    {
        Writer writer;
        writeHeader(writer, TYPE_STATE, session);
        writer.i32(state.x);
        writer.i32(state.y);
        writer.i32(state.z);
        writer.i16(state.yaw);
        writer.i16(state.pitch);
        writer.u32(state.cell);
        writer.u8(state.channel);
        writer.u8(state.ptt);
        writer.u8(state.muted);
        return writer.data();
    }

    inline bool parseState(Reader& reader, State& out)
    {
        out.x = reader.i32();
        out.y = reader.i32();
        out.z = reader.i32();
        out.yaw = reader.i16();
        out.pitch = reader.i16();
        out.cell = reader.u32();
        out.channel = reader.u8();
        out.ptt = reader.u8();
        out.muted = reader.u8();
        return reader.ok();
    }

    inline std::string makePeers(std::uint32_t session, const std::vector<PeerLevel>& peers)
    {
        Writer writer;
        writeHeader(writer, TYPE_PEERS, session);
        const std::size_t count = peers.size() < 255 ? peers.size() : 255;
        writer.u8(static_cast<std::uint8_t>(count));
        for (std::size_t i = 0; i < count; ++i)
        {
            writer.u32(peers[i].ssrc);
            writer.u8(peers[i].level);
            writer.u8(peers[i].flags);
        }
        return writer.data();
    }

    inline bool parsePeers(Reader& reader, std::vector<PeerLevel>& out)
    {
        out.clear();
        const std::size_t count = reader.u8();
        for (std::size_t i = 0; i < count && reader.ok(); ++i)
        {
            PeerLevel peer;
            peer.ssrc = reader.u32();
            peer.level = reader.u8();
            peer.flags = reader.u8();
            out.push_back(peer);
        }
        return reader.ok();
    }

    inline std::string makeControl(std::uint32_t session, std::uint8_t op, std::uint32_t arg)
    {
        Writer writer;
        writeHeader(writer, TYPE_CONTROL, session);
        writer.u8(op);
        writer.u32(arg);
        return writer.data();
    }

    // ── спатиальная математика (общая для сервера и клиента) ─────────────

    inline float channelRangeMeters(std::uint8_t channel, float say, float whisper, float shout)
    {
        switch (channel)
        {
            case CHANNEL_WHISPER: return whisper;
            case CHANNEL_SHOUT:   return shout;
            case CHANNEL_GROUP:   return 0.f;     // без дистанции
            default:              return say;
        }
    }

    /// Затухание по дистанции. Намеренно линейное с показателем 1.6, а не
    /// dMin/d: обратная пропорция даёт резкий обрыв у границы и «шёпот
    /// из ниоткуда». distanceUnits и оба радиуса — в юнитах.
    /// rolloff: 1.0 — ровное затухание, 1.6 — ближе к привычному по играм,
    /// больше 2 — «слышно только вплотную». Настраивается в [Voice] rolloff.
    inline constexpr float sDefaultRolloff = 1.6f;
    inline float distanceGain(float distanceUnits, float minUnits, float maxUnits,
        float rolloff = sDefaultRolloff)
    {
        if (maxUnits <= minUnits) return distanceUnits <= maxUnits ? 1.f : 0.f;
        if (distanceUnits <= minUnits) return 1.f;
        if (distanceUnits >= maxUnits) return 0.f;
        const float linear = (maxUnits - distanceUnits) / (maxUnits - minUnits);
        return rolloff == 1.f ? linear : std::pow(linear, rolloff);
    }

    inline float wrapToPi(float radians)
    {
        constexpr float twoPi = 6.2831853071795864f;
        radians = std::fmod(radians + 3.1415926535897932f, twoPi);
        if (radians < 0.f) radians += twoPi;
        return radians - 3.1415926535897932f;
    }

    /// Азимут говорящего относительно взгляда слушателя.
    /// Морровинд: +X восток, +Y север, yaw растёт по часовой от севера.
    inline float azimuthRadians(float dx, float dy, float listenerYaw)
    {
        return wrapToPi(std::atan2(dx, dy) - listenerYaw);
    }

    /// Равномощная панорама для ручного микса (Android, режим без OpenAL).
    inline void equalPowerPan(float azimuth, float gain, float& left, float& right)
    {
        const float pan = std::sin(azimuth);                 // -1 слева, +1 справа
        const float angle = (pan + 1.f) * 0.7853981633974483f;   // (pan+1)*PI/4
        left = gain * std::cos(angle);
        right = gain * std::sin(angle);
    }

    /// Позиция источника для OpenAL при AL_SOURCE_RELATIVE = AL_TRUE.
    /// Слушатель смотрит в -Z, +X справа (соглашение OpenAL).
    inline void openAlRelativePosition(float azimuth, float distanceMeters, float out[3])
    {
        out[0] = std::sin(azimuth) * distanceMeters;
        out[1] = 0.f;
        out[2] = -std::cos(azimuth) * distanceMeters;
    }

    inline std::uint8_t packGain(float gain)
    {
        if (gain <= 0.f) return 0;
        if (gain >= 1.f) return 255;
        return static_cast<std::uint8_t>(gain * 255.f + 0.5f);
    }
    inline float unpackGain(std::uint8_t gain) { return static_cast<float>(gain) / 255.f; }

    inline std::int16_t packAngle(float radians) { return static_cast<std::int16_t>(wrapToPi(radians) * 100.f); }
    inline float unpackAngle(std::int16_t packed) { return static_cast<float>(packed) / 100.f; }

    inline std::uint16_t packDistance(float meters)
    {
        const float decimeters = meters * 10.f;
        if (decimeters <= 0.f) return 0;
        if (decimeters >= 65535.f) return 65535;
        return static_cast<std::uint16_t>(decimeters + 0.5f);
    }
    inline float unpackDistance(std::uint16_t packed) { return static_cast<float>(packed) / 10.f; }

    /// FNV-1a: идентификатор ячейки, одинаковый на сервере и клиенте.
    inline std::uint32_t cellHash(const std::string& name)
    {
        std::uint32_t hash = 2166136261u;
        for (const char c : name)
        {
            hash ^= static_cast<std::uint8_t>(c);
            hash *= 16777619u;
        }
        return hash;
    }
}
#endif
