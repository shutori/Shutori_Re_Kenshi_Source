#include "../plugin/src/MatchRules.h"
#include <cstdio>
#include <cstdlib>

using namespace MatchRules;

static int g_fails = 0;
static void Expect(bool cond, const char* msg)
{
    if (!cond) { std::printf("FAIL: %s\n", msg); ++g_fails; }
}

int main()
{
    MatchParticipant avb[] = {
        {1, TeamA, true, false},
        {2, TeamA, true, false},
        {3, TeamB, true, false},
        {4, TeamB, false, false} // KO
    };
    Expect(IsOpponent(ModeTeamAvB, avb[0], avb[2]), "AvB cross-team enemy");
    Expect(!IsOpponent(ModeTeamAvB, avb[0], avb[1]), "AvB same-team not enemy");
    Expect(EvaluateEnd(ModeTeamAvB, avb, 4) == EndNone, "AvB not wiped yet");

    avb[2].conscious = false;
    Expect(EvaluateEnd(ModeTeamAvB, avb, 4) == EndTeamWipeB, "AvB B wiped");

    MatchParticipant t1[] = {
        {1, TeamA, true, false},
        {2, TeamA, true, false},
        {3, TeamB, true, false},
        {4, TeamB, true, false}
    };
    Expect(IsOpponent(ModeTeams1v1, t1[0], t1[2]), "Teams1v1 cross-team");
    Expect(!IsOpponent(ModeTeams1v1, t1[0], t1[1]), "Teams1v1 same-team");
    Expect(EvaluateEnd(ModeTeams1v1, t1, 4) == EndNone, "Teams1v1 all up continues");

    t1[0].conscious = false; // first A down, A2 still up
    Expect(EvaluateEnd(ModeTeams1v1, t1, 4) == EndNone, "Teams1v1 first A KO continues");

    t1[1].conscious = false; // all A down
    Expect(EvaluateEnd(ModeTeams1v1, t1, 4) == EndTeamWipeA, "Teams1v1 A wiped");

    // A1 woke up after being KO'd, but is eliminated — match must still end.
    MatchParticipant wake[] = {
        {1, TeamA, true, false, true},   // awake but eliminated
        {2, TeamA, false, false, true},  // KO + eliminated
        {3, TeamB, true, false, false},
        {4, TeamB, true, false, false}
    };
    Expect(EvaluateEnd(ModeTeams1v1, wake, 4) == EndTeamWipeA, "Teams1v1 wake-up still wiped");

    MatchParticipant ls[] = {
        {1, TeamNone, false, false, false},
        {2, TeamNone, true, false, false},
        {3, TeamNone, false, false, false}
    };
    Expect(EvaluateEnd(ModeLastStanding, ls, 3) == EndLastStanding, "Last standing");

    if (g_fails) { std::printf("%d failures\n", g_fails); return 1; }
    std::printf("MatchRulesTests OK\n");
    return 0;
}
