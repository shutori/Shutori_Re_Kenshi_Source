// MatchRules.h — no Character includes
#pragma once

namespace MatchRules
{
    enum MatchMode { ModeTeamAvB, ModeTeams1v1, ModeLastStanding };
    enum MatchTeam { TeamNone, TeamA, TeamB };
    enum MatchEndKind {
        EndNone, EndTeamWipeA, EndTeamWipeB, EndLastStanding, EndDraw
    };

    struct MatchParticipant
    {
        int id;
        MatchTeam team;
        bool conscious; // false if KO or dead for "conscious" checks
        bool dead;
        // Teams 1v1: once KO'd in a bout, stays out even if they wake up.
        bool eliminated;
    };

    bool IsOpponent(MatchMode mode, const MatchParticipant& a, const MatchParticipant& b);
    MatchEndKind EvaluateEnd(MatchMode mode, const MatchParticipant* list, int count);
}
