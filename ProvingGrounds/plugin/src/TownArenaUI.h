#pragma once
class Building;
namespace TownArenaUI
{
    void Show(Building* registry, bool bindRegistry = true);
    void Close();
    void AbandonWorldState();
    bool IsVisible();
    void Tick();
}
