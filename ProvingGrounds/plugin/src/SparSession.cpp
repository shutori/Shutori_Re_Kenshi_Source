#include "SparSession.h"
#include "ArenaIngress.h"
#include "FightStarter.h"
#include "LeaderboardStore.h"
#include "PrisonerMatchLogic.h"
#include "PrisonerUtil.h"
#include "ResultsUI.h"
#include "SparStats.h"

#include <Debug.h>

#include <vector>

#pragma warning(push)
#pragma warning(disable: 4091)
#include <kenshi/Character.h>
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
    bool g_koEliminationEnabled = false;
    std::string g_status = "Idle";
    Character* g_activeA = NULL;
    Character* g_activeB = NULL;
    SparPodium::OutcomeKind g_pendingOutcome = SparPodium::OutcomeStopped;
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

    void MarkEliminated(Character* c)
    {
        const int index = FindParticipantIndex(c);
        if (index < 0)
            return;
        if (index >= static_cast<int>(g_eliminated.size()))
            g_eliminated.resize(static_cast<size_t>(index) + 1, 0);
        g_eliminated[static_cast<size_t>(index)] = 1;
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
        for (size_t i = 0; i < g_fighters.size(); ++i)
        {
            Character* c = g_fighters[i];
            if (!c || c == g_activeA || c == g_activeB)
                continue;
            if (!c->isValid() || c->isDead())
                continue;
            c->setStandingOrder(MessageForB::M_SET_ORDER_PASSIVE, true);
            c->setStandingOrder(MessageForB::M_SET_ORDER_HOLD, true);
            c->setStandingOrder(MessageForB::M_SET_ORDER_AGG, false);
        }
    }

    void EngageActivePair()
    {
        if (!IsLiving(g_activeA) || !IsLiving(g_activeB))
            return;

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

        FightStarter::DisengageMatch(g_fighters.data(), count);
    }

    bool TryPromoteSide(Character*& active, MatchRules::MatchTeam team)
    {
        if (IsLiving(active))
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
        DebugLog(("Proving Grounds: " + g_status).c_str());

        active = next;
        if (!ArenaIngress::BeginWalkIn(next, team))
        {
            // Fallback: promote in place if walk-in cannot start.
            EngageActivePair();
        }
        return true;
    }
}

namespace SparSession
{
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

    void SetKoEliminationEnabled(bool enabled)
    {
        if (!g_active)
            g_koEliminationEnabled = enabled;
    }

    bool IsKoEliminationEnabled()
    {
        // Sequential 1v1 has always used winner-stays elimination.
        return g_mode == MatchRules::ModeTeams1v1 || g_koEliminationEnabled;
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

        if (ArenaIngress::IsWalkInPending())
        {
            ArenaIngress::TickWalkIn();
            if (ArenaIngress::IsWalkInArrived())
            {
                ArenaIngress::ClearWalkIn();
                EngageActivePair();
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

        g_mode = mode;
        g_fighters.assign(fighters, fighters + count);
        g_teams.assign(teams, teams + count);
        g_eliminated.assign(static_cast<size_t>(count), 0);
        g_activeA = NULL;
        g_activeB = NULL;
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

        g_active = true;
        g_status = BuildStatusString(mode, fighters, teams, count);
        DebugLog(("Proving Grounds: spar start " + g_status).c_str());

        FightStarter::RememberMatchFighters(fighters, count);
        ResultsUI::Cancel();
        ResultsUI::Close();
        SparStats::ResetForMatch();
        g_hasPendingOutcome = false;
        g_pendingLastStanding = NULL;
        g_pendingOutcome = SparPodium::OutcomeStopped;

        EngageMatch(fighters, teams, count, mode);
        return true;
    }

    bool Start(Character* a, Character* b)
    {
        Character* fighters[] = { a, b };
        MatchRules::MatchTeam teams[] = { MatchRules::TeamA, MatchRules::TeamB };
        return StartMatch(MatchRules::ModeTeamAvB, fighters, teams, 2);
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
            LeaderboardStore::ApplyFromSnapshot(SparStats::GetMutableSnapshot());
            ResultsUI::ScheduleShow(0.8f);
        }

        ArenaIngress::ClearWalkIn();
        DisengageAll();

        g_active = false;
        g_fighters.clear();
        g_teams.clear();
        g_eliminated.clear();
        g_activeA = NULL;
        g_activeB = NULL;
        g_hasPendingOutcome = false;
        g_pendingLastStanding = NULL;
        g_pendingOutcome = SparPodium::OutcomeStopped;

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
            DebugLog(returnStatus.c_str());
            if (!g_status.empty())
                g_status += "\n";
            g_status += returnStatus;
        }

        DebugLog(("Proving Grounds: spar stop — " + g_status).c_str());
    }

    void Stop(StopReason reason, Character* koVictim)
    {
        StopWithStatus(reason, koVictim, NULL);
    }
}
