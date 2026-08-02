#include "../plugin/src/MmrRating.h"
#include <cstdio>
#include <cmath>
#include <cstring>

using namespace SparPodium;

static int g_fails = 0;
static void Expect(bool c, const char* m)
{
    if (!c) { std::printf("FAIL: %s\n", m); ++g_fails; }
}

static void ExpectNear(float a, float b, float eps, const char* m)
{
    if (std::fabs(a - b) > eps)
    {
        std::printf("FAIL: %s (got %f expected %f)\n", m, a, b);
        ++g_fails;
    }
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
    Expect(MmrRating::ShouldRate(OutcomeTeamAWins), "rate team A");
    Expect(MmrRating::ShouldRate(OutcomeLastStanding), "rate LMS");
    Expect(!MmrRating::ShouldRate(OutcomeStopped), "skip stop");
    Expect(!MmrRating::ShouldRate(OutcomeDraw), "skip draw");

    ExpectNear(MmrRating::kDefaultMmr, 0.0f, 0.001f, "default rating starts at zero");
    ExpectNear(MmrRating::kFloorMmr, 0.0f, 0.001f, "rating floor is zero");

    Expect(MmrRating::KFactor(0) == MmrRating::kKEarly, "early K");
    Expect(MmrRating::KFactor(19) == MmrRating::kKEarly, "early K 19");
    Expect(MmrRating::KFactor(20) == MmrRating::kKLate, "late K");

    ExpectNear(MmrRating::ExpectedScore(1000.f, 1000.f), 0.5f, 0.001f, "E equal");

    FighterRow rows[2];
    rows[0] = Row(0, MatchRules::TeamA, "A", 200.f, 10.f, 10);
    rows[1] = Row(1, MatchRules::TeamB, "B", 10.f, 200.f, 1);
    float mult[2];
    MmrRating::ComputePerfMultipliers(rows, 2, mult);
    ExpectNear(mult[0], MmrRating::kPerfMultMax, 0.001f, "best perf max");
    ExpectNear(mult[1], MmrRating::kPerfMultMin, 0.001f, "worst perf min");

    FighterRow same[2];
    same[0] = Row(0, MatchRules::TeamA, "A", 50.f, 0.f, 1);
    same[1] = Row(1, MatchRules::TeamB, "B", 50.f, 0.f, 1);
    float sameMult[2];
    MmrRating::ComputePerfMultipliers(same, 2, sameMult);
    ExpectNear(sameMult[0], 1.0f, 0.001f, "equal perf 0");
    ExpectNear(sameMult[1], 1.0f, 0.001f, "equal perf 1");

    Snapshot snap = {};
    snap.mode = MatchRules::ModeTeamAvB;
    snap.outcome = OutcomeTeamAWins;
    snap.fighterCount = 2;
    snap.fighters[0] = Row(0, MatchRules::TeamA, "Winner", 100.f, 20.f, 5);
    snap.fighters[1] = Row(1, MatchRules::TeamB, "Loser", 40.f, 80.f, 2);
    float before[2] = { 1000.f, 1000.f };
    int matches[2] = { 0, 0 };
    bool rate[2] = { true, true };
    float after[2] = {};
    int wins[2] = {};
    int losses[2] = {};
    MmrRating::ApplyMatch(snap, before, matches, rate, after, wins, losses);
    Expect(after[0] > before[0], "winner gains");
    Expect(after[1] < before[1], "loser loses");
    Expect(wins[0] == 1 && losses[0] == 0, "winner W");
    Expect(wins[1] == 0 && losses[1] == 1, "loser L");
    Expect(after[0] - before[0] <= MmrRating::kMaxAbsDelta + 0.01f, "clamp win");
    Expect(before[1] - after[1] <= MmrRating::kMaxAbsDelta + 0.01f, "clamp loss");
    // A fighter who performs strongly despite losing should lose less rating,
    // while a weak-performing winner receives a smaller gain.
    Snapshot upset = snap;
    upset.fighters[0] = Row(0, MatchRules::TeamA, "WeakWinner", 1.f, 200.f, 1);
    upset.fighters[1] = Row(1, MatchRules::TeamB, "StrongLoser", 200.f, 1.f, 10);
    float upsetAfter[2] = {};
    MmrRating::ApplyMatch(
        upset, before, matches, rate, upsetAfter, wins, losses);
    Expect(upsetAfter[0] - before[0] < 16.0f, "weak winner gains less");
    Expect(before[1] - upsetAfter[1] < 16.0f, "strong loser loses less");

    Snapshot stopped = snap;
    stopped.outcome = OutcomeStopped;
    float afterStop[2] = {};
    MmrRating::ApplyMatch(stopped, before, matches, rate, afterStop, wins, losses);
    ExpectNear(afterStop[0], before[0], 0.001f, "stop no change 0");
    ExpectNear(afterStop[1], before[1], 0.001f, "stop no change 1");

    Snapshot draw = snap;
    draw.outcome = OutcomeDraw;
    float afterDraw[2] = {};
    MmrRating::ApplyMatch(draw, before, matches, rate, afterDraw, wins, losses);
    ExpectNear(afterDraw[0], before[0], 0.001f, "draw no change");

    bool rateOne[2] = { true, false };
    float afterMask[2] = {};
    int winsM[2] = {};
    int lossesM[2] = {};
    MmrRating::ApplyMatch(snap, before, matches, rateOne, afterMask, winsM, lossesM);
    Expect(afterMask[0] != before[0], "masked rates self");
    ExpectNear(afterMask[1], before[1], 0.001f, "masked skips other");

    float lowBefore[1] = { 5.f };
    int lowMatches[1] = { 0 };
    bool lowRate[1] = { true };
    Snapshot lms = {};
    lms.mode = MatchRules::ModeLastStanding;
    lms.outcome = OutcomeLastStanding;
    lms.fighterCount = 1;
    lms.lastStandingId = 99;
    lms.fighters[0] = Row(0, MatchRules::TeamNone, "AlmostFloor", 1.f, 1.f, 0);
    float lowAfter[1] = {};
    int lowW[1] = {};
    int lowL[1] = {};
    MmrRating::ApplyMatch(lms, lowBefore, lowMatches, lowRate, lowAfter, lowW, lowL);
    Expect(lowAfter[0] >= MmrRating::kFloorMmr, "floor mmr");
    ExpectNear(lowAfter[0], 0.0f, 0.001f, "loss clamps to zero");

    if (g_fails == 0)
        std::printf("MmrRatingTests: OK\n");
    else
        std::printf("MmrRatingTests: %d FAIL(s)\n", g_fails);
    return g_fails == 0 ? 0 : 1;
}
