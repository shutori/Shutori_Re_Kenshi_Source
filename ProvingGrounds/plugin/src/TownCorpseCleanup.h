#pragma once
#include <string>
#include <vector>

class Building;
class Character;

namespace TownCorpseCleanup {
    void Begin(Building* arena, const std::vector<Character*>& fighters);
    void Seal();
    int AdmitArenaDeaths();
    void Tick();
    bool HasPending();
    const std::string& GetStatus();
    void Release();
    void AbandonWorldState();
}
