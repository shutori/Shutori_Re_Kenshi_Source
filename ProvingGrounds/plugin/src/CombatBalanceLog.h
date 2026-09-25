#pragma once
#include "MatchRules.h"
#include "SparPodium.h"
#include <string>
class Character;
namespace CombatBalanceLog {
    void StartMatch(MatchRules::MatchMode mode, Character** fighters, MatchRules::MatchTeam* teams, int count);
    void FinishMatch(const SparPodium::Snapshot& snapshot, int stopReason);
    void AbortMatch(const char* reason);
    void OpenMarket(Character* const* fighters, const MatchRules::MatchTeam* teams, int count, double scoreA, double scoreB, bool eligible);
    void MarketEvent(const char* event, const std::string& fields = "");
    void CloseMarket(int winner, int side, int stake, int quotedReturn, int credit, bool hadTicket);
    // Everything a booked challenge is offered under. Gathered at the booking
    // site because that is the only place the resolved offer, the priced quote and
    // the two team scores are in scope together; the bout it becomes is described
    // later by StartMatch, which carries the same challenge_id.
    struct ChallengeCard {
        int slot;          // which offer was booked, 0-4, or 5 for the Skarn slot
        int division;      // Easy / Medium / Hard
        int rarity;        // Common / Uncommon / Rare / Legendary
        int encounter;     // TownChallengePolicy::Encounter
        int format, mode;
        int players;       // player fighters committed
        int enemies;       // opponents in the lineup
        double ownScore;   // TeamScore of the player's team
        double enemyScore; // TeamScore of the opponents, i.e. what priced the quote
        std::string opponents[4], opponentNames[4], opponentRoles[4];
        int stake, payout;
        ChallengeCard() : slot(-1), division(0), rarity(0), encounter(0), format(0), mode(0),
            players(0), enemies(0), ownScore(0), enemyScore(0), stake(0), payout(0) {}
    };
    void ChallengeAccepted(const ChallengeCard& card);
    void ChallengeSettled(int outcome, int credit);
    void DiagnosticEvent(const char* event, const std::string& fields = "");
    void Abandon();
}
