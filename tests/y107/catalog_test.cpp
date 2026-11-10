#include "apps/openmw-mp/content/ContentCatalog.hpp"
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace fs=std::filesystem;
static std::string le(unsigned n){std::string s;for(int i=0;i<4;++i)s+=char(n>>(8*i));return s;}
static std::string fl(float n){unsigned v;std::memcpy(&v,&n,4);return le(v);}
static std::string sub(const std::string& t,const std::string& b){return t+le(b.size())+b;}
static std::string rec(const std::string& t,const std::string& b){return t+le(b.size())+le(0)+le(0)+b;}
static std::string named(const std::string& t,const std::string& id,const std::string& b=""){return rec(t,sub("NAME",id+'\0')+b);}
static std::string header(const std::string& master="",bool modern=false)
{
    std::string h(300,'\0');const auto v=fl(1.3f);h.replace(0,4,v);
    return rec("TES3",(modern?sub("FORM",le(1)):"")+sub("HEDR",h)+(master.empty()?"":sub("MAST",master+'\0')+sub("DATA",std::string(8,'\0'))));
}
static std::string cellData(unsigned flag,int x=0,int y=0){return sub("DATA",le(flag)+le(x)+le(y));}
static std::string reference(unsigned n,const std::string& id,const std::string& body=""){return sub("FRMR",le(n))+sub("NAME",id+'\0')+body;}
static void write(const fs::path& p,const std::string& s){std::ofstream f(p,std::ios::binary);f<<s;}
static void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
int main()
{
    const auto root=fs::temp_directory_path()/fs::path("arena-y107-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    try
    {
        const auto base=root/"AnyName.esm",mod=root/"Произвольный.esp",modern=root/"Modern.omwaddon";
        const auto exterior=named("CELL","",cellData(0,1,-2)+sub("RGNN",std::string("Регион из мода")+'\0')
            +reference(10,"door",sub("DNAM","Shop\0")+sub("DODT",std::string(24,'\0'))));
        const auto shop=named("CELL","Shop",cellData(1)+reference(20,"chest",sub("ANAM","trader\0")+sub("DATA",fl(10)+fl(20)+fl(30)+std::string(12,'\0')))
            +reference(21,"deleted-object",sub("ANAM","trader\0")));
        const auto nested=named("CELL","Nested",cellData(1)+reference(30,"door",sub("DNAM","Shop\0")+sub("DODT",std::string(24,'\0'))));
        std::string npc(52,'\0');npc[34]=71;npc[19]=65;npc.replace(48,4,le(123));
        const auto effect=rec("MGEF",sub("INDX",le(14))+sub("MEDT",le(0)+fl(3)+le(0x400)+std::string(24,'\0')));
        const auto misc=named("MISC","gem",sub("MCDT",fl(.1f)+le(45)+le(0)));
        const auto book=named("BOOK","scroll",sub("BKDT",fl(1)+le(30)+le(1)+le(0xffffffff)+le(60)));
        const auto appa=named("APPA","apparatus",sub("AADT",le(2)+fl(1.2f)+fl(3)+le(90)));
        std::string creature(96,'\0');creature.replace(52,4,le(400));creature.replace(92,4,le(55));
        write(base,header()+exterior+shop+nested+named("NPC_","trader",sub("NPDT",npc)+sub("AIDT",std::string(8,'\0')+le(0x10001)))
            +effect+misc+book+appa+named("CREA","soul",sub("NPDT",creature)));
        write(mod,header("AnyName.esm")+named("CELL","Shop",cellData(1)+reference(0x01000014,"chest")+reference(0x01000015,"deleted-object",sub("DELE",le(0)))));
        ArenaContent::Catalog c;const auto decode=[](const std::string& s){return s;};
        const auto digest=[](const std::string& s){return std::string(64,char('a'+s.size()%20));};
        c.load({base.u8string(),mod.u8string()},"utf8",decode,digest);
        check(c.find("CELL","Shop") && !c.find("CELL","Shop")->deleted,"DELE of reference must not delete cell");
        check(c.find("CELL","Shop")->text.at("economyRegion").find("Регион")!=std::string::npos,"door region inference");
        check(c.find("CELL","Nested")->text.at("economyRegion")==c.find("CELL","Shop")->text.at("economyRegion"),"nested interior inference");
        const auto ref=c.referenceJson("Shop",20,"chest");
        check(ref.find("\"owner\":\"trader\"")!=std::string::npos && ref.find("\"x\":10")!=std::string::npos,"reference override retains owner and position");
        check(c.referenceJson("Shop",21,"deleted-object")=="null","deleted reference excluded");
        check(c.find("NPC_","trader")->data.at("mercantile")==71 && c.find("NPC_","trader")->data.at("enchant")==65,"NPC skills");
        check(c.find("NPC_","trader")->data.at("services")==65537,"merchant services");
        check(c.find("CREA","soul")->data.at("soul")==400 && c.find("CREA","soul")->data.at("gold")==55,"creature metadata");
        check(c.find("BOOK","scroll")->data.at("scroll")==1 && c.find("BOOK","scroll")->data.at("enchantmentPoints")==6,"scroll metadata");
        check(c.find("MISC","gem")->data.at("value")==45 && c.find("APPA","apparatus")->data.at("value")==90,"item pricing data");
        check(c.find("MGEF","14")->data.at("effectFlags")==double(0x400|0x1f0),"legacy flags restored");
        write(modern,header("AnyName.esm",true)+rec("MGEF",sub("INDX",le(14))+sub("MEDT",le(3)+fl(4)+le(0x444)+std::string(24,'\0'))));
        c.load({base.u8string(),modern.u8string()},"utf8",decode,digest);
        check(c.find("MGEF","14")->data.at("effectFlags")==double(0x444),"modern FORM retains custom flags");
        fs::remove_all(root);
        std::cout<<"PASS economic plugin metadata: region doors, merged ownership, deletion, merchant/items/souls and FORM effect flags\n";
    }
    catch(...){fs::remove_all(root);throw;}
}
