#include "PrisonerMatchLogic.h"

#include <cstdio>
#include <limits>

namespace PrisonerMatchLogic
{
    CageLockAction ChooseCageLockAction(const CageLockObservation& observation)
    {
        if (!observation.destinationValid) return CageLockMissingDestination;
        if (observation.broken) return CageLockBroken;
        if (!observation.correctOccupant) return CageLockWrongOccupant;
        if (!observation.hasLock) return CageLockMissingLock;
        if (!observation.verifierAvailable) return CageLockNoVerifier;
        if (observation.locked) return CageLockSecured;
        if (!observation.handlerAvailable) return CageLockNoHandler;
        if (observation.orderRejected) return CageLockOrderFailed;
        if (observation.timedOut) return CageLockTimedOut;
        return observation.orderIssued ? CageLockWait : CageLockIssueOrder;
    }

    bool HasEnoughHandlers(int prisonersInMatch, int availableHandlers)
    {
        return availableHandlers >= prisonersInMatch;
    }

    std::string FormatHandlerShortage(int prisonersInMatch, int availableHandlers)
    {
        char buf[128];
        sprintf_s(
            buf,
            "Need %d handlers for %d prisoners (%d available)",
            prisonersInMatch,
            prisonersInMatch,
            availableHandlers);
        return std::string(buf);
    }

    CagePick ChooseReturnCage(
        int originalIndex,
        const bool* free,
        const float* distToPrisonerSq,
        const bool* inRegistryRange,
        const bool* alreadyReserved,
        int count)
    {
        if (originalIndex >= 0 && originalIndex < count &&
            free[originalIndex] &&
            inRegistryRange[originalIndex] &&
            !alreadyReserved[originalIndex])
        {
            CagePick pick;
            pick.index = originalIndex;
            return pick;
        }

        int bestIndex = -1;
        float bestDist = std::numeric_limits<float>::max();
        for (int i = 0; i < count; ++i)
        {
            if (!free[i] || !inRegistryRange[i] || alreadyReserved[i])
                continue;
            if (distToPrisonerSq[i] < bestDist)
            {
                bestDist = distToPrisonerSq[i];
                bestIndex = i;
            }
        }

        CagePick pick;
        pick.index = bestIndex;
        return pick;
    }


    bool ShouldSuppressNaturalEnemy(
        bool sparringOpponents,
        bool aIsMatchPrisoner,
        bool bIsMatchPrisoner)
    {
        if (sparringOpponents)
            return false;
        return aIsMatchPrisoner || bIsMatchPrisoner;
    }

    ReturnAction ChooseReturnAction(bool conscious, bool dead)
    {
        if (dead)
            return ReturnSkip;
        if (conscious)
            return ReturnEscortWalk;
        return ReturnCarry;
    }
}
