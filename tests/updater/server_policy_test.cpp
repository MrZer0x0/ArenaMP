#include "../../apps/launcher/updater/serverupdatepolicy.hpp"
#include <cassert>
#include <map>
#include <string>
using namespace ArenaUpdater;
int main()
{
    const std::map<std::string, ServerFilePolicy> cases = {
        {"server/scripts/houseHelper.lua", ServerFilePolicy::Managed},
        {"server/scripts/cell/base.lua", ServerFilePolicy::Managed},
        {"Server\\Scripts\\adminInterface.lua", ServerFilePolicy::Managed},
        {"Resources/server/scripts/serverCore.lua", ServerFilePolicy::Managed},
        {"ArenaMP.app/Contents/Resources/server/lib/lua/cjson.lua", ServerFilePolicy::Managed},
        {"server/ARENAMP_CORE_VERSION.txt", ServerFilePolicy::Managed},
        {"server/scripts/config.lua", ServerFilePolicy::SeedOnly},
        {"Resources/SERVER/scripts/CustomScripts.lua", ServerFilePolicy::SeedOnly},
        {"server/scripts/custom/myQuest.lua", ServerFilePolicy::SeedOnly},
        {"server/data/player/alice.json", ServerFilePolicy::Preserve},
        {"Resources/server/data/cell/home.json", ServerFilePolicy::Preserve},
        {"server/data/requiredDataFiles.json", ServerFilePolicy::Preserve},
        {"server/server.log", ServerFilePolicy::Preserve},
        {"server/backup/scripts/houseHelper.lua", ServerFilePolicy::Preserve},
        {"server/custom.dat", ServerFilePolicy::Preserve},
        {"tes3mp.exe", ServerFilePolicy::NotServer},
        {"other-server/scripts/test.lua", ServerFilePolicy::NotServer}
    };
    for (const auto& c : cases) assert(serverFilePolicy(c.first) == c.second);
    // Simulate two engine payloads against an existing host tree with the
    // same production policy. Local-only plugins and state must survive.
    std::map<std::string,std::string> disk = {
        {"server/scripts/houseHelper.lua","old"},
        {"server/scripts/config.lua","my settings"},
        {"server/scripts/customScripts.lua","my imports"},
        {"server/scripts/custom/local.lua","my plugin"},
        {"server/data/player/alice.json","save"}
    };
    auto install = [&](const std::string& p,const std::string& bytes) {
        const auto policy=serverFilePolicy(p);
        if (policy==ServerFilePolicy::Preserve) return;
        if (policy==ServerFilePolicy::SeedOnly && disk.count(p)) return;
        disk[p]=bytes;
    };
    for(int pass=0;pass<2;++pass) {
        install("server/scripts/houseHelper.lua",pass?"newer":"new");
        install("server/scripts/config.lua","default");
        install("server/scripts/customScripts.lua","default");
        install("server/scripts/custom/local.lua","default");
        install("server/data/player/alice.json","empty");
        install("server/scripts/custom/new.lua","seed");
    }
    assert(disk.at("server/scripts/houseHelper.lua")=="newer");
    assert(disk.at("server/scripts/config.lua")=="my settings");
    assert(disk.at("server/scripts/customScripts.lua")=="my imports");
    assert(disk.at("server/scripts/custom/local.lua")=="my plugin");
    assert(disk.at("server/data/player/alice.json")=="save");
    assert(disk.at("server/scripts/custom/new.lua")=="seed");
}
