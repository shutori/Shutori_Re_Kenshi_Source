#include "MmrRating.h"

#include <cmath>

namespace MmrRating
{
    namespace
    {
        float Clamp(float v, float lo, float hi)
        {
            if (v < lo)
                return lo;
            if (v > hi)
                return hi;
            return v;
        }

        bool FighterWon(const SparPodium::Snapshot& snap, const SparPodium::FighterRow& row)
        {
            switch (snap.outcome)
            {
            case SparPodium::OutcomeTeamAWins:
                return row.team == MatchRules::TeamA;
            case SparPodium::OutcomeTeamBWins:
                return row.team == MatchRules::TeamB;
            case SparPodium::OutcomeLastStanding:
                return row.id == snap.lastStandingId;
            default:
                return false;
            }
        }

        float OpponentAverage(
            const SparPodium::Snapshot& snap,
            int selfIndex,
            const float* mmrBefore)
        {
            float sum = 0.0f;
            int n = 0;
            const SparPodium::FighterRow& self = snap.fighters[selfIndex];

            for (int i = 0; i < snap.fighterCount; ++i)
            {
                if (i == selfIndex)
                    continue;

                const SparPodium::FighterRow& other = snap.fighters[i];
                bool isOpponent = false;
                if (snap.outcome == SparPodium::OutcomeLastStanding ||
                    snap.mode == MatchRules::ModeLastStanding)
                {
                    isOpponent = true;
                }
                else if (self.team != MatchRules::TeamNone &&
                    other.team != MatchRules::TeamNone &&
                    other.team != self.team)
                {
                    isOpponent = true;
                }

                if (!isOpponent)
                    continue;

                sum += mmrBefore[i];
                ++n;
            }

            if (n <= 0)
                return kDefaultMmr;
            return sum / static_cast<float>(n);
        }
    }

    bool ShouldRate(SparPodium::OutcomeKind outcome)
    {
        return outcome == SparPodium::OutcomeTeamAWins
            || outcome == SparPodium::OutcomeTeamBWins
            || outcome == SparPodium::OutcomeLastStanding;
    }

    int KFactor(int matchesBefore)
    {
        return matchesBefore >= kKDecayMatches ? kKLate : kKEarly;
    }

    void ComputePerfMultipliers(
        const SparPodium::FighterRow* fighters,
        int fighterCount,
        float* outMult)
    {
        if (!fighters || !outMult || fighterCount <= 0)
            return;

        float minScore = 0.0f;
        float maxScore = 0.0f;
        for (int i = 0; i < fighterCount; ++i)
        {
            const float s = SparPodium::ComputeMvpScore(fighters[i]);
            if (i == 0 || s < minScore)
                minScore = s;
            if (i == 0 || s > maxScore)
                maxScore = s;
        }

        const float span = maxScore - minScore;
        for (int i = 0; i < fighterCount; ++i)
        {
            if (span <= 0.0001f)
            {
                outMult[i] = 1.0f;
                continue;
            }
            const float s = SparPodium::ComputeMvpScore(fighters[i]);
            const float t = (s - minScore) / span;
            outMult[i] = kPerfMultMin + t * (kPerfMultMax - kPerfMultMin);
        }
    }

    float ExpectedScore(float mmr, float opponentAvgMmr)
    {
        const float exponent = (opponentAvgMmr - mmr) / 400.0f;
        return 1.0f / (1.0f + powf(10.0f, exponent));
    }

    void ApplyMatch(
        const SparPodium::Snapshot& snap,
        const float* mmrBefore,
        const int* matchesBefore,
        const bool* rateMask,
        float* mmrAfter,
        int* winsDelta,
        int* lossesDelta)
    {
        if (!mmrBefore || !matchesBefore || !rateMask || !mmrAfter)
            return;

        for (int i = 0; i < snap.fighterCount; ++i)
        {
            mmrAfter[i] = mmrBefore[i];
            if (winsDelta)
                winsDelta[i] = 0;
            if (lossesDelta)
                lossesDelta[i] = 0;
        }

        if (!ShouldRate(snap.outcome) || snap.fighterCount <= 0)
            return;

        float perfMult[16];
        ComputePerfMultipliers(snap.fighters, snap.fighterCount, perfMult);

        for (int i = 0; i < snap.fighterCount; ++i)
        {
            if (!rateMask[i])
                continue;

            const float S = FighterWon(snap, snap.fighters[i]) ? 1.0f : 0.0f;
            const float oppAvg = OpponentAverage(snap, i, mmrBefore);
            const float E = ExpectedScore(mmrBefore[i], oppAvg);
            const float k = static_cast<float>(KFactor(matchesBefore[i]));
            // Strong performance amplifies a win, but protects a defeated
            // fighter from an equally large loss. The [0.75, 1.25] range is
            // mirrored around 1.0 for losses.
            const float resultMult = S >= 0.5f
                ? perfMult[i]
                : 2.0f - perfMult[i];
            float delta = k * (S - E) * resultMult;
            delta = Clamp(delta, -kMaxAbsDelta, kMaxAbsDelta);

            float next = mmrBefore[i] + delta;
            if (next < kFloorMmr)
                next = kFloorMmr;
            mmrAfter[i] = next;

            if (S >= 0.5f)
            {
                if (winsDelta)
                    winsDelta[i] = 1;
            }
            else
            {
                if (lossesDelta)
                    lossesDelta[i] = 1;
            }
        }
    }
}
