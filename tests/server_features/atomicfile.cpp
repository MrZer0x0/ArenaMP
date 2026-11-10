#include "apps/openmw-mp/atomicfile.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
int main(int argc, char** argv)
{
    assert(argc == 2);
    const auto dir = std::filesystem::u8path(argv[1]) / std::filesystem::u8path("профиль-测试");
    std::filesystem::create_directories(dir);
    const auto saved = dir / "profile.json";
    const auto temp = dir / "profile.json.tmp";
    { std::ofstream out(saved); out << "old"; }
    { std::ofstream out(temp); out << "new"; }
    assert(ArenaFileOps::replace(temp.u8string(), saved.u8string()));
    std::string value;
    { std::ifstream in(saved); in >> value; }
    assert(value == "new");
    assert(ArenaFileOps::status(saved.u8string()) == 1);
    assert(ArenaFileOps::status(temp.u8string()) == 0);
    assert(!ArenaFileOps::replace(temp.u8string(), saved.u8string()));
    { std::ifstream in(saved); in >> value; }
    assert(value == "new");
    std::filesystem::remove_all(dir);
    std::cout << "PASS: UTF-8 atomic replacement, missing source preserves destination\n";
}
