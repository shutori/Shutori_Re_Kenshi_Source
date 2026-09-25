#pragma once
#include "TownMatchmakingPolicy.h"

namespace TownMatchmakingSearch
{
    struct Request
    {
        unsigned generation;
        int slot;
        std::vector<TownMatchmakingPolicy::Fighter> players;
        std::vector<TownMatchmakingPolicy::Fighter> npcs;
        TownMatchmakingPolicy::Division division;
        unsigned seed;
        TownMatchmakingPolicy::History history;
        int maxEnemies;
        int maxTotal;
        std::string requiredRole;
        int rarity;
        bool forceSoloNamed;
        bool teams1v1;
        PGConfig::ChallengeDifficulty challengeDifficulty;
        Request() : generation(0), slot(-1), division(TownMatchmakingPolicy::Easy),
            seed(0), maxEnemies(4), maxTotal(6), rarity(-1), forceSoloNamed(false), teams1v1(false),
            challengeDifficulty(PGConfig::ChallengeNormal) {}
    };

    struct Result
    {
        unsigned generation;
        int slot;
        TownMatchmakingPolicy::Match match;
        Result() : generation(0), slot(-1) {}
    };

    // Replaces all queued work. A currently running request may finish, but
    // its stale result is discarded before it reaches the game thread.
    void BeginGeneration(unsigned generation);
    bool Enqueue(const Request& request);
    bool Poll(Result& result);
    void Shutdown();
}
