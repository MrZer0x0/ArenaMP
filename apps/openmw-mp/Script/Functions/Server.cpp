#include "Server.hpp"
#include "../../atomicfile.hpp"

#include <components/misc/stringops.hpp>
#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include <components/openmw-mp/Version.hpp>

#include <apps/openmw-mp/Script/ScriptFunctions.hpp>
#include <apps/openmw-mp/Networking.hpp>
#include <apps/openmw-mp/MasterClient.hpp>
#include <Script/Script.hpp>

static std::string tempFilename;
static std::string fileContent, fileIoError;
static std::chrono::steady_clock::time_point startupTime = std::chrono::steady_clock::now();

void ServerFunctions::LogMessage(unsigned short level, const char *message) noexcept
{
    LOG_MESSAGE_SIMPLE(level, "[Script]: %s", message);
}

void ServerFunctions::LogAppend(unsigned short level, const char *message) noexcept
{
    LOG_APPEND(level, "[Script]: %s", message);
}

void ServerFunctions::StopServer(int code) noexcept
{
    mwmp::Networking::getPtr()->stopServer(code);
}

void ServerFunctions::Kick(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player,);

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Kicking player %s (%i)", player->npc.mName.c_str(), player->getId());
    mwmp::Networking::getPtr()->kickPlayer(player->guid);
    player->setLoadState(Player::KICKED);
}

void ServerFunctions::BanAddress(const char *ipAddress) noexcept
{
    mwmp::Networking::getPtr()->banAddress(ipAddress);
}

void ServerFunctions::UnbanAddress(const char *ipAddress) noexcept
{
    mwmp::Networking::getPtr()->unbanAddress(ipAddress);
}

bool ServerFunctions::ReplaceFileAtomic(const char* source, const char* destination) noexcept
{
    return source && destination && ArenaFileOps::replace(source, destination);
}

int ServerFunctions::GetFileStatusUtf8(const char* path) noexcept
{
    return path ? ArenaFileOps::status(path) : -1;
}

const char* ServerFunctions::ReadFileUtf8(const char* path) noexcept
{
    if (path && ArenaFileOps::read(path, fileContent)) { fileIoError.clear(); return fileContent.c_str(); }
    fileIoError = "file_read_failed"; return "";
}
bool ServerFunctions::WriteFileUtf8(const char* path, const char* content) noexcept
{
    const bool ok = path && content && ArenaFileOps::write(path, content);
    fileIoError = ok ? "" : "file_write_failed"; return ok;
}
const char* ServerFunctions::GetFileIoError() noexcept { return fileIoError.c_str(); }
bool ServerFunctions::RenameFileUtf8(const char* source, const char* destination) noexcept
{
    return source && destination && ArenaFileOps::renameNoReplace(source, destination);
}
bool ServerFunctions::RemoveFileUtf8(const char* path) noexcept
{
    return path && ArenaFileOps::remove(path);
}

bool ServerFunctions::DoesFilePathExist(const char *filePath) noexcept
{
    return filePath && ArenaFileOps::status(filePath) == 1;
}

// ArenaMP Y111: GetCaseInsensitiveFilename is called by the CoreScripts for every
// Cell() construction (cell/json.lua) and for account lookups. The previous
// implementation always enumerated the WHOLE folder and converted/lower-cased
// every name. data/cell grows with every visited cell, so on a populated world
// each new Cell() cost tens of milliseconds on Windows. A login triggers a
// restock burst of ~114 Container ADD packets into ~30 unloaded merchant cells,
// which blocked the single server thread for 3-5 seconds and froze activation
// round-trips for every other player. Resolve the exact name with one
// filesystem query first; the full scan is kept only as a rare fallback.
namespace
{
    bool isPlainFileName(const std::string& name) noexcept
    {
        return !name.empty() && name.find_first_of("/\\*?:<>|\"") == std::string::npos;
    }

#ifndef _WIN32
    bool joinedPathExists(const std::string& folder, const std::string& name) noexcept
    {
        std::string full = folder;
        if (!full.empty() && full.back() != '/') full += '/';
        full += name;
        return ArenaFileOps::status(full) == 1;
    }
#endif
}

