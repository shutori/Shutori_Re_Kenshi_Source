#pragma once

#include "SparPodium.h"

class Character;

namespace SparStats
{
    void ResetForMatch();
    void OnHit(
        Character* attacker,
        Character* defender,
        float appliedDamage,
        float incomingDamage,
        bool isMiss);
    void OnBlock(Character* defender, Character* attacker);
    void OnDodge(Character* attacker, Character* defender);
    void OnEliminated(Character* who);
    void Freeze(SparPodium::OutcomeKind outcome, MatchRules::MatchMode mode, Character* lastStandingOrNull);
    const SparPodium::Snapshot& GetSnapshot();
    SparPodium::Snapshot& GetMutableSnapshot();
    bool HasSnapshot();
    void ClearSnapshot();
    // Clear both the frozen snapshot and live Character pointer caches.
    void AbandonWorldState();

    // Character for podium slot 0=winner/1st, 1=2nd, 2=3rd (valid at freeze time).
    Character* GetPodiumCharacter(int podiumIndex);
}
