#ifndef OPENMW_MWPHYSICS_NPCCOLLISIONFOOTPRINT_HPP
#define OPENMW_MWPHYSICS_NPCCOLLISIONFOOTPRINT_HPP

namespace MWPhysics
{
    namespace NpcCollisionFootprint
    {
        // Permanent physical footprint for ordinary NPCs: 5% narrower and
        // 5% shallower. The vertical extent is intentionally left unchanged.
        inline constexpr float NpcHorizontalScale = 0.95f;

        // Local and remote players use the native OpenMW collision extents.
        // A scale of one also bypasses horizontal multiplication below.
        inline constexpr float PlayerHorizontalScale = 1.f;

        template <class Vector3>
        Vector3 applyHorizontalScale(Vector3 value, float horizontalScale)
        {
            if (horizontalScale != 1.f)
            {
                value.x() *= horizontalScale;
                value.y() *= horizontalScale;
            }
            return value;
        }
    }
}

#endif
