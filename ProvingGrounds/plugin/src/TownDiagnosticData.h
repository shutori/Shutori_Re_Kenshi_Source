#pragma once

namespace TownDiagnosticData {
enum RunStatus { Off, Active, Paused, Complete, Ended };
enum Model { StatsOnly, ReducedMmr, FullMmr, ModelCount };
enum Coverage { Duel, EqualGroup, DifferenceOne, DifferenceLarge, CoverageCount };

struct Config {
    int target;
    bool models[ModelCount];
    bool coverage[CoverageCount];
    double reducedInfluence;
    int underdogPercent;
    bool balanceStyleTier;
    Config() : target(150), reducedInfluence(.25), underdogPercent(20), balanceStyleTier(true) {
        for (int i = 0; i < ModelCount; ++i) models[i] = true;
        for (int i = 0; i < CoverageCount; ++i) coverage[i] = true;
    }
};

struct Counters {
    int opened, completed, cancelled, aborted;
    int teamAWins, teamBWins, largerWins, smallerWins, equalWins, favoriteWins;
    int modelCompleted[ModelCount];
    int coverageCompleted[CoverageCount];
    double schedulingHours, lastCompletedHours;
    int schedulingSamples;
    Counters() : opened(0), completed(0), cancelled(0), aborted(0),
        teamAWins(0), teamBWins(0), largerWins(0), smallerWins(0), equalWins(0),
        favoriteWins(0), schedulingHours(0), lastCompletedHours(0), schedulingSamples(0) {
        for (int i = 0; i < ModelCount; ++i) modelCompleted[i] = 0;
        for (int i = 0; i < CoverageCount; ++i) coverageCompleted[i] = 0;
    }
};

struct Pending {
    bool active, underdog;
    unsigned sequence;
    int model, requestedCoverage, actualCoverage, relaxation;
    int sizeA, sizeB;
    double scoreA[ModelCount], scoreB[ModelCount], probabilityA[ModelCount];
    double announcedHours;
    Pending() : active(false), underdog(false), sequence(0), model(StatsOnly),
        requestedCoverage(Duel), actualCoverage(Duel), relaxation(0), sizeA(0), sizeB(0), announcedHours(0) {
        for (int i = 0; i < ModelCount; ++i) scoreA[i] = scoreB[i] = probabilityA[i] = 0;
    }
};

struct State {
    int status;
    unsigned runId, nextSequence;
    Config config;
    Counters counters;
    Pending pending;
    State() : status(Off), runId(0), nextSequence(1) {}
};

inline const char* StatusName(int value) {
    static const char* names[] = {"off", "active", "paused", "complete", "ended"};
    return value >= Off && value <= Ended ? names[value] : "invalid";
}
inline const char* ModelName(int value) {
    static const char* names[] = {"stats", "reduced_mmr", "full_mmr"};
    return value >= 0 && value < ModelCount ? names[value] : "invalid";
}
inline const char* CoverageName(int value) {
    static const char* names[] = {"1v1", "equal_group", "difference_one", "difference_large"};
    return value >= 0 && value < CoverageCount ? names[value] : "invalid";
}
}
