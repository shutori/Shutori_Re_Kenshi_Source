#pragma once
#include <cmath>
#include <cfloat>
#include <climits>
// Pure decisions only. Runtime adapters own persistence, health assessment and NPC control.
namespace TownArenaPolicy {
    static const int kOffersPerCard = 5;
    enum EventKind { EventNone, EventPlayer, EventNpc };
    // Signed epoch: initial night is -1; invalid clock is -2.
    inline int CardEpoch(double hours) {
        if (!(hours >= 0.0) || hours > 24000000.0) return -2;
        return static_cast<int>(std::floor((hours - 6.0) / 12.0));
    }
    inline int ChallengeCardEpoch(double hours, int refreshHours) {
        if (!(hours >= 0.0) || hours > 24000000.0 || refreshHours < 1 || refreshHours > 168) return -2;
        return static_cast<int>(std::floor((hours - 6.0) / refreshHours));
    }
    inline double HoursUntilChallengeRefresh(double hours, int refreshHours) {
        if (!(hours >= 0.0) || hours > 24000000.0 || refreshHours < 1 || refreshHours > 168) return -1.0;
        const double epoch = std::floor((hours - 6.0) / refreshHours);
        return 6.0 + (epoch + 1.0) * refreshHours - hours;
    }
    inline int PaidRefreshCost(int baseCats, double multiplier, int paidRefreshCount) {
        if (baseCats < 1 || !(multiplier >= 1.0) || !(multiplier <= 10.0) || paidRefreshCount < 0)
            return 0;
        const double cost = baseCats * std::pow(multiplier, static_cast<double>(paidRefreshCount));
        if (!(cost < static_cast<double>(INT_MAX))) return INT_MAX;
        const double rounded = std::floor(cost + 0.5);
        return rounded < static_cast<double>(INT_MAX) ? static_cast<int>(rounded) : INT_MAX;
    }
    inline EventKind SelectNext(bool busy, bool playerPending, bool npcReady) {
        if (busy) return EventNone;
        if (playerPending) return EventPlayer;
        return npcReady ? EventNpc : EventNone;
    }
    inline EventKind AmbientNext(bool paused, bool registryReady, bool busy,
    bool playerPending, double now, double due) {
        if (!(now >= 0.0) || now > DBL_MAX || !(due >= 0.0) || due > DBL_MAX)
            return EventNone;
        return SelectNext(busy, playerPending,
            !paused && registryReady && now >= due);
    }
    inline bool ShouldDiscoverRegistry(bool paused, bool registryReady,
    bool playerBookingPending) {
        return !paused && !registryReady && !playerBookingPending;
    }
    inline int SelectNpcTeamSize(int recoveredFighters, unsigned roll) {
        const int maximum = recoveredFighters / 2;
        if (maximum < 1) return 0;
        const int capped = maximum > 4 ? 4 : maximum;
        return 1 + static_cast<int>(roll % static_cast<unsigned>(capped));
    }
    enum Milestone { MilestoneChampion = 1, MilestoneSenn = 2, MilestoneTorka = 4 };
    enum SkarnEncounter { SkarnDuel, SkarnRandomized };
    inline unsigned AddMilestones(unsigned current, unsigned defeated, bool won) {
        return won ? (current | (defeated & 7u)) : current;
    }
    inline bool SkarnUnlocked(unsigned milestones) { return (milestones & 7u) == 7u; }
    inline SkarnEncounter SkarnFormat(bool won) { return won ? SkarnRandomized : SkarnDuel; }
    // lastEnd is total in-game hours, not hour of day; ignored before first attempt.
    inline bool CanBookSkarn(unsigned milestones, bool attempted,
    double lastEnd, double now, bool recovered) {
        if (!SkarnUnlocked(milestones) || !recovered) return false;
        if (!(now >= 0.0) || now > DBL_MAX) return false;
        if (!attempted) return true;
        return lastEnd >= 0.0 && lastEnd <= now && now - lastEnd >= 24.0;
    }

    enum OfferState { OfferAvailable, OfferBooked, OfferConsumed };
    inline bool BookOffer(OfferState& state) {
        if (state != OfferAvailable) return false;
        state = OfferBooked;
        return true;
    }
    inline bool BeginOffer(OfferState& state) {
        if (state != OfferBooked) return false;
        state = OfferConsumed;
        return true;
    }
    inline void CancelBooking(OfferState& state) {
        if (state == OfferBooked) state = OfferAvailable;
    }

}
