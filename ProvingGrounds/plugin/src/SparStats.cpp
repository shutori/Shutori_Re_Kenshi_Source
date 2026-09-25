#include "SparStats.h"
#include "SparSession.h"

#include <cstring>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable: 4091)
#include <kenshi/Character.h>
#pragma warning(pop)

#ifndef NULL
#define NULL 0
#endif

namespace
{
    struct LiveStats
    {
        Character* character;
        float damageDealt;
        float damageTaken;
        float damageMitigated;
        int hitsLanded;
        int blocks;
        int misses;
        int dodges;
    };

    std::vector<LiveStats> g_live;
    std::vector<Character*> g_elimOrder;
    SparPodium::Snapshot g_snap;
    bool g_hasSnap = false;
    Character* g_podiumChars[3] = { NULL, NULL, NULL };

    LiveStats* FindLive(Character* c)
    {
        for (size_t i = 0; i < g_live.size(); ++i)
        {
            if (g_live[i].character == c)
                return &g_live[i];
        }
        return NULL;
    }

    LiveStats* EnsureLive(Character* c)
    {
        LiveStats* existing = FindLive(c);
        if (existing)
            return existing;
        LiveStats row = {};
        row.character = c;
        g_live.push_back(row);
        return &g_live.back();
    }

    bool AlreadyEliminated(Character* c)
    {
        for (size_t i = 0; i < g_elimOrder.size(); ++i)
        {
            if (g_elimOrder[i] == c)
                return true;
        }
        return false;
    }

    int ParticipantId(Character* c)
    {
        const int count = SparSession::GetParticipantCount();
        for (int i = 0; i < count; ++i)
        {
            if (SparSession::GetParticipant(i) == c)
                return i;
        }
        return -1;
    }
}

namespace SparStats
{
    void AbandonWorldState()
    {
        ResetForMatch();
    }

    void ResetForMatch()
    {
        g_live.clear();
        g_elimOrder.clear();
        g_hasSnap = false;
        std::memset(&g_snap, 0, sizeof(g_snap));
        g_podiumChars[0] = NULL;
        g_podiumChars[1] = NULL;
        g_podiumChars[2] = NULL;
    }

    void OnHit(
        Character* attacker,
        Character* defender,
        float appliedDamage,
        float incomingDamage,
        bool isMiss)
    {
        if (!attacker || !defender)
            return;
        if (!SparSession::IsSparringOpponent(attacker, defender))
            return;

        LiveStats* atk = EnsureLive(attacker);
        LiveStats* def = EnsureLive(defender);
        if (isMiss)
        {
            ++atk->misses;
            ++def->dodges;
            return;
        }

        if (appliedDamage < 0.0f)
            appliedDamage = 0.0f;
        if (incomingDamage < appliedDamage)
            incomingDamage = appliedDamage;

        atk->damageDealt += appliedDamage;
        def->damageTaken += appliedDamage;
        def->damageMitigated += incomingDamage - appliedDamage;
        ++atk->hitsLanded;
    }

    void OnBlock(Character* defender, Character* attacker)
    {
        if (!attacker || !defender)
            return;
        if (!SparSession::IsSparringOpponent(attacker, defender))
            return;

        ++EnsureLive(defender)->blocks;
        EnsureLive(attacker);
    }

    void OnDodge(Character* attacker, Character* defender)
    {
        if (!attacker || !defender)
            return;
        if (!SparSession::IsSparringOpponent(attacker, defender))
            return;

        ++EnsureLive(attacker)->misses;
        ++EnsureLive(defender)->dodges;
    }

    void OnEliminated(Character* who)
    {
        if (!who || AlreadyEliminated(who))
            return;
        g_elimOrder.push_back(who);
    }

    void Freeze(SparPodium::OutcomeKind outcome, MatchRules::MatchMode mode, Character* lastStandingOrNull)
    {
        std::memset(&g_snap, 0, sizeof(g_snap));
        g_snap.outcome = outcome;
        g_snap.mode = mode;
        g_snap.lastStandingId = -1;
        g_snap.fighterCount = 0;
        g_snap.eliminationCount = 0;

        const int count = SparSession::GetParticipantCount();
        for (int i = 0; i < count && g_snap.fighterCount < 16; ++i)
        {
            Character* c = SparSession::GetParticipant(i);
            if (!c)
                continue;

            SparPodium::FighterRow& row = g_snap.fighters[g_snap.fighterCount];
            row.id = i;
            row.team = SparSession::GetTeam(c);
            {
                const std::string name = c->getName();
                std::strncpy(row.name, name.c_str(), 63);
                row.name[63] = '\0';
            }
            row.eliminationIndex = -1;

            LiveStats* live = FindLive(c);
            if (live)
            {
                row.damageDealt = live->damageDealt;
                row.damageTaken = live->damageTaken;
                row.damageMitigated = live->damageMitigated;
                row.hitsLanded = live->hitsLanded;
                row.blocks = live->blocks;
                row.misses = live->misses;
                row.dodges = live->dodges;
            }

            ++g_snap.fighterCount;
        }

        for (size_t e = 0; e < g_elimOrder.size() && g_snap.eliminationCount < 16; ++e)
        {
            const int id = ParticipantId(g_elimOrder[e]);
            if (id < 0)
                continue;
            for (int f = 0; f < g_snap.fighterCount; ++f)
            {
                if (g_snap.fighters[f].id == id)
                {
                    g_snap.fighters[f].eliminationIndex = g_snap.eliminationCount;
                    break;
                }
            }
            g_snap.eliminationIds[g_snap.eliminationCount++] = id;
        }

        if (lastStandingOrNull)
            g_snap.lastStandingId = ParticipantId(lastStandingOrNull);

        SparPodium::BuildPodium(g_snap);

        g_podiumChars[0] = NULL;
        g_podiumChars[1] = NULL;
        g_podiumChars[2] = NULL;
        for (int p = 0; p < g_snap.podiumCount && p < 3; ++p)
        {
            const int id = g_snap.podium[p].fighter.id;
            if (id >= 0 && id < count)
                g_podiumChars[p] = SparSession::GetParticipant(id);
        }

        g_hasSnap = true;
    }

    const SparPodium::Snapshot& GetSnapshot()
    {
        return g_snap;
    }

    SparPodium::Snapshot& GetMutableSnapshot()
    {
        return g_snap;
    }

    bool HasSnapshot()
    {
        return g_hasSnap;
    }

    void ClearSnapshot()
    {
        g_hasSnap = false;
        std::memset(&g_snap, 0, sizeof(g_snap));
        g_podiumChars[0] = NULL;
        g_podiumChars[1] = NULL;
        g_podiumChars[2] = NULL;
    }

    Character* GetPodiumCharacter(int podiumIndex)
    {
        if (!g_hasSnap || podiumIndex < 0 || podiumIndex >= 3)
            return NULL;
        return g_podiumChars[podiumIndex];
    }
}
