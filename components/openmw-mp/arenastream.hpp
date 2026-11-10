#ifndef ARENAMP_ARENASTREAM_HPP
#define ARENAMP_ARENASTREAM_HPP

// ArenaMP U025 — общий сериализатор для ArenaLink (TCP-чат) и ArenaVoice
// (UDP-голос). Ни одного внешнего заголовка, ни байта RakNet: оба канала
// живут на своих портах и обычных сокетах, чтобы игровой трафик и трафик
// чата/голоса нельзя было уронить друг об друга.
//
// Порядок байтов — big-endian, как в остальном протоколе.

#include <cstdint>
#include <cstring>
#include <string>

namespace ArenaNet
{
    class Writer
    {
    public:
        explicit Writer(std::size_t reserve = 256) { mData.reserve(reserve); }

        void u8(std::uint8_t v) { mData.push_back(static_cast<char>(v)); }
        void u16(std::uint16_t v) { u8(static_cast<std::uint8_t>(v >> 8)); u8(static_cast<std::uint8_t>(v)); }
        void u32(std::uint32_t v) { u16(static_cast<std::uint16_t>(v >> 16)); u16(static_cast<std::uint16_t>(v)); }
        void u64(std::uint64_t v) { u32(static_cast<std::uint32_t>(v >> 32)); u32(static_cast<std::uint32_t>(v)); }
        void i16(std::int16_t v) { u16(static_cast<std::uint16_t>(v)); }
        void i32(std::int32_t v) { u32(static_cast<std::uint32_t>(v)); }

        void raw(const void* data, std::size_t size)
        {
            const char* bytes = static_cast<const char*>(data);
            mData.insert(mData.end(), bytes, bytes + size);
        }

        /// Короткая строка: длина одним байтом (ники, названия каналов).
        void text(const std::string& value, std::size_t limit)
        {
            const std::size_t size = value.size() < limit ? value.size() : limit;
            u8(static_cast<std::uint8_t>(size));
            raw(value.data(), size);
        }

        /// Длинная строка: длина двумя байтами (тексты сообщений).
        void text16(const std::string& value, std::size_t limit)
        {
            const std::size_t size = value.size() < limit ? value.size() : limit;
            u16(static_cast<std::uint16_t>(size));
            raw(value.data(), size);
        }

        /// Дописать 16-битную длину в уже записанное место (каркас кадра).
        void patchU16(std::size_t offset, std::uint16_t value)
        {
            if (offset + 2 > mData.size()) return;
            mData[offset] = static_cast<char>(value >> 8);
            mData[offset + 1] = static_cast<char>(value & 0xFF);
        }

        const std::string& data() const { return mData; }
        std::size_t size() const { return mData.size(); }

    private:
        std::string mData;
    };

    class Reader
    {
    public:
        Reader(const char* data, std::size_t size) : mData(data), mSize(size) {}
        explicit Reader(const std::string& data) : mData(data.data()), mSize(data.size()) {}

        bool ok() const { return !mFailed; }
        std::size_t remaining() const { return mFailed ? 0 : mSize - mPos; }

        std::uint8_t u8()
        {
            if (mFailed || mPos + 1 > mSize) { mFailed = true; return 0; }
            return static_cast<std::uint8_t>(mData[mPos++]);
        }
        std::uint16_t u16() { const std::uint16_t hi = u8(); return static_cast<std::uint16_t>((hi << 8) | u8()); }
        std::uint32_t u32() { const std::uint32_t hi = u16(); return (hi << 16) | u16(); }
        std::uint64_t u64() { const std::uint64_t hi = u32(); return (hi << 32) | u32(); }
        std::int16_t i16() { return static_cast<std::int16_t>(u16()); }
        std::int32_t i32() { return static_cast<std::int32_t>(u32()); }

        bool raw(void* out, std::size_t size)
        {
            if (mFailed || mPos + size > mSize) { mFailed = true; return false; }
            std::memcpy(out, mData + mPos, size);
            mPos += size;
            return true;
        }

        std::string text(std::size_t limit)
        {
            const std::size_t size = u8();
            if (mFailed || size > limit || mPos + size > mSize) { mFailed = true; return {}; }
            std::string value(mData + mPos, size);
            mPos += size;
            return value;
        }

        std::string text16(std::size_t limit)
        {
            const std::size_t size = u16();
            if (mFailed || size > limit || mPos + size > mSize) { mFailed = true; return {}; }
            std::string value(mData + mPos, size);
            mPos += size;
            return value;
        }

        std::string rest()
        {
            if (mFailed) return {};
            std::string value(mData + mPos, mSize - mPos);
            mPos = mSize;
            return value;
        }

    private:
        const char* mData;
        std::size_t mSize;
        std::size_t mPos = 0;
        bool mFailed = false;
    };
}
#endif
