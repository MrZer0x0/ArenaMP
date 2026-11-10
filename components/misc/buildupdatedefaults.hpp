// ArenaMP updater bootstrap. Original GPL-3.0-or-later code.
#ifndef ARENA_BUILD_UPDATE_DEFAULTS_HPP
#define ARENA_BUILD_UPDATE_DEFAULTS_HPP
#include <array>
#include <map>
#include <vector>
#include <algorithm>
#include <string>
#include <utility>
namespace Misc
{
    inline const std::array<std::pair<const char*,const char*>,7>& arenaUpdateDefaults()
    {
        static const std::array<std::pair<const char*,const char*>,7> values {{
            {"version","9999999"},
            {"build","1"},
            {"url_check","https://mro1.myarena.site/build/check.ini"},
            {"url_win","https://mro1.myarena.site/build/ArenaMP-Windows-x64-main.zip"},
            {"url_linux","https://mro1.myarena.site/build/ArenaMP-Linux-x86_64-SteamDeck-main.tar.gz"},
            {"url_macos",""},
            {"url_android","https://mro1.myarena.site/build/ArenaMP-arm64-client-server.apk"}
        }};
        return values;
    }
    inline std::string withArenaUpdateDefaults(const std::string& text)
    {
        const auto trim=[](std::string v)
        {
            const auto first=v.find_first_not_of(" \t\r\n");
            if(first==std::string::npos)return std::string();
            return v.substr(first,v.find_last_not_of(" \t\r\n")-first+1);
        };
        const auto lower=[](std::string v){for(char& c:v)if(c>='A'&&c<='Z')c+=('a'-'A');return v;};
        const std::string bom="\xef\xbb\xbf";
        const auto offset=text.compare(0,3,bom)==0 ? 3u : 0u;
        struct Assignment {std::size_t first=0,last=0;bool empty=false;};
        std::map<std::string,Assignment> present;
        std::string section;
        std::size_t begin=offset;
        while(begin<text.size())
        {
            auto end=text.find('\n',begin);if(end==std::string::npos)end=text.size();
            auto line=trim(text.substr(begin,end-begin));
            if(!line.empty() && line[0]!='#' && line[0]!=';')
            {
                if(line.front()=='[' && line.back()==']')section=lower(trim(line.substr(1,line.size()-2)));
                else if(section.empty() || section=="build" || section=="general" || section=="manifest")
                {
                    const auto at=line.find('=');
                    if(at!=std::string::npos && at>0)
                    {
                        const auto key=lower(trim(line.substr(0,at)));
                        auto value=trim(line.substr(at+1));
                        if(value.size()>=2 && value.front()=='"' && value.back()=='"')value=trim(value.substr(1,value.size()-2));
                        const auto raw=text.substr(begin,end-begin);
                        auto first=begin+raw.find('=')+1;
                        auto last=end;
                        if(last>first && text[last-1]=='\r')--last;
                        while(first<last && (text[first]==' ' || text[first]=='\t'))++first;
                        while(last>first && (text[last-1]==' ' || text[last-1]=='\t'))--last;
                        present[key]={first,last,value.empty()};
                    }
                }
            }
            if(end==text.size())break;
            begin=end+1;
        }
        const std::string newline=text.find("\r\n")!=std::string::npos ? "\r\n" : "\n";
        std::string added;
        std::vector<std::pair<Assignment,std::string>> edits;
        for(const auto& field:arenaUpdateDefaults())
        {
            const auto found=present.find(field.first);
            if(found==present.end())added+=std::string(field.first)+"=\""+field.second+'"'+newline;
            else if(found->second.empty && *field.second)edits.push_back({found->second,std::string("\"")+field.second+'"'});
        }
        std::sort(edits.begin(),edits.end(),[](const auto& a,const auto& b){return a.first.first>b.first.first;});
        auto updated=text;
        for(const auto& edit:edits)updated.replace(edit.first.first,edit.first.last-edit.first.first,edit.second);
        if(added.empty())return updated;
        return updated.substr(0,offset)+added+updated.substr(offset);
    }
}
#endif
