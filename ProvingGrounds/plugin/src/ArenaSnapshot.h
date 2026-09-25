#pragma once
#include "LeaderboardData.h"
#include "TownChallengePolicy.h"
#include "TownDiagnosticData.h"
namespace ArenaPersistence {
    enum LoadCode { Ready, Missing, LegacyReset, InvalidJson, InvalidData,
        UnsupportedVersion, WrongSnapshot, IoFailure };
    struct Snapshot {
        int version;
        std::string saveKey, generation;
        LeaderboardData::Collections fighters;
        TownChallengePolicy::Card challenges;
        int bookieCredit, challengeCredit;
        TownDiagnosticData::State diagnostic;
        // Runtime-only warning. It is deliberately not serialized; a later save
        // replaces an invalid diagnostic subsection with the safe default state.
        std::string diagnosticError;
        Snapshot() : version(15), bookieCredit(0), challengeCredit(0) {}
    };
    struct LoadResult {
        LoadCode code;
        Snapshot snapshot;
        std::string field, message;
        LoadResult() : code(Missing) {}
    };
}
