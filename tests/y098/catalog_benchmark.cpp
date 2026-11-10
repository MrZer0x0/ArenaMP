// Reproducible headless lookup benchmark; this is not a game/network latency test.
#include "ContentCatalog.hpp"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
static std::string le(unsigned n){std::string s;for(int i=0;i<4;++i)s+=char(n>>(i*8));return s;}
static std::string sub(const std::string& tag,const std::string& bytes){return tag+le(bytes.size())+bytes;}
static std::string record(const std::string& tag,const std::string& bytes){return tag+le(bytes.size())+le(0)+le(0)+bytes;}
int main(){
namespace fs=std::filesystem;using Clock=std::chrono::steady_clock;
const auto path=fs::temp_directory_path()/("arenamp-bench-"+std::to_string(Clock::now().time_since_epoch().count())+".esm");
std::string bytes=record("TES3",sub("HEDR",std::string(300,'\0')));
for(int i=0;i<40000;++i){std::ostringstream id;id<<"item_"<<std::setw(5)<<std::setfill('0')<<i;bytes+=record("MISC",sub("NAME",id.str()+'\0'));}
for(int i=0;i<1000;++i){std::ostringstream id;id<<"npc_"<<std::setw(4)<<std::setfill('0')<<i;bytes+=record("NPC_",sub("NAME",id.str()+'\0'));}
{std::ofstream out(path,std::ios::binary);out.write(bytes.data(),bytes.size());}
ArenaContent::Catalog c;c.load({path.u8string()},"utf8",[](const std::string& s){return s;},[](const std::string&){return std::string(64,'0');});
fs::remove(path);assert(c.count()==41000 && c.count("NPC_")==1000);
volatile std::size_t sum=0;const auto start=Clock::now();
for(int i=0;i<2000;++i){sum+=c.count("NPC_");sum+=c.ids("NPC_","npc_099",0,10).size();}
assert(sum==2020000);std::cout<<std::chrono::duration<double,std::milli>(Clock::now()-start).count()<<" ms: 2000 count + 2000 prefix queries on 41000 records\n";
}
