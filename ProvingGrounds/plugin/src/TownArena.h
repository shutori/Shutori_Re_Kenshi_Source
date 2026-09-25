#pragma once
#include <string>
#include <vector>
class Character;
class Building;
class Faction;
namespace TownChallengeBuyInPolicy { struct Quote; }
namespace SparPodium { struct Snapshot; }

namespace TownArena
{
    void BindRegistry(Building* registry);
    bool BindRegistryNear(Character* character);
    std::string GetChallengeCaption(int slot, bool includeLineup = true);
    TownChallengeBuyInPolicy::Quote GetChallengeBuyIn(int slot);
    // Presentation queries use the same offer and readiness rules as booking.
    int GetChallengeRarity(int slot);
    bool IsUniqueLegendaryChallenge(int slot);
    std::string GetChallengeUnavailableReason(int slot);
    // Exact displayed opponents, resolved only for this UI refresh. Null entries
    // retain unavailable members' positions; unresolved offers return no members.
    void GetChallengePortraits(std::vector<Character*> (&opponents)[6], std::vector<std::string> (&names)[6]);
    bool GetPlayerReadiness(Character* player, std::string& reason);
    std::string GetDirectCaption();
    int GetDivision();
    std::string GetNextChallengeRefreshCaption();
    int GetChallengeRefreshCost();
    bool CanBuyChallengeRefresh();
    bool BuyChallengeRefresh();
    bool SetDivision(int division);
    bool IsChallengeVisible(int slot);
    // Forces the next registry refresh to recapture main-thread roster health.
    void RefreshMatchmakingRoster();
    void UpdateMatchmakingPreview(const std::vector<Character*>& players);
    std::string GetProgressCaption();
    void OnMatchResult(const SparPodium::Snapshot& result);
    void OnMatchAborted();
    bool BookChallenge(const std::vector<Character*>& players, int slot);
    // Debug-only pause boundary. Ambient bouts are enabled by default.
    void SetScheduleEnabled(bool enabled);
    bool IsScheduleEnabled();
    bool BookPlayer(Character* player);
    bool HasBooking();
    void CancelBooking();
    bool IsPlayerMatch();
    bool IsLineupLocked();
    bool IsChallengeSearching(int slot);
    bool IsBestAvailableChallenge(int slot);
    unsigned GetUniqueWins();
    bool IsSkarnUnlocked();
    // Captured when the event starts; browsing other cards cannot change rewards.
    float GetActiveMarksMultiplier();
    int GetActiveMarksBonus();
    bool BeginTrial();
    bool HasPlannedNpcBout();
    double GetPlannedNpcStartHours();
    void CancelPlannedNpcBout(const std::string& reason);
    void Tick();
    void Cancel();
    void AbandonWorldState();
    bool IsBusy();
    bool IsAftercare();
    bool IsFighter(Character* character);
    bool OwnsMatch();
    // Incident reporting only; does not permit attacks after KO.
    bool IsIncidentPair(Character* a, Character* b);
    // Live arena collateral only; targeting isolation still rejects deliberate
    // attacks between booked fighters and outsiders.
    bool IsActiveCollateralIncidentPair(Character* a, Character* b);
    // Retained cross-team faction identity for asynchronous combat outcome
    // notifications that arrive without the characters that caused them.
    bool IsIncidentFactionPair(Faction* a, Faction* b);
    void OnMatchStarted();
    bool IsNpcReadyForPreparation(Character* character);
    // Excludes an NPC that could not reach its arena formation mark until the
    // current world session is torn down.
    void QuarantineIngressFighter(Character* character);
    // Cancels/refunds an unready locked NPC bout before ingress starts combat.
    bool ValidatePreparation();
    const std::string& GetStatus();
}
