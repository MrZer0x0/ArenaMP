#ifndef ARENAMP_ARENALINKAUTH_HPP
#define ARENAMP_ARENALINKAUTH_HPP
#include <array>
#include <string>
#include <extern/PicoSHA2/picosha2.h>
namespace ArenaLink
{
    inline bool verifyStoredProof(const std::string& hashHex, const std::string& nonce, const std::string& proof)
    {
        if (hashHex.size() != 64 || nonce.size() != 16 || proof.size() != 32) return false;
        const auto digit = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        std::array<unsigned char, 64> inner{}, outer{};
        inner.fill(0x36); outer.fill(0x5c);
        for (std::size_t i = 0; i < 32; ++i)
        {
            const int hi = digit(hashHex[i * 2]), lo = digit(hashHex[i * 2 + 1]);
            if (hi < 0 || lo < 0) return false;
            const unsigned char key = static_cast<unsigned char>(hi * 16 + lo);
            inner[i] ^= key; outer[i] ^= key;
        }
        std::string input(reinterpret_cast<const char*>(inner.data()), inner.size());
        input += nonce;
        std::array<unsigned char, 32> digest{};
        picosha2::hash256(input, digest);
        input.assign(reinterpret_cast<const char*>(outer.data()), outer.size());
        input.append(reinterpret_cast<const char*>(digest.data()), digest.size());
        picosha2::hash256(input, digest);
        unsigned difference = 0;
        for (std::size_t i = 0; i < digest.size(); ++i)
            difference |= digest[i] ^ static_cast<unsigned char>(proof[i]);
        return difference == 0;
    }
}
#endif
