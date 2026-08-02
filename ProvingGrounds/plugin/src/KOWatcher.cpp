#include "KOWatcher.h"
#include "FightStarter.h"
#include "MatchRules.h"
#include "SparSession.h"
#include "SparStats.h"

#pragma warning(push)
#pragma warning(disable: 4091)
#include <kenshi/Character.h>
#pragma warning(pop)

#include <string>
#include <vector>

#ifndef NULL
#define NULL 0
#endif

namespace
{
    int g_keepAliveCounter = 0;

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
            g_keepAliveCounter = 0;
            return;
        }

        const int count = SparSession::GetParticipantCount();
        if (count <= 0)
        {
            SparSession::Stop(SparSession::StopInvalid);
            return;
        }

        const MatchRules::MatchMode mode = SparSession::GetMode();
        std::vector<Character*> fighters(static_cast<size_t>(count));
        std::vector<MatchRules::MatchParticipant> snapshot(static_cast<size_t>(count));
        bool eliminatedThisTick = false;

        for (int i = 0; i < count; ++i)
        {
            Character* c = SparSession::GetParticipant(i);
            fighters[static_cast<size_t>(i)] = c;
            if (!c || !c->isValid())
            {
                SparSession::Stop(SparSession::StopInvalid);
                return;
            }

            MatchRules::MatchParticipant& p = snapshot[static_cast<size_t>(i)];
            p.id = i;
            p.team = SparSession::GetTeam(c);
            p.dead = c->isDead();
            p.conscious = !p.dead && !c->isUnconcious();

            if (SparSession::IsKoEliminationEnabled() &&
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
                g_keepAliveCounter = 0;
                return;
            }

            // Promotion marks newly KO'd actives eliminated — refresh before wipe check
            // so a woken bench fighter can't keep a wiped team "alive".
            for (int i = 0; i < count; ++i)
            {
                snapshot[static_cast<size_t>(i)].eliminated =
                    SparSession::IsEliminated(fighters[static_cast<size_t>(i)]);
            }
        }

        const MatchRules::MatchEndKind end =
            MatchRules::EvaluateEnd(mode, snapshot.data(), count);

        if (end != MatchRules::EndNone)
        {
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
                    if (snapshot[static_cast<size_t>(i)].conscious)
                    {
                        winner = fighters[static_cast<size_t>(i)];
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

        if (eliminatedThisTick && mode != MatchRules::ModeTeams1v1)
        {
            // Clear stale orders and temporary-enemy memories that still point
            // at the eliminated fighter, then immediately restart the remaining
            // combatants against valid opponents.
            FightStarter::DisengageMatch(fighters.data(), count);

            std::vector<Character*> remaining;
            std::vector<MatchRules::MatchTeam> remainingTeams;
            for (int i = 0; i < count; ++i)
            {
                Character* c = fighters[static_cast<size_t>(i)];
                if (SparSession::IsEliminated(c) ||
                    !c || !c->isValid() || c->isDead() || c->isUnconcious())
                {
                    continue;
                }
                remaining.push_back(c);
                remainingTeams.push_back(SparSession::GetTeam(c));
            }

            if (remaining.size() >= 2)
            {
                FightStarter::EngageMatch(
                    remaining.data(),
                    remainingTeams.data(),
                    static_cast<int>(remaining.size()),
                    mode);
            }
            g_keepAliveCounter = 0;
            return;
        }

        // Periodically refresh attack orders if AI dropped the target.
        if (++g_keepAliveCounter >= 30)
        {
            g_keepAliveCounter = 0;

            if (mode == MatchRules::ModeTeams1v1)
            {
                Character* a = SparSession::GetActiveA();
                Character* b = SparSession::GetActiveB();
                if (a && b)
                    FightStarter::KeepEngaged(a, b);
                return;
            }

            std::vector<Character*> remaining;
            std::vector<MatchRules::MatchTeam> teams;
            for (int i = 0; i < count; ++i)
            {
                Character* c = fighters[static_cast<size_t>(i)];
                if (SparSession::IsEliminated(c))
                    continue;
                remaining.push_back(c);
                teams.push_back(SparSession::GetTeam(c));
            }

            if (remaining.size() >= 2)
            {
                FightStarter::KeepEngagedMatch(
                    remaining.data(),
                    teams.data(),
                    static_cast<int>(remaining.size()),
                    mode);
            }
        }
    }
}
