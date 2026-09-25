#pragma once

#include <cmath>

namespace FrameCadencePolicy
{
    inline void Reset(float& elapsedSec)
    {
        elapsedSec = 0.0f;
    }

    inline bool Advance(float& elapsedSec, float dtSec, float intervalSec)
    {
        if (intervalSec <= 0.0f)
            return true;

        if (dtSec > 0.0f)
            elapsedSec += dtSec;
        if (elapsedSec < intervalSec)
            return false;

        elapsedSec = std::fmod(elapsedSec, intervalSec);
        return true;
    }
}
