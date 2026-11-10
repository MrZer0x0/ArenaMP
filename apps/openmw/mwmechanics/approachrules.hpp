#ifndef ARENAMP_APPROACH_RULES_HPP
#define ARENAMP_APPROACH_RULES_HPP
#include <algorithm>
#include <cmath>
namespace MWMechanics::ApproachRules {
// Relative position/velocity in the same horizontal coordinate system.
inline float collisionTime(float x,float y,float vx,float vy,float radius,float limit) {
    if (!(std::isfinite(x)&&std::isfinite(y)&&std::isfinite(vx)&&std::isfinite(vy)
        &&std::isfinite(radius)&&std::isfinite(limit)) || radius<=0.f || limit<=0.f) return limit;
    radius=std::min(radius,std::sqrt(x*x+y*y));
    const float vr=x*vx+y*vy, v2=vx*vx+vy*vy;
    const float d=vr*vr-v2*(x*x+y*y-radius*radius);
    if (d<=0.f || v2<=0.f) return limit;
    const float t=(-vr-std::sqrt(d))/v2;
    return t>=0.f && t<limit ? t : limit;
}
}
#endif
