#pragma once
#include <string>

namespace PrisonerMatchLogic
{
    struct CagePick
    {
        int index; // -1 = none
    };

    enum ReturnAction
    {
        ReturnSkip = 0,
        ReturnInstantCage = 1,
        ReturnEscortWalk = 2,
        ReturnCarry = 3
    };

    enum CageLockAction
    {
        CageLockSecured, CageLockIssueOrder, CageLockWait,
        CageLockMissingDestination, CageLockWrongOccupant, CageLockMissingLock,
        CageLockBroken, CageLockNoVerifier, CageLockNoHandler,
        CageLockOrderFailed, CageLockTimedOut
    };

    struct CageLockObservation
    {
        bool destinationValid;
        bool correctOccupant;
        bool hasLock;
        bool broken;
        bool verifierAvailable;
        bool locked;
        bool handlerAvailable;
        bool orderIssued;
        bool orderRejected;
        bool timedOut;
    };

    CageLockAction ChooseCageLockAction(const CageLockObservation& observation);

    bool HasEnoughHandlers(int prisonersInMatch, int availableHandlers);

    std::string FormatHandlerShortage(int prisonersInMatch, int availableHandlers);

    // originalIndex may be -1 (no original). Arrays length == count.
    CagePick ChooseReturnCage(
        int originalIndex,
        const bool* free,
        const float* distToPrisonerSq,
        const bool* inRegistryRange,
        const bool* alreadyReserved,
        int count);

    // When true, CombatHooks must not treat the pair as natural enemies.
    // Sparring opponents are never suppressed (spar hook forces enmity).
    bool ShouldSuppressNaturalEnemy(
        bool sparringOpponents,
        bool aIsMatchPrisoner,
        bool bIsMatchPrisoner);

    ReturnAction ChooseReturnAction(bool conscious, bool dead);
}
