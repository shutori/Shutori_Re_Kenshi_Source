#pragma once
#include "PGConfig.h"
#include "TownMatchmakingPolicy.h"
namespace TownBettingPolicy
{
    struct Ticket
    {
        int side, stake, payout;
        bool pending;
        Ticket() : side(-1), stake(0), payout(0), pending(false) {}
    };
    enum ResultOutcome { ResultNone, ResultWon, ResultLost, ResultRefunded };
    struct Result
    {
        ResultOutcome outcome;
        int winningSide, selectedSide, stake, returned, net;
        Result() : outcome(ResultNone), winningSide(-1), selectedSide(-1),
            stake(0), returned(0), net(0) {}
    };
    inline Result Resolve(const Ticket& ticket, int winningSide) {
        Result result;
        if (!ticket.pending) return result;
        result.winningSide = winningSide;
        result.selectedSide = ticket.side;
        result.stake = ticket.stake;
        if (winningSide < 0) {
            result.outcome = ResultRefunded;
            result.returned = ticket.stake;
        } else if (winningSide == ticket.side) {
            result.outcome = ResultWon;
            result.returned = ticket.payout;
        } else {
            result.outcome = ResultLost;
        }
        result.net = result.returned - result.stake;
        return result;
    }
    // Wagers are held between the configured Cats range, in whole stake steps;
    // the shipped 10 to 10,000 is what pg_config.json's "bookie" section edits.
    inline int AdjustStake(int current, int delta) {
        const PGConfig::Bookie& bookie = PGConfig::BookieValues();
        if (current < bookie.minimum) current = bookie.minimum;
        if (current > bookie.maximum) current = bookie.maximum;
        const int adjusted = current + delta;
        if (adjusted < bookie.minimum) return bookie.minimum;
        if (adjusted > bookie.maximum) return bookie.maximum;
        return adjusted;
    }
    // Raw model probability. This is the value the combat log records as
    // predicted_p_a and what the diagnostic fields compare, so it must stay the
    // untransformed model even when the pricing below is tuned -- otherwise we
    // lose the ability to measure the model against its own capture.
    inline double WinProbability(double ownAbility, double otherAbility) {
        if (!(ownAbility > 0) || !(otherAbility > 0) || !TownMatchmakingPolicy::Finite(ownAbility) || !TownMatchmakingPolicy::Finite(otherAbility))
            return 0;
        const double ratio = otherAbility / ownAbility;
        return 1.0 / (1.0 + ratio * ratio);
    }
    // Priced probability -- what the book actually pays against. Shipped this is
    // identical to WinProbability (knee 0, exponent 2). The transform is
    // p = r^k / (1 + r^k) on r = max(score)/min(score), with k = 2 below
    // pricingKnee and k = pricingExponent above it.
    //
    // It is computed from the UNORDERED pair, never from own/other: with a knee
    // the map is not symmetric, so pricing by own/other would apply the wide-ratio
    // exponent to the favourite and leave the underdog at the shipped price,
    // pricing the two sides of one market inconsistently.
    inline double PricedProbability(double ownAbility, double otherAbility) {
        if (!(ownAbility > 0) || !(otherAbility > 0) || !TownMatchmakingPolicy::Finite(ownAbility) || !TownMatchmakingPolicy::Finite(otherAbility))
            return 0;
        const BalanceTuning::Values& tuning = BalanceTuning::Get();
        // Shipped settings: take the original path, bit-for-bit, so a capture on
        // defaults is byte-identical to the shipped build.
        if (!(tuning.pricingKnee > 0) && tuning.pricingExponent == 2.0)
            return WinProbability(ownAbility, otherAbility);
        const bool ownIsHigher = ownAbility >= otherAbility;
        const double high = ownIsHigher ? ownAbility : otherAbility;
        const double low = ownIsHigher ? otherAbility : ownAbility;
        const double ratio = high / low;
        const double exponent = (tuning.pricingKnee > 0 && ratio <= tuning.pricingKnee)
            ? 2.0 : tuning.pricingExponent;
        const double powered = exponent == 2.0 ? ratio * ratio : std::pow(ratio, exponent);
        const double favourite = powered / (1.0 + powered);
        if (!(favourite > 0) || !(favourite < 1) || !TownMatchmakingPolicy::Finite(favourite))
            return 0;
        return ownIsHigher ? favourite : 1.0 - favourite;
    }
    inline int ReturnCats(int stake, double ownAbility, double otherAbility) {
        const PGConfig::Bookie& bookie = PGConfig::BookieValues();
        if (stake < bookie.minimum || stake > bookie.maximum ||
            stake % PGConfig::kStakeStep != 0 || !(ownAbility > 0) || !(otherAbility > 0) ||
            !TownMatchmakingPolicy::Finite(ownAbility) || !TownMatchmakingPolicy::Finite(otherAbility)) return 0;
        const double probability = PricedProbability(ownAbility, otherAbility);
        if (!(probability > 0)) return 0;
        double odds = .9 / probability;
        if (odds < 1.1) odds = 1.1;
        if (odds > 5.0) odds = 5.0;
        return static_cast<int>(stake * odds);
    }
    inline double TeamAbility(const double* members, int count) {
        if (!members || count <= 0 || count > 4) return 0;
        const double total = TownMatchmakingPolicy::TeamScore(members, count);
        return total > 0 && TownMatchmakingPolicy::Finite(total) ? total : 0;
    }
    inline bool CanAccept(const Ticket& ticket, bool npcOnly, bool beforeCombat, int side, int stake, int balance) {
        const PGConfig::Bookie& bookie = PGConfig::BookieValues();
        return !ticket.pending && npcOnly && beforeCombat && side >= 0 && side <= 1 &&
            stake >= bookie.minimum && stake <= bookie.maximum &&
            stake % PGConfig::kStakeStep == 0 && balance >= stake;
    }
    inline int Settle(Ticket& ticket, int winningSide) {
        if (!ticket.pending) return 0;
        ticket.pending = false;
        return winningSide < 0 ? ticket.stake : winningSide == ticket.side ? ticket.payout : 0;
    }
}
