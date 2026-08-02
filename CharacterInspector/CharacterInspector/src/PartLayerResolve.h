#pragma once

#include <string>

namespace PartLayer
{
    enum Kind
    {
        Head,
        Torso,
        Arm,
        Leg
    };

    enum Side
    {
        Neither,
        Left,
        Right,
        Both
    };

    // English name heuristics (legacy). Empty if unknown.
    std::string FromName(const std::string& partName);

    // torsoRank: 0=chest, 1=stomach; ignored unless kind==Torso. Empty if unmapped.
    std::string Resolve(Kind kind, Side side, int torsoRank, const std::string& partName);
}
