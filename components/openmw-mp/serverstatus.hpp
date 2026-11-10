#ifndef ARENAMP_SERVERSTATUS_HPP
#define ARENAMP_SERVERSTATUS_HPP
#include <cstdint>
#include <string>

namespace ArenaStatus
{
    // RakNet offline ping/pong framing (RakPeer.cpp). No connection or player slot.
    inline std::string magic()
    {
        static const unsigned char bytes[] = {0,255,255,0,254,254,254,254,253,253,253,253,18,52,86,120};
        return std::string(reinterpret_cast<const char*>(bytes), sizeof(bytes));
    }
    inline std::string ping(const std::string& token, unsigned timeBytes)
    {
        if (token.size() != 8 || (timeBytes != 4 && timeBytes != 8)) return {};
        return std::string(1, '\x01') + token.substr(0, timeBytes) + magic() + std::string(8, '\0');
    }
    struct Status
    {
        bool reachable = false;
        bool details = false;
        unsigned players = 0;
        unsigned capacity = 0;
        std::uint64_t uptime = 0;
    };
    inline std::string payload(unsigned players, unsigned capacity, std::uint64_t uptime)
    {
        return "AMPSTATUS1|" + std::to_string(players) + "|" + std::to_string(capacity) + "|" + std::to_string(uptime);
    }
    inline bool number(const std::string& text, std::uint64_t max, std::uint64_t& value)
    {
        value = 0;
        if (text.empty() || text.size() > 20) return false;
        for (const char c : text)
        {
            if (c < '0' || c > '9' || value > (max - static_cast<unsigned>(c - '0')) / 10) return false;
            value = value * 10 + static_cast<unsigned>(c - '0');
        }
        return value <= max;
    }
    inline Status pong(const std::string& packet, const std::string& token)
    {
        Status result;
        if (packet.empty() || packet.size() > 512 || packet[0] != '\x1c' || token.size() != 8) return result;
        for (unsigned timeBytes : {8u, 4u})
        {
            const unsigned headerSize = 1 + timeBytes + 8 + 16;
            if (packet.size() < headerSize || packet.compare(1, timeBytes, token, 0, timeBytes) != 0
                || packet.compare(1 + timeBytes + 8, 16, magic()) != 0) continue;
            result.reachable = true;
            const std::string text = packet.substr(headerSize);
            const std::string prefix = "AMPSTATUS1|";
            if (text.compare(0, prefix.size(), prefix) != 0) return result;
            const auto first = text.find('|', prefix.size());
            const auto second = first == std::string::npos ? first : text.find('|', first + 1);
            if (first == std::string::npos || second == std::string::npos) return result;
            std::uint64_t players, capacity, uptime;
            if (!number(text.substr(prefix.size(), first - prefix.size()), 65535, players)
                || !number(text.substr(first + 1, second - first - 1), 65535, capacity)
                || !number(text.substr(second + 1), 3155760000ULL, uptime)
                || players > capacity) return result;
            result.details = true;
            result.players = static_cast<unsigned>(players);
            result.capacity = static_cast<unsigned>(capacity);
            result.uptime = uptime;
            return result;
        }
        return result;
    }
}
#endif
