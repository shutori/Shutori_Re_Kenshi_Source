#pragma once

namespace ArenaIngressPolicy
{
    inline bool ShouldAdvancePrisonerRelease(
        bool inPrisonerReleasePhase,
        bool matchIncludesPrisoner,
        bool allPrisonersReleased)
    {
        return inPrisonerReleasePhase &&
            matchIncludesPrisoner &&
            allPrisonersReleased;
    }

    inline bool ShouldOpenInteractUiImmediately(
        bool matchActive,
        bool fightIngressPending,
        bool walkInPending)
    {
        return matchActive || fightIngressPending || walkInPending;
    }

    inline bool ShouldBindInteractSource(bool openImmediately)
    {
        return !openImmediately;
    }

    struct FightReadiness
    {
        bool allPrisonersReleased;
        bool allFightersAtFormation;
    };

    struct IngressObservation
    {
        bool allPrisonersReleased;
        bool allFightersValid;
        bool allAtArrivalRadius;
        bool allAtSettleRadius;
        bool allAtFormationRadius;
        bool allSettled;
    };

    inline bool CanOpenApproachUi(const IngressObservation& observation)
    {
        return observation.allFightersValid &&
            observation.allPrisonersReleased &&
            observation.allAtArrivalRadius;
    }

    inline bool CanBeginCountdown(const IngressObservation& observation)
    {
        return observation.allFightersValid &&
            observation.allPrisonersReleased &&
            observation.allAtFormationRadius;
    }

    inline bool CanBeginCountdown(const FightReadiness& readiness)
    {
        return readiness.allPrisonersReleased &&
            readiness.allFightersAtFormation;
    }
}
