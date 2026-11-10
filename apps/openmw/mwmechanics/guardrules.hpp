#ifndef OPENMW_MWMECHANICS_GUARDRULES_HPP
#define OPENMW_MWMECHANICS_GUARDRULES_HPP
#include <algorithm>
#include <cmath>
namespace MWMechanics::GuardRules
{
    struct Result { unsigned char kind = 0; float damage = 0, fatigue = 0, wear = 0; };
    inline Result resolve(float damage, float fatigue, float skill, float age, bool shield, bool ranged)
    {
        Result r;
        if (!std::isfinite(damage) || damage <= 0 || !std::isfinite(fatigue) || fatigue <= 0
            || !std::isfinite(skill) || !std::isfinite(age) || age < 0) return r;
        damage = std::clamp(damage, 0.f, 100000.f);
        skill = std::clamp(skill, 0.f, 100.f);
        r.fatigue = std::min(500.f, 8.f + damage * (shield ? .40f : .65f));
        const bool perfect = skill >= 35.f && age >= 0.f && age <= .08f + .0006f * skill;
        r.kind = fatigue < r.fatigue ? 3 : (perfect ? 2 : 1);
        const float reduction = shield ? .65f + .0025f * skill : .30f + .0035f * skill;
        r.damage = r.kind == 2 ? 0.f : damage * (1.f - reduction * (r.kind == 3 ? .35f : 1.f));
        r.fatigue = std::min(fatigue, r.fatigue * (r.kind == 2 ? .5f : 1.f));
        r.wear = r.kind == 2 ? 0.f : std::clamp(damage * (shield ? .08f : .03f), 1.f, 50.f);
        // Without a shield only a timed parry can deflect an arrow.
        if (ranged && !shield && r.kind != 2) return Result{};
        return r;
    }
}
#endif
