// MatchRules.h — no Character includes
#pragma once

namespace MatchRules
{
    enum MatchMode { ModeTeamAvB, ModeTeams1v1, ModeLastStanding };
    enum MatchTeam { TeamNone, TeamA, TeamB };
    enum MatchEndKind {
        EndNone, EndTeamWipeA, EndTeamWipeB, EndLastStanding, EndDraw
    };

    // Discriminating label for the log. CombatBalanceLog used to record only
    // SparSession::StopReason, which routes EVERY terminal outcome through the
    // KO branch, so a team wipe and a mutual knockout were indistinguishable.
    inline const char* EndKindName(MatchEndKind kind) {
        switch (kind) {
        case EndTeamWipeA: return "team_wipe_a";
        case EndTeamWipeB: return "team_wipe_b";
        case EndLastStanding: return "last_standing";
        case EndDraw: return "draw";
        default: return "none";
        }
    }

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
    // downedPercent 0 requires an entire team to be down (shipped behaviour).
    // A positive value ends a team bout as soon as that percentage of the team is
    // down, which bounds the injury load a single bout can inflict.
    MatchEndKind EvaluateEnd(MatchMode mode, const MatchParticipant* list, int count,
        int downedPercent = 0);
}
