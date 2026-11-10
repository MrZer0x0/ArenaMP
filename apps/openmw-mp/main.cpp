#include <iostream>
#include <vector>
#include <string>
#include <cstdlib>
#include <cwchar>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#include <sys/types.h>
#endif

#include <boost/filesystem/fstream.hpp>
#include <boost/iostreams/concepts.hpp>
#include <boost/iostreams/stream_buffer.hpp>

#include <components/files/configurationmanager.hpp>
#include <components/files/escape.hpp>
#include <components/settings/settings.hpp>
#include <components/version/version.hpp>

#include <components/openmw-mp/ErrorMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/Utils.hpp>
#include <components/openmw-mp/Version.hpp>

#include <BitStream.h>
#include <MessageIdentifiers.h>
#include <RakPeer.h>
#include <RakPeerInterface.h>

#include "Player.hpp"
#include "Networking.hpp"
#include "LinkServer.hpp"
#include "LinkAccountStore.hpp"
#include "MasterClient.hpp"
#include "Utils.hpp"

#include <apps/openmw-mp/Script/Script.hpp>

#ifdef ENABLE_LUA
#include <apps/openmw-mp/Script/LangLua/LangLua.hpp>
#endif

#ifdef ENABLE_BREAKPAD
#include <handler/exception_handler.h>
#endif

using namespace mwmp;

#ifdef ENABLE_BREAKPAD
google_breakpad::ExceptionHandler *pHandler = 0;
#if defined(_WIN32)
bool DumpCallback(const wchar_t* _dump_dir,const wchar_t* _minidump_id,void* context,EXCEPTION_POINTERS* exinfo,MDRawAssertionInfo* assertion,bool success)
#elif defined(__linux)
bool DumpCallback(const google_breakpad::MinidumpDescriptor &md, void *context, bool success)
#endif
{
    // NO STACK USE, NO HEAP USE THERE !!!
    return success;
}

void breakpad(std::string pathToDump)
{
#ifdef _WIN32
    pHandler = new google_breakpad::ExceptionHandler(
            L"crashdumps\\",
            /*FilterCallback*/ 0,
            DumpCallback,
            0,
            google_breakpad::ExceptionHandler::HANDLER_ALL);
#else
    google_breakpad::MinidumpDescriptor md(pathToDump);
    pHandler = new google_breakpad::ExceptionHandler(
            md,
            /*FilterCallback*/ 0,
            DumpCallback,
            /*context*/ 0,
            true,
            -1
    );
#endif
}

void breakpad_close()
{
    delete pHandler;
}
#else
void breakpad(std::string pathToDump){}
void breakpad_close(){}
#endif

std::string loadSettings (Settings::Manager & settings)
{
    Files::ConfigurationManager mCfgMgr;
    // Create the settings manager and load default settings file
    const std::string localdefault = (mCfgMgr.getLocalPath() / "tes3mp-server-default.cfg").string();
    const std::string globaldefault = (mCfgMgr.getGlobalPath() / "tes3mp-server-default.cfg").string();

    // prefer local
    if (boost::filesystem::exists(localdefault))
        settings.loadDefault(localdefault, false);
    else if (boost::filesystem::exists(globaldefault))
        settings.loadDefault(globaldefault, false);
    else
        throw std::runtime_error ("No default settings file found! Make sure the file \"tes3mp-server-default.cfg\" was properly installed.");

    // load user settings if they exist
    const std::string settingspath = (mCfgMgr.getUserConfigPath() / "tes3mp-server.cfg").string();
    if (boost::filesystem::exists(settingspath))
        settings.loadUser(settingspath);

    return settingspath;
}

class Tee : public boost::iostreams::sink
{
public:
    Tee(std::ostream &stream, std::ostream &stream2)
            : out(stream), out2(stream2)
    {
    }

    std::streamsize write(const char *str, std::streamsize size)
    {
        out.write (str, size);
        out.flush();
        out2.write (str, size);
        out2.flush();
        return size;
    }

private:
    std::ostream &out;
    std::ostream &out2;
};

