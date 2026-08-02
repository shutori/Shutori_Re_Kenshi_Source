#include "SparHitLogic.h"

bool SparHitLogic::ShouldForceApplySparMiss(
    bool resultIsMiss,
    float damageTotal,
    bool defenderIsDodging)
{
    if (!resultIsMiss || damageTotal <= 0.01f)
        return false;
    if (defenderIsDodging)
        return false;
    return true;
}
