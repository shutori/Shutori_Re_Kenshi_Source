#pragma once

#include "MatchRules.h"
#include "SparPodium.h"

#include <string>

class Character;

namespace SparSession
{
    enum StopReason
    {
        StopManual,
        StopKo,
        StopDeath,
        StopInvalid
    };

    bool IsActive();
    const std::string& GetStatus();

    MatchRules::MatchMode GetMode();
    int GetParticipantCount();
    Character* GetParticipant(int i);
    MatchRules::MatchTeam GetTeam(Character* c);

    bool IsEliminated(Character* c);
    bool EliminateFighter(Character* c);
    void SetKoEliminationEnabled(bool enabled);
    bool IsKoEliminationEnabled();

    Character* GetActiveA();
    Character* GetActiveB();
    bool HasPendingWalkIn();

    bool TickTeams1v1Bouts();

    bool IsSparringOpponent(Character* a, Character* b);
    bool IsParticipant(Character* c);

    void SetPendingResultsOutcome(SparPodium::OutcomeKind kind, Character* lastStanding);

    // Why the fight ended, for the telemetry log. SparSession::StopReason cannot
    // express this: every terminal outcome - team wipe, last standing, draw - is
    // routed through the StopKo branch, so the recorded stop reason is identical
    // for all of them.
    void SetPendingEndKind(MatchRules::MatchEndKind kind);
    MatchRules::MatchEndKind GetPendingEndKind();

    bool StartMatch(MatchRules::MatchMode mode, Character** fighters, MatchRules::MatchTeam* teams, int count);

    bool Start(Character* a, Character* b);
    void Stop(StopReason reason, Character* koVictim = 0);
    void StopWithStatus(StopReason reason, Character* koVictim, const char* statusOverride);

    // Save path: restore live fighters but do not produce results or ratings.
    void AbortForSave();
    // Load/teardown path: clear pointers without dereferencing game objects.
    void AbandonWorldState();

    Character* GetFighterA();
    Character* GetFighterB();
    bool IsSparringPair(Character* a, Character* b);
}
