// LeaderboardStore.h — per-save MMR career records
#pragma once

#include <string>
#include <vector>

class Character;

namespace SparPodium
{
    struct Snapshot;
}

namespace LeaderboardStore
{
    struct Record
    {
        std::string id;
        std::string name;
        float mmr;
        int wins;
        int losses;
        int matches;
    };

    // Resolve active Kenshi save and reload sidecar if the save key changed.
    void EnsureLoaded();

    // Apply a rated arena match for all participants; no-op for Stop/Draw.
    void ApplyFromSnapshot(SparPodium::Snapshot& snap);

    // All arena participants with matches >= 1, sorted by MMR descending.
    void GetStandings(std::vector<Record>& out);

    // Current per-save rating for a fighter, or the default for an unrated fighter.
    float GetRating(Character* character);

    // Debug tooling: overwrite a fighter's per-save rating and persist it.
    bool SetRating(Character* character, float rating);

    // Live squad member or cached prisoner for portrait; may be null.
    Character* FindRatedCharacter(const std::string& id);

    const std::string& GetActiveSaveKey();
    bool HasActiveSave();
}
