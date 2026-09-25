#include "SparSession.h"
#include "CombatBalanceLog.h"
#include "TownArena.h"
#include "TownAftercare.h"
#include "ArenaIngress.h"
#include "FightStarter.h"
#include "LeaderboardStore.h"
#include "PrisonerMatchLogic.h"
#include "PrisonerUtil.h"
#include "ResultsUI.h"
#include "SparStats.h"

#include "PGLog.h"

#include <vector>
#include <cstdio>
#include <Windows.h>

#pragma warning(push)
#pragma warning(disable: 4091)
#include <kenshi/Character.h>
#include <kenshi/CharMovement.h>
#include <kenshi/Enums.h>
#pragma warning(pop)

#ifndef NULL
#define NULL 0
#endif

namespace
{
    bool g_active = false;
    MatchRules::MatchMode g_mode = MatchRules::ModeTeamAvB;
    std::vector<Character*> g_fighters;
    std::vector<MatchRules::MatchTeam> g_teams;
    std::vector<char> g_eliminated; // non-zero = permanently out for this match
    struct BenchSlot {
        void* receiver;
        Ogre::Vector3 position;
        DWORD lastRecall;
        BenchSlot() : receiver(NULL), position(0,0,0), lastRecall(0) {}
    };
    std::vector<BenchSlot> g_bench;
    std::vector<char> g_retired; // Combat state already restored at KO, before care.
    std::string g_status = "Idle";
    Character* g_activeA = NULL;
    Character* g_activeB = NULL;
    Character* g_interBoutWaiting = NULL;
    SparPodium::OutcomeKind g_pendingOutcome = SparPodium::OutcomeStopped;
    MatchRules::MatchEndKind g_pendingEndKind = MatchRules::EndNone;
    Character* g_pendingLastStanding = NULL;
    bool g_hasPendingOutcome = false;

    std::string CharName(Character* c)
    {
        if (!c)
            return "(none)";
        return c->getName();
    }

    int FindParticipantIndex(Character* c)
    {
        for (size_t i = 0; i < g_fighters.size(); ++i)
        {
            if (g_fighters[i] == c)
                return static_cast<int>(i);
        }
        return -1;
    }

    bool IsLiving(Character* c)
    {
        return c && c->isValid() && !c->isDead() && !c->isUnconcious();
    }

    MatchRules::MatchParticipant MakeParticipant(int index)
    {
        Character* c = g_fighters[static_cast<size_t>(index)];
        MatchRules::MatchParticipant p = {};
        p.id = index;
        p.team = g_teams[static_cast<size_t>(index)];
        p.conscious = IsLiving(c);
        p.dead = c && c->isDead();
        p.eliminated = (index >= 0 && index < static_cast<int>(g_eliminated.size()) &&
            g_eliminated[static_cast<size_t>(index)] != 0);
        return p;
    }

    void ReleaseBench(int index, const char* reason) {
        if (index < 0 || index >= static_cast<int>(g_bench.size()) || !g_bench[index].receiver) return;
        TownAftercare::ReleaseBenchOrders(g_bench[index].receiver);
        g_bench[index].receiver = NULL;
        char line[192];
        sprintf_s(line, "Proving Grounds: Teams 1v1 bench released slot=%d reason=%s", index+1, reason);
        PGLog::Debug(line);
    }
    void ReleaseAllBenches() {
        for (size_t i = 0; i < g_bench.size(); ++i) ReleaseBench(static_cast<int>(i), "match-ended");
    }
    void MarkEliminated(Character* c)
    {
        const int index = FindParticipantIndex(c);
        if (index < 0)
            return;
        if (index >= static_cast<int>(g_eliminated.size()))
            g_eliminated.resize(static_cast<size_t>(index) + 1, 0);
        if (g_eliminated[static_cast<size_t>(index)]) return;
        g_eliminated[static_cast<size_t>(index)] = 1;
        if (g_mode == MatchRules::ModeTeams1v1) {
            if (ArenaIngress::GetWalkInFighter() == c) {
                ArenaIngress::ClearWalkIn();
                PGLog::Debug("Proving Grounds: Teams 1v1 walk-in cancelled for eliminated fighter");
            }
            ReleaseBench(index, "eliminated");
            // Restore original combat orders once, before a medic takes over.
            // Final match cleanup must not cancel a carried/bed-bound patient.
            FightStarter::DisengageMatch(&c, 1);
            g_retired[static_cast<size_t>(index)] = 1;
            if (g_interBoutWaiting == c) g_interBoutWaiting = NULL;
            Character* other = c == g_activeA ? g_activeB : c == g_activeB ? g_activeA : NULL;
            const int otherIndex = FindParticipantIndex(other);
            if (IsLiving(other) && otherIndex >= 0 && !g_eliminated[otherIndex] &&
                other != ArenaIngress::GetWalkInFighter()) {
                FightStarter::RetargetMatchAfterElimination(&other, 1, NULL, NULL, 0, g_mode);
                g_interBoutWaiting = other;
            }
            PGLog::Debug("Proving Grounds: Teams 1v1 fighter permanently eliminated; released for care name=" + CharName(c));
        }
    }

