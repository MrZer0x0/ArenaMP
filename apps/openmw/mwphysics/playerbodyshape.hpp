#ifndef ARENAMP_PLAYER_BODY_SHAPE_HPP
#define ARENAMP_PLAYER_BODY_SHAPE_HPP

#include <BulletCollision/CollisionShapes/btConvexHullShape.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace MWPhysics
{
    // A flat-footed elliptical prism keeps stairs walkable while allowing the
    // shoulder width to differ from the chest depth. btCylinderShapeZ uses one
    // radius for both horizontal axes, so passing it unequal radii is unsafe.
    inline std::unique_ptr<btConvexHullShape> makePlayerBodyShape(const btVector3& halfExtents)
    {
        auto shape = std::make_unique<btConvexHullShape>();
        constexpr btScalar margin = btScalar(0.04);
        shape->setMargin(margin);
        const btScalar x = std::max(btScalar(0.001), halfExtents.x() - margin);
        const btScalar y = std::max(btScalar(0.001), halfExtents.y() - margin);
        const btScalar z = std::max(btScalar(0.001), halfExtents.z() - margin);
        constexpr int segments = 32;
        constexpr double fullTurn = 6.28318530717958647692;
        for (int i = 0; i < segments; ++i)
        {
            const double angle = fullTurn * i / segments;
            const btScalar px = x * btScalar(std::cos(angle));
            const btScalar py = y * btScalar(std::sin(angle));
            shape->addPoint(btVector3(px, py, -z), false);
            shape->addPoint(btVector3(px, py, z), false);
        }
        shape->recalcLocalAabb();
        return shape;
    }
}

#endif
