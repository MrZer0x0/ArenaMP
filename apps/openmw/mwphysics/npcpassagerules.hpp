#ifndef OPENMW_MWPHYSICS_NPCPASSAGERULES_HPP
#define OPENMW_MWPHYSICS_NPCPASSAGERULES_HPP
#include <algorithm>
#include <cmath>
namespace MWPhysics { namespace NpcPassageRules {
inline float clampScale(float value, float fallback)
{
    return std::clamp(std::isfinite(value) ? value : fallback, .1f, 1.f);
}
inline float scale(bool npcA, bool npcB, bool playerA, bool playerB,
    float playerNpc, float npcNpc)
{
    if (!npcA || !npcB || (playerA && playerB)) return 1.f;
    if (playerA || playerB) return clampScale(playerNpc, .2f);
    return clampScale(npcNpc, .88f);
}
}}
#endif
