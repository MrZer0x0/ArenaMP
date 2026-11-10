#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

int main(int argc, char** argv)
{
    if (argc != 3 || std::string(argv[1]) != "check") return 2;
    std::ifstream input(argv[2]);
    if (!input) return 3;
    const std::string request((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    // Let Qt show/process the progress dialog before the helper completes.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    if (request.find("both_available.ini") != std::string::npos)
    {
        std::puts("{\"phase\":\"available\",\"versions\":{\"version\":\"00002\",\"build\":\"00003\"}}");
        return 10;
    }
    if (request.find("exit_only.ini") != std::string::npos) return 10;
    if (request.find("offline.ini") != std::string::npos)
        std::puts("{\"phase\":\"offline\",\"message\":\"Test: check.ini unavailable\"}");
    if (request.find("cancel.ini") != std::string::npos)
        std::this_thread::sleep_for(std::chrono::seconds(10));
    return 0;
}
