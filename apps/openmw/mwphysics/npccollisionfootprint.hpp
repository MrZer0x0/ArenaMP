#ifndef OPENMW_MWPHYSICS_NPCCOLLISIONFOOTPRINT_HPP
#define OPENMW_MWPHYSICS_NPCCOLLISIONFOOTPRINT_HPP

namespace MWPhysics
{
    namespace NpcCollisionFootprint
    {
        // Ordinary NPCs use 90% of the native OpenMW width and depth.
        // The vertical extent is intentionally left unchanged.
        inline constexpr float NpcHorizontalScale = 0.90f;

        // Local and remote players use the same 10% horizontal reduction.
        // Race height and weight do not affect the native collision baseline.
        inline constexpr float PlayerHorizontalScale = 0.90f;

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
