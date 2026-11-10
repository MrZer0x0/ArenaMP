#ifndef OPENMW_MWPHYSICS_NPCCOLLISIONFOOTPRINT_HPP
#define OPENMW_MWPHYSICS_NPCCOLLISIONFOOTPRINT_HPP

namespace MWPhysics
{
    namespace NpcCollisionFootprint
    {
        // Permanent physical footprint for ordinary NPCs: 5% narrower and
        // 5% shallower. The vertical extent is intentionally left unchanged.
        inline constexpr float HorizontalScale = 0.95f;

        template <class Vector3>
        Vector3 applyHorizontalScale(Vector3 value, bool isOrdinaryNpc)
        {
            if (isOrdinaryNpc)
            {
                value.x() *= HorizontalScale;
                value.y() *= HorizontalScale;
            }
            return value;
        }
    }
}

#endif
