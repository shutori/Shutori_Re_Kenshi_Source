#include "KOWatcher.h"
#include "BalanceTuning.h"
#include "FightStarter.h"
#include "FrameCadencePolicy.h"
#include "MatchRules.h"
#include "SparSession.h"
#include "SparStats.h"

#pragma warning(push)
#pragma warning(disable: 4091)
#include <kenshi/Character.h>
#pragma warning(pop)

#include <string>
#include <vector>
#include <windows.h>

#ifndef NULL
#define NULL 0
#endif

namespace
{
    const float kKoScanIntervalSec = 0.10f;
    const float kKeepAliveIntervalSec = 0.50f;

    float g_scanElapsedSec = kKoScanIntervalSec;
    float g_keepAliveElapsedSec = 0.0f;
    DWORD g_lastTick = 0;
    std::vector<Character*> g_fighters;
    std::vector<MatchRules::MatchParticipant> g_snapshot;
    std::vector<char> g_previousConscious;
    std::vector<Character*> g_remaining;
    std::vector<MatchRules::MatchTeam> g_remainingTeams;

    float AdvanceClock()
    {
        const DWORD now = GetTickCount();
        const float dt = g_lastTick == 0 ? 0.0f :
            static_cast<float>(now - g_lastTick) / 1000.0f;
        g_lastTick = now;
        return dt > 1.0f ? 1.0f : dt;
    }

    void ResetRuntimeState()
    {
        FrameCadencePolicy::Reset(g_scanElapsedSec);
        FrameCadencePolicy::Reset(g_keepAliveElapsedSec);
        g_scanElapsedSec = kKoScanIntervalSec;
        g_lastTick = 0;
        g_fighters.clear();
        g_snapshot.clear();
        g_previousConscious.clear();
        g_remaining.clear();
        g_remainingTeams.clear();
    }

    std::string CharName(Character* c)
    {
        if (!c)
            return "(none)";
        return c->getName();
    }
}

