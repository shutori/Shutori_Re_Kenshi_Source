// LeaderboardStore.h — per-save MMR career records
#pragma once

#include "LeaderboardData.h"
#include <string>
#include <vector>

class Character;
class Building;
namespace ArenaPersistence { struct Snapshot; }
namespace ArenaLedger { struct Purchase; }
namespace RewardTransaction { class ReservedAccounting; }
namespace TownChallengePolicy { struct Card; }
namespace TownChallengeBuyInPolicy { struct Account; }
namespace TownDiagnosticData { struct State; }

namespace SparPodium
{
    struct Snapshot;
}

namespace LeaderboardStore
{
    TownChallengePolicy::Card& GetTownChallenges();
    int& GetBookieCredit();
    TownChallengeBuyInPolicy::Account& GetChallengeAccount();
    TownDiagnosticData::State& GetTownDiagnostic();
    std::string GetDiagnosticError();
    void ClearDiagnosticError();

    bool ResetTownStandings();
    typedef LeaderboardData::Standing Record;

    // Compatibility no-op; native lifecycle explicitly activates the ledger.
    void EnsureLoaded();

    // Called only for identified town arena NPCs. Preserve players/prisoners.
    bool RemoveTownNpcProgression(Character* character);

    // Block and clear the active world ledger during load/teardown.
    void Unload();
    // Called only after a native New Game/Load/Import operation completes.
    void CompleteWorldTransition(bool newGame);

    // Commit an active world snapshot into the native save target. The caller
    // captures both names before the native save runs, so a stale pending name
    // can never turn a save into a sidecar context switch.
    bool CommitSaveSnapshot(
        const std::string& sourceSaveName,
        const std::string& targetSaveName);

    // Apply a rated arena match for all participants; no-op for Stop/Draw.
    void ApplyFromSnapshot(
        SparPodium::Snapshot& snap,
        LeaderboardData::Kind kind,
        float marksMultiplier = 1.0f,
        int marksTeamBonus = 0);

    // All arena participants with matches >= 1, sorted by MMR descending.
    void GetStandings(
        LeaderboardData::Kind kind,
        std::vector<Record>& out);

    // Current per-save rating for a fighter, or the default for an unrated fighter.
    float GetRating(
        Character* character,
        LeaderboardData::Kind kind = LeaderboardData::Player);

    // Persisted rated match count for deterministic per-career decisions.
    int GetMatchCount(
        Character* character,
        LeaderboardData::Kind kind = LeaderboardData::Player);

    // Current unspent Arena Marks for a fighter, or zero when unrecorded.
    int GetMarks(Character* character);

    // Highest purchased tier for a stable reward id; -1 means unclaimed.
    int GetRewardTier(Character* character, const char* rewardId);

    // Reserve/undo personal Marks and one sequential reward tier.
    bool BeginRewardTierPurchase(
        Character* character,
        int cost,
        const char* rewardId,
        int expectedPreviousTier,
        int nextTier);
    void RollbackRewardTierPurchase(
        Character* character,
        int cost,
        const char* rewardId,
        int previousTier);

    // Faction-wide catalogue access.
    bool HasFactionUnlock(const char* unlockId);
    bool BeginFactionUnlockPurchase(
        Character* character,
        int cost,
        const char* unlockId);
    void RollbackFactionUnlockPurchase(
        Character* character,
        int cost,
        const char* unlockId);
    bool AddFactionUnlock(const char* unlockId);
    void RemoveFactionUnlock(const char* unlockId);

    // Complete a reservation in memory; never reads or writes a file.
    bool CompleteProgressionTransaction();
    // Resolve identity once at reservation. Cancellation thereafter owns the
    // captured career/world token rather than the current Character binding.
    bool ReserveProgression(Character* character, const ArenaLedger::Purchase& purchase,
        RewardTransaction::ReservedAccounting& accounting);

    bool CaptureSnapshot(ArenaPersistence::Snapshot& out);
    void ActivateSnapshot(const ArenaPersistence::Snapshot& snapshot);
    void BlockPersistence(const std::string& reason);
    std::string GetPersistenceError();

    // Reserve/refund personal Marks for non-armour rewards such as recruitment.
    bool SpendMarks(Character* character, int cost);
    void RefundMarks(Character* character, int amount);

    // Debug tooling: overwrite a fighter's in-memory rating.
    bool SetRating(Character* character, float rating);

    // Debug tooling: overwrite a fighter's in-memory Arena Marks.
    bool SetMarks(Character* character, int marks);

    // Live squad member or cached prisoner for portrait; may be null.
    Character* FindRatedCharacter(
        LeaderboardData::Kind kind,
        const std::string& id,
        Building* source);

    const std::string& GetActiveSaveKey();
    bool HasActiveSave();
}
