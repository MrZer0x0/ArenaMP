#ifndef OPENMW_MWMECHANICS_ARENACOMBO_H
#define OPENMW_MWMECHANICS_ARENACOMBO_H
#include <cmath>
#include <string>
namespace MWMechanics { namespace ArenaCombo {
// Y086: local-player melee state only. No pointer retention or per-hit allocation
// except the weapon identity. Authoritative server migration remains separate.
class Chain
{
    int mTarget = -1;
    std::string mWeapon;
    unsigned mHits = 0;
    float mRemaining = 0.f;
public:
    void reset() { mTarget = -1; mWeapon.clear(); mHits = 0; mRemaining = 0.f; }
    void update(float dt)
    {
        if (!std::isfinite(dt) || dt < 0.f) { reset(); return; }
        mRemaining -= dt;
        if (mRemaining < 0.f) reset();
    }
    void checkWeapon(const std::string& weapon)
    { if (mHits && weapon != mWeapon) reset(); }
    bool matches(int target, const std::string& weapon) const
    { return mHits && mRemaining >= 0.f && mTarget == target && mWeapon == weapon; }
    float multiplier(int target, const std::string& weapon) const
    { return matches(target, weapon) && mHits == 2 ? 1.35f : 1.f; }
    bool landed(int target, const std::string& weapon, float actualDamage)
    {
        if (target < 0 || !std::isfinite(actualDamage) || actualDamage <= 0.f) { reset(); return false; }
        if (!matches(target, weapon)) reset();
        mTarget = target; mWeapon = weapon; mRemaining = 2.5f;
        if (++mHits == 3) { reset(); return true; }
        return false;
    }
};
inline Chain& local() { static Chain chain; return chain; }
// Every failed melee contact (including early returns) breaks the chain.
class Attempt
{
    Chain* mChain;
    bool mCompleted = false;
public:
    explicit Attempt(bool enabled) : mChain(enabled ? &local() : nullptr) {}
    ~Attempt() { if (mChain && !mCompleted) mChain->reset(); }
    float multiplier(int target, const std::string& weapon) const
    { return mChain ? mChain->multiplier(target, weapon) : 1.f; }
    bool complete(int target, const std::string& weapon, float damage)
    { mCompleted = true; return mChain && mChain->landed(target, weapon, damage); }
};
}}
#endif