namespace KOWatcher
{
    void Tick()
    {
        if (!SparSession::IsActive())
        {
            ResetRuntimeState();
            return;
        }

        const float dt = AdvanceClock();
        if (!FrameCadencePolicy::Advance(g_scanElapsedSec, dt, kKoScanIntervalSec))
            return;

        const int count = SparSession::GetParticipantCount();
        if (count <= 0)
        {
            SparSession::Stop(SparSession::StopInvalid);
            return;
        }

        const MatchRules::MatchMode mode = SparSession::GetMode();
        g_fighters.resize(static_cast<size_t>(count));
        g_snapshot.resize(static_cast<size_t>(count));
        if (g_previousConscious.size() != static_cast<size_t>(count))
            g_previousConscious.assign(static_cast<size_t>(count), -1);
        bool eliminatedThisTick = false;
        bool teamStateChangedThisTick = false;

        for (int i = 0; i < count; ++i)
        {
            Character* c = SparSession::GetParticipant(i);
            g_fighters[static_cast<size_t>(i)] = c;
            if (!c || !c->isValid())
            {
                SparSession::Stop(SparSession::StopInvalid);
                return;
            }

            MatchRules::MatchParticipant& p = g_snapshot[static_cast<size_t>(i)];
            p.id = i;
            p.team = SparSession::GetTeam(c);
            p.dead = c->isDead();
            p.conscious = !p.dead && !c->isUnconcious();

            char& previousConscious = g_previousConscious[static_cast<size_t>(i)];
            if (mode == MatchRules::ModeTeamAvB && previousConscious >= 0 &&
                previousConscious != (p.conscious ? 1 : 0))
                teamStateChangedThisTick = true;
            previousConscious = p.conscious ? 1 : 0;

            if (SparSession::IsKoEliminationEnabled() &&
                mode != MatchRules::ModeTeamAvB &&
                !p.dead &&
                c->isUnconcious())
            {
                eliminatedThisTick =
                    SparSession::EliminateFighter(c) || eliminatedThisTick;
            }

            p.eliminated = SparSession::IsEliminated(c);
            if (p.eliminated)
                p.conscious = false;

            if (!p.conscious)
                SparStats::OnEliminated(c);
        }

        if (mode == MatchRules::ModeTeams1v1)
        {
            if (SparSession::TickTeams1v1Bouts())
            {
                // Bout transition / walk-in in progress — do not end yet.
                FrameCadencePolicy::Reset(g_keepAliveElapsedSec);
                return;
            }

            // Promotion marks newly KO'd actives eliminated — refresh before wipe check
            // so a woken bench fighter can't keep a wiped team "alive".
            for (int i = 0; i < count; ++i)
            {
                g_snapshot[static_cast<size_t>(i)].eliminated =
                    SparSession::IsEliminated(g_fighters[static_cast<size_t>(i)]);
            }
        }

        const MatchRules::MatchEndKind end =
            MatchRules::EvaluateEnd(mode, g_snapshot.data(), count,
                BalanceTuning::Get().downedPercent);

        if (end != MatchRules::EndNone)
        {
            // Recorded so the telemetry log can tell a team wipe from a draw or a
            // last-standing; StopReason alone cannot.
            SparSession::SetPendingEndKind(end);
            switch (end)
            {
            case MatchRules::EndTeamWipeA:
                SparSession::SetPendingResultsOutcome(SparPodium::OutcomeTeamBWins, NULL);
                if (mode == MatchRules::ModeTeams1v1)
                    SparSession::StopWithStatus(SparSession::StopKo, NULL, "Over: Team B wins");
                else
                    SparSession::StopWithStatus(SparSession::StopKo, NULL, "Over: Team A wiped");
                return;
            case MatchRules::EndTeamWipeB:
                SparSession::SetPendingResultsOutcome(SparPodium::OutcomeTeamAWins, NULL);
                if (mode == MatchRules::ModeTeams1v1)
                    SparSession::StopWithStatus(SparSession::StopKo, NULL, "Over: Team A wins");
                else
                    SparSession::StopWithStatus(SparSession::StopKo, NULL, "Over: Team B wiped");
                return;
            case MatchRules::EndLastStanding:
            {
                Character* winner = NULL;
                for (int i = 0; i < count; ++i)
                {
                    if (g_snapshot[static_cast<size_t>(i)].conscious)
                    {
                        winner = g_fighters[static_cast<size_t>(i)];
                        break;
                    }
                }
                SparSession::SetPendingResultsOutcome(SparPodium::OutcomeLastStanding, winner);
                const std::string status = "Over: " + CharName(winner) + " last standing";
                SparSession::StopWithStatus(SparSession::StopKo, winner, status.c_str());
                return;
            }
            case MatchRules::EndDraw:
                SparSession::SetPendingResultsOutcome(SparPodium::OutcomeDraw, NULL);
                SparSession::StopWithStatus(SparSession::StopKo, NULL, "Over: Draw / none standing");
                return;
            default:
                break;
            }
        }

        if ((eliminatedThisTick || teamStateChangedThisTick) &&
            mode != MatchRules::ModeTeams1v1)
        {
            // Clear stale orders and temporary-enemy memories that still point
            // at the eliminated fighter, then immediately restart the remaining
            // combatants against valid opponents. This is an in-match retarget,
            // not a full disengage: keep Jobs and saved pre-match orders intact.

            g_remaining.clear();
            g_remainingTeams.clear();
            for (int i = 0; i < count; ++i)
            {
                Character* c = g_fighters[static_cast<size_t>(i)];
                if (SparSession::IsEliminated(c) ||
                    !c || !c->isValid() || c->isDead() || c->isUnconcious())
                {
                    continue;
                }
                g_remaining.push_back(c);
                g_remainingTeams.push_back(SparSession::GetTeam(c));
            }

            FightStarter::RetargetMatchAfterElimination(
                g_fighters.data(),
                count,
                g_remaining.empty() ? NULL : g_remaining.data(),
                g_remainingTeams.empty() ? NULL : g_remainingTeams.data(),
                static_cast<int>(g_remaining.size()),
                mode);
            FrameCadencePolicy::Reset(g_keepAliveElapsedSec);
            return;
        }

        // Periodically refresh attack orders if AI dropped the target.
        if (FrameCadencePolicy::Advance(
            g_keepAliveElapsedSec, kKoScanIntervalSec, kKeepAliveIntervalSec))
        {
            if (mode == MatchRules::ModeTeams1v1)
            {
                Character* a = SparSession::GetActiveA();
                Character* b = SparSession::GetActiveB();
                if (a && b)
                    FightStarter::KeepEngaged(a, b);
                return;
            }

            g_remaining.clear();
            g_remainingTeams.clear();
            for (int i = 0; i < count; ++i)
            {
                Character* c = g_fighters[static_cast<size_t>(i)];
                if (SparSession::IsEliminated(c) || !c || !c->isValid() ||
                    c->isDead() || c->isUnconcious())
                    continue;
                g_remaining.push_back(c);
                g_remainingTeams.push_back(SparSession::GetTeam(c));
            }

            if (g_remaining.size() >= 2)
            {
                FightStarter::KeepEngagedMatch(
                    g_remaining.data(),
                    g_remainingTeams.data(),
                    static_cast<int>(g_remaining.size()),
                    mode);
            }
        }
    }
}
