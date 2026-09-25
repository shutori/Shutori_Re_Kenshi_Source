#pragma once
#include "PGConfig.h"
#include "TownMatchmakingPolicy.h"

namespace TownChallengeBuyInPolicy {
    struct Quote {
        int buyIn, payout;
        Quote() : buyIn(0), payout(0) {}
        bool Valid() const { return buyIn >= 100 && buyIn <= 5000 && payout >= buyIn && payout <= 20000; }
    };
    struct Account {
        int credit, escrow;
        Account() : credit(0), escrow(0) {}
    };
    struct Ticket {
        Quote quote;
        bool pending;
        Ticket() : pending(false) {}
    };
    // The base buy-in for the division, in Cats, is a configured price (shipped
    // 500 / 1000 / 2500); the rest of the quote is the model around it -- the
    // 0.5-2.0 scale on the power ratio, and the 1.1-5.0 odds band. Valid() bounds
    // what the wallet will accept, so a base outside 200-2500 prices challenges
    // the scale cannot keep inside those bounds.
    inline Quote Price(int division, double own, double enemy) {
        Quote result;
        if (division < 0 || division > 2 || !TownMatchmakingPolicy::Finite(own) ||
            !TownMatchmakingPolicy::Finite(enemy) || own <= 0 || enemy <= 0) return result;
        const double target[] = {.70, 1.00, 1.30};
        const double ratio = enemy / own;
        const double scale = (std::max)(.5, (std::min)(2.0, ratio / target[division]));
        const int base = PGConfig::ChallengeBase(division);
        result.buyIn = static_cast<int>(std::floor(base * scale / 10.0 + .5)) * 10;
        const double odds = (std::max)(1.1, (std::min)(5.0, .9 * (1.0 + ratio)));
        result.payout = static_cast<int>(std::floor(result.buyIn * odds + 1e-8));
        return result;
    }
    inline bool CanAccept(const Account& a, const Ticket& t, const Quote& q, int balance) {
        return !t.pending && a.credit == 0 && a.escrow == 0 && q.Valid() && balance >= q.buyIn;
    }
    inline bool Accept(Account& a, Ticket& t, const Quote& q, int balance) {
        if (!CanAccept(a, t, q, balance)) return false;
        t.quote = q; t.pending = true; a.escrow = q.buyIn;
        return true;
    }
    // Outcome: 1 = player victory, 0 = loss, -1 = cancellation/draw.
    inline int Settle(Account& a, Ticket& t, int outcome) {
        if (!t.pending) return 0;
        const int credit = outcome < 0 ? t.quote.buyIn : outcome == 1 ? t.quote.payout : 0;
        t.pending = false; a.escrow = 0; a.credit += credit;
        return credit;
    }
    // Bookings do not resume on load. Store their deducted stake as a refund
    // alongside deferred winnings, without changing the running session.
    inline int SaveCredit(const Account& a) { return a.credit + a.escrow; }
}
