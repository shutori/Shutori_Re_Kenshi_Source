#include "ArenaMarks.h"
#include "PGConfig.h"

#include <cmath>

namespace ArenaMarks
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

        bool FighterWon(
            const SparPodium::Snapshot& snap,
            const SparPodium::FighterRow& fighter)
        {
            switch (snap.outcome)
            {
            case SparPodium::OutcomeTeamAWins:
                return fighter.team == MatchRules::TeamA;
            case SparPodium::OutcomeTeamBWins:
                return fighter.team == MatchRules::TeamB;
            case SparPodium::OutcomeLastStanding:
                return fighter.id == snap.lastStandingId;
            default:
                return false;
            }
        }

        float OpponentAverageCombat(
            const SparPodium::Snapshot& snap,
            int selfIndex,
            const float* combatSkill)
        {
            if (!combatSkill)
                return 0.0f;

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

                sum += combatSkill[i];
                ++n;
            }

            if (n <= 0)
                return combatSkill[selfIndex];
            return sum / static_cast<float>(n);
        }
    }

    bool ShouldAward(SparPodium::OutcomeKind outcome)
    {
        return outcome == SparPodium::OutcomeTeamAWins
            || outcome == SparPodium::OutcomeTeamBWins
            || outcome == SparPodium::OutcomeLastStanding;
    }

    float DifficultyMultiplier(float selfCombat, float opponentAvgCombat)
    {
        const float delta = opponentAvgCombat - selfCombat;
        return Clamp(1.0f + delta / kDifficultyDivisor, kDifficultyMin, kDifficultyMax);
    }

    int ApplyDifficulty(int baseMarks, float selfCombat, float opponentAvgCombat)
    {
        if (baseMarks <= 0)
            return 0;

        const float mult = DifficultyMultiplier(selfCombat, opponentAvgCombat);
        const float scaled = static_cast<float>(baseMarks) * mult;
        int earned = static_cast<int>(scaled >= 0.0f
            ? floorf(scaled + 0.5f)
            : ceilf(scaled - 0.5f));
        if (earned < 1)
            earned = 1;
        return earned;
    }

    float DivisionMultiplier(int division)
    {
        return division == 0 ? 0.75f : division == 2 ? 1.25f : 1.0f;
    }

    float RarityMultiplier(int rarity)
    {
        return rarity == 1 ? 1.25f : rarity == 2 ? 1.5f : rarity == 3 ? 2.0f : 1.0f;
    }

    float ChallengeMultiplier(int division, int rarity)
    {
        return DivisionMultiplier(division) * RarityMultiplier(rarity);
    }

    int UniqueChallengeBonus(int division, bool skarn)
    {
        const double multiplier = skarn ? PGConfig::ChallengeValues().skarnBonusMultiplier :
            PGConfig::ChallengeValues().uniqueBonusMultiplier;
        if (multiplier == 1.0)
            return skarn ? 100 : division == 0 ? 20 : division == 1 ? 40 : division == 2 ? 50 : 0;
        if (skarn)
            return static_cast<int>(floor(100.0 * multiplier + 0.5));
        const int base = division == 0 ? 20 : division == 1 ? 40 : division == 2 ? 50 : 0;
        return static_cast<int>(floor(base * multiplier + 0.5));
    }

    void ApplyWinningTeamBonus(
        const SparPodium::Snapshot& snap,
        const bool* eligible,
        int* marksEarned,
        int teamBonus)
    {
        if (!eligible || !marksEarned || teamBonus <= 0 ||
            snap.outcome != SparPodium::OutcomeTeamAWins)
            return;

        int winners[16];
        int winnerCount = 0;
        int remainderWinner = -1;
        const int mvpId = snap.podiumCount > 0 ? snap.podium[0].fighter.id : -1;
        for (int i = 0; i < snap.fighterCount && i < 16; ++i)
        {
            if (!eligible[i] || snap.fighters[i].team != MatchRules::TeamA)
                continue;
            winners[winnerCount++] = i;
            if (snap.fighters[i].id == mvpId)
                remainderWinner = i;
        }
        if (!winnerCount)
            return;
        if (remainderWinner < 0)
            remainderWinner = winners[0];

        const int share = teamBonus / winnerCount;
        const int remainder = teamBonus % winnerCount;
        for (int i = 0; i < winnerCount; ++i)
            marksEarned[winners[i]] += share;
        marksEarned[remainderWinner] += remainder;
    }

    void ComputeAwards(
        const SparPodium::Snapshot& snap,
        const float* combatSkill,
        int* marksEarned,
        float matchMultiplier)
    {
        if (!marksEarned)
            return;

        for (int i = 0; i < snap.fighterCount; ++i)
            marksEarned[i] = 0;

        if (!ShouldAward(snap.outcome))
            return;

        const int mvpId = snap.podiumCount > 0
            ? snap.podium[0].fighter.id
            : -1;

        for (int i = 0; i < snap.fighterCount; ++i)
        {
            int earned = kCompletionMarks;
            if (FighterWon(snap, snap.fighters[i]))
                earned += kVictoryMarks;
            if (snap.fighters[i].id == mvpId)
                earned += kMvpMarks;

            float multiplier = matchMultiplier;
            if (combatSkill)
            {
                const float oppAvg = OpponentAverageCombat(snap, i, combatSkill);
                multiplier *= DifficultyMultiplier(combatSkill[i], oppAvg);
            }
            // Round once after all bonuses, preserving zero for stops/draws.
            earned = static_cast<int>(floorf(earned * multiplier + 0.5f));
            marksEarned[i] = earned < 1 ? 1 : earned;
        }
    }
}
