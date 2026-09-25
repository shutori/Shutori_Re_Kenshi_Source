#pragma once

#include <string>
#include <vector>

class Building;
class Character;
class UseableStuff;
namespace Ogre { class Vector3; }

namespace PrisonerUtil
{
    static const float kCageNearRegistry = 2500.0f;

    void CollectNearbyCagePrisoners(Building* registry, std::vector<Character*>& out);
    bool IsRosterPrisoner(Character* c);
    bool IsMatchPrisoner(Character* c);
    bool IsMatchPrisonerReleased(Character* c);
    bool AllMatchPrisonersReleased();
    UseableStuff* FindCageForOccupant(Character* c);
    int CountHandlerCandidates(
        const std::vector<Character*>& squadInRange,
        const std::vector<Character*>& matchFighters);
    bool PrepareMatch(
        const std::vector<Character*>& prisonerFighters,
        const std::vector<Character*>& handlerCandidates,
        std::string& statusOut);
    const std::vector<Character*>& GetMatchHandlers();
    const std::vector<Character*>& GetMatchPrisoners();
    const std::vector<Character*>& GetRosterPrisoners();
    void ForgetRosterPrisoner(Character* prisoner);
    bool MatchIncludesPrisoner();

    // Handler runs to cage, unlocks (releases) prisoner, then prisoner can walk.
    void BeginHandlerUnlocks();
    void TickHandlerUnlocks();
    // True if this prisoner was released this tick (caller should issue arena move).
    bool ConsumeNewlyReleased(Character* prisoner);
    // Move the released prisoner and assigned handler to stable arena marks.
    void SendPairToArena(
        Character* prisoner, const Ogre::Vector3& prisonerTarget);

    // Strip outsider combat focus from match prisoners.
    void ProtectMatchPrisoners();
    // Let a handler stabilize only their eliminated prisoner during a fight.
    void TickRingsideAid();

    // HOLD+PASSIVE handlers at their current gather positions for the spar.
    void ParkMatchHandlers();
    // Re-assert HOLD without clearing AI (safe each tick during the match).
    void ReinforceParkedHandlers();
    // Cadenced protection and handler upkeep for the GUI/update thread.
    void TickMatchUpkeep();

    // Async escorted / carry return. Tick until IsReturning() is false.
    void BeginReturnToCages(std::string& statusOut);
    void TickReturn();
    bool IsReturning();

    // Lock orders remain pending after placement, including forced returns.
    // TickReturn calls this from the GUI update independently of spar state.
    void TickPendingCageLocks();
    bool HasPendingCageLocks();
    bool HasCageReturnFailure();
    const std::string& GetCageReturnStatus();

    // Force placement (fail / emergency paths); locking remains asynchronous.
    void ReturnAllToCages(std::string& statusOut);
    void ClearMatchState();

    // Includes prepared, unlocking, active, or returning prisoner state.
    bool HasRuntimeActivity();
    // Load/teardown path: clear pointers without restoring or moving actors.
    void AbandonWorldState();
}
