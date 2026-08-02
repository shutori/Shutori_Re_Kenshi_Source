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
    bool AllMatchHandlersReady(float maxDistanceFromPrisoner);
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
    bool MatchIncludesPrisoner();

    // Handler runs to cage, unlocks (releases) prisoner, then prisoner can walk.
    void BeginHandlerUnlocks();
    void TickHandlerUnlocks();
    // True if this prisoner was released this tick (caller should issue arena move).
    bool ConsumeNewlyReleased(Character* prisoner);
    // Walk the assigned handler to a side-by-side marker beside the prisoner.
    void EscortHandlerBesidePrisoner(
        Character* prisoner, const Ogre::Vector3& prisonerTarget);

    // Strip outsider combat focus from match prisoners.
    void ProtectMatchPrisoners();

    // HOLD+PASSIVE handlers at their current gather positions for the spar.
    void ParkMatchHandlers();

    // Async escorted / carry return. Tick until IsReturning() is false.
    void BeginReturnToCages(std::string& statusOut);
    void TickReturn();
    bool IsReturning();

    // Synchronous force-cage (fail / emergency paths).
    void ReturnAllToCages(std::string& statusOut);
    void ClearMatchState();
}
