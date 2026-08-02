#pragma once

#include "MatchRules.h"
#include <string>

class Character;
class RootObject;
class Building;

namespace ArenaIngress
{
    enum LocationMode
    {
        LocationArena = 0,
        LocationBanner = 1
    };

    void BindRegistry(RootObject* registry);
    void ClearBind();
    bool HasValidRegistry();
    Building* GetBoundRegistry();

    void SetLocationMode(LocationMode mode);
    LocationMode GetLocationMode();

    bool HasLocationSite();
    const char* GetMissingLocationMessage();

    bool IsPending();
    const std::string& GetStatus();

    // Selected characters walk to finished Registry or Leaderboard;
    // on arrival opens Arena UI or Leaderboard UI respectively.
    bool BeginApproachForUI(Building* building);
    void NotifyExternalOrder(Character* fighter);

    bool Begin(MatchRules::MatchMode mode, Character** fighters, MatchRules::MatchTeam* teams, int count);
    bool Begin(
        MatchRules::MatchMode mode,
        Character** fighters,
        MatchRules::MatchTeam* teams,
        int count,
        Character** escorts,
        int escortCount);
    void Cancel();
    void Tick();

    // Teams 1v1 replacement: walk a bench fighter to their side's pit marker.
    bool BeginWalkIn(Character* fighter, MatchRules::MatchTeam team);
    bool IsWalkInPending();
    bool IsWalkInArrived();
    void TickWalkIn();
    void ClearWalkIn();
}
