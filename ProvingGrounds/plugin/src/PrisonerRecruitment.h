#pragma once

#include <string>

class Character;

namespace PrisonerRecruitment
{
    float AverageCombatStat(Character* prisoner);
    int RequiredMarks(Character* prisoner);
    bool Recruit(Character* prisoner, std::string& status);
}
