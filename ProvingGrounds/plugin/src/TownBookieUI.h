#pragma once
#include <kenshi/util/hand.h>
#include <string>
#include <vector>

// Presentation only: all transactions and validation remain in TownBookie.
namespace TownBookieUI {
    struct FighterView {
        hand actor;
        std::string name, style, record, stats;
    };
    struct View {
        std::vector<FighterView> fighters[2];
        std::string header, timer, status, slip, odds[2];
        int selectedSide, stake;
        bool canSelect, canStake, canBet, canArrange;
        View() : selectedSide(-1), stake(100), canSelect(false), canStake(false),
            canBet(false), canArrange(false) {}
    };
    struct Actions {
        void (*selectSide)(int);
        void (*changeStake)(int);
        void (*placeBet)();
        void (*arrange)();
        void (*close)();
        Actions() : selectSide(NULL), changeStake(NULL), placeBet(NULL), arrange(NULL), close(NULL) {}
    };
    bool Show(const View& view, const Actions& actions);
    void Refresh(const View& view);
    bool IsVisible();
    void Close();
    void Destroy();
}
