#pragma once

#include "RewardProgression.h"
#include <climits>
#include <string>
#include <vector>

namespace LeaderboardData
{
    enum Kind
    {
        Player,
        Town
    };

    const int kCurrentVersion = 9;
    const char* const kPlayerBuildingId = "53-Proving Grounds.mod";
    const char* const kPlayerFunctionId = "57-Proving Grounds.mod";
    const char* const kTownBuildingId = "272-Proving Grounds.mod";
    const char* const kTownFunctionId = "273-Proving Grounds.mod";

    inline bool IdEquals(const char* a, const char* b)
    {
        if (!a || !b)
            return false;
        while (*a && *b && *a == *b)
        {
            ++a;
            ++b;
        }
        return *a == '\0' && *b == '\0';
    }

    inline bool IsLeaderboardId(const char* id)
    {
        return IdEquals(id, kPlayerBuildingId) ||
            IdEquals(id, kPlayerFunctionId) ||
            IdEquals(id, kTownBuildingId) ||
            IdEquals(id, kTownFunctionId);
    }

    inline Kind KindForId(const char* id)
    {
        return IdEquals(id, kTownBuildingId) ||
            IdEquals(id, kTownFunctionId) ? Town : Player;
    }

    // id is a mod-owned FighterIdentity token; name is display metadata only.
    struct Standing
    {
        std::string id;
        std::string name;
        float mmr;
        int wins;
        int losses;
        int matches;

        Standing() : mmr(100.0f), wins(0), losses(0), matches(0) {}
    };

    // Shared by both standings tables, with no pointer or name balance cache.
    struct Progression
    {
        std::string id;
        std::string name;
        int marks;
        std::vector<RewardProgression::TierEntry> rewardTiers;

        Progression() : marks(0) {}
    };

    struct Collections
    {
        std::vector<Standing> playerStandings;
        std::vector<Standing> townStandings;
        std::vector<Progression> progression;
        std::vector<std::string> factionUnlocks;
    };

    inline Kind ForMatch(bool townOwned)
    {
        return townOwned ? Town : Player;
    }

    inline bool ShouldResetFighters(int version)
    {
        return version < kCurrentVersion;
    }

    inline std::vector<Standing>& Standings(Collections& data, Kind kind)
    {
        return kind == Town ? data.townStandings : data.playerStandings;
    }

    inline const std::vector<Standing>& Standings(
        const Collections& data,
        Kind kind)
    {
        return kind == Town ? data.townStandings : data.playerStandings;
    }

    inline Standing& EnsureStanding(
        Collections& data,
        Kind kind,
        const std::string& id,
        const std::string& name)
    {
        std::vector<Standing>& rows = Standings(data, kind);
        for (size_t i = 0; i < rows.size(); ++i)
        {
            if (rows[i].id != id)
                continue;
            if (!name.empty())
                rows[i].name = name;
            return rows[i];
        }

        Standing row;
        row.id = id;
        row.name = name;
        rows.push_back(row);
        return rows.back();
    }

    inline Progression& EnsureProgression(
        Collections& data,
        const std::string& id,
        const std::string& name)
    {
        for (size_t i = 0; i < data.progression.size(); ++i)
        {
            if (data.progression[i].id != id)
                continue;
            if (!name.empty())
                data.progression[i].name = name;
            return data.progression[i];
        }

        Progression row;
        row.id = id;
        row.name = name;
        data.progression.push_back(row);
        return data.progression.back();
    }

    inline int AwardedTotal(int value, int increase)
    {
        if (increase <= 0) return value;
        return value > INT_MAX - increase ? INT_MAX : value + increase;
    }

    inline void ApplyCompetitiveResult(
        Collections& data,
        Kind kind,
        const std::string& id,
        const std::string& name,
        float mmr,
        int winsDelta,
        int lossesDelta)
    {
        Standing& row = EnsureStanding(data, kind, id, name);
        row.mmr = mmr;
        row.wins = AwardedTotal(row.wins, winsDelta);
        row.losses = AwardedTotal(row.losses, lossesDelta);
        row.matches = AwardedTotal(row.matches, 1);
    }

    inline void AddMarks(
        Collections& data,
        const std::string& id,
        const std::string& name,
        int marks)
    {
        Progression& row = EnsureProgression(data, id, name);
        row.marks = AwardedTotal(row.marks, marks);
    }

    inline void ResetFighters(Collections& data)
    {
        data.playerStandings.clear();
        data.townStandings.clear();
        data.progression.clear();
        data.factionUnlocks.clear();
    }
}
