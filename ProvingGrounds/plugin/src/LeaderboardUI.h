#pragma once

#include "LeaderboardData.h"

class Building;

namespace LeaderboardUI
{
    void Show(LeaderboardData::Kind kind, Building* source);
    void Close();
    void Tick();
    bool IsVisible();
}
