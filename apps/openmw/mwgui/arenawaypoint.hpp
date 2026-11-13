#ifndef OPENMW_MWGUI_ARENAWAYPOINT_H
#define OPENMW_MWGUI_ARENAWAYPOINT_H

#include <exception>
#include <locale>
#include <sstream>
#include <string>

#include <components/settings/settings.hpp>

namespace MWGui
{
    /// ArenaMP WF1: the player's personal gold flag. Set on the world map,
    /// shown there and on the horizontal compass. Exterior world coordinates.
    /// Persisted in settings.cfg ([Map] arena waypoint = "x y"; empty = none),
    /// so it survives a restart. Purely client-side: nothing is sent to the server.
    class ArenaWaypoint
    {
    public:
        static bool has() { load(); return state().mSet; }
        static float x() { load(); return state().mX; }
        static float y() { load(); return state().mY; }

        static void set(float worldX, float worldY)
        {
            State& s = state();
            s.mLoaded = true;
            s.mSet = true;
            s.mX = worldX;
            s.mY = worldY;
            std::ostringstream stream;
            stream.imbue(std::locale::classic());
            stream.setf(std::ios::fixed);
            stream.precision(1);
            stream << worldX << ' ' << worldY;
            save(stream.str());
        }

        static void clear()
        {
            State& s = state();
            s.mLoaded = true;
            s.mSet = false;
            save(std::string());
        }

    private:
        struct State
        {
            bool mLoaded = false;
            bool mSet = false;
            float mX = 0.f;
            float mY = 0.f;
        };

        static State& state()
        {
            static State s;
            return s;
        }

        static void load()
        {
            State& s = state();
            if (s.mLoaded)
                return;
            s.mLoaded = true;
            std::string value;
            try
            {
                value = Settings::Manager::getString("arena waypoint", "Map");
            }
            catch (const std::exception&)
            {
                // A stale settings-default.cfg without the key: no flag yet.
                return;
            }
            std::istringstream stream(value);
            stream.imbue(std::locale::classic());
            float worldX = 0.f;
            float worldY = 0.f;
            if (stream >> worldX >> worldY)
            {
                s.mSet = true;
                s.mX = worldX;
                s.mY = worldY;
            }
        }

        static void save(const std::string& value)
        {
            try
            {
                Settings::Manager::setString("arena waypoint", "Map", value);
            }
            catch (const std::exception&)
            {
            }
        }
    };
}

#endif
