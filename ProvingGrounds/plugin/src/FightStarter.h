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
    void KeepEngagedMatch(Character** fighters, MatchRules::MatchTeam* teams, int count, MatchRules::MatchMode mode);
}