boost::program_options::variables_map launchOptions(int argc, char *argv[], Files::ConfigurationManager cfgMgr)
{
    namespace bpo = boost::program_options;
    bpo::variables_map variables;
    bpo::options_description desc;

    desc.add_options()
            ("resources", bpo::value<Files::EscapeHashString>()->default_value("resources"), "set resources directory")
            ("no-logs", bpo::value<bool>()->implicit_value(true)->default_value(false),
             "Do not write logs. Useful for daemonizing.");

    cfgMgr.readConfiguration(variables, desc, true);

    bpo::parsed_options valid_opts = bpo::command_line_parser(argc, argv).options(desc).allow_unregistered().run();

    bpo::store(valid_opts, variables);
    bpo::notify(variables);

    return variables;
}

namespace
{
    constexpr int sArenaEmbeddedRestartExitCode = 42;

    bool relaunchDedicatedServer(int argc, char* argv[])
    {
#if defined(_WIN32)
        // Reuse the exact command line (including custom resource/config flags).
        // The old RakPeer is already destroyed before this is called, so the new
        // process can bind the same port immediately without an external launcher.
        const wchar_t* current = GetCommandLineW();
        if (current == nullptr || *current == L'\0')
            return false;

        std::vector<wchar_t> commandLine;
        const std::size_t length = std::wcslen(current);
        commandLine.assign(current, current + length + 1);

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        const BOOL ok = CreateProcessW(
            nullptr, commandLine.data(), nullptr, nullptr, FALSE,
            CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &startup, &process);
        if (!ok)
            return false;

        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return true;
#else
        const pid_t child = fork();
        if (child < 0)
            return false;
        if (child == 0)
        {
            setsid();
            execvp(argv[0], argv);
            _exit(127);
        }
        return true;
#endif
    }
}


int main(int argc, char *argv[])
{
    Settings::Manager mgr;
    Files::ConfigurationManager cfgMgr;

    breakpad(boost::filesystem::path(cfgMgr.getLogPath()).string());

    loadSettings(mgr);

    auto variables = launchOptions(argc, argv, cfgMgr);

    auto version = Version::getOpenmwVersion(variables["resources"].as<Files::EscapeHashString>().toStdString());

    int logLevel = mgr.getInt("logLevel", "General");
    if (logLevel < TimedLog::LOG_VERBOSE || logLevel > TimedLog::LOG_FATAL)
        logLevel = TimedLog::LOG_INFO;

    // Some objects used to redirect cout and cerr
    // Scope must be here, so this still works inside the catch block for logging exceptions
    std::streambuf* cout_rdbuf = std::cout.rdbuf ();
    std::streambuf* cerr_rdbuf = std::cerr.rdbuf ();

    boost::iostreams::stream_buffer<Tee> coutsb;
    boost::iostreams::stream_buffer<Tee> cerrsb;

    std::ostream oldcout(cout_rdbuf);
    std::ostream oldcerr(cerr_rdbuf);

    boost::filesystem::ofstream logfile;

    if (!variables["no-logs"].as<bool>())
    {
        // Redirect cout and cerr to tes3mp server log

        // Y050: a server process owns exactly one current log. Always truncate
        // on process start instead of ever inheriting/continuing an old session.
        logfile.open(boost::filesystem::path(cfgMgr.getLogPath() / "tes3mp-server.log"),
            std::ios_base::out | std::ios_base::trunc);

        coutsb.open(Tee(logfile, oldcout));
        cerrsb.open(Tee(logfile, oldcerr));

        std::cout.rdbuf(&coutsb);
        std::cerr.rdbuf(&cerrsb);
    }

    LOG_INIT(logLevel);

    int players = mgr.getInt("maximumPlayers", "General");
    std::string address = mgr.getString("localAddress", "General");
    int port = mgr.getInt("port", "General");

    std::string password = mgr.getString("password", "General");

    std::string pluginHome = mgr.getString("home", "Plugins");
    std::string dataDirectory = Utils::convertPath(pluginHome + "/data");

    std::vector<std::string> plugins(Utils::split(mgr.getString("plugins", "Plugins"), ','));

    std::string versionInfo = Utils::getVersionInfo("TES3MP dedicated server", TES3MP_VERSION, TES3MP_COMPAT_COMMIT_HASH, TES3MP_PROTO_VERSION);
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "%s", versionInfo.c_str());
    
    Script::SetModDir(dataDirectory);

