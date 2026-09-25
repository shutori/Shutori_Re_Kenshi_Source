#include "PrisonerRecruitmentPresentation.h"

#include <sstream>

namespace PrisonerRecruitmentPresentation
{
    namespace
    {
        State Blocked(const char* caption, const char* explanation)
        {
            State state;
            state.enabled = false;
            state.actionCaption = caption;
            state.explanation = explanation;
            return state;
        }
    }

    Input::Input()
        : hasSelection(false),
          livingRosterPrisoner(false),
          busy(false),
          unconscious(false),
          playerReady(false),
          currentMarks(0),
          requiredMarks(0)
    {
    }

    State Evaluate(const Input& input)
    {
        if (!input.hasSelection)
            return Blocked("No Prisoner Selected",
                "No nearby living prisoners are available.");
        if (!input.livingRosterPrisoner)
            return Blocked("Prisoner Unavailable",
                "This fighter is not an available arena prisoner.");
        if (input.busy)
            return Blocked("Arena Operation Active",
                "Finish the current arena operation before recruiting.");
        if (input.unconscious)
            return Blocked("Prisoner Unconscious",
                "The prisoner must be conscious to accept freedom.");
        if (!input.playerReady)
            return Blocked("Player Faction Unavailable",
                "The player faction is not ready.");
        if (input.currentMarks < input.requiredMarks)
        {
            const int deficit = input.requiredMarks - input.currentMarks;
            std::ostringstream message;
            message << "This prisoner needs " << deficit << " more Arena Mark";
            if (deficit != 1)
                message << "s";
            message << ".";
            return Blocked("More Marks Required", message.str().c_str());
        }

        State state;
        state.enabled = true;
        std::ostringstream caption;
        caption << "Grant Freedom - " << input.requiredMarks << " Marks";
        state.actionCaption = caption.str();
        state.explanation = "This prisoner can earn freedom now.";
        return state;
    }
}
