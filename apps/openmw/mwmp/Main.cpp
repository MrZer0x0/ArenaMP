#include <cstdlib>
/*
    Start of AMP addition
*/
#include <unordered_set>

#include <components/misc/stringops.hpp>
/*
    End of AMP addition
*/

#include <components/openmw-mp/Utils.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include <components/openmw-mp/Version.hpp>

#include <components/esm/esmwriter.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/files/escape.hpp>

#include "../mwbase/environment.hpp"

#include "../mwclass/creature.hpp"
#include "../mwclass/npc.hpp"

#include "../mwdialogue/dialoguemanagerimp.hpp"

#include "../mwgui/windowmanagerimp.hpp"

#include "../mwinput/inputmanagerimp.hpp"

#include "../mwmechanics/aitravel.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/mechanicsmanagerimp.hpp"
#include "../mwmechanics/spellcasting.hpp"

#include "../mwscript/scriptmanagerimp.hpp"

#include "../mwstate/statemanagerimp.hpp"

#include "../mwworld/cellstore.hpp"
#include "../mwworld/customdata.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwworld/manualref.hpp"
#include "../mwworld/player.hpp"
#include "../mwworld/ptr.hpp"
#include "../mwworld/worldimp.hpp"

#include <exception>

#include "Main.hpp"
#include "ArenaHousing.hpp"
#include "CraftEconomic.hpp"
#include "Networking.hpp"
#include "LocalSystem.hpp"
#include "LocalPlayer.hpp"
#include "DedicatedPlayer.hpp"
#include "PlayerList.hpp"
#include "GUIController.hpp"
#include "CellController.hpp"
#include "ObjectList.hpp"
#include "MechanicsHelper.hpp"
#include "RecordHelper.hpp"
#include "VoiceChat.hpp"
#include <components/settings/parser.hpp>
/*
    Start of AMP addition
*/
#include "ScriptController.hpp"
/*
    End of AMP addition
*/

using namespace mwmp;

Main *Main::pMain = 0;
std::string Main::address = "";
std::string Main::serverPassword = TES3MP_DEFAULT_PASSW;
std::string Main::resourceDir = "";

std::string Main::getResDir()
{
    return resourceDir;
}

VoiceChat* Main::getVoiceChat() const
{
    return mVoiceChat;
}


std::string loadSettings(Settings::Manager& settings)
{
    Files::ConfigurationManager mCfgMgr;
    // Create the settings manager and load default settings file
    const std::string localdefault = (mCfgMgr.getLocalPath() / "tes3mp-client-default.cfg").string();
    const std::string globaldefault = (mCfgMgr.getGlobalPath() / "tes3mp-client-default.cfg").string();

    // Ignore obsolete Chat defaults even when upgrading an old installation.
    const std::string defaults = boost::filesystem::exists(localdefault) ? localdefault : globaldefault;
    if (!boost::filesystem::exists(defaults))
        throw std::runtime_error("No default settings file found! Make sure tes3mp-client-default.cfg was installed.");
    Settings::CategorySettingValueMap clientDefaults;
    Settings::SettingsFileParser defaultParser;
    defaultParser.loadSettingsFile(defaults, clientDefaults);
    for (const auto& entry : clientDefaults)
        if (entry.first.first != "Chat")
            settings.mDefaultSettings.emplace(entry);


    // load user settings if they exist
    const std::string settingspath = (mCfgMgr.getUserConfigPath() / "tes3mp-client.cfg").string();
    if (boost::filesystem::exists(settingspath))
    {
        // Do not retarget saveUser() away from the active build's settings.cfg.
        Settings::CategorySettingValueMap legacy;
        Settings::SettingsFileParser parser;
        parser.loadSettingsFile(settingspath, legacy);
        for (const auto& entry : legacy)
        {
            // Existing settings.cfg chat values win; old client values migrate once.
            if (entry.first.first == "Chat")
                Settings::Manager::mUserSettings.emplace(entry);
            else
                Settings::Manager::mUserSettings[entry.first] = entry.second;
        }
    }

    return settingspath;
}

Main::Main()
{
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "tes3mp started");
    mNetworking = new Networking();
    mLocalSystem = new LocalSystem();
    mLocalPlayer = new LocalPlayer();
    mGUIController = new GUIController();
    mCellController = new CellController();
    mVoiceChat = new VoiceChat();

    // X031: never fall back to the public/vanilla TES3MP server.
    server = "localhost";
    port = 25565;
}

Main::~Main()
{
    // Flush the final coalesced script-local changes while the connection and
    // ObjectList still exist. A hard connection loss cannot be recovered here,
    // but a normal quit can.
    if (mNetworking != nullptr && mLocalPlayer != nullptr && mNetworking->isConnected()
        && mLocalPlayer->isLoggedIn())
        ScriptController::flushQueuedLocalChanges(0.0f, true);

    if (mVoiceChat != nullptr)
        mVoiceChat->shutdown();

    // X022: DedicatedPlayers own MWWorld::Ptrs. They must be removed before the
    // cell/GUI controllers and, most importantly, before Engine destroys World.
    // cleanUp() also calls deleteReference() so no stale Ptr reaches a destructor.
    PlayerList::cleanUp();

    delete mNetworking;
    mNetworking = nullptr;
    delete mLocalSystem;
    mLocalSystem = nullptr;
    delete mLocalPlayer;
    mLocalPlayer = nullptr;
    delete mCellController;
    mCellController = nullptr;
    delete mGUIController;
    mGUIController = nullptr;
    delete mVoiceChat;
    mVoiceChat = nullptr;

    // Keep this marker truthful: reaching it now means multiplayer teardown
    // actually completed, instead of merely having started.
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "tes3mp stopped");
}

