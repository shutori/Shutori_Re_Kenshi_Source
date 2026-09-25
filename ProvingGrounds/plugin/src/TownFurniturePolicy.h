#pragma once
namespace TownFurniturePolicy {
    inline bool Occupied(bool handleValid, bool characterResolved, bool characterValid) { return characterResolved && characterValid; }
}
