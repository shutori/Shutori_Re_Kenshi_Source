// MmrRating.h — pure performance-weighted Elo (no Character / IO)
#pragma once

#include "SparPodium.h"

namespace MmrRating
{
    static const float kDefaultMmr = 0.0f;
    static const float kFloorMmr = 0.0f;
    static const float kMaxAbsDelta = 50.0f;
    static const float kPerfMultMin = 0.75f;
    static const float kPerfMultMax = 1.25f;
    static const int kKEarly = 32;
    static const int kKLate = 16;
    static const int kKDecayMatches = 20;

    bool ShouldRate(SparPodium::OutcomeKind outcome);

    int KFactor(int matchesBefore);

    // Maps MvpScore within the match to [kPerfMultMin, kPerfMultMax].
    // Equal scores → 1.0 for everyone.
    void ComputePerfMultipliers(
        const SparPodium::FighterRow* fighters,
        int fighterCount,
        float* outMult);

    // Expected score vs opponent average (classic Elo).
    float ExpectedScore(float mmr, float opponentAvgMmr);

    // Writes mmrAfter[i] for each fighter. Fighters with rateMask[i]==false are
    // left unchanged (mmrAfter = mmrBefore). Stopped/Draw → no changes.
    // winsOut/lossesOut: +1 when that fighter is rated and wins/loses.
    void ApplyMatch(
        const SparPodium::Snapshot& snap,
        const float* mmrBefore,
        const int* matchesBefore,
        const bool* rateMask,
        float* mmrAfter,
        int* winsDelta,
        int* lossesDelta);
}
