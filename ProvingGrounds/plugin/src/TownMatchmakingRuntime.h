#pragma once
#include "TownMatchmakingPolicy.h"
#include <map>
class Character;
class Building;
namespace TownMatchmakingRuntime {
    typedef bool (*ReadyCheck)(Character*, bool, bool, std::string*);
    struct Snapshot {
        std::vector<TownMatchmakingPolicy::Fighter> players, npcs;
        std::map<std::string, Character*> characters;
        std::vector<Character*> medics;
        std::string context;
        std::string availability, diagnostics, playerBlocker;
        bool playersReady;
        Snapshot() : playersReady(false) {}
    };
    std::string Identity(Character* character);
    // Session-only player identity, including the live handle generation.
    std::string SelectionIdentity(Character* character);
    std::string Role(Character* character);
    double Ability(Character* character);
    std::string Label(Character* character);
    Snapshot Collect(Building* registry, const std::vector<Character*>& players,
        ReadyCheck ready, bool includeNamed);
    Character* Resolve(const Snapshot& snapshot, const std::string& id);
}
