#ifndef ARENAMP_ARENALINK_HPP
#define ARENAMP_ARENALINK_HPP

// ArenaMP U025 — ArenaLink: чат лаунчера напрямую с игровым сервером.
//
// Отдельный TCP-порт (по умолчанию игровой + 2), обычные сокеты. RakNet не
// используется вообще: игровой протокол и его версия (TES3MP 806) остаются
// нетронутыми, а чат может падать, обновляться и обрываться, не задевая игру.
// Веб-обвязки нет — ни PHP, ни внешней БД: сервер сам хранит каналы, историю
// и присутствие.
//
// Соединение постоянное, сообщения приходят пушем. Опроса (polling) нет:
// лаунчер держит один сокет и просто слушает.
//
// Кадр:
//   0      3 4     5 6      7 8            (8 + length)
//   +-------+------+--------+---------------+
//   |'ALK1' | type | flags  |  length u16   |  payload
//   +-------+------+--------+---------------+
// length — длина payload, максимум sMaxPayload. Заголовок 8 байт.

#include <cstdint>
#include <string>
#include <vector>

#include <components/openmw-mp/arenastream.hpp>

namespace ArenaLink
{
    using Writer = ArenaNet::Writer;
    using Reader = ArenaNet::Reader;

    inline constexpr std::uint32_t sMagic = 0x414C4B31u;      // 'ALK1'
    inline constexpr std::uint16_t sProtocol = 2;
    inline constexpr std::size_t sHeaderSize = 8;
    inline constexpr std::size_t sMaxPayload = 16384;
    inline constexpr std::size_t sNonceSize = 16;
    inline constexpr std::size_t sProofSize = 32;             // HMAC-SHA256
    inline constexpr std::size_t sTicketSize = 16;
    inline constexpr std::size_t sMaxNick = 120; // bytes: up to 30 UTF-8 code points
    inline constexpr std::size_t sMaxText = 4000;
    inline constexpr std::size_t sMaxChannelName = 32;

    enum Type : std::uint8_t
    {
        // рукопожатие и вход
        TYPE_HELLO          = 0x01,   // c→s
        TYPE_CHALLENGE      = 0x02,   // s→c
        TYPE_AUTH           = 0x03,   // c→s
        TYPE_AUTH_OK        = 0x04,   // s→c
        TYPE_AUTH_FAIL      = 0x05,   // s→c
        // каналы и сообщения
        TYPE_CHANNELS       = 0x10,   // s→c  (сразу после входа и при изменении)
        TYPE_JOIN_CHANNEL   = 0x11,   // c→s
        TYPE_HISTORY_REQ    = 0x12,   // c→s
        TYPE_MESSAGES       = 0x13,   // s→c  (пачка истории)
        TYPE_SEND           = 0x14,   // c→s
        TYPE_MESSAGE        = 0x15,   // s→c  (пуш одного сообщения)
        // присутствие
        TYPE_PRESENCE       = 0x20,   // s→c  полный список
        TYPE_PRESENCE_DELTA = 0x21,   // s→c  один игрок вошёл/вышел/сменил уровень
        // голос
        TYPE_VOICE_TICKET_REQ = 0x30, // c→s
        TYPE_VOICE_TICKET     = 0x31, // s→c
        TYPE_VOICE_STATE      = 0x32, // s→c  кто сейчас в голосе и заглушен ли
        // служебное
        TYPE_PING           = 0x40,   // обе стороны
        TYPE_PONG           = 0x41,
        TYPE_NOTICE         = 0x50,   // s→c  кик, mute, остановка сервера
    };

    enum AuthMode : std::uint8_t
    {
        // Пароль не уходит по сети: сервер шлёт nonce, клиент отвечает
        // HMAC-SHA256(sha256(пароль), nonce). Требует, чтобы скриптовый
        // набор отдавал ArenaLink сам пароль или его sha256.
        AUTH_PROOF    = 0,
        // Запасной режим, если пароли лежат в bcrypt и пересчитать proof
        // нельзя. Пароль идёт по каналу так же, как при обычном входе в
        // игру по RakNet — не хуже и не лучше существующего.
        AUTH_PLAIN    = 1,
        // Первая привязка без пароля: игрок пишет /chatlink в игре и
        // вводит шестизначный код. Живёт 5 минут, одноразовый.
        AUTH_CODE     = 2,
        AUTH_TES3MP_PROOF = 3, // HMAC keyed by the stored salted TES3MP verifier
    };

