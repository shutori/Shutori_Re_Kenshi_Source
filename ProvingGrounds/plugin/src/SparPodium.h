// SparPodium.h — pure ranking (no Character)
#pragma once

#include "MatchRules.h"

namespace SparPodium
{
    enum OutcomeKind {
        OutcomeTeamAWins,
        OutcomeTeamBWins,
        OutcomeLastStanding,
        OutcomeStopped,
        OutcomeDraw
    };

    struct FighterRow
    {
        int id;
        MatchRules::MatchTeam team;
        char name[64];
        float damageDealt;
        float damageTaken;
        float damageMitigated;
        int hitsLanded;
        int blocks;
        int misses;
        int dodges;
        int eliminationIndex; // -1 survived; otherwise first-KO order
        float ratingBefore;
        float ratingAfter;
        float ratingDelta;
        bool ratingUpdated;
        int marksBefore;
        int marksAfter;
        int marksEarned;
        bool marksUpdated;
    };

    struct PodiumEntry
    {
        int place;
        FighterRow fighter;
    };

    struct Snapshot
    {
        OutcomeKind outcome;
        MatchRules::MatchMode mode;
        char header[96];
        FighterRow fighters[16];
        int fighterCount;
        int eliminationCount;
        int eliminationIds[16];
        int lastStandingId;
        PodiumEntry podium[3];
        int podiumCount;
    };

    void BuildPodium(Snapshot& snap);
    float ComputeMvpScore(const FighterRow& fighter);
}
