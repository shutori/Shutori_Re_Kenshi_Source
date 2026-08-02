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

    static bool TeamAllDown(const MatchParticipant* list, int count, MatchTeam team, bool honorEliminated)
    {
        bool any = false;
        for (int i = 0; i < count; ++i)
        {
            if (list[i].team != team)
                continue;
            any = true;
            const bool up = list[i].conscious && !(honorEliminated && list[i].eliminated);
            if (up)
                return false;
        }
        return any;
    }

    MatchEndKind EvaluateEnd(MatchMode mode, const MatchParticipant* list, int count)
    {
        if (!list || count <= 0)
            return EndNone;

        if (mode == ModeTeamAvB || mode == ModeTeams1v1)
        {
            const bool honorEliminated = (mode == ModeTeams1v1);
            const bool aDown = TeamAllDown(list, count, TeamA, honorEliminated);
            const bool bDown = TeamAllDown(list, count, TeamB, honorEliminated);
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