    enum AuthFail : std::uint8_t
    {
        FAIL_BAD_CREDENTIALS = 1,
        FAIL_NO_ACCOUNT      = 2,   // персонаж ещё не создан: нужно зайти в игру
        FAIL_BANNED          = 3,
        FAIL_RATE            = 4,
        FAIL_VERSION         = 5,
        FAIL_CODE_EXPIRED    = 6,
    };

    enum ChannelFlag : std::uint8_t
    {
        CHANNEL_WRITABLE   = 1 << 0,
        CHANNEL_ADMIN_ONLY = 1 << 1,
        /// Канал зеркалит внутриигровой чат: то, что пишут в игре, видно
        /// в лаунчере и наоборот. Настраивается на сервере по каналу.
        CHANNEL_MIRRORS_GAME = 1 << 2,
    };

    enum MessageFlag : std::uint8_t
    {
        MESSAGE_SYSTEM   = 1 << 0,
        MESSAGE_FROM_GAME = 1 << 1,   // пришло из внутриигрового чата
        MESSAGE_DELETED  = 1 << 2,
    };

    enum PresenceFlag : std::uint8_t
    {
        PRESENCE_ONLINE  = 1 << 0,   // в чате лаунчера
        PRESENCE_INGAME  = 1 << 1,   // в мире
        PRESENCE_VOICE   = 1 << 2,   // в голосе
        PRESENCE_MUTED   = 1 << 3,
    };

    // ── структуры ────────────────────────────────────────────────────────

    struct Channel
    {
        std::uint16_t id = 0;
        std::string name;
        std::uint8_t flags = CHANNEL_WRITABLE;
    };

    struct Message
    {
        std::uint64_t id = 0;
        std::uint32_t timestamp = 0;      // unix-секунды
        std::uint16_t channel = 0;
        std::uint32_t userId = 0;
        std::string author;
        std::uint16_t level = 0;
        std::uint32_t color = 0xC8C8C8u;  // 0x00RRGGBB, снимок на момент отправки
        std::uint8_t flags = 0;
        std::string text;
    };

    struct Presence
    {
        std::uint32_t userId = 0;
        std::string name;
        std::uint16_t level = 0;
        std::uint32_t color = 0xC8C8C8u;
        std::uint8_t flags = 0;
    };

    struct AuthResult
    {
        std::uint32_t userId = 0;
        std::string name;
        std::uint16_t level = 0;
        std::uint32_t color = 0xC8C8C8u;
        std::string className;
        std::uint16_t voicePort = 0;      // 0 = голос на сервере выключен
        std::uint8_t flags = 0;
    };

    // ── кадрирование ─────────────────────────────────────────────────────

    inline std::string frame(std::uint8_t type, const std::string& payload, std::uint8_t flags = 0)
    {
        Writer writer(sHeaderSize + payload.size());
        writer.u32(sMagic);
        writer.u8(type);
        writer.u8(flags);
        writer.u16(static_cast<std::uint16_t>(payload.size() > sMaxPayload ? sMaxPayload : payload.size()));
        writer.raw(payload.data(), payload.size() > sMaxPayload ? sMaxPayload : payload.size());
        return writer.data();
    }

    struct FrameHeader
    {
        std::uint8_t type = 0;
        std::uint8_t flags = 0;
        std::uint16_t length = 0;
    };

    /// Разбор потока TCP: возвращает true, если в буфере лежит целый кадр.
    /// consumed — сколько байт можно выбросить из буфера.
    /// Кадр с чужой сигнатурой — повод закрыть соединение, а не искать
    /// синхронизацию дальше по потоку.
    inline bool nextFrame(const char* data, std::size_t size, FrameHeader& header,
        std::string& payload, std::size_t& consumed, bool& fatal)
    {
        fatal = false;
        consumed = 0;
        if (size < sHeaderSize) return false;

        Reader reader(data, size);
        if (reader.u32() != sMagic) { fatal = true; return false; }
        header.type = reader.u8();
        header.flags = reader.u8();
        header.length = reader.u16();
        if (header.length > sMaxPayload) { fatal = true; return false; }
        if (size < sHeaderSize + header.length) return false;

        payload.assign(data + sHeaderSize, header.length);
        consumed = sHeaderSize + header.length;
        return true;
    }

    // ── сборка полезных нагрузок ─────────────────────────────────────────