#ifdef ENABLE_LUA
    LangLua::AddPackagePath(Utils::convertPath(pluginHome + "/scripts/?.lua" + ";"
        + pluginHome + "/lib/lua/?.lua" + ";"));
#ifdef _WIN32
    LangLua::AddPackageCPath(Utils::convertPath(pluginHome + "/lib/?.dll"));
#else
    LangLua::AddPackageCPath(Utils::convertPath(pluginHome + "/lib/?.so"));
#endif

#endif

    int code;

    RakNet::RakPeerInterface *peer = RakNet::RakPeerInterface::GetInstance();

    std::stringstream sstr;
    sstr << TES3MP_VERSION;
    sstr << TES3MP_PROTO_VERSION;
    // Stable ArenaMP compatibility identity: do not derive the network password
    // from the current Git HEAD, otherwise every rebuild becomes incompatible.
    sstr << TES3MP_COMPAT_COMMIT_HASH;

    peer->SetIncomingPassword(sstr.str().c_str(), (int) sstr.str().size());

    if (RakNet::NonNumericHostString(address.c_str()))
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "You cannot use non-numeric addresses for the server.");
        return 1;
    }

    RakNet::SocketDescriptor sd((unsigned short) port, address.c_str());

    try
    {
        for (auto plugin : plugins)
            Script::LoadScript(plugin.c_str(), pluginHome.c_str());

        switch (peer->Startup((unsigned) players, &sd, 1))
        {
            case RakNet::CRABNET_STARTED:
                break;
            case RakNet::CRABNET_ALREADY_STARTED:
                throw std::runtime_error("Already started");
            case RakNet::INVALID_SOCKET_DESCRIPTORS:
                throw std::runtime_error("Incorrect port or address");
            case RakNet::INVALID_MAX_CONNECTIONS:
                throw std::runtime_error("Max players cannot be negative or 0");
            case RakNet::SOCKET_FAILED_TO_BIND:
            case RakNet::SOCKET_PORT_ALREADY_IN_USE:
            case RakNet::PORT_CANNOT_BE_ZERO:
                throw std::runtime_error("Failed to bind port. Make sure a server isn't already running on that port.");
            case RakNet::SOCKET_FAILED_TEST_SEND:
            case RakNet::SOCKET_FAMILY_NOT_SUPPORTED:
            case RakNet::FAILED_TO_CREATE_NETWORK_THREAD:
            case RakNet::COULD_NOT_GENERATE_GUID:
            case RakNet::STARTUP_OTHER_FAILURE:
                throw std::runtime_error("Cannot start server");
        }

        peer->SetMaximumIncomingConnections((unsigned short) (players));

        Networking networking(peer);
        networking.setServerPassword(password);
        networking.setVoiceEnabled(mgr.getBool("enabled", "Voice"));
        networking.setVoiceRangeMeters(mgr.getFloat("rangeMeters", "Voice"));

        if (mgr.getBool("enabled", "MasterServer"))
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sharing server query info to master enabled.");
            std::string masterAddr = mgr.getString("address", "MasterServer");
            int masterPort = mgr.getInt("port", "MasterServer");
            int updateRate = mgr.getInt("rate", "MasterServer");

            // Is this an attempt to connect to the official master server at the old port? If so,
            // redirect it to the correct port for the currently used fork of RakNet
            if (Misc::StringUtils::ciEqual(masterAddr, "master.tes3mp.com") && masterPort == 25560)
            {
                masterPort = 25561;
                LOG_APPEND(TimedLog::LOG_INFO, "- switching to port %i because the correct official master server for this version is on that port",
                    masterPort);
            }

            if (updateRate < 8000)
            {
                updateRate = 8000;
                LOG_APPEND(TimedLog::LOG_INFO, "- switching to updateRate %i because the one in the server config was too low", updateRate);
            }

            networking.InitQuery(masterAddr, (unsigned short) masterPort);
            networking.getMasterClient()->SetMaxPlayers((unsigned) players);
            networking.getMasterClient()->SetUpdateRate((unsigned) updateRate);
            std::string hostname = mgr.getString("hostname", "General");
            networking.getMasterClient()->SetHostname(hostname);
            networking.getMasterClient()->SetRuleString("CommitHash", std::string(TES3MP_COMPAT_COMMIT_HASH).substr(0, 10));

            networking.getMasterClient()->Start();
        }

        networking.postInit();

        // ArenaLink owns a separate nonblocking TCP listener and never calls Lua
        // from its worker. Scope destruction stops it before Networking is destroyed.
        LinkServer launcherChat;
        LinkConfig linkConfig;
        linkConfig.bindAddress = address;
        linkConfig.authMode = ArenaLink::AUTH_TES3MP_PROOF;
        const char* disableChat = std::getenv("ARENAMP_CHAT_DISABLED");
        linkConfig.enabled = !(disableChat && std::string(disableChat) == "1");
        launcherChat.configure(linkConfig, jsonLinkCallbacks(dataDirectory));
        const bool chatStarted = launcherChat.start(static_cast<unsigned short>(port));
        networking.setLinkServer(launcherChat.running() ? &launcherChat : nullptr);
        if (linkConfig.enabled && !chatStarted)
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "ArenaLink: TCP chat could not start (game port + 2)");
        else if (launcherChat.running())
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "ArenaLink U032: chat listening on TCP %u; diagnostics: %s/ChatServer.log", launcherChat.port(), dataDirectory.c_str());
        else if (!linkConfig.enabled)
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "ArenaLink U032: chat disabled by ARENAMP_CHAT_DISABLED=1");

        code = networking.mainLoop();
        networking.setLinkServer(nullptr);
        launcherChat.stop();

        // ArenaMP Y052: without this guard a server running with [MasterServer]
        // disabled crashed here, which is *before* the reserved-exit-code
        // relaunch block below - so the scheduled 12h restart never happened.
        if (networking.getMasterClient() != nullptr)
            networking.getMasterClient()->Stop();
    }
    catch (std::exception &e)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, e.what());
        Script::Call<Script::CallbackIdentity("OnServerScriptCrash")>(e.what());
        throw; //fall through
    }

    RakNet::RakPeerInterface::DestroyInstance(peer);

    if (code == 0)
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Quitting peacefully.");

    LOG_QUIT();

    if (!variables["no-logs"].as<bool>())
    {
        // Restore cout/cerr before explicitly closing the current run's file.
        // This matters for standalone self-relaunch on Windows: the replacement
        // process must be able to truncate the log immediately.
        std::cout.rdbuf(cout_rdbuf);
        std::cerr.rdbuf(cerr_rdbuf);
        logfile.flush();
        logfile.close();
    }


    breakpad_close();

    // Y050 embedded restart: CoreScripts use the reserved code only after the
    // 30-second warning, final save and kick. Relaunch here, after RakPeer and
    // log streams are fully closed, so no launcher/supervisor is needed.
    if (code == sArenaEmbeddedRestartExitCode)
    {
        // If the launcher owns this process, return the reserved code so its
        // existing QProcess remains authoritative and it can relaunch a tracked
        // child. This avoids an orphan server plus a launcher that incorrectly
        // thinks the server stopped. The schedule/countdown/save remain in the
        // server core; the launcher is only the process supervisor in this mode.
        const char* launcherManaged = std::getenv("ARENAMP_LAUNCHER_MANAGED");
        if (launcherManaged != nullptr && std::string(launcherManaged) == "1")
            return sArenaEmbeddedRestartExitCode;

        // Standalone dedicated-server launches do not need an external wrapper:
        // reopen the exact executable/arguments after RakNet and the log are closed.
        if (relaunchDedicatedServer(argc, argv))
            return 0;

        // A failed self-relaunch is intentionally non-zero so a service manager
        // can still detect the failure instead of silently leaving the server off.
        return sArenaEmbeddedRestartExitCode;
    }

    return code;
}
