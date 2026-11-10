#include <components/misc/buildupdatedefaults.hpp>
#include <cassert>
#include <iostream>
int main()
{
    using Misc::withArenaUpdateDefaults;
    const std::string original="\xef\xbb\xbf# untouched\r\n[Build]\r\nbuild=\"42\"\r\nURL_WIN=\"\"\r\nname=\"Сборка\"\r\nunknown=yes\r\n[Content]\r\ncontent=b.esp\r\ncontent=a.esm\r\n[Other]\r\nurl_check=unrelated\r\n";
    const auto updated=withArenaUpdateDefaults(original);
    assert(updated.substr(0,3)==original.substr(0,3));
    assert(updated.find("# untouched\r\n[Build]\r\nbuild=\"42\"")!=std::string::npos);
    assert(updated.find("name=\"Сборка\"\r\nunknown=yes\r\n[Content]\r\ncontent=b.esp\r\ncontent=a.esm")!=std::string::npos);
    assert(updated.find("build=\"1\"")==std::string::npos && updated.find("URL_WIN=\"https://mro1.myarena.site/build/ArenaMP-Windows-x64-main.zip\"")!=std::string::npos);
    assert(updated.find("url_check=\"https://mro1.myarena.site/build/check.ini\"\r\n")!=std::string::npos);
    assert(withArenaUpdateDefaults(updated)==updated);
    const auto fresh=withArenaUpdateDefaults("# new\n[Content]\ncontent=total-conversion.esm");
    for(const auto& field:Misc::arenaUpdateDefaults())assert(fresh.find(std::string(field.first)+"=\""+field.second+'"')!=std::string::npos);
    assert(fresh.find("content=total-conversion.esm")!=std::string::npos);
    assert(withArenaUpdateDefaults(fresh)==fresh);
    assert(withArenaUpdateDefaults("[Build]\nurl_android=\"custom.apk\"\n").find("url_android=\"https:")==std::string::npos);
    const auto blank=withArenaUpdateDefaults("[Build]\nbuild=\"   \"\nurl_check =  \"\"  \nurl_macos=\"\"\n");
    assert(blank.find("build=\"1\"")!=std::string::npos && blank.find("url_check =  \"https://mro1.myarena.site/build/check.ini\"  ")!=std::string::npos);
    assert(withArenaUpdateDefaults(blank)==blank);
    for(const auto& input:{"", "[Build]\n", "[Build]\nversion=\"\"\n", "[General]\nversion=   \n"})
    {
        const auto result=withArenaUpdateDefaults(input);
        assert(result.find("version=\"9999999\"")!=std::string::npos || result.find("version=   \"9999999\"")!=std::string::npos);
        assert(result.find("build=\"1\"")!=std::string::npos);
        assert(withArenaUpdateDefaults(result)==result);
    }
    const auto customVersion=withArenaUpdateDefaults("[Build]\nversion=\"00007\"\nbuild=\"42\"\n");
    assert(customVersion.find("version=\"00007\"")!=std::string::npos);
    assert(customVersion.find("version=\"9999999\"")==std::string::npos);
    const auto serverVersion=withArenaUpdateDefaults("[Server]\nversion=\"0.8.1\"\n");
    assert(serverVersion.find("version=\"9999999\"")!=std::string::npos);
    assert(serverVersion.find("[Server]\nversion=\"0.8.1\"")!=std::string::npos);
    const auto mixedCase=withArenaUpdateDefaults("[Manifest]\nVeRsIoN = \"123\"\n");
    assert(mixedCase.find("VeRsIoN = \"123\"")!=std::string::npos);
    assert(mixedCase.find("version=\"9999999\"")==std::string::npos);
    std::cout<<"PASS updater bootstrap: default version 9999999, missing/blank fields, custom versions/values, server version isolation, empty macOS URL, BOM, CRLF, comments, unknown sections, plugin order and idempotence\n";
}
