#ifndef OPENMW_MWGUI_MAPMARKERSTYLE_H
#define OPENMW_MWGUI_MAPMARKERSTYLE_H

#include <string>
#include <vector>
#include <MyGUI_Colour.h>

namespace MWGui
{
    struct ArenaMapMarkerStyle
    {
        bool styled = false;
        bool group = false;
        std::string kind = "?";
        std::string color = "yellow";
        std::string text;
    };

    inline std::vector<std::string> arenaSplitMarker(const std::string& value, char sep)
    {
        std::vector<std::string> out;
        std::size_t start=0;
        while (start <= value.size())
        {
            std::size_t end=value.find(sep,start);
            out.push_back(value.substr(start,end==std::string::npos?std::string::npos:end-start));
            if (end==std::string::npos) break;
            start=end+1;
        }
        return out;
    }

    inline ArenaMapMarkerStyle parseArenaMapMarker(const std::string& note)
    {
        ArenaMapMarkerStyle s;
        constexpr const char* personal="@AMP_MARK@|";
        constexpr const char* group="@AMP_GMARK@|";
        std::string body;
        if (note.compare(0,std::char_traits<char>::length(personal),personal)==0)
        {
            s.styled=true;
            body=note.substr(std::char_traits<char>::length(personal));
            auto f=arenaSplitMarker(body,'|');
            if (f.size()>=4) { s.kind=f[0]; s.color=f[1]; s.group=f[2]=="1"; s.text=f[3]; }
        }
        else if (note.compare(0,std::char_traits<char>::length(group),group)==0)
        {
            s.styled=true; s.group=true;
            body=note.substr(std::char_traits<char>::length(group));
            auto f=arenaSplitMarker(body,'|');
            if (f.size()>=3) { s.kind=f[0]; s.color=f[1]; s.text=f[2]; }
        }
        if (s.kind!="?" && s.kind!="!" && s.kind!="A" && s.kind!="B" && s.kind!="C") s.kind="?";
        if (!s.styled) s.text=note;
        return s;
    }

    inline MyGUI::Colour arenaMarkerColour(const std::string& name)
    {
        if (name=="red") return MyGUI::Colour(1.f,0.22f,0.18f);
        if (name=="green") return MyGUI::Colour(0.25f,1.f,0.35f);
        if (name=="blue") return MyGUI::Colour(0.25f,0.55f,1.f);
        if (name=="purple") return MyGUI::Colour(0.85f,0.35f,1.f);
        if (name=="orange") return MyGUI::Colour(1.f,0.55f,0.12f);
        if (name=="white") return MyGUI::Colour(0.95f,0.95f,0.95f);
        return MyGUI::Colour(1.f,0.9f,0.2f);
    }

    inline std::string makeArenaPersonalMarker(const std::string& kind, const std::string& color, bool group, std::string text)
    {
        for (char& c : text) if (c=='|' || c=='\n' || c=='\r' || c=='\t') c=' ';
        return "@AMP_MARK@|"+kind+"|"+color+"|"+(group?"1":"0")+"|"+text;
    }
}
#endif
