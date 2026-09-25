#pragma once

namespace PrisonerRecruitmentLogic
{
    // Cost in Arena Marks to free one prisoner, from the bound average combat
    // stat of the prisoner. The model -- the marks per stat point, the 5-Mark
    // step, the bounds, and the marks multiplier a configured file may apply on
    // top -- lives in PGConfig, which reads pg_config.json; the shipped
    // numbers are 2.5 marks per point, 25 to 250.
    int RequiredMarks(float averageCombatStat);
    bool CanRecruit(float averageCombatStat, int currentMarks);
}