const char *ServerFunctions::GetCaseInsensitiveFilename(const char *folderPath, const char *filename) noexcept
{
    try
    {
        if (!folderPath || !filename) return "invalid";
        const std::string requested(filename);
#ifdef _WIN32
        std::wstring wide;
        if (!ArenaFileOps::widePath(folderPath, wide)) return "invalid";
        const boost::filesystem::path folder(wide);
#else
        const boost::filesystem::path folder(folderPath);
#endif
        if (!boost::filesystem::is_directory(folder)) return "invalid";
        const auto wanted = Misc::StringUtils::lowerCaseUtf8(requested);

        if (isPlainFileName(requested))
        {
#ifdef _WIN32
            // NTFS lookups are case-insensitive. FindFirstFileW on an exact name
            // (no wildcards) is a single directory-index lookup and returns the
            // real on-disk spelling, which is exactly what the scan returned.
            std::wstring wideName;
            if (ArenaFileOps::widePath(requested, wideName))
            {
                std::wstring fullPath = wide;
                if (!fullPath.empty() && fullPath.back() != L'\\' && fullPath.back() != L'/')
                    fullPath += L'\\';
                fullPath += wideName;

                WIN32_FIND_DATAW found;
                HANDLE handle = FindFirstFileW(fullPath.c_str(), &found);
                if (handle != INVALID_HANDLE_VALUE)
                {
                    FindClose(handle);
                    const std::wstring name(found.cFileName);
                    const int size = WideCharToMultiByte(CP_UTF8, 0, name.data(), static_cast<int>(name.size()),
                        nullptr, 0, nullptr, nullptr);
                    if (size > 0)
                    {
                        std::string utf8(size, '\0');
                        WideCharToMultiByte(CP_UTF8, 0, name.data(), static_cast<int>(name.size()), &utf8[0], size,
                            nullptr, nullptr);
                        // Guard against 8.3 short-name or trailing-dot matches:
                        // only accept the same name, compared case-insensitively.
                        if (Misc::StringUtils::lowerCaseUtf8(utf8) == wanted)
                        {
                            tempFilename = utf8; return tempFilename.c_str();
                        }
                    }
                }
                else
                {
                    const DWORD error = GetLastError();
                    // The folder exists (checked above) and the case-insensitive
                    // lookup found nothing: the file does not exist. Skip the scan.
                    if (error == ERROR_FILE_NOT_FOUND)
                        return "invalid";
                }
            }
#else
            // Files written by the server already use the requested spelling.
            if (joinedPathExists(folderPath, requested))
            {
                tempFilename = requested; return tempFilename.c_str();
            }
#endif
        }

        // Fallback (unusual names, other filesystem errors, or a differently
        // cased file on a case-sensitive filesystem): the original full scan.
        for (boost::filesystem::directory_iterator itr(folder), end; itr != end; ++itr)
        {
#ifdef _WIN32
            const auto name = itr->path().filename().wstring();
            const int size = WideCharToMultiByte(CP_UTF8, 0, name.data(), static_cast<int>(name.size()), nullptr, 0, nullptr, nullptr);
            if (size <= 0) continue;
            std::string utf8(size, '\0');
            WideCharToMultiByte(CP_UTF8, 0, name.data(), static_cast<int>(name.size()), &utf8[0], size, nullptr, nullptr);
#else
            const auto utf8 = itr->path().filename().string();
#endif
            if (Misc::StringUtils::lowerCaseUtf8(utf8) == wanted)
            {
                tempFilename = utf8; return tempFilename.c_str();
            }
        }
    }
    catch (...) { /* noexcept: inaccessible directories cannot terminate the server. */ }
    return "invalid";
}

const char* ServerFunctions::GetDataPath() noexcept
{
    return Script::GetModDir();
}

unsigned int ServerFunctions::GetMillisecondsSinceServerStart() noexcept
{
    std::chrono::steady_clock::time_point currentTime = std::chrono::steady_clock::now();
    std::chrono::milliseconds milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startupTime);
    return milliseconds.count();
}

const char *ServerFunctions::GetOperatingSystemType() noexcept
{
    static const std::string operatingSystemType = Utils::getOperatingSystemType();
    return operatingSystemType.c_str();
}

const char *ServerFunctions::GetArchitectureType() noexcept
{
    static const std::string architectureType = Utils::getArchitectureType();
    return architectureType.c_str();
}

const char *ServerFunctions::GetServerVersion() noexcept
{
    return TES3MP_VERSION;
}

const char *ServerFunctions::GetProtocolVersion() noexcept
{
    static std::string version = std::to_string(TES3MP_PROTO_VERSION);
    return version.c_str();
}

int ServerFunctions::GetAvgPing(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, -1);
    return mwmp::Networking::get().getAvgPing(player->guid);
}

const char *ServerFunctions::GetIP(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, "");
    RakNet::SystemAddress addr = mwmp::Networking::getPtr()->getSystemAddress(player->guid);
    return addr.ToString(false);
}

unsigned short ServerFunctions::GetPort() noexcept
{
    return mwmp::Networking::get().getPort();
}

