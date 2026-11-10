#ifndef OPENMW_MWRENDER_FOOTPRINTPOLICY_H
#define OPENMW_MWRENDER_FOOTPRINTPOLICY_H
#include <algorithm>
#include <cmath>
#include <cctype>
#include <string>
namespace MWRender { namespace FootprintPolicy
{
    inline std::string normalize(std::string value)
    {
        for (char& c : value)
            c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return value;
    }
    inline std::string baseName(const std::string& path)
    {
        std::string value = normalize(path);
        const auto slash = value.find_last_of('/');
        if (slash != std::string::npos) value.erase(0, slash + 1);
        const auto dot = value.find_last_of('.');
        if (dot != std::string::npos) value.resize(dot);
        return value;
    }
    inline bool contains(const std::string& value, const char* word)
    { return value.find(word) != std::string::npos; }
    inline bool forbidden(const std::string& name, bool skipStairs)
    {
        if (contains(name, "water") || contains(name, "tree") || contains(name, "ex_v_ban")
            || contains(name, "glass")) return true;
        if (contains(name, "lava") && !contains(name, "crust")) return true;
        return skipStairs && !contains(name, "upstair")
            && (contains(name, "stair") || contains(name, "stariway") || contains(name, "steps"));
    }
    inline int classify(const std::string& name)
    {
        if (forbidden(name, false)) return 0;
        for (const char* word : {"dirt", "snow", "ice", "sand", "ash", "mud", "salt", "muck", "crust"})
            if (contains(name, word)) return 2;
        for (const char* word : {"grass", "scrub", "grave", "moss"})
            if (contains(name, word)) return 1;
        return 0;
    }
    struct LandIndex { int cellX, cellY, x, y; };
    inline LandIndex landIndex(float worldX, float worldY)
    {
        // Match ESMTerrain::Storage::getVtexIndexAt, including its X border shift.
        LandIndex i;
        i.cellX = static_cast<int>(std::floor(worldX / 8192.f));
        i.cellY = static_cast<int>(std::floor(worldY / 8192.f));
        i.x = static_cast<int>(std::floor((worldX - i.cellX * 8192.f) / 512.f)) - 1;
        i.y = static_cast<int>(std::floor((worldY - i.cellY * 8192.f) / 512.f));
        if (i.x < 0) { --i.cellX; i.x += 16; }
        return i;
    }
    inline float alpha(float age, float lifetime)
    {
        if (lifetime <= 0 || age >= lifetime) return 0;
        return std::max(0.f, std::min(1.f, (lifetime - age) / (lifetime * 0.3f)));
    }
    inline bool acceptStep(double now, double previousTime, float distanceSquared, bool landing)
    {
        return now - previousTime >= 0.12 && (landing || distanceSquared >= 9.f);
    }
    inline bool validGround(float normalZ, float heightDifference)
    { return normalZ >= 0.35f && std::abs(heightDifference) <= 24.f; }
} }
#endif