    inline std::string makeHello(std::uint8_t clientKind, const std::string& clientVersion, const std::string& name = {})
    {
        Writer writer;
        writer.u16(sProtocol);
        writer.u8(clientKind);          // 0 — лаунчер ПК, 1 — Android
        writer.text(clientVersion, 32);
        writer.text(name, sMaxNick);
        return frame(TYPE_HELLO, writer.data());
    }

    inline std::string makeChallenge(const std::uint8_t nonce[sNonceSize], std::uint8_t authMode, const std::string& salt = {})
    {
        Writer writer;
        writer.raw(nonce, sNonceSize);
        writer.u8(authMode);
        writer.text(salt, 128);
        return frame(TYPE_CHALLENGE, writer.data());
    }

    inline std::string makeAuth(const std::string& name, std::uint8_t mode, const std::string& secret)
    {
        Writer writer;
        writer.text(name, sMaxNick);
        writer.u8(mode);
        writer.text(secret, 128);       // proof[32] в сыром виде, пароль или код
        return frame(TYPE_AUTH, writer.data());
    }

    inline std::string makeAuthOk(const AuthResult& result)
    {
        Writer writer;
        writer.u32(result.userId);
        writer.text(result.name, sMaxNick);
        writer.u16(result.level);
        writer.u32(result.color);
        writer.text(result.className, 32);
        writer.u16(result.voicePort);
        writer.u8(result.flags);
        return frame(TYPE_AUTH_OK, writer.data());
    }

    inline bool parseAuthOk(Reader& reader, AuthResult& out)
    {
        out.userId = reader.u32();
        out.name = reader.text(sMaxNick);
        out.level = reader.u16();
        out.color = reader.u32();
        out.className = reader.text(32);
        out.voicePort = reader.u16();
        out.flags = reader.u8();
        return reader.ok();
    }

    inline std::string makeAuthFail(std::uint8_t reason, const std::string& text)
    {
        Writer writer;
        writer.u8(reason);
        writer.text16(text, 256);
        return frame(TYPE_AUTH_FAIL, writer.data());
    }

    inline std::string makeChannels(const std::vector<Channel>& channels)
    {
        Writer writer;
        const std::size_t count = channels.size() < 255 ? channels.size() : 255;
        writer.u8(static_cast<std::uint8_t>(count));
        for (std::size_t i = 0; i < count; ++i)
        {
            writer.u16(channels[i].id);
            writer.text(channels[i].name, sMaxChannelName);
            writer.u8(channels[i].flags);
        }
        return frame(TYPE_CHANNELS, writer.data());
    }

    inline bool parseChannels(Reader& reader, std::vector<Channel>& out)
    {
        out.clear();
        const std::size_t count = reader.u8();
        for (std::size_t i = 0; i < count && reader.ok(); ++i)
        {
            Channel channel;
            channel.id = reader.u16();
            channel.name = reader.text(sMaxChannelName);
            channel.flags = reader.u8();
            out.push_back(channel);
        }
        return reader.ok();
    }

    inline void writeMessage(Writer& writer, const Message& message)
    {
        writer.u64(message.id);
        writer.u32(message.timestamp);
        writer.u16(message.channel);
        writer.u32(message.userId);
        writer.text(message.author, sMaxNick);
        writer.u16(message.level);
        writer.u32(message.color);
        writer.u8(message.flags);
        writer.text16(message.text, sMaxText);
    }

    inline bool readMessage(Reader& reader, Message& out)
    {
        out.id = reader.u64();
        out.timestamp = reader.u32();
        out.channel = reader.u16();
        out.userId = reader.u32();
        out.author = reader.text(sMaxNick);
        out.level = reader.u16();
        out.color = reader.u32();
        out.flags = reader.u8();
        out.text = reader.text16(sMaxText);
        return reader.ok();
    }

    inline std::string makeMessage(const Message& message)
    {
        Writer writer;
        writeMessage(writer, message);
        return frame(TYPE_MESSAGE, writer.data());
    }

    /// Пачка истории. Вызывающий обязан следить, чтобы сумма не вышла за
    /// sMaxPayload: на 4000-символьных сообщениях это примерно 4 штуки,
    /// поэтому история отдаётся порциями.
    inline std::string makeMessages(std::uint16_t channel, const std::vector<Message>& messages)
    {
        Writer writer;
        writer.u16(channel);
        const std::size_t count = messages.size() < 255 ? messages.size() : 255;
        writer.u8(static_cast<std::uint8_t>(count));
        for (std::size_t i = 0; i < count; ++i)
            writeMessage(writer, messages[i]);
        return frame(TYPE_MESSAGES, writer.data());
    }

