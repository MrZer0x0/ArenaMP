#include "ContentCatalog.hpp"
#include <deque>

#include <algorithm>
#include <cmath>
#include <cstring>
#if !defined(__ANDROID__)
#include <filesystem>
#endif
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

namespace ArenaContent
{
namespace
{
    std::uint32_t u32(const std::string& s, std::size_t p = 0)
    {
        if (p > s.size() || s.size() - p < 4) throw std::runtime_error("truncated uint32");
        std::uint32_t n = 0;
        for (unsigned int i = 0; i < 4; ++i) n |= std::uint32_t(static_cast<unsigned char>(s[p+i])) << (8*i);
        return n;
    }
    std::int32_t i32(const std::string& s, std::size_t p = 0)
    {
        const auto n = u32(s,p);
        return n <= 0x7fffffffu ? static_cast<std::int32_t>(n)
            : static_cast<std::int32_t>(static_cast<std::int64_t>(n) - 0x100000000ll);
    }
    unsigned int u16(const std::string& s, std::size_t p)
    {
        if (p > s.size() || s.size()-p < 2) throw std::runtime_error("truncated uint16");
        return static_cast<unsigned char>(s[p]) | (static_cast<unsigned char>(s[p+1]) << 8);
    }
    double f32(const std::string& s, std::size_t p = 0)
    {
        const auto n = u32(s,p); float value; static_assert(sizeof(value)==4,"float32 required");
        std::memcpy(&value,&n,4);
        if (!std::isfinite(value)) throw std::runtime_error("non-finite plugin float");
        return value;
    }
    std::string zstring(const std::string& s) { return s.substr(0,s.find('\0')); }
    std::string upper(std::string s)
    {
        for (char& c : s) if (c >= 'a' && c <= 'z') c -= 'a'-'A';
        return s;
    }
    bool tagValid(const std::string& tag)
    {
        return tag.size()==4 && std::all_of(tag.begin(),tag.end(),[](unsigned char c) {
            return (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_'; });
    }
    const std::string* first(const Record& r, const std::string& tag)
    {
        for (const auto& s : r.subrecords) if (s.tag==tag) return &s.bytes;
        return nullptr;
    }
    std::string text(const Record& r, const std::string& tag, const Catalog::Decode& decode)
    {
        const auto* s=first(r,tag); return s ? decode(zstring(*s)) : "";
    }
    void requireSize(const std::string* p, std::size_t size, const std::string& tag)
    {
        if (p && p->size()!=size) throw std::runtime_error("invalid "+tag+" size");
    }
    // Same legacy TES3 fixed effect flags as components/esm/loadmgef.cpp (OpenMW GPL).
    constexpr unsigned int legacyEffectFlags[] = {
        0x11c8, 0x11c0, 0x11c8, 0x11e0, 0x11e0, 0x11e0, 0x11e0, 0x11d0,
        0x11c0, 0x11c0, 0x11e0, 0x11c0, 0x11184, 0x11184, 0x1f0, 0x1f0,
        0x1f0, 0x11d2, 0x11f0, 0x11d0, 0x11d0, 0x11d1, 0x1d2, 0x1f0,
        0x1d0, 0x1d0, 0x1d1, 0x1f0, 0x11d0, 0x11d0, 0x11d0, 0x11d0,
        0x11d0, 0x11d0, 0x11d0, 0x11d0, 0x11d0, 0x1d0, 0x1d0, 0x11c8,
        0x31c0, 0x11c0, 0x11c0, 0x11c0, 0x1180, 0x11d8, 0x11d8, 0x11d0,
        0x11d0, 0x11180, 0x11180, 0x11180, 0x11180, 0x11180, 0x11180, 0x11180,
        0x11180, 0x11c4, 0x111b8, 0x1040, 0x104c, 0x104c, 0x104c, 0x104c,
        0x1040, 0x1040, 0x1040, 0x11c0, 0x11c0, 0x1cc, 0x1cc, 0x1cc,
        0x1cc, 0x1cc, 0x1c2, 0x1c0, 0x1c0, 0x1c0, 0x1c1, 0x11c2,
        0x11c0, 0x11c0, 0x11c0, 0x11c1, 0x11c0, 0x21192, 0x20190, 0x20190,
        0x20190, 0x21191, 0x11c0, 0x11c0, 0x11c0, 0x11c0, 0x11c0, 0x11c0,
        0x11c0, 0x11c0, 0x11c0, 0x11c0, 0x1c0, 0x11190, 0x9048, 0x9048,
        0x9048, 0x9048, 0x9048, 0x9048, 0x9048, 0x9048, 0x9048, 0x9048,
        0x9048, 0x9048, 0x9048, 0x9048, 0x9048, 0x11c0, 0x1180, 0x1180,
        0x5048, 0x5048, 0x5048, 0x5048, 0x5048, 0x5048, 0x1188, 0x5048,
        0x5048, 0x5048, 0x5048, 0x5048, 0x1048, 0x104c, 0x1048, 0x40,
        0x11c8, 0x1048, 0x1048, 0x1048, 0x1048, 0x1048, 0x1048
    };
    void describe(Record& r, const Catalog::Decode& decode, bool legacy)
    {
        static const std::set<std::string> displayNameTypes={"NPC_","CREA","CLOT","ARMO","WEAP","ALCH","MISC","LIGH",
            "BOOK","INGR","APPA","LOCK","PROB","REPA","CONT","DOOR","ACTI","SPEL","RACE","REGN","BSGN","CLAS","FACT"};
        for (const auto& field : std::vector<std::pair<std::string,std::string>>{
            {"FNAM","name"},{"MODL","model"},{"ITEX","icon"},{"SCRI","script"},
            {"ENAM","enchantment"}})
        {
            if (field.first=="FNAM" && !displayNameTypes.count(r.type)) continue;
            // ENAM is binary for spell/enchantment records, and a string only on items.
            if (field.first=="ENAM" && r.type!="CLOT" && r.type!="ARMO" && r.type!="WEAP" && r.type!="BOOK") continue;
            if (first(r,field.first)) r.text[field.second]=text(r,field.first,decode);
        }
        if (r.type=="NPC_")
        {
            for (const auto& field : std::vector<std::pair<std::string,std::string>>{
                {"RNAM","race"},{"CNAM","class"},{"ANAM","faction"},{"BNAM","head"},{"KNAM","hair"}})
                r.text[field.second]=text(r,field.first,decode);
            if (const auto* p=first(r,"NPDT"))
            {
                if (p->size()!=12 && p->size()!=52) throw std::runtime_error("invalid NPC NPDT size");
                const unsigned int level=u16(*p,0);
                r.data["level"]=level<32768 ? int(level) : int(level)-65536;
                r.data["gold"]=i32(*p,p->size()==52 ? 48 : 8);
                r.data["autoCalculated"]=p->size()==12 ? 1 : 0;
                if (p->size()==52)
                {
                    r.data["health"]=u16(*p,38);r.data["magicka"]=u16(*p,40);r.data["fatigue"]=u16(*p,42);
                    const char* attributes[]={"strength","intelligence","willpower","agility","speed","endurance","personality","luck"};
                    for (unsigned int i=0;i<8;++i) r.data[attributes[i]]=static_cast<unsigned char>((*p)[2+i]);
                    r.data["mercantile"]=static_cast<unsigned char>((*p)[34]);
                    r.data["enchant"]=static_cast<unsigned char>((*p)[19]);
                }
            }
            if (const auto* ai=first(r,"AIDT"))
                if (ai->size()>=12) r.data["services"]=u32(*ai,8);
        }
        if (r.type=="CELL")
        {
            r.text["region"]=text(r,"RGNN",decode);
            const auto* p=first(r,"DATA");requireSize(p,12,"CELL DATA");
            if (p) {r.data["flags"]=u32(*p);r.data["gridX"]=i32(*p,4);r.data["gridY"]=i32(*p,8);}
        }
        if (r.type=="MGEF")
        {
            const auto* p=first(r,"MEDT");requireSize(p,36,"MEDT");
            if (p)
            {
                unsigned int flags=u32(*p,8);
                const auto* index=first(r,"INDX");
                const int id=index ? i32(*index) : -1;
                if (legacy)
                {
                    flags &= 0x200u | 0x400u | 0x800u;
                    if (id>=0 && id<143) flags |= legacyEffectFlags[id];
                }
                r.data["school"]=i32(*p);r.data["baseCost"]=f32(*p,4);r.data["effectFlags"]=flags;
            }
        }
        if (r.type=="CREA")
            if (const auto* p=first(r,"NPDT"))
                if (p->size()>=96) {r.data["soul"]=i32(*p,52);r.data["gold"]=i32(*p,92);}
        if (r.type=="CREA")
            if (const auto* ai=first(r,"AIDT"))
                if (ai->size()>=12) r.data["services"]=u32(*ai,8);
        if (r.type=="NPC_" || r.type=="CREA" || r.type=="CONT")
            for (const auto& s : r.subrecords) if (s.tag=="NPCO")
            {
                if (s.bytes.size()!=36) throw std::runtime_error("invalid NPCO size");
                r.inventory.push_back({decode(zstring(s.bytes.substr(4,32))),i32(s.bytes)});
            }
        if (r.type=="CLOT")
        {
            const auto* p=first(r,"CTDT"); requireSize(p,12,"CTDT");
            if (p)
            {
                const int type=i32(*p); r.data["type"]=type; r.data["weight"]=f32(*p,4);
                r.data["value"]=u16(*p,8); r.data["enchantmentPoints"]=u16(*p,10)/10.0;
                const std::vector<std::vector<int>> slots={{9},{7},{8},{15},{11},{6},{5},{10},{12,13},{14}};
                if (type>=0 && type<int(slots.size())) r.slots=slots[type];
            }
        }
        else if (r.type=="ARMO")
        {
            const auto* p=first(r,"AODT"); requireSize(p,24,"AODT");
            if (p)
            {
                const int type=i32(*p); r.data["type"]=type; r.data["weight"]=f32(*p,4);
                r.data["value"]=i32(*p,8); r.data["health"]=i32(*p,12);
                r.data["enchantmentPoints"]=i32(*p,16)/10.0; r.data["armorRating"]=i32(*p,20);
                const int slots[]={0,1,3,4,2,7,5,6,17,5,6};
                if (type>=0 && type<11) r.slots={slots[type]};
            }
        }
        else if (r.type=="WEAP")
        {
            const auto* p=first(r,"WPDT"); requireSize(p,32,"WPDT");
            if (p)
            {
                r.data["weight"]=f32(*p); r.data["value"]=i32(*p,4); const int type=u16(*p,8);
                r.data["type"]=type; r.data["health"]=u16(*p,10); r.data["speed"]=f32(*p,12);
                r.data["reach"]=f32(*p,16); r.data["enchantmentPoints"]=u16(*p,20)/10.0;
                r.data["flags"]=u32(*p,28); r.slots={type==12 || type==13 ? 18 : 16};
            }
        }
        else if (r.type=="RACE")
        {
            const auto* p=first(r,"RADT"); requireSize(p,140,"RADT");
            if (p) {r.data["flags"]=u32(*p,136);r.data["beast"]=(u32(*p,136)&2) ? 1 : 0;}
        }
        else if (r.type=="SPEL")
        {
            const auto* p=first(r,"SPDT");requireSize(p,12,"SPDT");
            if (p) {r.data["type"]=i32(*p);r.data["cost"]=i32(*p,4);r.data["flags"]=u32(*p,8);}
        }
        else if (r.type=="ENCH")
        {
            const auto* p=first(r,"ENDT");requireSize(p,16,"ENDT");
            if (p) {r.data["type"]=i32(*p);r.data["cost"]=i32(*p,4);r.data["charge"]=i32(*p,8);r.data["flags"]=u32(*p,12);}
        }
        else if (r.type=="ALCH")
        {
            const auto* p=first(r,"ALDT");requireSize(p,12,"ALDT");
            if (p) {r.data["weight"]=f32(*p);r.data["value"]=i32(*p,4);r.data["flags"]=u32(*p,8);}
        }
        else if (r.type=="BOOK")
        {
            const auto* p=first(r,"BKDT");requireSize(p,20,"BKDT");
            if (p) {r.data["weight"]=f32(*p);r.data["value"]=i32(*p,4);r.data["scroll"]=i32(*p,8);r.data["enchantmentPoints"]=i32(*p,16)/10.0;}
        }
        else if (r.type=="MISC" || r.type=="INGR" || r.type=="LIGH" || r.type=="APPA"
            || r.type=="LOCK" || r.type=="PROB" || r.type=="REPA")
        {
            const std::map<std::string,std::string> tags={{"MISC","MCDT"},{"INGR","IRDT"},{"LIGH","LHDT"},
                {"APPA","AADT"},{"LOCK","LKDT"},{"PROB","PBDT"},{"REPA","RIDT"}};
            if (const auto* p=first(r,tags.at(r.type)))
            {
                if (p->size()<8) throw std::runtime_error("truncated item data");
                r.data["weight"]=f32(*p,r.type=="APPA" ? 8 : 0);
                r.data["value"]=i32(*p,r.type=="APPA" ? 12 : 4);
            }
        }
        else if (r.type=="GMST" || r.type=="GLOB")
        {
            if (const auto* p=first(r,"INTV")) r.data["value"]=i32(*p);
            if (const auto* p=first(r,"FLTV")) r.data["value"]=f32(*p);
            if (first(r,"STRV")) r.text["value"]=text(r,"STRV",decode);
        }
        if (r.type=="NPC_" || r.type=="CREA" || r.type=="RACE")
            for (const auto& s : r.subrecords) if (s.tag=="NPCS") r.spells.push_back(decode(zstring(s.bytes)));
        if (r.type=="SPEL" || r.type=="ENCH" || r.type=="ALCH")
            for (const auto& s : r.subrecords) if (s.tag=="ENAM")
            {
                if (s.bytes.size()!=24) throw std::runtime_error("invalid effect ENAM size");
                const auto signedByte=[](unsigned char v) {return v<128 ? int(v) : int(v)-256;};
                r.effects.push_back({{"id",u16(s.bytes,0)},{"skill",signedByte(s.bytes[2])},
                    {"attribute",signedByte(s.bytes[3])},{"range",i32(s.bytes,4)},{"area",i32(s.bytes,8)},
                    {"duration",i32(s.bytes,12)},{"magnitudeMin",i32(s.bytes,16)},{"magnitudeMax",i32(s.bytes,20)}});
            }
    }
}

std::string Catalog::fold(std::string s)
{
    for (std::size_t i=0;i<s.size();++i)
    {
        auto c=static_cast<unsigned char>(s[i]);
        if (c>='A' && c<='Z') s[i]+=32;
        // Match Russian identifiers/cell names after decoding the plugin's code page.
        else if (c==0xd0 && i+1<s.size())
        {
            auto next=static_cast<unsigned char>(s[i+1]);
            if (next>=0x90 && next<=0x9f) s[++i]=static_cast<char>(next+32);
            else if (next>=0xa0 && next<=0xaf) {s[i]=static_cast<char>(0xd1);s[++i]=static_cast<char>(next-32);}
            else if (next==0x81) {s[i]=static_cast<char>(0xd1);s[++i]=static_cast<char>(0x91);}
        }
    }
    return s;
}

std::string Catalog::quote(const std::string& text)
{
    std::ostringstream out;out<<'"';
    for (unsigned char c : text)
    {
        if (c=='"' || c=='\\') out<<'\\'<<c;
        else if (c<32) out<<"\\u00"<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(c)<<std::dec;
        else out<<c;
    }
    out<<'"';return out.str();
}

void Catalog::load(const std::vector<std::string>& paths, const std::string& encoding,
    const Decode& decode, const Digest& digest, const std::string& expectedKey)
{
    if (paths.empty() || paths.size()>1024 || !decode || !digest) throw std::runtime_error("invalid content file list");
    Catalog staged;staged.mEncoding=encoding;
    std::set<std::string> seen;
    std::size_t totalBytes=0;
    std::string manifest="ArenaMP.ContentCatalog/1\n"+encoding+"\n";
    for (const auto& path : paths)
    {
#if defined(__ANDROID__)
        // Y103: NDK r21 has no linked implementation of path::__filename and
        // path::__extension. POSIX paths are UTF-8 bytes; only the basename and
        // suffix are needed here, so preserve those bytes without filesystem.
        const auto separator=path.find_last_of('/');
        const std::string name=path.substr(separator==std::string::npos ? 0 : separator+1);
        const auto dot=name.find_last_of('.');
        const std::string extension=dot==std::string::npos || dot==0 ? "" : fold(name.substr(dot));
        const auto& filePath=path;
#else
        const auto filePath=std::filesystem::u8path(path);
        const std::string name=filePath.filename().u8string();
        const std::string extension=fold(filePath.extension().u8string());
#endif
        if (extension!=".esm" && extension!=".esp" && extension!=".omwaddon" && extension!=".omwgame") throw std::runtime_error("expected TES3-compatible content file: "+path);
        const std::string foldedName=fold(name);
        if (seen.count(foldedName)) throw std::runtime_error("duplicate plugin filename: "+name);
        std::ifstream stream(filePath,std::ios::binary|std::ios::ate);
        if (!stream) throw std::runtime_error("cannot open content file: "+path);
        const auto end=stream.tellg();
        if (end<16 || std::uint64_t(end)>MaxFileBytes) throw std::runtime_error("invalid/oversized content file: "+name);
        const auto length=static_cast<std::size_t>(end);
        if (length>MaxCatalogBytes-totalBytes) throw std::runtime_error("content catalog exceeds byte limit");
        totalBytes+=length;
        std::string bytes(length,'\0');stream.seekg(0);
        if (!stream.read(&bytes[0],static_cast<std::streamsize>(length))) throw std::runtime_error("short plugin read: "+name);
        Plugin plugin;plugin.name=name;plugin.sha256=digest(bytes);plugin.sizeBytes=length;
        std::size_t position=0;bool header=true;std::string dialogue;
        while (position<bytes.size())
        {
            if (bytes.size()-position<16) throw std::runtime_error("truncated record header: "+name);
            Record record;record.type=bytes.substr(position,4);record.source=name;
            record.pluginIndex=static_cast<unsigned int>(staged.mPlugins.size());record.flags=u32(bytes,position+12);
            const auto size=u32(bytes,position+4);position+=16;
            if (!tagValid(record.type) || size>MaxRecordBytes || size>bytes.size()-position)
                throw std::runtime_error("invalid record boundary: "+name);
            const auto recordEnd=position+size;
            while (position<recordEnd)
            {
                if (recordEnd-position<8) throw std::runtime_error("truncated subrecord header: "+name);
                const auto tag=bytes.substr(position,4);const auto subSize=u32(bytes,position+4);position+=8;
                if (!tagValid(tag) || subSize>recordEnd-position) throw std::runtime_error("invalid subrecord boundary: "+name);
                if (record.subrecords.size()>=65536) throw std::runtime_error("too many subrecords in one record: "+name);
                record.subrecords.push_back({tag,bytes.substr(position,subSize)});position+=subSize;
            }
            if (header)
            {
                const auto* hedr=first(record,"HEDR");
                if (record.type!="TES3" || !hedr || hedr->size()!=300) throw std::runtime_error("not a TES3 plugin: "+name);
                if (const auto* format=first(record,"FORM")) {requireSize(format,4,"FORM");plugin.format=i32(*format);if (plugin.format<0) throw std::runtime_error("invalid FORM");}
                plugin.version=f32(*hedr);plugin.fileType=u32(*hedr,4);plugin.declaredRecords=u32(*hedr,296);
                plugin.author=decode(zstring(hedr->substr(8,32)));plugin.description=decode(zstring(hedr->substr(40,256)));
                for (const auto& s : record.subrecords) if (s.tag=="MAST")
                {
                    const auto master=decode(zstring(s.bytes));
                    if (!seen.count(fold(master))) throw std::runtime_error("missing/out-of-order master "+master+" for "+name);
                    plugin.masters.push_back(master);
                }
                header=false;continue;
            }
            if (record.type=="TES3") throw std::runtime_error("duplicate TES3 header: "+name);
            record.id=text(record,"NAME",decode);
            if (record.type=="INFO") {record.id=text(record,"INAM",decode);record.parent=dialogue;}
            if (record.type=="SCPT") if (const auto* schd=first(record,"SCHD")) record.id=decode(zstring(schd->substr(0,32)));
            if ((record.type=="CELL" && record.id.empty()) || record.type=="LAND")
            {
                const auto* p=first(record,record.type=="LAND" ? "INTV" : "DATA");
                const std::size_t offset=record.type=="LAND" ? 0 : 4;
                if (p && p->size()>=offset+8) record.id=std::to_string(i32(*p,offset))+", "+std::to_string(i32(*p,offset+4));
            }
            if (record.id.empty() && (record.type=="SKIL" || record.type=="MGEF"))
                if (const auto* p=first(record,"INDX")) record.id=std::to_string(i32(*p));
            if (record.type=="DIAL") dialogue=record.id;
            if (record.id.empty()) {++staged.mUnindexed;continue;}
            if (record.id.size()>1024) throw std::runtime_error("oversized record identifier: "+name);
            record.deleted=false;
            for (const auto& sub : record.subrecords)
            {
                if (record.type=="CELL" && (sub.tag=="FRMR" || sub.tag=="MVRF")) break;
                if (sub.tag=="DELE") record.deleted=true;
            }
            if (!record.deleted) describe(record,decode,plugin.format==0);
            if (record.type=="CELL")
            {
                // Y107: CELL header overrides and per-reference overrides are
                // separate. A deleted reference never deletes its entire cell.
                const auto cellKey=fold(record.id);
                const auto previous=staged.mRecords.find({"CELL",cellKey});
                if (!first(record,"RGNN") && previous!=staged.mRecords.end())
                    record.text["region"]=previous->second.text["region"];
                auto& refs=staged.mCellReferences[cellKey];
                CellReference* ref=nullptr;
                for (const auto& sub : record.subrecords)
                {
                    if (sub.tag=="FRMR")
                    {
                        requireSize(&sub.bytes,4,"FRMR");const auto raw=u32(sub.bytes);
                        unsigned int owner=static_cast<unsigned int>(staged.mPlugins.size());
                        const unsigned int local=raw>>24;
                        if (local && local<=plugin.masters.size())
                            for (unsigned int i=0;i<staged.mPlugins.size();++i)
                                if (fold(staged.mPlugins[i].name)==fold(plugin.masters[local-1])) {owner=i;break;}
                        const auto key=(std::uint64_t(owner)<<32)|(raw&0xffffffu);
                        if (!refs.count(key) && refs.size()>=65536) throw std::runtime_error("too many references in cell: "+record.id);
                        ref=&refs[key];
                        ref->number=raw&0xffffffu;ref->deleted=false;
                    }
                    else if (sub.tag=="MVRF") ref=nullptr;
                    else if (ref)
                    {
                        if (sub.tag=="NAME") ref->refId=decode(zstring(sub.bytes));
                        else if (sub.tag=="ANAM") ref->owner=decode(zstring(sub.bytes));
                        else if (sub.tag=="CNAM") ref->faction=decode(zstring(sub.bytes));
                        else if (sub.tag=="DELE") ref->deleted=true;
                        else if (sub.tag=="DNAM") ref->destination=decode(zstring(sub.bytes));
                        else if (sub.tag=="DATA" && sub.bytes.size()==24)
                        {ref->x=f32(sub.bytes);ref->y=f32(sub.bytes,4);ref->z=f32(sub.bytes,8);ref->positioned=true;}
                        else if (sub.tag=="DODT" && sub.bytes.size()==24)
                        {ref->destinationX=f32(sub.bytes);ref->destinationY=f32(sub.bytes,4);ref->teleport=true;}
                    }
                }
                if (record.deleted) refs.clear();
            }
            staged.mRecords[{record.type,fold(record.id)}]=std::move(record);
            if (staged.mRecords.size()>2000000) throw std::runtime_error("too many content records");
        }
        seen.insert(foldedName);staged.mPlugins.push_back(plugin);
        manifest+=std::to_string(foldedName.size())+":"+foldedName+":"+plugin.sha256+"\n";
    }
    staged.mKey=digest(manifest);
    if (!expectedKey.empty() && staged.mKey!=expectedKey) throw std::runtime_error("content key does not match pinned expectedKey");
    // Infer interior regions from actual door links, without town, province or
    // plugin-name lists. Direct declarations always win; BFS is deterministic.
    std::map<std::string,std::string> exterior,regions;
    std::set<std::string> activeCells;
    std::map<std::string,std::vector<std::string>> links;
    for (const auto& entry : staged.mRecords)
        if (entry.second.type=="CELL" && !entry.second.deleted)
        {
            const auto& r=entry.second;const auto key=fold(r.id);activeCells.insert(key);
            const auto flags=r.data.find("flags");
            if (flags!=r.data.end() && (static_cast<unsigned int>(flags->second)&1u)==0)
                exterior[std::to_string(static_cast<int>(r.data.at("gridX")))+","+std::to_string(static_cast<int>(r.data.at("gridY")))]=key;
            const auto region=r.text.find("region");
            if (region!=r.text.end() && !region->second.empty()) regions[key]=region->second;
        }
    for (const auto& cell : staged.mCellReferences)
        for (const auto& ref : cell.second)
            if (activeCells.count(cell.first) && !ref.second.deleted && ref.second.teleport)
            {
                std::string destination=fold(ref.second.destination);
                if (destination.empty())
                {
                    const double bound=double(std::numeric_limits<int>::max())*8192.0;
                    if (std::abs(ref.second.destinationX)>bound || std::abs(ref.second.destinationY)>bound) continue;
                    const auto coordinate=std::to_string(static_cast<int>(std::floor(ref.second.destinationX/8192.0)))+","
                        +std::to_string(static_cast<int>(std::floor(ref.second.destinationY/8192.0)));
                    const auto found=exterior.find(coordinate);if (found!=exterior.end()) destination=found->second;
                }
                if (activeCells.count(destination)) {links[cell.first].push_back(destination);links[destination].push_back(cell.first);}
            }
    std::deque<std::string> pending;
    for (const auto& region : regions) pending.push_back(region.first);
    while (!pending.empty())
    {
        const auto cell=pending.front();pending.pop_front();
        auto& neighbours=links[cell];std::sort(neighbours.begin(),neighbours.end());
        for (const auto& other : neighbours)
            if (!regions.count(other)) {regions[other]=regions[cell];pending.push_back(other);}
    }
    for (auto& entry : staged.mRecords)
        if (entry.second.type=="CELL" && regions.count(entry.first.second))
            entry.second.text["economyRegion"]=regions[entry.first.second];
    for (const auto& entry : staged.mRecords)
        if (!entry.second.deleted) { ++staged.mCounts[entry.second.type]; ++staged.mCount; }
    *this=std::move(staged);
}

const Record* Catalog::find(const std::string& type,const std::string& id) const
{
    const auto it=mRecords.find({upper(type),fold(id)});return it==mRecords.end() ? nullptr : &it->second;
}

std::vector<std::string> Catalog::ids(const std::string& type,const std::string& prefix,unsigned int offset,unsigned int limit) const
{
    std::vector<std::string> result;const auto t=upper(type),p=fold(prefix);limit=std::min(limit,1000u);
    if (!limit) return result;
    auto it=t.empty() ? mRecords.begin() : mRecords.lower_bound({t,p});
    for (;it!=mRecords.end();++it)
    {
        const auto& r=it->second;
        if (!t.empty() && (r.type!=t || it->first.second.compare(0,p.size(),p)!=0)) break;
        if (r.deleted || it->first.second.compare(0,p.size(),p)!=0) continue;
        if (offset) {--offset;continue;}
        result.push_back(r.id);
        if (result.size()>=limit) break;
    }
    return result;
}

std::size_t Catalog::count(const std::string& type) const
{
    if (type.empty()) return mCount;
    const auto it=mCounts.find(upper(type));
    return it==mCounts.end() ? 0 : it->second;
}

std::string Catalog::referenceJson(const std::string& cell,unsigned int number,const std::string& refId) const
{
    const auto found=mCellReferences.find(fold(cell));if (found==mCellReferences.end()) return "null";
    const CellReference* match=nullptr;
    for (const auto& entry : found->second)
        if (entry.second.number==(number&0xffffffu) && fold(entry.second.refId)==fold(refId) && !entry.second.deleted)
        {
            if (match) return "null"; // protocol refNum alone cannot disambiguate plugins
            match=&entry.second;
        }
    if (!match) return "null";
    std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(17);
    out<<"{\"refId\":"<<quote(match->refId)<<",\"owner\":"<<quote(match->owner)
        <<",\"faction\":"<<quote(match->faction)<<",\"positioned\":"<<(match->positioned ? "true" : "false")
        <<",\"x\":"<<match->x<<",\"y\":"<<match->y<<",\"z\":"<<match->z<<'}';
    return out.str();
}

std::string Catalog::recordJson(const std::string& type,const std::string& id) const
{
    const auto* r=find(type,id);if (!r) return "null";
    std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(17);
    out<<"{\"schema\":1,\"type\":"<<quote(r->type)<<",\"id\":"<<quote(r->id)
       <<",\"contentKey\":"<<quote(mKey)<<",\"source\":"<<quote(r->source)<<",\"pluginIndex\":"<<r->pluginIndex
       <<",\"flags\":"<<r->flags<<",\"deleted\":"<<(r->deleted ? "true" : "false")
       <<",\"parent\":"<<quote(r->parent)<<",\"resolution\":"
       <<quote(r->type=="CELL" ? "last_record_without_reference_merge" : "last_record");
    for (const auto& f : r->text) out<<','<<quote(f.first)<<':'<<quote(f.second);
    out<<",\"data\":{";bool comma=false;
    for (const auto& f : r->data) {if (comma) out<<',';comma=true;out<<quote(f.first)<<':'<<f.second;}
    out<<"},\"inventory\":[";comma=false;
    for (const auto& item : r->inventory)
    {if (comma) out<<',';comma=true;out<<"{\"refId\":"<<quote(item.refId)<<",\"count\":"<<item.count<<'}';}
    out<<"],\"slots\":[";comma=false;
    for (int slot : r->slots) {if (comma) out<<',';comma=true;out<<slot;}
    out<<"],\"spells\":[";comma=false;
    for (const auto& spell : r->spells) {if (comma) out<<',';comma=true;out<<quote(spell);}
    out<<"],\"effects\":[";comma=false;
    for (const auto& effect : r->effects)
    {
        if (comma) out<<',';
        comma=true;out<<'{';bool fieldComma=false;
        for (const auto& field : effect) {if (fieldComma) out<<',';fieldComma=true;out<<quote(field.first)<<':'<<field.second;}
        out<<'}';
    }
    out<<"],\"subrecords\":[";comma=false;
    for (const auto& sub : r->subrecords)
    {if (comma) out<<',';comma=true;out<<"{\"tag\":"<<quote(sub.tag)<<",\"size\":"<<sub.bytes.size()<<'}';}
    out<<"]}";return out.str();
}

std::string Catalog::infoJson() const
{
    std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(17);
    out<<"{\"schema\":1,\"contentKey\":"<<quote(mKey)<<",\"encoding\":"<<quote(mEncoding)
        <<",\"records\":"<<count()<<",\"unindexed\":"<<mUnindexed<<",\"plugins\":[";bool comma=false;
    for (const auto& p : mPlugins)
    {
        if (comma) out<<',';
        comma=true;out<<"{\"name\":"<<quote(p.name)<<",\"sha256\":"<<quote(p.sha256)
            <<",\"author\":"<<quote(p.author)<<",\"description\":"<<quote(p.description)
            <<",\"version\":"<<p.version<<",\"fileType\":"<<p.fileType<<",\"declaredRecords\":"<<p.declaredRecords
            <<",\"sizeBytes\":"<<p.sizeBytes<<",\"masters\":[";
        bool masterComma=false;for (const auto& m : p.masters) {if (masterComma) out<<',';masterComma=true;out<<quote(m);}
        out<<"]}";
    }
    out<<"]}";return out.str();
}

std::string Catalog::subrecord(const std::string& type,const std::string& id,const std::string& tag,
    unsigned int occurrence,const std::string& format,const Decode& decode) const
{
    const auto* r=find(type,id);if (!r) throw std::runtime_error("record_not_found");
    for (const auto& sub : r->subrecords) if (sub.tag==upper(tag))
    {
        if (occurrence) {--occurrence;continue;}
        if (sub.bytes.size()>1024u*1024u) throw std::runtime_error("subrecord_response_too_large");
        if (format=="text") return decode(zstring(sub.bytes));
        if (format=="hex")
        {
            std::string out;out.reserve(sub.bytes.size()*2);const char* hex="0123456789abcdef";
            for (unsigned char c : sub.bytes) {out+=hex[c>>4];out+=hex[c&15];}return out;
        }
        if (sub.bytes.size()!=4) throw std::runtime_error("scalar_requires_four_bytes");
        std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(17);
        if (format=="uint32") out<<u32(sub.bytes);
        else if (format=="int32") out<<i32(sub.bytes);
        else if (format=="float32") out<<f32(sub.bytes);
        else throw std::runtime_error("unsupported_subrecord_format");
        return out.str();
    }
    throw std::runtime_error("subrecord_not_found");
}

std::string Catalog::subrecordChunk(const std::string& type,const std::string& id,const std::string& tag,
    unsigned int occurrence,unsigned int offset,unsigned int limit) const
{
    if (!limit || limit>65536) throw std::runtime_error("invalid_chunk_limit");
    const auto* r=find(type,id);if (!r) throw std::runtime_error("record_not_found");
    for (const auto& sub : r->subrecords) if (sub.tag==upper(tag))
    {
        if (occurrence) {--occurrence;continue;}
        if (offset>sub.bytes.size()) throw std::runtime_error("chunk_offset_out_of_range");
        const auto length=std::min<std::size_t>(limit,sub.bytes.size()-offset);
        const char* alphabet="0123456789abcdef";std::string hex;hex.reserve(length*2);
        for (std::size_t i=offset;i<offset+length;++i) {const auto c=static_cast<unsigned char>(sub.bytes[i]);hex+=alphabet[c>>4];hex+=alphabet[c&15];}
        return "{\"offset\":"+std::to_string(offset)+",\"nextOffset\":"+std::to_string(offset+length)
            +",\"totalSize\":"+std::to_string(sub.bytes.size())+",\"done\":"+(offset+length==sub.bytes.size() ? "true" : "false")
            +",\"hex\":"+quote(hex)+"}";
    }
    throw std::runtime_error("subrecord_not_found");
}
}