    bool IsEliminatedIndex(int index)
    {
        return index >= 0 &&
            index < static_cast<int>(g_eliminated.size()) &&
            g_eliminated[static_cast<size_t>(index)] != 0;
    }

    bool AllFightersValidAndConscious(Character** fighters, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            Character* c = fighters[i];
            if (!c || !c->isValid())
                return false;
            if (c->isDead() || c->isUnconcious())
                return false;
        }
        return true;
    }

    bool HasDuplicateFighters(Character** fighters, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            for (int j = i + 1; j < count; ++j)
            {
                if (fighters[i] == fighters[j])
                    return true;
            }
        }
        return false;
    }

    bool ValidateTeamMode(Character** /*fighters*/, MatchRules::MatchTeam* teams, int count)
    {
        bool hasA = false;
        bool hasB = false;
        for (int i = 0; i < count; ++i)
        {
            if (teams[i] != MatchRules::TeamA && teams[i] != MatchRules::TeamB)
            {
                g_status = "Team modes require TeamA or TeamB";
                return false;
            }
            if (teams[i] == MatchRules::TeamA)
                hasA = true;
            else
                hasB = true;
        }
        if (!hasA || !hasB)
        {
            g_status = "Need at least one fighter per team";
            return false;
        }
        return true;
    }

    Character* FirstOfTeam(MatchRules::MatchTeam team)
    {
        for (size_t i = 0; i < g_fighters.size(); ++i)
        {
            if (g_teams[i] == team)
                return g_fighters[i];
        }
        return NULL;
    }

    Character* NextConsciousAfter(Character* current, MatchRules::MatchTeam team)
    {
        const int start = FindParticipantIndex(current);
        const int n = static_cast<int>(g_fighters.size());
        for (int i = start + 1; i < n; ++i)
        {
            if (g_teams[static_cast<size_t>(i)] != team)
                continue;
            if (IsEliminatedIndex(i))
                continue;
            if (IsLiving(g_fighters[static_cast<size_t>(i)]))
                return g_fighters[static_cast<size_t>(i)];
        }
        return NULL;
    }

    void ParkBenchFighters()
    {
        if (g_mode != MatchRules::ModeTeams1v1) return;
        const DWORD now = GetTickCount();
        for (size_t i = 0; i < g_fighters.size(); ++i) {
            Character* c = g_fighters[i];
            if (!c || !c->isValid() || c->isDead() || c->isUnconcious() ||
                IsEliminatedIndex(static_cast<int>(i)) ||
                ((c == g_activeA || c == g_activeB) && c != g_interBoutWaiting)) {
                ReleaseBench(static_cast<int>(i), "not-waiting");
                continue;
            }
            BenchSlot& slot = g_bench[i];
            bool issue = false;
            if (!slot.receiver) {
                if (!TownAftercare::ReserveBenchOrders(c->getOrdersReciever())) continue;
                slot.receiver = c->getOrdersReciever();
                slot.position = c->getPosition();
                issue = true;
                PGLog::Debug("Proving Grounds: Teams 1v1 bench reserved name=" + c->getName());
            }
            c->setStandingOrder(MessageForB::M_SET_ORDER_PASSIVE, true);
            c->setStandingOrder(MessageForB::M_SET_ORDER_HOLD, (c->getPosition() - slot.position).squaredLength() <= 64.0f);
            c->setStandingOrder(MessageForB::M_SET_ORDER_AGG, false);
            CharMovement* movement = c->getMovement();
            if (!issue && now - slot.lastRecall >= 2000) {
                const Ogre::Vector3 offset = c->getPosition() - slot.position;
                const Ogre::Vector3 destinationError = movement ? movement->destination - slot.position : Ogre::Vector3(0,0,0);
                issue = offset.squaredLength() > 64.0f ||
                    (movement && movement->isCurrentlyMoving() && destinationError.squaredLength() > 64.0f);
            }
            if (issue) {
                slot.lastRecall = now;
                c->clearAllAIGoals();
                c->addOrder(NULL, MOVE_CUS_ORDERED, NULL, false, true, slot.position);
                if (movement) movement->setDestination(slot.position, HIGH_PRIORITY, true);
                c->setDestination(slot.position, false);
                char line[256];
                sprintf_s(line, "Proving Grounds: Teams 1v1 bench hold slot=%u position=(%.1f,%.1f,%.1f)",
                    static_cast<unsigned>(i+1), slot.position.x, slot.position.y, slot.position.z);
                PGLog::Debug(line);
            }
        }
    }

    void EngageActivePair()
    {
        if (!IsLiving(g_activeA) || !IsLiving(g_activeB) ||
            IsEliminatedIndex(FindParticipantIndex(g_activeA)) || IsEliminatedIndex(FindParticipantIndex(g_activeB)))
            return;

        g_interBoutWaiting = NULL;
        ReleaseBench(FindParticipantIndex(g_activeA), "active-duel");
        ReleaseBench(FindParticipantIndex(g_activeB), "active-duel");
        Character* pair[2] = { g_activeA, g_activeB };
        MatchRules::MatchTeam teams[2] = { MatchRules::TeamA, MatchRules::TeamB };
        FightStarter::EngageMatch(pair, teams, 2, MatchRules::ModeTeams1v1);
        ParkBenchFighters();
        g_status = "Sparring 1v1: " + CharName(g_activeA) + " vs " + CharName(g_activeB);
    }

    std::string BuildStatusString(MatchRules::MatchMode mode, Character** fighters, MatchRules::MatchTeam* teams, int count)
    {
        if (mode == MatchRules::ModeTeamAvB)
        {
            std::string teamA;
            std::string teamB;
            for (int i = 0; i < count; ++i)
            {
                const std::string name = CharName(fighters[i]);
                if (teams[i] == MatchRules::TeamA)
                {
                    if (!teamA.empty())
                        teamA += ", ";
                    teamA += name;
                }
                else if (teams[i] == MatchRules::TeamB)
                {
                    if (!teamB.empty())
                        teamB += ", ";
                    teamB += name;
                }
            }
            return "Sparring: " + teamA + " vs " + teamB;
        }

        if (mode == MatchRules::ModeTeams1v1)
        {
            return "Sparring 1v1: " + CharName(g_activeA) + " vs " + CharName(g_activeB);
        }

        std::string names;
        for (int i = 0; i < count; ++i)
        {
            if (!names.empty())
                names += ", ";
            names += CharName(fighters[i]);
        }
        return "Sparring Last Standing: " + names;
    }

    void EngageMatch(Character** fighters, MatchRules::MatchTeam* teams, int count, MatchRules::MatchMode mode)
    {
        if (mode == MatchRules::ModeTeams1v1)
        {
            EngageActivePair();
            return;
        }
        FightStarter::EngageMatch(fighters, teams, count, mode);
    }

    void DisengageAll()
    {
        const int count = static_cast<int>(g_fighters.size());
        if (count <= 0)
            return;

        if (g_mode != MatchRules::ModeTeams1v1) {
            FightStarter::DisengageMatch(g_fighters.data(), count);
            return;
        }
        ReleaseAllBenches();
        for (int i = 0; i < count; ++i) {
            if (i < static_cast<int>(g_retired.size()) && g_retired[i]) continue;
            Character* c = g_fighters[i];
            FightStarter::DisengageMatch(&c, 1);
        }
    }

    bool TryPromoteSide(Character*& active, MatchRules::MatchTeam team)
    {
        if (IsLiving(active) && !IsEliminatedIndex(FindParticipantIndex(active)))
            return false;

        // Bout KO = out for the rest of Teams 1v1 (wake-ups on the bench don't revive).
        MarkEliminated(active);

        Character* next = NextConsciousAfter(active, team);
        if (!next)
            return false;

        // Only one walk-in at a time (e.g. both actives dropped same tick).
        if (ArenaIngress::IsWalkInPending())
            return true;

        const std::string downName = CharName(active);
        g_status = downName + " KO'd — " + CharName(next) + " walking in";
        PGLog::Debug(("Proving Grounds: " + g_status).c_str());

        Character* other = team == MatchRules::TeamA ? g_activeB : g_activeA;
        if (IsLiving(other) && !IsEliminatedIndex(FindParticipantIndex(other))) {
            if (g_interBoutWaiting != other)
                FightStarter::RetargetMatchAfterElimination(&other, 1, NULL, NULL, 0, g_mode);
            g_interBoutWaiting = other;
        }
        ReleaseBench(FindParticipantIndex(next), "walk-in");
        active = next;
        if (!ArenaIngress::BeginWalkIn(next, team))
        {
            // Fallback: promote in place if walk-in cannot start.
            EngageActivePair();
        }
        ParkBenchFighters();
        return true;
    }
}

