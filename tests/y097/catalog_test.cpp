#include "apps/openmw-mp/content/ContentCatalog.hpp"
#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

using ArenaContent::Catalog;
namespace fs=std::filesystem;
static int checks=0;
static void check(bool condition,const char* label) {if(!condition)throw std::runtime_error(label);++checks;}
static std::string le(unsigned int value) {std::string s;for(int i=0;i<4;++i)s+=char(value>>(8*i));return s;}
static std::string sub(const std::string& tag,const std::string& data) {return tag+le(data.size())+data;}
static std::string rec(const std::string& type,const std::string& data) {return type+le(data.size())+le(0)+le(0)+data;}
static std::string str(const std::string& s) {return s+'\0';}
static std::string header(const std::string& master="")
{
    std::string h(300,'\0');float version=1.3f;std::memcpy(&h[0],&version,4);
    return rec("TES3",sub("HEDR",h)+(master.empty() ? "" : sub("MAST",str(master))+sub("DATA",std::string(8,'\0'))));
}
static std::string named(const std::string& type,const std::string& id,const std::string& body="") {return rec(type,sub("NAME",str(id))+body);}
static std::string npco(const std::string& id,int count) {auto s=id;s.resize(32,'\0');return sub("NPCO",le(count)+s);}
static void write(const fs::path& path,const std::string& bytes) {std::ofstream out(path,std::ios::binary);out.write(bytes.data(),bytes.size());}
// Parser tests inject a deterministic fixture digest. Production uses PicoSHA2;
// this fixture callback deliberately does not claim to test a SHA-256 algorithm.
static std::string digest(const std::string& data)
{
    unsigned long long n=14695981039346656037ull;for(unsigned char c:data){n^=c;n*=1099511628211ull;}
    std::ostringstream out;out<<n;auto s=out.str();return std::string(64-s.size(),'0')+s;
}
int main()
{
    const auto directory=fs::temp_directory_path()/fs::path("arenamp-content-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(directory);
    try
    {
        const auto base=directory/"Base.esm",mod=directory/"Mod.esp",bad=directory/"Bad.esp";
        std::string ctdt=le(4)+le(0)+std::string("\x32\0\x64\0",4);
        std::string armor=le(1)+le(0)+le(100)+le(200)+le(20)+le(15);
        std::string weapon(32,'\0');weapon[8]=9;weapon[10]=100;
        const auto npc=named("NPC_","arrille",sub("FNAM",str("Арилл"))+sub("RNAM",str("high elf"))
            +npco("shirt",1)+npco("ring",2)+npco("restock",-3)+sub("NPDT",std::string(12,'\0')));
        const auto contents=header()+npc+named("CLOT","robe",sub("FNAM",str("Роба"))+sub("CTDT",ctdt)+sub("MODL","")+sub("ZZZZ",std::string("a\0b",3))+sub("ZZZZ","second"))
            +named("ARMO","armor",sub("AODT",armor))+named("WEAP","bow",sub("WPDT",weapon))
            +named("MISC","delete_me")+named("MISC","Цена")
            +rec("CELL",sub("NAME",str(""))+sub("DATA",le(0)+le(0xffffffffu)+le(2)))
            +rec("SKIL",sub("INDX",le(5)))
            +named("DIAL","topic")+rec("INFO",sub("INAM",str("info_1")))
            +rec("SCPT",sub("SCHD",std::string("script").append(46,'\0'))+sub("SCTX",str("begin script\nend")))
            +named("GMST","iExample",sub("INTV",le(123)))+named("ZZZZ","future_record",sub("ABCD","payload"))
            +named("SPEL","fire",sub("SPDT",le(0)+le(12)+le(1))+sub("ENAM",std::string("\x0e\0\xff\xff",4)+le(1)+le(5)+le(10)+le(3)+le(7)));
        write(base,contents);
        write(mod,header("Base.esm")+named("NPC_","ARRILLE",sub("FNAM",str("New name"))+npco("robe",1))
            +named("MISC","delete_me",sub("DELE",le(0))));
        Catalog catalog;auto decode=[](const std::string& s){return s;};
        catalog.load({base.u8string()},"utf8",decode,digest);
        check(catalog.find("npc_","ARRILLE")->inventory.size()==3,"NPC inventory decoded");
        check(catalog.find("NPC_","arrille")->inventory[2].count==-3,"negative restock count preserved");
        check(catalog.find("CLOT","robe")->slots==std::vector<int>{11},"robe slot");
        check(catalog.find("CLOT","robe")->data.at("value")==50,"clothing value");
        check(catalog.find("ARMO","armor")->slots==std::vector<int>{1},"armor slot");
        check(catalog.find("ARMO","armor")->data.at("health")==200,"armor health");
        check(catalog.find("WEAP","bow")->slots==std::vector<int>{16},"bow weapon slot");
        check(catalog.find("SPEL","fire")->effects.size()==1,"spell effects decoded");
        check(catalog.find("SPEL","fire")->effects[0].at("magnitudeMax")==7,"effect magnitude");
        check(catalog.find("SPEL","fire")->effects[0].at("skill")==-1,"signed effect skill sentinel");
        check(catalog.find("CELL","-1, 2")!=nullptr,"exterior cell coordinate key");
        check(catalog.find("SKIL","5")!=nullptr,"numeric record key");
        check(catalog.find("INFO","info_1")->parent=="topic","dialogue INFO parent");
        check(catalog.find("SCPT","script")!=nullptr,"script key from SCHD");
        check(catalog.find("MISC","ЦЕНА")!=nullptr,"Russian case-insensitive lookup");
        check(catalog.subrecord("CLOT","robe","MODL",0,"hex",decode).empty(),"empty subrecord preserved");
        check(catalog.subrecord("CLOT","robe","ZZZZ",0,"hex",decode)=="610062","binary bytes preserved");
        check(catalog.subrecord("CLOT","robe","ZZZZ",1,"text",decode)=="second","repeated subrecord index");
        check(catalog.subrecordChunk("CLOT","robe","ZZZZ",0,1,1)=="{\"offset\":1,\"nextOffset\":2,\"totalSize\":3,\"done\":false,\"hex\":\"00\"}","binary subrecord chunk");
        check(catalog.subrecordChunk("CLOT","robe","MODL",0,0,10)=="{\"offset\":0,\"nextOffset\":0,\"totalSize\":0,\"done\":true,\"hex\":\"\"}","empty final chunk");
        check(catalog.subrecord("GMST","iExample","INTV",0,"int32",decode)=="123","numeric subrecord");
        check(catalog.subrecord("ZZZZ","future_record","ABCD",0,"text",decode)=="payload","future record type retained");
        const auto initialKey=catalog.key();catalog.load({base.u8string(),mod.u8string()},"utf8",decode,digest);
        check(catalog.key()!=initialKey,"ordered plugin manifest changes content key");
        check(catalog.find("NPC_","arrille")->source=="Mod.esp","last plugin wins");
        check(catalog.find("NPC_","arrille")->inventory.size()==1,"override replaces entire inventory");
        check(catalog.find("MISC","delete_me")->deleted,"deletion tombstone retained");
        check(catalog.ids("MISC","delete",0,10).empty(),"deleted record excluded from search");
        check(catalog.ids("NPC_","arr",0,10)==std::vector<std::string>{"ARRILLE"},"prefix search");
        check(catalog.ids("NPC_","",1,10).empty(),"search offset");
        check(catalog.ids("NPC_","",0,0).empty(),"search zero limit");
        check(catalog.recordJson("NPC_","missing")=="null","record miss is null");
        const auto key=catalog.key();const auto count=catalog.count();
        auto reject=[&](const std::vector<std::string>& paths,const std::string& label,const std::string& pin="") {
            bool failed=false;try{catalog.load(paths,"utf8",decode,digest,pin);}catch(const std::exception&){failed=true;}
            check(failed,label.c_str());check(catalog.key()==key && catalog.count()==count,"failed reload preserves live catalog");
        };
        reject({mod.u8string(),base.u8string()},"out-of-order master rejected");
        reject({base.u8string(),base.u8string()},"duplicate file rejected");
        reject({(directory/"Missing.esm").u8string()},"missing file rejected");
        reject({base.u8string(),mod.u8string()},"wrong pinned content key rejected","bad");
        write(bad,header()+"NPC_"+le(0xffffffffu)+le(0)+le(0));reject({bad.u8string()},"oversized record rejected");
        write(bad,header()+rec("NPC_","NAME"+le(100)+"short"));reject({bad.u8string()},"truncated subrecord rejected");
        write(bad,"TES4"+contents.substr(4));reject({bad.u8string()},"TES4 rejected");
        write(bad,header()+named("CLOT","broken",sub("CTDT","x")));reject({bad.u8string()},"typed data wrong size rejected");
        write(bad,header()+"x");reject({bad.u8string()},"trailing garbage rejected");
        bool missing=false;try{catalog.subrecord("NPC_","arrille","MISS",0,"hex",decode);}catch(const std::exception&){missing=true;}
        check(missing,"missing subrecord signalled");
        check(Catalog::quote("a\n\"b")=="\"a\\u000a\\\"b\"","JSON escapes");
        std::cout<<catalog.recordJson("NPC_","arrille")<<'\n'<<catalog.recordJson("CLOT","robe")<<'\n'<<catalog.infoJson()<<'\n';
        std::cout<<"PASS ContentCatalog: "<<checks<<" checks\n";
        fs::remove_all(directory);
    }
    catch(...){fs::remove_all(directory);throw;}
}
