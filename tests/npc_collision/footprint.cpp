#include <apps/openmw/mwphysics/npccollisionfootprint.hpp>
#include <cassert>
#include <cmath>

struct Vec3
{
    float vx, vy, vz;
    float& x() { return vx; }
    float& y() { return vy; }
    float& z() { return vz; }
    float x() const { return vx; }
    float y() const { return vy; }
    float z() const { return vz; }
};

int main()
{
    using MWPhysics::NpcCollisionFootprint::applyHorizontalScale;
    using MWPhysics::NpcCollisionFootprint::NpcHorizontalScale;
    using MWPhysics::NpcCollisionFootprint::PlayerHorizontalScale;

    const Vec3 npc = applyHorizontalScale(Vec3{100.f, 80.f, 64.f}, NpcHorizontalScale);
    assert(std::fabs(npc.x() - 95.f) < 1e-5f);
    assert(std::fabs(npc.y() - 76.f) < 1e-5f);
    assert(std::fabs(npc.z() - 64.f) < 1e-5f);

    const Vec3 player = applyHorizontalScale(Vec3{100.f, 80.f, 64.f}, PlayerHorizontalScale);
    assert(std::fabs(player.x() - 100.f) < 1e-5f);
    assert(std::fabs(player.y() - 80.f) < 1e-5f);
    assert(std::fabs(player.z() - 64.f) < 1e-5f);

    const Vec3 other = applyHorizontalScale(Vec3{100.f, 80.f, 64.f}, 1.f);
    assert(std::fabs(other.x() - 100.f) < 1e-5f);
    assert(std::fabs(other.y() - 80.f) < 1e-5f);
    assert(std::fabs(other.z() - 64.f) < 1e-5f);
}