namespace SparSession
{
    void AbandonWorldState()
    {
        CombatBalanceLog::Abandon();
        ReleaseAllBenches(); // Removes receiver identities only; no actor access.
        g_active = false;
        g_bench.clear(); g_retired.clear();
        g_fighters.clear();
        g_teams.clear();
        g_eliminated.clear();
        g_activeA = NULL;
        g_activeB = NULL;
        g_interBoutWaiting = NULL;
        g_hasPendingOutcome = false;
        g_pendingLastStanding = NULL;
        g_pendingOutcome = SparPodium::OutcomeStopped;
        g_pendingEndKind = MatchRules::EndNone;
        g_status = "Idle";
    }

    void AbortForSave()
    {
        if (!g_active)
            return;

        CombatBalanceLog::AbortMatch("safe_save");
        ArenaIngress::ClearWalkIn();
        DisengageAll();
        AbandonWorldState();
        g_status = "Match cancelled to safely save the game";
        PGLog::Debug("Proving Grounds: active match cancelled for safe save");
    }

    bool IsActive()
    {
        return g_active;
    }

    const std::string& GetStatus()
    {
        return g_status;
    }

    MatchRules::MatchMode GetMode()
    {
        return g_mode;
    }

    int GetParticipantCount()
    {
        return g_active ? static_cast<int>(g_fighters.size()) : 0;
    }

