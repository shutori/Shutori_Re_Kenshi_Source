#pragma once

class Building;
class RootObject;

namespace ArenaIdentity
{
    extern const char* kRegistryStringId;
    extern const char* kRegistryFunctionStringId;
    extern const char* kArenaStringId;
    extern const char* kBannerStringId;
    extern const char* kBannerFunctionStringId;
    extern const char* kLeaderboardStringId;
    extern const char* kLeaderboardFunctionStringId;

    const char* GetStringId(RootObject* obj);
    bool MatchesId(RootObject* obj, const char* id);
    bool IsRegistry(RootObject* obj);
    bool IsArena(RootObject* obj);
    bool IsBanner(RootObject* obj);
    bool IsMatchUiOpener(RootObject* obj);
    bool IsLeaderboard(RootObject* obj);
    bool IsRegistryFinished(Building* building);
    bool IsMatchUiOpenerFinished(Building* building);
    bool IsLeaderboardFinished(Building* building);

    // Debug: log resolved stringID + match flags for an interact target.
    void LogInteractIdentity(RootObject* obj, const char* where);
}