void Main::optionsDesc(boost::program_options::options_description *desc)
{
    namespace bpo = boost::program_options;
    desc->add_options()
            ("connect", bpo::value<std::string>()->default_value(""),
                        "connect to server (e.g. --connect=127.0.0.1:25565)")
            ("password", bpo::value<std::string>()->default_value(TES3MP_DEFAULT_PASSW),
                        "сonnect to a secured server. (e.g. --password=AnyPassword");
}

void Main::configure(const boost::program_options::variables_map &variables)
{
    Main::address = variables["connect"].as<std::string>();
    Main::serverPassword = variables["password"].as<std::string>();
    resourceDir = variables["resources"].as<Files::EscapePath>().mPath.string();
}

bool Main::init(std::vector<std::string> &content, std::vector<std::string> &groundcover, Files::Collections &collections)
{
    assert(!pMain);
    pMain = new Main();

    Settings::Manager manager;
    loadSettings(manager);

    int logLevel = manager.getInt("logLevel", "General");
    bool voiceEnabled = manager.getBool("enabled", "Voice");
    std::string voiceKey = manager.getString("pushToTalkKey", "Voice");
    if (const char* envEnabled = std::getenv("ARENAMP_VOICE_ENABLED"))
    {
        const std::string value(envEnabled);
        voiceEnabled = value != "0" && value != "false" && value != "FALSE" && value != "off" && value != "OFF";
    }
    if (const char* envKey = std::getenv("ARENAMP_VOICE_PTT_KEY"))
    {
        if (*envKey != '\0')
            voiceKey = envKey;
    }
    // Older tes3mp-client-default.cfg files do not carry the loudness keys and
    // Settings::Manager throws on a missing setting - fall back rather than
    // refuse to start.
    auto voiceFloat = [&manager](const char* key, float fallback) {
        try { return manager.getFloat(key, "Voice"); }
        catch (const std::exception&) { return fallback; }
    };
    auto voiceBool = [&manager](const char* key, bool fallback) {
        try { return manager.getBool(key, "Voice"); }
        catch (const std::exception&) { return fallback; }
    };
    auto voiceString = [&manager](const char* key, const std::string& fallback) {
        try { return manager.getString(key, "Voice"); }
        catch (const std::exception&) { return fallback; }
    };
    bool voiceToggleMode = voiceBool("toggleMode", true);
    if (const char* envToggle = std::getenv("ARENAMP_VOICE_TOGGLE"))
    {
        const std::string value(envToggle);
        voiceToggleMode = value != "0" && value != "false" && value != "FALSE"
            && value != "off" && value != "OFF";
    }
    pMain->mVoiceChat->configure(voiceEnabled, voiceKey, voiceString("captureDevice", std::string()),
        voiceFloat("rangeMeters", 30.f),
        voiceFloat("fullVolumeMeters", 12.f), voiceFloat("sourceVolume", 2.f),
        voiceFloat("micGain", 1.5f), voiceFloat("playbackGain", 1.f), voiceToggleMode);
    TimedLog::SetLevel(logLevel);
    if (address.empty())
    {
        pMain->server = manager.getString("destinationAddress", "General");
        pMain->port = (unsigned short) manager.getInt("port", "General");

        serverPassword = manager.getString("password", "General");
        if (serverPassword.empty())
            serverPassword = TES3MP_DEFAULT_PASSW;
    }
    else
    {
        size_t delimPos = address.find(':');
        pMain->server = address.substr(0, delimPos);
        pMain->port = atoi(address.substr(delimPos + 1).c_str());
    }
    get().mLocalSystem->serverPassword = serverPassword;

    pMain->mNetworking->connect(pMain->server, pMain->port, content, groundcover, collections);

    return pMain->mNetworking->isConnected();
}

void Main::postInit()
{
    pMain->mGUIController->setupChat();

    const MWBase::Environment &environment = MWBase::Environment::get();
    environment.getStateManager()->newGame(true);
    MWBase::Environment::get().getMechanicsManager()->toggleAI();
    RecordHelper::createPlaceholderInteriorCell();
    pMain->mVoiceChat->init();
}

bool Main::isInitialized()
{
    return pMain != nullptr;
}

void Main::destroy()
{
    assert(pMain);

    // Z001: the housing registry belongs to one server session. Clearing it
    // here keeps a second connection from inheriting the first server's
    // managed doors, which would otherwise make ordinary doors hang waiting
    // for an answer that never comes.
    ArenaHousing::get().reset();
    CraftEconomic::get().reset();

    delete pMain;
    pMain = 0;
}

