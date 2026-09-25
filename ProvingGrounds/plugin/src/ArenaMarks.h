// ArenaMarks.h - pure per-fighter arena reward payouts (no Character / IO)
#pragma once

#include "SparPodium.h"

namespace ArenaMarks
{
    static const int kCompletionMarks = 5;
    static const int kVictoryMarks = 15;
    static const int kMvpMarks = 5;

    static const float kDifficultyDivisor = 60.0f;
    static const float kDifficultyMin = 0.50f;
    static const float kDifficultyMax = 1.50f;

    bool ShouldAward(SparPodium::OutcomeKind outcome);

    // Marks multiplier from self vs average opponent overall combat skill.
    float DifficultyMultiplier(float selfCombat, float opponentAvgCombat);

    // Scales a base payout and floors finished awards at one Mark.
    int ApplyDifficulty(int baseMarks, float selfCombat, float opponentAvgCombat);

    float DivisionMultiplier(int division);
    float RarityMultiplier(int rarity);
    float ChallengeMultiplier(int division, int rarity);
    int UniqueChallengeBonus(int division, bool skarn);

    // Adds a flat bonus after normal multipliers. The bonus is divided among
    // eligible Team A winners; any integer remainder goes to their MVP.
    void ApplyWinningTeamBonus(
        const SparPodium::Snapshot& snap,
        const bool* eligible,
        int* marksEarned,
        int teamBonus);

    // Writes the Marks earned by each snapshot fighter. Stopped and drawn
    // matches award zero. The visible first podium slot receives the MVP bonus.
    // combatSkill[i] is overall combat 0-100 for fighters[i]; may be NULL to
    // treat every matchup as even (multiplier 1.0). matchMultiplier combines
    // booked division/rarity, applied before the single final rounding step.
    void ComputeAwards(
        const SparPodium::Snapshot& snap,
        const float* combatSkill,
        int* marksEarned,
        float matchMultiplier = 1.0f);
}
