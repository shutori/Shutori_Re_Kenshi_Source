#pragma once

#include "LeaderboardData.h"

class Building;
class RootObject;

namespace ArenaIdentity
{
    extern const char* kRegistryStringId;
    extern const char* kRegistryFunctionStringId;
    extern const char* kArenaStringId;
    extern const char* kSmallArenaStringId;
    extern const char* kBannerStringId;
    extern const char* kBannerFunctionStringId;
    extern const char* kLeaderboardStringId;
    extern const char* kLeaderboardFunctionStringId;
    extern const char* kTownLeaderboardStringId;
    extern const char* kTownLeaderboardFunctionStringId;

    const char* GetStringId(RootObject* obj);
    bool MatchesId(RootObject* obj, const char* id);
    bool IsRegistry(RootObject* obj);
    bool IsTownRegistry(RootObject* obj);
    bool IsArena(RootObject* obj);
    bool IsSmallArena(RootObject* obj);
    bool IsBanner(RootObject* obj);
    bool IsMatchUiOpener(RootObject* obj);
    bool IsLeaderboard(RootObject* obj);
    bool GetLeaderboardKind(
        RootObject* obj,
        LeaderboardData::Kind& kind);
    bool IsRegistryFinished(Building* building);
    bool IsMatchUiOpenerFinished(Building* building);
    bool IsLeaderboardFinished(Building* building);
    bool GetFinishedLeaderboardKind(
        Building* building,
        LeaderboardData::Kind& kind);

    // Debug: log resolved stringID + match flags for an interact target.
    void LogInteractIdentity(RootObject* obj, const char* where);
}