    Character* GetParticipant(int i)
    {
        if (!g_active || i < 0 || i >= static_cast<int>(g_fighters.size()))
            return NULL;
        return g_fighters[static_cast<size_t>(i)];
    }

    MatchRules::MatchTeam GetTeam(Character* c)
    {
        const int index = FindParticipantIndex(c);
        if (index < 0)
            return MatchRules::TeamNone;
        return g_teams[static_cast<size_t>(index)];
    }

    Character* GetActiveA()
    {
        return g_active ? g_activeA : NULL;
    }

    Character* GetActiveB()
    {
        return g_active ? g_activeB : NULL;
    }

    bool IsEliminated(Character* c)
    {
        if (!g_active || !c)
            return false;
        return IsEliminatedIndex(FindParticipantIndex(c));
    }

    bool EliminateFighter(Character* c)
    {
        if (!g_active || !c)
            return false;
        const int index = FindParticipantIndex(c);
        if (index < 0 || IsEliminatedIndex(index))
            return false;
        MarkEliminated(c);
        return true;
    }

    void SetKoEliminationEnabled(bool /*enabled*/)
    {
        // KO elimination is always on; API kept for call-site compatibility.
    }

    bool IsKoEliminationEnabled()
    {
        return true;
    }

    bool HasPendingWalkIn()
    {
        return g_active && ArenaIngress::IsWalkInPending();
    }

    Character* GetFighterA()
    {
        if (g_active && g_mode == MatchRules::ModeTeams1v1)
            return g_activeA;
        if (!g_active || g_fighters.size() < 1)
            return NULL;
        return g_fighters[0];
    }

    Character* GetFighterB()
    {
        if (g_active && g_mode == MatchRules::ModeTeams1v1)
            return g_activeB;
        if (!g_active || g_fighters.size() < 2)
            return NULL;
        return g_fighters[1];
    }

