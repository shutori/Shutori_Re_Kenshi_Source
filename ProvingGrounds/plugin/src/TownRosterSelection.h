#pragma once
#include <algorithm>
#include <string>
#include <vector>

// Session-only UI lineup. IDs are resolved against the current player roster.
namespace TownRosterSelection
{
    inline bool Add(std::vector<std::string>& team, const std::string& id, bool ready, bool locked)
    {
        if (locked || !ready || id.empty() || team.size() >= 3 ||
            std::find(team.begin(), team.end(), id) != team.end()) return false;
        team.push_back(id);
        return true;
    }
    inline bool Remove(std::vector<std::string>& team, const std::string& id, bool locked)
    {
        if (locked) return false;
        std::vector<std::string>::iterator found = std::find(team.begin(), team.end(), id);
        if (found == team.end()) return false;
        team.erase(found);
        return true;
    }
    inline void Prune(std::vector<std::string>& team, const std::vector<std::string>& roster)
    {
        for (size_t i = 0; i < team.size(); )
            if (std::find(roster.begin(), roster.end(), team[i]) == roster.end()) team.erase(team.begin() + i);
            else ++i;
    }
    inline bool Toggle(std::vector<std::string>& team, const std::string& id, bool ready, bool locked)
    {
        if (std::find(team.begin(), team.end(), id) != team.end()) return Remove(team, id, locked);
        return Add(team, id, ready, locked);
    }
}
