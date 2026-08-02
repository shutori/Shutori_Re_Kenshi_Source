#pragma once

#include <vector>

class Building;

namespace PrisonerUtilInternal
{
    void CollectPlayerOwnedCageBuildings(std::vector<Building*>& out);
    bool IsBuildingOwnedByPlayer(Building* building);
}