    bool IsSparringOpponent(Character* a, Character* b)
    {
        if (!g_active || !a || !b)
            return false;

        const int ia = FindParticipantIndex(a);
        const int ib = FindParticipantIndex(b);
        if (ia < 0 || ib < 0)
            return false;
        if (IsEliminatedIndex(ia) || IsEliminatedIndex(ib))
            return false;

        if (g_mode == MatchRules::ModeTeams1v1)
        {
            if (ArenaIngress::IsWalkInPending())
                return false;
            const bool aActive = (a == g_activeA || a == g_activeB);
            const bool bActive = (b == g_activeA || b == g_activeB);
            if (!aActive || !bActive)
                return false;
        }

        const MatchRules::MatchParticipant pa = MakeParticipant(ia);
        const MatchRules::MatchParticipant pb = MakeParticipant(ib);
        return MatchRules::IsOpponent(g_mode, pa, pb);
    }

    bool IsSparringPair(Character* a, Character* b)
    {
        return IsSparringOpponent(a, b);
    }

    bool IsParticipant(Character* c)
    {
        return g_active && c && FindParticipantIndex(c) >= 0;
    }

    bool TickTeams1v1Bouts()
    {
        if (!g_active || g_mode != MatchRules::ModeTeams1v1)
            return false;

        ParkBenchFighters();
        if (ArenaIngress::IsWalkInPending())
        {
            ArenaIngress::TickWalkIn();
            if (ArenaIngress::IsWalkInArrived())
            {
                Character* arrived = ArenaIngress::GetWalkInFighter();
                ArenaIngress::ClearWalkIn();
                if (IsLiving(arrived) && !IsEliminatedIndex(FindParticipantIndex(arrived)))
                    g_interBoutWaiting = arrived;
                EngageActivePair();
                ParkBenchFighters();
            }
            return true;
        }

        if (TryPromoteSide(g_activeA, MatchRules::TeamA))
            return true;
        if (TryPromoteSide(g_activeB, MatchRules::TeamB))
            return true;
        return false;
    }

    bool StartMatch(MatchRules::MatchMode mode, Character** fighters, MatchRules::MatchTeam* teams, int count)
    {
        if (TownArena::IsAftercare())
        {
            g_status = "Town medics are clearing the arena";
            return false;
        }
        if (g_active)
        {
            g_status = "Already sparring";
            return false;
        }
        if (!fighters || !teams || count < 2)
        {
            g_status = "Need at least two fighters";
            return false;
        }
        if (HasDuplicateFighters(fighters, count))
        {
            g_status = "Pick different fighters";
            return false;
        }
        if (!AllFightersValidAndConscious(fighters, count))
        {
            g_status = "Fighters must be valid and conscious";
            return false;
        }

        if (mode == MatchRules::ModeTeamAvB || mode == MatchRules::ModeTeams1v1)
        {
            if (!ValidateTeamMode(fighters, teams, count))
                return false;
        }
        else if (mode == MatchRules::ModeLastStanding)
        {
            for (int i = 0; i < count; ++i)
            {
                if (teams[i] != MatchRules::TeamNone)
                {
                    g_status = "Last Standing requires no teams";
                    return false;
                }
            }
        }

        if (mode == MatchRules::ModeTeams1v1 && !TownAftercare::ControlAvailable()) {
            g_status = "Teams 1v1 unavailable: bench AI control is not installed";
            return false;
        }
        g_mode = mode;
        g_bench.assign(static_cast<size_t>(count), BenchSlot());
        g_retired.assign(static_cast<size_t>(count), 0);
        g_fighters.assign(fighters, fighters + count);
        g_teams.assign(teams, teams + count);
        g_eliminated.assign(static_cast<size_t>(count), 0);
        g_activeA = NULL;
        g_activeB = NULL;
        g_interBoutWaiting = NULL;
        ArenaIngress::ClearWalkIn();

        if (mode == MatchRules::ModeTeams1v1)
        {
            g_activeA = FirstOfTeam(MatchRules::TeamA);
            g_activeB = FirstOfTeam(MatchRules::TeamB);
            if (!g_activeA || !g_activeB)
            {
                g_fighters.clear();
                g_teams.clear();
                g_status = "Need at least one fighter per team";
                return false;
            }
        }

        TownArena::CancelPlannedNpcBout("Player match took priority");
        g_active = true;
        g_status = BuildStatusString(mode, fighters, teams, count);
        PGLog::Debug(("Proving Grounds: spar start " + g_status).c_str());

        FightStarter::RememberMatchFighters(fighters, count);
        ResultsUI::Cancel();
        ResultsUI::Close();
        SparStats::ResetForMatch();
        g_hasPendingOutcome = false;
        g_pendingLastStanding = NULL;
        g_pendingOutcome = SparPodium::OutcomeStopped;
        g_pendingEndKind = MatchRules::EndNone;

        CombatBalanceLog::StartMatch(mode, fighters, teams, count);
        EngageMatch(fighters, teams, count, mode);
        TownArena::OnMatchStarted();
        return true;
    }

