#include "MatchRules.h"

namespace MatchRules
{
    bool IsOpponent(MatchMode mode, const MatchParticipant& a, const MatchParticipant& b)
    {
        if (a.id == b.id)
            return false;
        if (mode == ModeTeamAvB || mode == ModeTeams1v1)
            return a.team != TeamNone && b.team != TeamNone && a.team != b.team;
        return true; // LastStanding
    }

    // Returns true when a team counts as defeated. downedPercent 0 requires every
    // member to be down (shipped behaviour); a positive value ends the team once
    // that percentage is down.
    static bool TeamDown(const MatchParticipant* list, int count, MatchTeam team,
        bool honorEliminated, int downedPercent)
    {
        int total = 0;
        int down = 0;
        for (int i = 0; i < count; ++i)
        {
            if (list[i].team != team)
                continue;
            ++total;
            const bool up = list[i].conscious && !(honorEliminated && list[i].eliminated);
            if (!up)
                ++down;
        }
        if (total <= 0)
            return false;
        if (downedPercent <= 0)
            return down == total;
        // Integer comparison, so a 1-fighter team ends at down == 1 for any
        // threshold and a 4-fighter team at 50% ends at down == 2.
        return down * 100 >= downedPercent * total;
    }

    MatchEndKind EvaluateEnd(MatchMode mode, const MatchParticipant* list, int count,
        int downedPercent)
    {
        if (!list || count <= 0)
            return EndNone;

        if (mode == ModeTeamAvB || mode == ModeTeams1v1)
        {
            const bool honorEliminated = (mode == ModeTeams1v1);
            const bool aDown = TeamDown(list, count, TeamA, honorEliminated, downedPercent);
            const bool bDown = TeamDown(list, count, TeamB, honorEliminated, downedPercent);
            if (aDown && bDown)
                return EndDraw;
            if (aDown)
                return EndTeamWipeA;
            if (bDown)
                return EndTeamWipeB;
            return EndNone;
        }

        // LastStanding
        int awake = 0;
        for (int i = 0; i < count; ++i)
        {
            if (list[i].conscious)
                ++awake;
        }
        if (awake <= 0)
            return EndDraw;
        if (awake == 1)
            return EndLastStanding;
        return EndNone;
    }
}
