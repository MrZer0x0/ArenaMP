// No <filesystem> even in the harness: verifies the older libc++ link path.
#include "apps/openmw-mp/atomicfile.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <unistd.h>
#include <sys/stat.h>
#include <cerrno>
int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::string dir = std::string(argv[1]) + "/профиль-测试";
    assert(::mkdir(dir.c_str(),0700)==0);
    const std::string dest=dir+"/saved.json", temp=dir+"/saved.tmp";
    { std::ofstream out(dest);out<<"old"; }
    { std::ofstream out(temp);out<<"new"; }
    assert(ArenaFileOps::replace(temp,dest));
    assert(ArenaFileOps::status(dest)==1 && ArenaFileOps::status(temp)==0);
    assert(!ArenaFileOps::replace(temp,dest));
    std::string data;{std::ifstream in(dest);in>>data;}assert(data=="new");
    assert(!ArenaFileOps::replace(dest,dir+"/missing/saved.json"));
    assert(ArenaFileOps::status(dest)==1);
    assert(ArenaFileOps::status(dest+"/child")==0);
    assert(ArenaFileOps::status("")==-1);
    assert(ArenaFileOps::status(std::string("bad\0path",8))==-1);
    assert(!ArenaFileOps::replace(dest,std::string("bad\0path",8)));
    const std::string loop=dir+"/loop";
    assert(::symlink("loop",loop.c_str())==0);
    assert(ArenaFileOps::status(loop)==-1); // ELOOP is not 'missing'.
    assert(::unlink(loop.c_str())==0);
    assert(::unlink(dest.c_str())==0);
    assert(::rmdir(dir.c_str())==0);
    std::cout<<"PASS: POSIX UTF-8 replacement, preservation on failure, missing/error distinction; no filesystem runtime\n";
}
