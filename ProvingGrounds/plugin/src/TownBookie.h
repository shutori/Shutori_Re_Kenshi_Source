#pragma once
#include "MatchRules.h"
#include <string>
class Character;
class RootObject;
namespace ArenaPersistence { struct PlannedNpcBout; }
namespace TownBookie {
    bool IsBookie(RootObject* object);
    void Show(RootObject* object);
    void BeginNpcBout(Character* const* fighters, const MatchRules::MatchTeam* teams, int count, double scoreA, double scoreB);
    bool PreparationValid();
    bool HasPendingWager();
    void CapturePlannedNpcBout(ArenaPersistence::PlannedNpcBout& out);
    bool RestorePlannedNpcBout(const ArenaPersistence::PlannedNpcBout& saved);
    void RefundUnrestoredWager(const ArenaPersistence::PlannedNpcBout& saved);
    void CombatStarted();
    void NpcAssembling();
    void CancelNpcBout(const std::string& reason);
    void Settle(int winner);
    bool HoldForBets();
    void Tick();
    bool IsVisible();
    void Close();
    void AbandonWorldState();
}
