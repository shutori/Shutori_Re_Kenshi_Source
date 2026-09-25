#include "../src/TownBettingPolicy.h"
#include <cassert>

int main()
{
    TownBettingPolicy::Ticket open;
    assert(TownBettingPolicy::AdjustStake(100, 10) == 110);
    assert(TownBettingPolicy::AdjustStake(100, -100) == 10);
    assert(TownBettingPolicy::AdjustStake(9500, 1000) == 10000);
    assert(TownBettingPolicy::AdjustStake(10, -10) == 10);
    assert(TownBettingPolicy::CanAccept(open, true, true, 0, 10, 10000));
    assert(TownBettingPolicy::CanAccept(open, true, true, 0, 10000, 10000));
    assert(!TownBettingPolicy::CanAccept(open, true, true, 0, 10010, 20000));
    assert(TownBettingPolicy::ReturnCats(10000, 100.0, 100.0) == 18000);

    TownBettingPolicy::Ticket winning;
    winning.side = 0;
    winning.stake = 500;
    winning.payout = 1250;
    winning.pending = true;
    const TownBettingPolicy::Result won = TownBettingPolicy::Resolve(winning, 0);
    assert(won.outcome == TownBettingPolicy::ResultWon);
    assert(won.returned == 1250);
    assert(won.net == 750);

    TownBettingPolicy::Ticket losing = winning;
    const TownBettingPolicy::Result lost = TownBettingPolicy::Resolve(losing, 1);
    assert(lost.outcome == TownBettingPolicy::ResultLost);
    assert(lost.returned == 0);
    assert(lost.net == -500);

    TownBettingPolicy::Ticket refunded = winning;
    const TownBettingPolicy::Result draw = TownBettingPolicy::Resolve(refunded, -1);
    assert(draw.outcome == TownBettingPolicy::ResultRefunded);
    assert(draw.returned == 500);
    assert(draw.net == 0);

    TownBettingPolicy::Ticket empty;
    const TownBettingPolicy::Result none = TownBettingPolicy::Resolve(empty, 0);
    assert(none.outcome == TownBettingPolicy::ResultNone);
    assert(none.returned == 0);
    assert(none.net == 0);
    return 0;
}
