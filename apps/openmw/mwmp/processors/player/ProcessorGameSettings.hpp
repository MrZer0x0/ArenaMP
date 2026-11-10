#ifndef OPENMW_PROCESSORGAMESETTINGS_HPP
#define OPENMW_PROCESSORGAMESETTINGS_HPP

#include "apps/openmw/mwbase/environment.hpp"
#include "apps/openmw/mwworld/worldimp.hpp"
#include "apps/openmw/mwworld/class.hpp"
#include "apps/openmw/mwgui/windowmanagerimp.hpp"

#include "../PlayerProcessor.hpp"

namespace mwmp
{
    class ProcessorGameSettings final: public PlayerProcessor
    {
        const std::string GAME_SETTING_CATEGORY = "Game";
        const std::string VR_SETTING_CATEGORY = "VR";
    public:
        ProcessorGameSettings()
        {
            BPP_INIT(ID_GAME_SETTINGS)
        }

        virtual void Do(PlayerPacket &packet, BasePlayer *player)
        {
            static const int initialLogLevel = TimedLog::GetLevel();

            if (isLocal())
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received ID_GAME_SETTINGS");
                LOG_APPEND(TimedLog::LOG_INFO, "- player %s rest in beds, %s rest in the wilderness, %s wait",
                    player->bedRestAllowed ? "can" : "cannot", player->wildernessRestAllowed ? "can" : "cannot",
                    player->waitAllowed ? "can" : "cannot");

                if (MWBase::Environment::get().getWindowManager()->isGuiMode())
                {
                    if (MWBase::Environment::get().getWindowManager()->isConsoleMode() && !player->consoleAllowed)
                        MWBase::Environment::get().getWindowManager()->popGuiMode();
                    else if (MWBase::Environment::get().getWindowManager()->getMode() == MWGui::GM_Rest &&
                        (!player->bedRestAllowed || !player->wildernessRestAllowed || !player->waitAllowed))
                        MWBase::Environment::get().getWindowManager()->popGuiMode();
                }

                if (player->enforcedLogLevel > -1)
                {
                    LOG_APPEND(TimedLog::LOG_INFO, "- server is enforcing log level %i", player->enforcedLogLevel);
                    TimedLog::SetLevel(player->enforcedLogLevel);
                }
                else if (initialLogLevel != TimedLog::GetLevel())
                {
                    LOG_APPEND(TimedLog::LOG_INFO, "- log level has been reset to initial value %i", initialLogLevel);
                    TimedLog::SetLevel(initialLogLevel);
                }

                MWBase::Environment::get().getWorld()->setPhysicsFramerate(player->physicsFramerate);

                // ArenaMP can enforce settings outside the regular [Game]
                // category without changing the ID_GAME_SETTINGS wire format.
                // Server-side config.lua encodes them as:
                //   @ArenaMP|Category|setting name
                // Stock/older clients do not understand this routing. X012 bumps
                // TES3MP_PROTO_VERSION to 807 because quest-item phasing extends
                // Container/ObjectDelete; client and server must therefore use
                // matching ArenaMP X012-or-newer protocol builds.
                const std::string categoryPrefix = "@ArenaMP|";
                for (const auto& setting : player->gameSettings)
                {
                    if (setting.first.compare(0, categoryPrefix.size(), categoryPrefix) == 0)
                    {
                        const std::size_t separator = setting.first.find('|', categoryPrefix.size());
                        if (separator != std::string::npos && separator + 1 < setting.first.size())
                        {
                            const std::string category = setting.first.substr(
                                categoryPrefix.size(), separator - categoryPrefix.size());
                            const std::string name = setting.first.substr(separator + 1);
                            if (!category.empty() && !name.empty())
                                Settings::Manager::setString(name, category, setting.second);
                        }
                    }
                    else
                        Settings::Manager::setString(setting.first, GAME_SETTING_CATEGORY, setting.second);
                }

                // A live friendly-fire mode change must immediately rebuild
                // cached active effects so hostile player DoTs are suspended
                // or resumed according to the new rule.
                MWWorld::Ptr localPlayerPtr = MWBase::Environment::get().getWorld()->getPlayerPtr();
                if (localPlayerPtr)
                    localPlayerPtr.getClass().getCreatureStats(localPlayerPtr).getActiveSpells().refreshEffects();

                // Only read VR settings for players using a VR build
#ifdef USE_OPENXR
                for (auto setting : player->vrSettings)
                {
                    Settings::Manager::setString(setting.first, VR_SETTING_CATEGORY, setting.second);
                }
#endif
            }
        }
    };
}

#endif //OPENMW_PROCESSORGAMESETTINGS_HPP
