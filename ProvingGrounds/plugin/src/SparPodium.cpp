#include "SparPodium.h"

#include <cstdio>
#include <cstring>

namespace SparPodium
{
    namespace
    {
        const FighterRow* FindById(const Snapshot& snap, int id)
        {
            for (int i = 0; i < snap.fighterCount; ++i)
            {
                if (snap.fighters[i].id == id)
                    return &snap.fighters[i];
            }
            return 0;
        }

        bool BetterMvp(const FighterRow& a, const FighterRow& b)
        {
            const float aScore = ComputeMvpScore(a);
            const float bScore = ComputeMvpScore(b);
            if (aScore != bScore)
                return aScore > bScore;
            if (a.damageDealt != b.damageDealt)
                return a.damageDealt > b.damageDealt;
            const float aDefense = a.damageMitigated +
                static_cast<float>(a.blocks + a.dodges);
            const float bDefense = b.damageMitigated +
                static_cast<float>(b.blocks + b.dodges);
            if (aDefense != bDefense)
                return aDefense > bDefense;
            if (a.hitsLanded != b.hitsLanded)
                return a.hitsLanded > b.hitsLanded;
            return std::strcmp(a.name, b.name) < 0;
        }

        void SortByMvp(FighterRow* rows, int count)
        {
            for (int i = 0; i < count; ++i)
            {
                for (int j = i + 1; j < count; ++j)
                {
                    if (BetterMvp(rows[j], rows[i]))
                    {
                        FighterRow tmp = rows[i];
                        rows[i] = rows[j];
                        rows[j] = tmp;
                    }
                }
            }
        }

        void FillMvpPodium(Snapshot& snap, MatchRules::MatchTeam filterTeam, bool filter)
        {
            FighterRow pool[16];
            int n = 0;
            for (int i = 0; i < snap.fighterCount && n < 16; ++i)
            {
                if (filter && snap.fighters[i].team != filterTeam)
                    continue;
                pool[n++] = snap.fighters[i];
            }
            SortByMvp(pool, n);
            snap.podiumCount = 0;
            for (int i = 0; i < n && snap.podiumCount < 3; ++i)
            {
                snap.podium[snap.podiumCount].place = snap.podiumCount + 1;
                snap.podium[snap.podiumCount].fighter = pool[i];
                ++snap.podiumCount;
            }
        }

        void SetHeader(Snapshot& snap, const char* text)
        {
            std::strncpy(snap.header, text, 95);
            snap.header[95] = '\0';
        }
    }

    float ComputeMvpScore(const FighterRow& fighter)
    {
        const int attempts = fighter.hitsLanded + fighter.misses;
        const float accuracy = attempts > 0
            ? static_cast<float>(fighter.hitsLanded) / static_cast<float>(attempts)
            : 0.0f;
        const float survivalBonus = fighter.eliminationIndex < 0 ? 10.0f : 0.0f;

        return fighter.damageDealt +
            (fighter.damageMitigated * 0.25f) -
            (fighter.damageTaken * 0.10f) +
            (static_cast<float>(fighter.hitsLanded) * 2.0f) +
            (static_cast<float>(fighter.blocks) * 4.0f) +
            (static_cast<float>(fighter.dodges) * 4.0f) -
            (static_cast<float>(fighter.misses) * 1.5f) +
            (accuracy * 15.0f) +
            survivalBonus;
    }

    void BuildPodium(Snapshot& snap)
    {
        snap.podiumCount = 0;
        snap.header[0] = '\0';

        switch (snap.outcome)
        {
        case OutcomeDraw:
            SetHeader(snap, "Draw");
            return;

        case OutcomeTeamAWins:
            SetHeader(snap, "Team A wins");
            FillMvpPodium(snap, MatchRules::TeamA, true);
            return;

        case OutcomeTeamBWins:
            SetHeader(snap, "Team B wins");
            FillMvpPodium(snap, MatchRules::TeamB, true);
            return;

        case OutcomeStopped:
            SetHeader(snap, "Stopped");
            FillMvpPodium(snap, MatchRules::TeamNone, false);
            return;

        case OutcomeLastStanding:
        {
            const FighterRow* winner = FindById(snap, snap.lastStandingId);
            if (winner)
                std::sprintf(snap.header, "%s last standing", winner->name);
            else
                SetHeader(snap, "Last standing");

            if (winner)
            {
                snap.podium[0].place = 1;
                snap.podium[0].fighter = *winner;
                snap.podiumCount = 1;
            }

            for (int i = snap.eliminationCount - 1; i >= 0 && snap.podiumCount < 3; --i)
            {
                const int id = snap.eliminationIds[i];
                if (id == snap.lastStandingId)
                    continue;
                const FighterRow* row = FindById(snap, id);
                if (!row)
                    continue;
                snap.podium[snap.podiumCount].place = snap.podiumCount + 1;
                snap.podium[snap.podiumCount].fighter = *row;
                ++snap.podiumCount;
            }
            return;
        }
        }
    }
}
