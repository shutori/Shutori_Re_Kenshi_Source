#pragma once

namespace SparHitLogic
{
    // Soft-miss safety net for same-faction spars.
    // Never force-apply a successful dodge (defenderIsDodging).
    bool ShouldForceApplySparMiss(
        bool resultIsMiss,
        float damageTotal,
        bool defenderIsDodging);
}