    bool Start(Character* a, Character* b)
    {
        Character* fighters[] = { a, b };
        MatchRules::MatchTeam teams[] = { MatchRules::TeamA, MatchRules::TeamB };
        return StartMatch(MatchRules::ModeTeamAvB, fighters, teams, 2);
    }

    void SetPendingEndKind(MatchRules::MatchEndKind kind)
    {
        g_pendingEndKind = kind;
    }

    MatchRules::MatchEndKind GetPendingEndKind()
    {
        return g_pendingEndKind;
    }

    void SetPendingResultsOutcome(SparPodium::OutcomeKind kind, Character* lastStanding)
    {
        g_pendingOutcome = kind;
        g_pendingLastStanding = lastStanding;
        g_hasPendingOutcome = true;
    }

    void StopWithStatus(StopReason reason, Character* koVictim, const char* statusOverride)
    {
        if (!g_active)
            return;

        if (reason != StopInvalid)
        {
            SparPodium::OutcomeKind outcome = SparPodium::OutcomeStopped;
            if (reason == StopManual)
                outcome = SparPodium::OutcomeStopped;
            else if (g_hasPendingOutcome)
                outcome = g_pendingOutcome;

            SparStats::Freeze(outcome, g_mode, g_pendingLastStanding);
            const LeaderboardData::Kind leaderboard =
                LeaderboardData::ForMatch(TownArena::OwnsMatch());
            LeaderboardStore::ApplyFromSnapshot(
                SparStats::GetMutableSnapshot(), leaderboard, TownArena::GetActiveMarksMultiplier(),
                TownArena::GetActiveMarksBonus());
            CombatBalanceLog::FinishMatch(SparStats::GetSnapshot(), reason);
            TownArena::OnMatchResult(SparStats::GetSnapshot());
            if (!TownArena::OwnsMatch() || TownArena::IsPlayerMatch())
                ResultsUI::ScheduleShow(1.0f);
        }

        if (reason == StopInvalid) { CombatBalanceLog::AbortMatch("invalid_fighter"); TownArena::OnMatchAborted(); }
        ArenaIngress::ClearWalkIn();
        DisengageAll();

        g_active = false;
        g_bench.clear(); g_retired.clear();
        g_fighters.clear();
        g_teams.clear();
        g_eliminated.clear();
        g_activeA = NULL;
        g_activeB = NULL;
        g_interBoutWaiting = NULL;
        g_hasPendingOutcome = false;
        g_pendingLastStanding = NULL;
        g_pendingOutcome = SparPodium::OutcomeStopped;
        g_pendingEndKind = MatchRules::EndNone;

        std::string returnStatus;
        PrisonerUtil::BeginReturnToCages(returnStatus);

        if (statusOverride && statusOverride[0] != '\0')
        {
            g_status = statusOverride;
        }
        else
        {
            switch (reason)
            {
            case StopKo:
                g_status = "Over: " + CharName(koVictim) + " KO'd";
                break;
            case StopDeath:
                g_status = "Over: " + CharName(koVictim) + " died";
                break;
            case StopInvalid:
                g_status = "Over: fighter invalid";
                break;
            case StopManual:
            default:
                g_status = "Stopped";
                break;
            }
        }

        if (!returnStatus.empty())
        {
            PGLog::Debug(returnStatus.c_str());
            if (!g_status.empty())
                g_status += "\n";
            g_status += returnStatus;
        }

        PGLog::Debug(("Proving Grounds: spar stop — " + g_status).c_str());
    }

    void Stop(StopReason reason, Character* koVictim)
    {
        StopWithStatus(reason, koVictim, NULL);
    }
}
