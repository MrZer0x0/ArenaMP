#ifndef ARENA_SERVER_UPDATE_POLICY_HPP
#define ARENA_SERVER_UPDATE_POLICY_HPP

#include <algorithm>
#include <cctype>
#include <string>

namespace ArenaUpdater
{
    enum class ServerFilePolicy { NotServer, Managed, Preserve, SeedOnly };

    // Called for validated, relative archive paths. Match server components
    // also inside macOS Resources; never exempt an entire server tree.
    inline ServerFilePolicy serverFilePolicy(std::string path)
    {
        std::replace(path.begin(), path.end(), '\\', '/');
        std::transform(path.begin(), path.end(), path.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const std::string marker = "/server/";
        std::string tail;
        if (path.compare(0, 7, "server/") == 0) tail = path.substr(7);
        else
        {
            const auto at = path.find(marker);
            if (at == std::string::npos) return ServerFilePolicy::NotServer;
            tail = path.substr(at + marker.size());
        }
        // Host configuration and extension entry points are installed only
        // when missing. Unknown/local files are never pruned by the updater.
        if (tail == "scripts/config.lua" || tail == "scripts/customscripts.lua"
            || tail.compare(0, 15, "scripts/custom/") == 0)
            return ServerFilePolicy::SeedOnly;
        if (tail.compare(0, 8, "scripts/") == 0 || tail.compare(0, 4, "lib/") == 0
            || tail == "arenamp_core_version.txt")
            return ServerFilePolicy::Managed;
        return ServerFilePolicy::Preserve;
    }
}
#endif
