#pragma once

class Character;

namespace RewardsUI
{
    // preferred: pre-select this fighter when present in the Rewards list.
    void Show(Character* preferred = 0);
    void Close();
    void Tick();
    bool IsVisible();
    void AbandonWorldState();
}
