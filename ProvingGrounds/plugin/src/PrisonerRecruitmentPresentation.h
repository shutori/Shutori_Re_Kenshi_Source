#pragma once

#include <string>

namespace PrisonerRecruitmentPresentation
{
    struct Input
    {
        bool hasSelection;
        bool livingRosterPrisoner;
        bool busy;
        bool unconscious;
        bool playerReady;
        int currentMarks;
        int requiredMarks;

        Input();
    };

    struct State
    {
        bool enabled;
        std::string actionCaption;
        std::string explanation;
    };

    State Evaluate(const Input& input);
}
