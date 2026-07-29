#ifndef OPENMW_MWMP_INTERACTIONANIMATIONSYNC_H
#define OPENMW_MWMP_INTERACTIONANIMATIONSYNC_H

#include <string>

#include "../mwworld/ptr.hpp"

namespace mwmp
{
    struct InteractionAnimationData
    {
        std::string group;
        int blendMask = 0;
        float speed = 1.f;
        int loops = 1;
        float duration = 0.f;
        int prop = 0;
        std::string propModel;
        bool stop = false;
    };

    std::string encodeInteractionAnimation(const InteractionAnimationData& data);
    bool decodeInteractionAnimation(const std::string& value, InteractionAnimationData& data);
    bool sameInteractionAnimation(const InteractionAnimationData& left, const InteractionAnimationData& right);
    bool playInteractionAnimation(const MWWorld::Ptr& ptr, const InteractionAnimationData& data);
    bool ensureInteractionAnimationProp(const MWWorld::Ptr& ptr, const InteractionAnimationData& data);
    void stopInteractionAnimation(const MWWorld::Ptr& ptr, const InteractionAnimationData& data);

    /// Dynamic Animations 1.14-compatible walking styles are sent through the
    /// existing PlayerAnimPlay channel so no TES3MP packet format changes are
    /// required.
    std::string encodeWalkAnimationStyle(const std::string& group);
    bool decodeWalkAnimationStyle(const std::string& value, std::string& group);
    bool isValidWalkAnimationStyle(const std::string& group);
    void setWalkAnimationStyle(const MWWorld::Ptr& ptr, const std::string& group);
    void clearWalkAnimationStyle(const MWWorld::Ptr& ptr);
    std::string getWalkAnimationStyle(const MWWorld::Ptr& ptr);

    /// Return a native 0.47 animation override for the current movement group.
    /// An empty result means that the stock CharacterController animation
    /// should be used.
    std::string getDynamicMovementAnimation(const MWWorld::Ptr& ptr,
        const std::string& baseGroup);
}

#endif