unsigned int ServerFunctions::GetMaxPlayers() noexcept
{
    return mwmp::Networking::get().maxConnections();
}

bool ServerFunctions::HasPassword() noexcept
{
    return mwmp::Networking::get().isPassworded();
}

bool ServerFunctions::GetDataFileEnforcementState() noexcept
{
    return mwmp::Networking::getPtr()->getDataFileEnforcementState();
}

bool ServerFunctions::GetScriptErrorIgnoringState() noexcept
{
    return mwmp::Networking::getPtr()->getScriptErrorIgnoringState();
}

void ServerFunctions::SetGameMode(const char *gameMode) noexcept
{
    if (mwmp::Networking::getPtr()->getMasterClient())
        mwmp::Networking::getPtr()->getMasterClient()->SetModname(gameMode);
}

void ServerFunctions::SetHostname(const char *name) noexcept
{
    if (mwmp::Networking::getPtr()->getMasterClient())
        mwmp::Networking::getPtr()->getMasterClient()->SetHostname(name);
}

void ServerFunctions::SetServerPassword(const char *password) noexcept
{
    mwmp::Networking::getPtr()->setServerPassword(password);
}

void ServerFunctions::SetStartLocation(const char *location) noexcept
{
    mwmp::Networking::getPtr()->setStartLocation(location == nullptr ? "default" : location);
}

void ServerFunctions::SetDataFileEnforcementState(bool state) noexcept
{
    mwmp::Networking::getPtr()->setDataFileEnforcementState(state);
}

void ServerFunctions::SetScriptErrorIgnoringState(bool state) noexcept
{
    mwmp::Networking::getPtr()->setScriptErrorIgnoringState(state);
}

void ServerFunctions::SetRuleString(const char *key, const char *value) noexcept
{
    auto mc = mwmp::Networking::getPtr()->getMasterClient();
    if (mc)
        mc->SetRuleString(key, value);
}

void ServerFunctions::SetRuleValue(const char *key, double value) noexcept
{
    auto mc = mwmp::Networking::getPtr()->getMasterClient();
    if (mc)
        mc->SetRuleValue(key, value);
}

void ServerFunctions::AddDataFileRequirement(const char *dataFilename, const char *checksumString) noexcept
{
    auto &samples = mwmp::Networking::getPtr()->getSamples();
    
    auto it = std::find_if(samples.begin(), samples.end(), [&dataFilename](mwmp::PacketPreInit::PluginPair &item) {
        return item.first == dataFilename;
    });

    if (it != samples.end())
    {
        // If this is a filename we've added before, ensure our new checksumString for it isn't empty
        if (strlen(checksumString) != 0)
            it->second.push_back((unsigned)std::stoul(checksumString));
    }
    else
    {
        mwmp::PacketPreInit::HashList checksumList;

        unsigned checksum = 0;

        if (strlen(checksumString) != 0)
        {
            checksum = (unsigned) std::stoul(checksumString);
            checksumList.push_back(checksum);
        }
        samples.emplace_back(dataFilename, checksumList);

        auto masterClient = mwmp::Networking::getPtr()->getMasterClient();
        
        if (masterClient)
            masterClient->PushPlugin({dataFilename, checksum});
    }
}


void ServerFunctions::AddGroundcoverRequirement(const char *dataFilename, const char *checksumString) noexcept
{
    auto &samples = mwmp::Networking::getPtr()->getGroundcoverSamples();

    auto it = std::find_if(samples.begin(), samples.end(), [&dataFilename](mwmp::PacketPreInit::PluginPair &item) {
        return item.first == dataFilename;
    });

    if (it != samples.end())
    {
        if (strlen(checksumString) != 0)
            it->second.push_back((unsigned)std::stoul(checksumString));
    }
    else
    {
        mwmp::PacketPreInit::HashList checksumList;
        if (strlen(checksumString) != 0)
            checksumList.push_back((unsigned)std::stoul(checksumString));
        samples.emplace_back(dataFilename, checksumList);
    }
}

// All methods below are deprecated versions of methods from above

bool ServerFunctions::DoesFileExist(const char *filePath) noexcept
{
    return DoesFilePathExist(filePath);
}

const char* ServerFunctions::GetModDir() noexcept
{
    return GetDataPath();
}

bool ServerFunctions::GetPluginEnforcementState() noexcept
{
    return mwmp::Networking::getPtr()->getDataFileEnforcementState();
}

void ServerFunctions::SetPluginEnforcementState(bool state) noexcept
{
    SetDataFileEnforcementState(state);
}

void ServerFunctions::AddPluginHash(const char *pluginName, const char *checksumString) noexcept
{
    AddDataFileRequirement(pluginName, checksumString);
}