void Main::frame(float dt)
{
    get().getNetworking()->update(dt);
    // Y106: do not publish state or flush script traffic into a new transport
    // before the server has restored the previous authenticated session.
    if (!get().getNetworking()->isConnected())
        return;
    CraftEconomic::get().update(dt);
    get().getVoiceChat()->update(dt);

    PlayerList::update(dt);
    get().getCellController()->updateDedicated(dt);
    get().updateWorld(dt);

    /*
        Start of AMP addition

        Send whatever script local changes have accumulated since the last flush
    */
    ScriptController::flushQueuedLocalChanges(dt);
    /*
        End of AMP addition
    */

    get().getGUIController()->update(dt);
}

void Main::updateWorld(float dt) const
{

    if (!mLocalPlayer->processCharGen())
        return;

    static bool init = true;
    if (init)
    {
        init = false;
        getLocalPlayer()->updateLanguage();
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending ID_PLAYER_BASEINFO to server (language %s)",
            getLocalPlayer()->language.c_str());

        mNetworking->getPlayerPacket(ID_PLAYER_BASEINFO)->setPlayer(getLocalPlayer());
        mNetworking->getPlayerPacket(ID_LOADED)->setPlayer(getLocalPlayer());
        /*
            Start of AMP addition (Y044)

            The server answers ID_LOADED with the full character profile. Until every
            section of it has been applied here, our own state is still the CharGen
            placeholder and must not be published, or the server will persist it over
            the profile it just loaded.
        */
        mLocalPlayer->beginLoginSync();
        /*
            End of AMP addition (Y044)
        */

        mNetworking->getPlayerPacket(ID_PLAYER_BASEINFO)->Send();
        mNetworking->getPlayerPacket(ID_LOADED)->Send();

        // Y044: this forced send used to race the server's own LoadStatsDynamic()
        // and persist the CharGen placeholder health/magicka/fatigue over it.
        if (!mLocalPlayer->isLoginSyncPending())
            mLocalPlayer->updateStatsDynamic(true);

        get().getGUIController()->setChatVisible(true);
    }
    else
    {
        mLocalPlayer->update();
        mCellController->updateLocal(false);

        /*
            Start of AMP addition (X048)

            Drain at most one queued cell data reply per frame so that exterior cell
            transitions no longer serialize nine cells' worth of containers at once.
        */
        ObjectList::processDeferredCellRequests();
        /*
            End of AMP addition (X048)
        */
    }
}

const Main &Main::get()
{
    return *pMain;
}

Networking *Main::getNetworking() const
{
    return mNetworking;
}

LocalSystem *Main::getLocalSystem() const
{
    return mLocalSystem;
}

LocalPlayer *Main::getLocalPlayer() const
{
    return mLocalPlayer;
}

GUIController *Main::getGUIController() const
{
    return mGUIController;
}

CellController *Main::getCellController() const
{
    return mCellController;
}

/*
    Start of AMP change

    These two used to walk a std::vector of strings with an exact, case sensitive comparison,
    once per script per frame.

    Case sensitivity was an outright bug: script IDs come out of the content files with
    whatever capitalisation the author used, while the server sends the list as written in
    config.synchronizedClientScriptIds, so a mismatch in a single letter silently dropped a
    script out of synchronization with no diagnostic anywhere.

    Both lookups are now case insensitive and backed by a hash set that is rebuilt only when
    the server sends us a new list
*/
namespace
{
    std::unordered_set<std::string> sPacketScriptIds;
    std::unordered_set<std::string> sPacketGlobalIds;
    bool sPacketIdCachesValid = false;

    void rebuildPacketIdCaches(mwmp::BaseWorldstate *worldstate)
    {
        sPacketScriptIds.clear();
        sPacketGlobalIds.clear();

        for (const auto &scriptId : worldstate->synchronizedClientScriptIds)
            sPacketScriptIds.insert(Misc::StringUtils::lowerCase(scriptId));

        for (const auto &globalId : worldstate->synchronizedClientGlobalIds)
            sPacketGlobalIds.insert(Misc::StringUtils::lowerCase(globalId));

        sPacketIdCachesValid = true;
    }
}

void Main::invalidatePacketScriptCache()
{
    sPacketIdCachesValid = false;
}

bool Main::isValidPacketScript(std::string scriptId)
{
    mwmp::BaseWorldstate *worldstate = get().getNetworking()->getWorldstate();

    if (!sPacketIdCachesValid)
        rebuildPacketIdCaches(worldstate);

    return sPacketScriptIds.count(Misc::StringUtils::lowerCase(scriptId)) > 0;
}

bool Main::isValidPacketGlobal(std::string globalId)
{
    mwmp::BaseWorldstate *worldstate = get().getNetworking()->getWorldstate();

    if (!sPacketIdCachesValid)
        rebuildPacketIdCaches(worldstate);

    return sPacketGlobalIds.count(Misc::StringUtils::lowerCase(globalId)) > 0;
}
/*
    End of AMP change
*/
