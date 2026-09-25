#pragma once

#include <string>

class Building;
class Character;

namespace TownLimbShop
{
    void Begin(Building* registry, Character* const* fighters, int count);
    bool Tick();
    bool IsActive();
    bool IsManaged(Character* fighter);
    bool IsVisitActive();
    void SuspendVisit();
    void Release();
    void AbandonWorldState();
    const std::string& GetStatus();
}
