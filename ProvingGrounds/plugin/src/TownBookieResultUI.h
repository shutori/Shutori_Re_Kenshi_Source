#pragma once
#include "TownBookieUI.h"
#include <string>
#include <vector>

namespace TownBookieResultUI
{
    struct View
    {
        std::vector<TownBookieUI::FighterView> fighters[2];
        std::string title, context, payout;
        int winningSide, selectedSide, tone;
        View() : winningSide(-1), selectedSide(-1), tone(0) {}
    };
    struct Actions
    {
        void (*close)();
        Actions() : close(NULL) {}
    };

    bool Show(const View& view, const Actions& actions);
    bool IsVisible();
    void Close();
    void Destroy();
}