    inline bool parseMessages(Reader& reader, std::uint16_t& channel, std::vector<Message>& out)
    {
        out.clear();
        channel = reader.u16();
        const std::size_t count = reader.u8();
        for (std::size_t i = 0; i < count && reader.ok(); ++i)
        {
            Message message;
            if (!readMessage(reader, message)) break;
            out.push_back(message);
        }
        return reader.ok();
    }

    inline std::string makeSend(std::uint16_t channel, const std::string& text)
    {
        Writer writer;
        writer.u16(channel);
        writer.text16(text, sMaxText);
        return frame(TYPE_SEND, writer.data());
    }

    inline std::string makeHistoryRequest(std::uint16_t channel, std::uint64_t beforeId, std::uint16_t limit)
    {
        Writer writer;
        writer.u16(channel);
        writer.u64(beforeId);           // 0 = последние сообщения
        writer.u16(limit);
        return frame(TYPE_HISTORY_REQ, writer.data());
    }

    inline std::string makePresence(const std::vector<Presence>& players)
    {
        Writer writer;
        const std::size_t count = players.size() < 255 ? players.size() : 255;
        writer.u8(static_cast<std::uint8_t>(count));
        for (std::size_t i = 0; i < count; ++i)
        {
            writer.u32(players[i].userId);
            writer.text(players[i].name, sMaxNick);
            writer.u16(players[i].level);
            writer.u32(players[i].color);
            writer.u8(players[i].flags);
        }
        return frame(TYPE_PRESENCE, writer.data());
    }

    inline bool parsePresence(Reader& reader, std::vector<Presence>& out)
    {
        out.clear();
        const std::size_t count = reader.u8();
        for (std::size_t i = 0; i < count && reader.ok(); ++i)
        {
            Presence presence;
            presence.userId = reader.u32();
            presence.name = reader.text(sMaxNick);
            presence.level = reader.u16();
            presence.color = reader.u32();
            presence.flags = reader.u8();
            out.push_back(presence);
        }
        return reader.ok();
    }

    inline std::string makeVoiceTicketRequest(std::uint8_t scope)
    {
        Writer writer;
        writer.u8(scope);               // 0 — лобби, 1 — игровая сессия
        return frame(TYPE_VOICE_TICKET_REQ, writer.data());
    }

    inline std::string makeVoiceTicket(const std::uint8_t ticket[sTicketSize],
        std::uint16_t port, std::uint16_t ttlSeconds)
    {
        Writer writer;
        writer.raw(ticket, sTicketSize);
        writer.u16(port);
        writer.u16(ttlSeconds);
        return frame(TYPE_VOICE_TICKET, writer.data());
    }

    inline std::string makeNotice(std::uint8_t severity, const std::string& text)
    {
        Writer writer;
        writer.u8(severity);            // 0 — информация, 1 — предупреждение, 2 — разрыв
        writer.text16(text, 512);
        return frame(TYPE_NOTICE, writer.data());
    }

    // ── цвет ─────────────────────────────────────────────────────────────

    /// "#RRGGBB" из палитры сервера (GUIChat.cpp: parseHexColour) → 0x00RRGGBB.
    /// Некорректная строка превращается в нейтральный серый, а не в чёрный:
    /// чёрный ник на тёмном фоне нечитаем.
    inline std::uint32_t colorFromHex(const std::string& value)
    {
        if (value.size() != 7 || value[0] != '#') return 0xC8C8C8u;
        std::uint32_t result = 0;
        for (std::size_t i = 1; i < 7; ++i)
        {
            const char c = value[i];
            std::uint32_t digit;
            if (c >= '0' && c <= '9') digit = static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') digit = static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') digit = static_cast<std::uint32_t>(c - 'A' + 10);
            else return 0xC8C8C8u;
            result = (result << 4) | digit;
        }
        return result & 0x00FFFFFFu;
    }

    inline std::string colorToHex(std::uint32_t color)
    {
        static const char digits[] = "0123456789ABCDEF";
        std::string out = "#000000";
        for (int i = 0; i < 6; ++i)
            out[6 - i] = digits[(color >> (i * 4)) & 0xF];
        return out;
    }
}
#endif
