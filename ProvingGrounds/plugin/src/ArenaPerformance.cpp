#include "ArenaPerformance.h"

namespace ArenaPerformance
{
    bool IsValid(int value)
    {
        return value >= High && value <= Potato;
    }

    const char* Name(Profile profile)
    {
        switch (profile)
        {
        case Normal: return "Normal";
        case Potato: return "Potato";
        default: return "High";
        }
    }

    const char* PersistedName(Profile profile)
    {
        switch (profile)
        {
        case Normal: return "normal";
        case Potato: return "potato";
        default: return "high";
        }
    }

    bool Parse(const std::string& value, Profile& profile)
    {
        if (value == "high") profile = High;
        else if (value == "normal") profile = Normal;
        else if (value == "potato") profile = Potato;
        else return false;
        return true;
    }

    Policy PolicyFor(Profile profile)
    {
        switch (profile)
        {
        case Normal: return Policy(60, 100, 8.0f, 12.0f, 2, true);
        case Potato: return Policy(20, 250, 0.0f, 0.0f, 0, false);
        default: return Policy();
        }
    }
}
