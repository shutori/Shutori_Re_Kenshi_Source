#pragma once

#include <string>

namespace RosterStatusPolicy
{
    enum Band
    {
        Red,
        Orange,
        Green
    };

    inline float Clamp(float value)
    {
        if (value < 0.0f) return 0.0f;
        if (value > 1.0f) return 1.0f;
        return value;
    }

    inline float Recovery(float lowestLimb, float blood)
    {
        return Clamp(lowestLimb < blood ? lowestLimb : blood);
    }

    inline float LowestLimb(float current, float candidate)
    {
        return candidate < current ? candidate : current;
    }

    inline Band BandFor(float recovery)
    {
        if (recovery >= 0.9f) return Green;
        if (recovery >= 0.5f) return Orange;
        return Red;
    }

    inline bool IsRecoveryReason(const std::string& reason)
    {
        return reason.compare(0, 10, "recovering") == 0;
    }

    inline std::string InlineStatus(bool ready, const std::string& reason)
    {
        if (ready) return "Ready";
        if (IsRecoveryReason(reason)) return "Recovering";
        return std::string();
    }
}
