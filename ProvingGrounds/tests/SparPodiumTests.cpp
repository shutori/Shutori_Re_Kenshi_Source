#include "../plugin/src/SparPodium.h"
#include <cstdio>
#include <cstring>

using namespace SparPodium;

static int g_fails = 0;
static void Expect(bool c, const char* m)
{
    if (!c) { std::printf("FAIL: %s\n", m); ++g_fails; }
}

static FighterRow Row(int id, MatchRules::MatchTeam team, const char* name,
    float dealt, float taken, int hits)
{
    FighterRow r = {};
    r.id = id;
    r.team = team;
    std::strncpy(r.name, name, 63);
    r.name[63] = '\0';
    r.damageDealt = dealt;
    r.damageTaken = taken;
    r.hitsLanded = hits;
    r.eliminationIndex = -1;
    return r;
}

int main()
{
    Snapshot s = {};
    s.mode = MatchRules::ModeTeamAvB;
    s.outcome = OutcomeTeamAWins;
    s.fighterCount = 4;
    s.fighters[0] = Row(1, MatchRules::TeamA, "Beep", 100.f, 10.f, 5);
    s.fighters[1] = Row(2, MatchRules::TeamA, "Ruka", 50.f, 20.f, 3);
    s.fighters[2] = Row(3, MatchRules::TeamB, "Hobbs", 200.f, 5.f, 9);
    s.fighters[3] = Row(4, MatchRules::TeamA, "Burn", 75.f, 15.f, 4);
    BuildPodium(s);
    Expect(s.podiumCount == 3, "team A top3 count");
    Expect(s.podium[0].fighter.id == 1, "team A #1 Beep");
    Expect(s.podium[1].fighter.id == 4, "team A #2 Burn");
    Expect(s.podium[2].fighter.id == 2, "team A #3 Ruka");
    Expect(std::strstr(s.header, "Team A") != 0, "header Team A");

    Snapshot mvp = {};
    mvp.mode = MatchRules::ModeTeamAvB;
    mvp.outcome = OutcomeTeamAWins;
    mvp.fighterCount = 4;
    mvp.fighters[0] = Row(20, MatchRules::TeamA, "Glass Cannon", 120.f, 120.f, 6);
    mvp.fighters[0].misses = 8;
    mvp.fighters[0].eliminationIndex = 0;
    mvp.fighters[1] = Row(21, MatchRules::TeamA, "Guardian", 95.f, 35.f, 5);
    mvp.fighters[1].damageMitigated = 60.f;
    mvp.fighters[1].blocks = 5;
    mvp.fighters[1].misses = 1;
    mvp.fighters[1].dodges = 3;
    mvp.fighters[2] = Row(22, MatchRules::TeamA, "Precise", 90.f, 20.f, 6);
    mvp.fighters[2].dodges = 1;
    mvp.fighters[3] = Row(23, MatchRules::TeamA, "Striker", 200.f, 80.f, 8);
    mvp.fighters[3].misses = 4;
    mvp.fighters[3].eliminationIndex = 1;
    BuildPodium(mvp);
    Expect(mvp.podium[0].fighter.id == 23, "MVP offense remains primary");
    Expect(mvp.podium[1].fighter.id == 21, "MVP rewards defense and efficiency");
    Expect(mvp.podium[2].fighter.id == 22, "MVP rewards accurate survivor");

    Snapshot d = {};
    d.mode = MatchRules::ModeTeamAvB;
    d.outcome = OutcomeDraw;
    d.fighterCount = 2;
    d.fighters[0] = Row(1, MatchRules::TeamA, "A", 1.f, 1.f, 1);
    d.fighters[1] = Row(2, MatchRules::TeamB, "B", 1.f, 1.f, 1);
    BuildPodium(d);
    Expect(d.podiumCount == 0, "draw empty podium");
    Expect(std::strstr(d.header, "Draw") != 0, "draw header");

    Snapshot ls = {};
    ls.mode = MatchRules::ModeLastStanding;
    ls.outcome = OutcomeLastStanding;
    ls.fighterCount = 4;
    ls.fighters[0] = Row(10, MatchRules::TeamNone, "FirstOut", 10.f, 1.f, 1);
    ls.fighters[1] = Row(11, MatchRules::TeamNone, "SecondOut", 20.f, 1.f, 1);
    ls.fighters[2] = Row(12, MatchRules::TeamNone, "ThirdOut", 5.f, 1.f, 1);
    ls.fighters[3] = Row(13, MatchRules::TeamNone, "Winner", 30.f, 1.f, 1);
    ls.eliminationCount = 3;
    ls.eliminationIds[0] = 10;
    ls.eliminationIds[1] = 11;
    ls.eliminationIds[2] = 12;
    ls.lastStandingId = 13;
    BuildPodium(ls);
    Expect(ls.podiumCount == 3, "LMS podium 3");
    Expect(ls.podium[0].fighter.id == 13, "LMS 1st winner");
    Expect(ls.podium[1].fighter.id == 12, "LMS 2nd last KO");
    Expect(ls.podium[2].fighter.id == 11, "LMS 3rd");

    Snapshot st = {};
    st.mode = MatchRules::ModeTeams1v1;
    st.outcome = OutcomeStopped;
    st.fighterCount = 3;
    st.fighters[0] = Row(1, MatchRules::TeamA, "A", 10.f, 0.f, 1);
    st.fighters[1] = Row(2, MatchRules::TeamB, "B", 40.f, 0.f, 2);
    st.fighters[2] = Row(3, MatchRules::TeamA, "C", 25.f, 0.f, 3);
    BuildPodium(st);
    Expect(st.podiumCount == 3, "stopped count");
    Expect(st.podium[0].fighter.id == 2, "stopped #1 by damage");
    Expect(std::strstr(st.header, "Stopped") != 0, "stopped header");

    if (g_fails) { std::printf("%d failures\n", g_fails); return 1; }
    std::printf("SparPodiumTests OK\n");
    return 0;
}
