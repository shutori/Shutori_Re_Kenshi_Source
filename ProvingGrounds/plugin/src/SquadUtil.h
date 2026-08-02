#pragma once

#include <vector>

class Character;

namespace SquadUtil
{
    // Collect living characters from the player faction's active squads.
    void CollectPlayerSquad(std::vector<Character*>& out);

    // Living characters currently multi-selected in PlayerInterface.
    void CollectSelectedCharacters(std::vector<Character*>& out);
}
