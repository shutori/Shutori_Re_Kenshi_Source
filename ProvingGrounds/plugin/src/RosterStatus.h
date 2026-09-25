#pragma once

#include "RosterStatusPolicy.h"
#include <string>

class Character;

namespace RosterStatus
{
    struct Snapshot
    {
        float lowestLimb;
        float blood;
        float recovery;
        RosterStatusPolicy::Band band;
    };

    bool Read(Character* character, Snapshot& snapshot, std::string* error = 0);
}
