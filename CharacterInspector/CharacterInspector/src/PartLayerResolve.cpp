#include "PartLayerResolve.h"

#include <cctype>
#include <string>

namespace
{
    std::string ToLower(const std::string& s)
    {
        std::string out = s;
        for (size_t i = 0; i < out.size(); ++i)
            out[i] = (char)tolower((unsigned char)out[i]);
        return out;
    }
}

namespace PartLayer
{
    std::string FromName(const std::string& partName)
    {
        std::string n = ToLower(partName);

        if (n.find("head") != std::string::npos)
            return "head";
        if (n.find("stomach") != std::string::npos || n.find("belly") != std::string::npos)
            return "stomach";
        if (n.find("chest") != std::string::npos || n.find("torso") != std::string::npos)
            return "chest";

        bool left = (n.find("left") != std::string::npos) || (n.find("l.") != std::string::npos);
        bool right = (n.find("right") != std::string::npos) || (n.find("r.") != std::string::npos);

        if (n.find("arm") != std::string::npos)
        {
            if (left)
                return "left_arm";
            if (right)
                return "right_arm";
        }
        if (n.find("leg") != std::string::npos)
        {
            if (left)
                return "left_leg";
            if (right)
                return "right_leg";
        }

        return std::string();
    }

    std::string Resolve(Kind kind, Side side, int torsoRank, const std::string& partName)
    {
        if (kind == Head)
            return "head";

        if (kind == Torso)
        {
            if (torsoRank == 0)
                return "chest";
            if (torsoRank == 1)
                return "stomach";
            return std::string();
        }

        if (kind == Arm || kind == Leg)
        {
            if (side == Left || side == Right)
            {
                if (kind == Arm)
                    return side == Left ? "left_arm" : "right_arm";
                return side == Left ? "left_leg" : "right_leg";
            }
            return FromName(partName);
        }

        return std::string();
    }
}
