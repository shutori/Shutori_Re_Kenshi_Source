#pragma once
class Building;
class Character;
namespace SparPodium { struct Snapshot; }
namespace TownSpectators {
    void Begin(Building* arena);
    void Tick();
    void ReactToResult(const SparPodium::Snapshot& result);
    // Relinquish one invited guest before another arena system reserves them.
    void ReleaseCharacter(Character* character);
    void Release();
    void AbandonWorldState();
}
