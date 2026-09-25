#pragma once

class Character;

namespace MatchRules
{
    enum MatchMode;
    enum MatchTeam;
}

namespace FightStarter
{
    void RememberMatchFighters(Character** fighters, int count);
    void Engage(Character* a, Character* b);
    void Disengage(Character* a, Character* b);
    // Re-issue attack orders if targets were lost mid-spar.
    void KeepEngaged(Character* a, Character* b);

    void EngageMatch(Character** fighters, MatchRules::MatchTeam* teams, int count, MatchRules::MatchMode mode);
    void DisengageMatch(Character** fighters, int count);
    // Clear stale KO targets and engage the survivors without restoring/toggling
    // their saved pre-match order state.
    void RetargetMatchAfterElimination(
        Character** allFighters,
        int allCount,
        Character** remainingFighters,
        MatchRules::MatchTeam* remainingTeams,
        int remainingCount,
        MatchRules::MatchMode mode);
    // Transfer a sticky focused order to an opponent who is actively landing
    // or blocking attacks in open multi-fighter modes.
    void ReactToIncomingAttack(Character* defender, Character* attacker);
    void KeepEngagedMatch(Character** fighters, MatchRules::MatchTeam* teams, int count, MatchRules::MatchMode mode);

    // World teardown path: forget cached Character pointers without touching them.
    void AbandonWorldState();
}
