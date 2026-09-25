#include "MmrRating.h"
#include "BalanceTuning.h"

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

        // One definition of "opponent" for both the Elo expectation and the
        // team-size balance, so the two can never disagree.
        bool IsOpponentRow(
            const SparPodium::Snapshot& snap,
            const SparPodium::FighterRow& self,
            const SparPodium::FighterRow& other)
        {
            if (snap.outcome == SparPodium::OutcomeLastStanding ||
                snap.mode == MatchRules::ModeLastStanding)
                return true;
            return self.team != MatchRules::TeamNone &&
                other.team != MatchRules::TeamNone &&
                other.team != self.team;
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

                if (!IsOpponentRow(snap, self, snap.fighters[i]))
                    continue;

                sum += mmrBefore[i];
                ++n;
            }

            if (n <= 0)
                return kDefaultMmr;
            return sum / static_cast<float>(n);
        }

        // Rated team sizes around one fighter. Unrated fighters take no part in
        // the rating economy, so they must not dilute the team balance either.
        void TeamSizes(
            const SparPodium::Snapshot& snap,
            int selfIndex,
            const bool* rateMask,
            int& own,
            int& opp)
        {
            own = 1;
            opp = 0;
            const SparPodium::FighterRow& self = snap.fighters[selfIndex];
            for (int i = 0; i < snap.fighterCount; ++i)
            {
                if (i == selfIndex || (rateMask && !rateMask[i]))
                    continue;
                if (IsOpponentRow(snap, self, snap.fighters[i]))
                    ++opp;
                else
                    ++own;
            }
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

        const bool zeroSum = BalanceTuning::Get().zeroSumRating;

        // Pass 1 - raw delta per rated fighter, weighted by rated team sizes.
        float pending[16];
        for (int i = 0; i < snap.fighterCount; ++i)
        {
            pending[i] = 0.0f;
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

            if (zeroSum)
            {
                // Each member's share of the result is diluted by their own team
                // size and amplified by the opposing one. For equal teams this is
                // exactly 1.0, so 1v1 behaviour is untouched.
                int own = 1, opp = 0;
                TeamSizes(snap, i, rateMask, own, opp);
                if (opp > 0)
                    delta *= (2.0f * opp) / static_cast<float>(own + opp);
            }
            pending[i] = delta;
        }

        // Pass 2 - remove the residual. The weights alone are only exactly
        // zero-sum when E and the performance multiplier are uniform, so rescale
        // the losing side to make the bout conserve rating in every case. Once
        // pass 1 has run this factor is near 1.0, so it cannot blow past the
        // clamp.
        if (zeroSum)
        {
            float gain = 0.0f, loss = 0.0f;
            for (int i = 0; i < snap.fighterCount; ++i)
            {
                if (!rateMask[i])
                    continue;
                if (pending[i] > 0.0f)
                    gain += pending[i];
                else if (pending[i] < 0.0f)
                    loss -= pending[i];
            }
            if (gain > 0.0001f && loss > 0.0001f && fabsf(gain - loss) > 0.0001f)
            {
                const float scale = gain / loss;
                for (int i = 0; i < snap.fighterCount; ++i)
                    if (rateMask[i] && pending[i] < 0.0f)
                        pending[i] *= scale;
            }
        }

        // Pass 3 - apply, clamp, write.
        for (int i = 0; i < snap.fighterCount; ++i)
        {
            if (!rateMask[i])
                continue;

            const float S = FighterWon(snap, snap.fighters[i]) ? 1.0f : 0.0f;
            const float delta = Clamp(pending[i], -kMaxAbsDelta, kMaxAbsDelta);

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
