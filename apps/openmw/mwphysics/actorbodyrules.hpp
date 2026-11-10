#ifndef ARENAMP_ACTOR_BODY_RULES_HPP
#define ARENAMP_ACTOR_BODY_RULES_HPP
#include <algorithm>
#include <cmath>
namespace MWPhysics::ActorBodyRules {
template <class Scalar>
inline bool blocksMotion(Scalar projectedMotion, Scalar motionLength, Scalar minimum = Scalar(0)) {
    // GJK can return a tiny vertical component for a vertical stair riser.
    // Do not let that tangential contact stop an up/down stair sweep.
    return projectedMotion > std::max(minimum, motionLength * Scalar(0.001));
}
inline void reducePlayerWidth(bool player, float& halfWidth) {
    if (player && std::isfinite(halfWidth) && halfWidth > 0.f)
        halfWidth *= 0.85f; // shoulders (local X); chest-to-back (local Y) is unchanged
}
inline void reducePlayerHeight(bool player, float& halfHeight, float& centerHeight) {
    if (!player || !std::isfinite(halfHeight) || halfHeight <= 0.f) return;
    const float previous = halfHeight;
    halfHeight *= 0.85f;
    centerHeight -= previous - halfHeight; // preserve the bottom/feet plane
}
inline float fit(float authored, float visual) {
    if (!std::isfinite(authored) || authored <= 0.f) return std::isfinite(visual) && visual>0.f ? visual : 1.f;
    if (!std::isfinite(visual) || visual <= 0.f) return authored;
    return std::clamp(visual, authored*0.5f, authored); // conservative, never enlarge into walls
}
inline bool roundFootprint(float x,float y) {
    return std::isfinite(x) && std::isfinite(y) && x>0.f && y>0.f && std::max(x,y)/std::min(x,y)<1.2f;
}
}
#endif
