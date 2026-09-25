#include "PrisonerRecruitmentLogic.h"
#include "PGConfig.h"

#include <cmath>

namespace PrisonerRecruitmentLogic
{
    int RequiredMarks(float averageCombatStat)
    {
        if (averageCombatStat < 0.0f)
            averageCombatStat = 0.0f;
        if (averageCombatStat > 100.0f)
            averageCombatStat = 100.0f;

        const PGConfig::Recruitment& recruitment = PGConfig::RecruitmentValues();
        const float scaled = averageCombatStat * static_cast<float>(recruitment.combatScale);
        int cost = static_cast<int>(ceilf(
            scaled / static_cast<float>(recruitment.step))) * recruitment.step;
        if (cost < recruitment.minimum)
            cost = recruitment.minimum;
        if (cost > recruitment.maximum)
            cost = recruitment.maximum;
        // The multiplier is applied to the bounded price, so it scales the
        // ceiling too: 2.0 doubles every prisoner's price, not only the cheap
        // ones. At the shipped 1.0 this returns the value above unchanged.
        return PGConfig::ScaleMarks(cost);
    }

    bool CanRecruit(float averageCombatStat, int currentMarks)
    {
        return currentMarks >= RequiredMarks(averageCombatStat);
    }
}
