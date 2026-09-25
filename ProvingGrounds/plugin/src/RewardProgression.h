#pragma once

#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace RewardProgression
{
    struct TierEntry
    {
        std::string id;
        int tier;

        TierEntry() : tier(-1) {}
        TierEntry(const std::string& value, int level) : id(value), tier(level) {}
    };

    inline int HighestTier(
        const std::vector<TierEntry>& entries,
        const std::string& id)
    {
        for (size_t i = 0; i < entries.size(); ++i)
            if (entries[i].id == id)
                return entries[i].tier;
        return -1;
    }

    inline bool SetHighestTier(
        std::vector<TierEntry>& entries,
        const std::string& id,
        int expectedPrevious,
        int nextTier)
    {
        if (id.empty() || nextTier < 0 ||
            (expectedPrevious >= 0 && nextTier != expectedPrevious + 1))
            return false;
        for (size_t i = 0; i < entries.size(); ++i)
        {
            if (entries[i].id != id)
                continue;
            if (entries[i].tier != expectedPrevious)
                return false;
            entries[i].tier = nextTier;
            return true;
        }
        if (expectedPrevious != -1)
            return false;
        entries.push_back(TierEntry(id, nextTier));
        return true;
    }

    inline bool RestoreHighestTier(
        std::vector<TierEntry>& entries,
        const std::string& id,
        int tier)
    {
        for (size_t i = 0; i < entries.size(); ++i)
        {
            if (entries[i].id != id)
                continue;
            if (tier < 0)
                entries.erase(entries.begin() + i);
            else
                entries[i].tier = tier;
            return true;
        }
        if (tier < 0 || id.empty())
            return false;
        entries.push_back(TierEntry(id, tier));
        return true;
    }

    inline bool HasUnlock(
        const std::vector<std::string>& unlocks,
        const std::string& id)
    {
        for (size_t i = 0; i < unlocks.size(); ++i)
            if (unlocks[i] == id)
                return true;
        return false;
    }

    inline bool AddUnlock(
        std::vector<std::string>& unlocks,
        const std::string& id)
    {
        if (id.empty() || HasUnlock(unlocks, id))
            return false;
        unlocks.push_back(id);
        return true;
    }

    inline bool RemoveUnlock(
        std::vector<std::string>& unlocks,
        const std::string& id)
    {
        for (size_t i = 0; i < unlocks.size(); ++i)
        {
            if (unlocks[i] != id)
                continue;
            unlocks.erase(unlocks.begin() + i);
            return true;
        }
        return false;
    }

    inline std::string EncodeTiers(const std::vector<TierEntry>& entries)
    {
        std::ostringstream out;
        for (size_t i = 0; i < entries.size(); ++i)
            out << entries[i].id.size() << ':' << entries[i].id << '='
                << entries[i].tier << ';';
        return out.str();
    }

    inline bool ReadLength(
        const std::string& text,
        size_t& cursor,
        size_t& length)
    {
        const size_t colon = text.find(':', cursor);
        if (colon == std::string::npos || colon == cursor)
            return false;
        for (size_t i = cursor; i < colon; ++i)
            if (text[i] < '0' || text[i] > '9')
                return false;
        length = static_cast<size_t>(std::strtoul(
            text.substr(cursor, colon - cursor).c_str(), NULL, 10));
        cursor = colon + 1;
        return cursor + length <= text.size();
    }

    inline bool DecodeTiers(
        const std::string& text,
        std::vector<TierEntry>& entries)
    {
        std::vector<TierEntry> parsed;
        size_t cursor = 0;
        while (cursor < text.size())
        {
            size_t length = 0;
            if (!ReadLength(text, cursor, length))
                return false;
            const std::string id = text.substr(cursor, length);
            cursor += length;
            if (id.empty() || cursor >= text.size() || text[cursor++] != '=')
                return false;
            const size_t end = text.find(';', cursor);
            if (end == std::string::npos || end == cursor)
                return false;
            const int tier = std::atoi(text.substr(cursor, end - cursor).c_str());
            if (tier < 0)
                return false;
            bool replaced = false;
            for (size_t i = 0; i < parsed.size(); ++i)
            {
                if (parsed[i].id == id)
                {
                    parsed[i].tier = tier;
                    replaced = true;
                    break;
                }
            }
            if (!replaced)
                parsed.push_back(TierEntry(id, tier));
            cursor = end + 1;
        }
        entries.swap(parsed);
        return true;
    }

    inline std::string EncodeUnlocks(const std::vector<std::string>& unlocks)
    {
        std::ostringstream out;
        for (size_t i = 0; i < unlocks.size(); ++i)
            out << unlocks[i].size() << ':' << unlocks[i] << ';';
        return out.str();
    }

    inline bool DecodeUnlocks(
        const std::string& text,
        std::vector<std::string>& unlocks)
    {
        std::vector<std::string> parsed;
        size_t cursor = 0;
        while (cursor < text.size())
        {
            size_t length = 0;
            if (!ReadLength(text, cursor, length))
                return false;
            const std::string id = text.substr(cursor, length);
            cursor += length;
            if (id.empty() || cursor >= text.size() || text[cursor++] != ';')
                return false;
            AddUnlock(parsed, id);
        }
        unlocks.swap(parsed);
        return true;
    }
}
