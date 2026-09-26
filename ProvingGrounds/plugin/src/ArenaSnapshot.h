#pragma once
#include "LeaderboardData.h"
#include "TownChallengePolicy.h"
#include "TownDiagnosticData.h"
namespace ArenaPersistence {
    struct PlannedNpcBout {
        bool active;
        std::string registryId;
        double startHours, scoreA, scoreB;
        std::vector<std::string> teamA, teamB;
        bool market, wagerPending;
        int wagerSide, stake, payout;
        PlannedNpcBout() : active(false), startHours(0), scoreA(0), scoreB(0),
            market(false), wagerPending(false), wagerSide(-1), stake(0), payout(0) {}
    };
    enum LoadCode { Ready, Missing, LegacyReset, InvalidJson, InvalidData,
        UnsupportedVersion, WrongSnapshot, IoFailure };
    struct Snapshot {
        int version;
        std::string saveKey, generation;
        LeaderboardData::Collections fighters;
        TownChallengePolicy::Card challenges;
        int bookieCredit, challengeCredit;
        TownDiagnosticData::State diagnostic;
        PlannedNpcBout plannedNpc;
        // Runtime-only warning. It is deliberately not serialized; a later save
        // replaces an invalid diagnostic subsection with the safe default state.
        std::string diagnosticError;
        Snapshot() : version(16), bookieCredit(0), challengeCredit(0) {}
    };
    struct LoadResult {
        LoadCode code;
        Snapshot snapshot;
        std::string field, message;
        LoadResult() : code(Missing) {}
    };
}
