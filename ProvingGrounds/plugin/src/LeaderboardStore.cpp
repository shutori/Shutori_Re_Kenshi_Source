#include "LeaderboardStore.h"
#include "MmrRating.h"
#include "PrisonerUtil.h"
#include "SparSession.h"
#include "SquadUtil.h"

#include <Debug.h>

#include <Windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <algorithm>

#pragma warning(push)
#pragma warning(disable: 4091)
#include <kenshi/Character.h>
#include <kenshi/InstanceID.h>
#include <kenshi/SaveManager.h>
#include <kenshi/util/hand.h>
#pragma warning(pop)

#ifndef NULL
#define NULL 0
#endif

namespace
{
    std::vector<LeaderboardStore::Record> g_records;
    std::string g_loadedSaveKey;
    std::string g_loadedFilePath;
    bool g_loaded = false;

    std::string CharacterKey(Character* c)
    {
        if (!c)
            return std::string();

        InstanceID* instanceId = c->getInstanceID();
        if (instanceId && !instanceId->empty() && !instanceId->uid.empty())
            return instanceId->uid;

        return c->getHandle().toString();
    }

    bool ResolveSavePaths(std::string& outKey, std::string& outFilePath)
    {
        SaveManager* sm = SaveManager::getSingleton();
        if (!sm)
            return false;

        const std::string& game = sm->getCurrentGame();
        if (game.empty())
            return false;

        outKey = game;

        std::string dir = sm->getSavePath();
        if (dir.empty())
            dir = sm->userSavePath;
        if (dir.empty())
            dir = sm->localSavePath;
        if (dir.empty())
            return false;

        // Sidecar lives next to the save folder: <savePath>/<game>/proving_grounds_mmr.json
        outFilePath = dir;
        if (!outFilePath.empty())
        {
            const char last = outFilePath[outFilePath.size() - 1];
            if (last != '\\' && last != '/')
                outFilePath += "\\";
        }
        outFilePath += game;
        outFilePath += "\\proving_grounds_mmr.json";
        return true;
    }

    void ClearMemory()
    {
        g_records.clear();
        g_loadedSaveKey.clear();
        g_loadedFilePath.clear();
        g_loaded = false;
    }

    std::string EscapeJson(const std::string& s)
    {
        std::string out;
        out.reserve(s.size() + 8);
        for (size_t i = 0; i < s.size(); ++i)
        {
            const char c = s[i];
            if (c == '\\' || c == '"')
            {
                out.push_back('\\');
                out.push_back(c);
            }
            else if (c == '\n')
            {
                out += "\\n";
            }
            else if (c >= 32)
            {
                out.push_back(c);
            }
        }
        return out;
    }

    bool ExtractStringField(const std::string& obj, const char* key, std::string& out)
    {
        const std::string needle = std::string("\"") + key + "\"";
        size_t pos = obj.find(needle);
        if (pos == std::string::npos)
            return false;
        pos = obj.find(':', pos);
        if (pos == std::string::npos)
            return false;
        pos = obj.find('"', pos);
        if (pos == std::string::npos)
            return false;
        ++pos;
        std::string value;
        while (pos < obj.size())
        {
            const char c = obj[pos++];
            if (c == '\\' && pos < obj.size())
            {
                value.push_back(obj[pos++]);
                continue;
            }
            if (c == '"')
                break;
            value.push_back(c);
        }
        out = value;
        return true;
    }

    bool ExtractNumberField(const std::string& obj, const char* key, double& out)
    {
        const std::string needle = std::string("\"") + key + "\"";
        size_t pos = obj.find(needle);
        if (pos == std::string::npos)
            return false;
        pos = obj.find(':', pos);
        if (pos == std::string::npos)
            return false;
        ++pos;
        while (pos < obj.size() && (obj[pos] == ' ' || obj[pos] == '\t'))
            ++pos;
        char* endPtr = NULL;
        const double v = strtod(obj.c_str() + pos, &endPtr);
        if (endPtr == obj.c_str() + pos)
            return false;
        out = v;
        return true;
    }

    void ParseFighterObject(const std::string& obj)
    {
        LeaderboardStore::Record rec;
        if (!ExtractStringField(obj, "id", rec.id))
            return;
        ExtractStringField(obj, "name", rec.name);
        double mmr = MmrRating::kDefaultMmr;
        double wins = 0;
        double losses = 0;
        double matches = 0;
        ExtractNumberField(obj, "mmr", mmr);
        ExtractNumberField(obj, "wins", wins);
        ExtractNumberField(obj, "losses", losses);
        ExtractNumberField(obj, "matches", matches);
        rec.mmr = static_cast<float>(mmr);
        rec.wins = static_cast<int>(wins);
        rec.losses = static_cast<int>(losses);
        rec.matches = static_cast<int>(matches);
        if (rec.matches < 0)
            rec.matches = 0;
        g_records.push_back(rec);
    }

    void LoadFromFile(const std::string& path)
    {
        g_records.clear();
        std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
        if (!in)
            return;

        std::stringstream buffer;
        buffer << in.rdbuf();
        const std::string text = buffer.str();

        const size_t fightersKey = text.find("\"fighters\"");
        if (fightersKey == std::string::npos)
            return;
        const size_t arrayStart = text.find('[', fightersKey);
        if (arrayStart == std::string::npos)
            return;

        size_t i = arrayStart + 1;
        while (i < text.size())
        {
            while (i < text.size() && (text[i] == ' ' || text[i] == '\n' || text[i] == '\r' || text[i] == '\t' || text[i] == ','))
                ++i;
            if (i >= text.size() || text[i] == ']')
                break;
            if (text[i] != '{')
            {
                ++i;
                continue;
            }

            const size_t objStart = i;
            int depth = 0;
            bool inString = false;
            bool escape = false;
            for (; i < text.size(); ++i)
            {
                const char c = text[i];
                if (inString)
                {
                    if (escape)
                        escape = false;
                    else if (c == '\\')
                        escape = true;
                    else if (c == '"')
                        inString = false;
                    continue;
                }
                if (c == '"')
                {
                    inString = true;
                    continue;
                }
                if (c == '{')
                    ++depth;
                else if (c == '}')
                {
                    --depth;
                    if (depth == 0)
                    {
                        ParseFighterObject(text.substr(objStart, i - objStart + 1));
                        ++i;
                        break;
                    }
                }
            }
        }
    }

    bool SaveToFile(const std::string& path)
    {
        // Ensure parent directory exists (save folder should already exist).
        const size_t slash = path.find_last_of("\\/");
        if (slash != std::string::npos)
        {
            const std::string dir = path.substr(0, slash);
            CreateDirectoryA(dir.c_str(), NULL);
        }

        std::ofstream out(path.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
        if (!out)
        {
            ErrorLog("Proving Grounds: failed to write rating sidecar");
            return false;
        }

        out << "{\n  \"version\": 1,\n  \"fighters\": [\n";
        for (size_t i = 0; i < g_records.size(); ++i)
        {
            const LeaderboardStore::Record& r = g_records[i];
            out << "    {"
                << "\"id\":\"" << EscapeJson(r.id) << "\","
                << "\"name\":\"" << EscapeJson(r.name) << "\","
                << "\"mmr\":" << r.mmr << ","
                << "\"wins\":" << r.wins << ","
                << "\"losses\":" << r.losses << ","
                << "\"matches\":" << r.matches
                << "}";
            if (i + 1 < g_records.size())
                out << ",";
            out << "\n";
        }
        out << "  ]\n}\n";
        return true;
    }

    LeaderboardStore::Record* FindRecord(const std::string& id)
    {
        for (size_t i = 0; i < g_records.size(); ++i)
        {
            if (g_records[i].id == id)
                return &g_records[i];
        }
        return NULL;
    }

    LeaderboardStore::Record& EnsureRecord(const std::string& id, const std::string& name)
    {
        LeaderboardStore::Record* existing = FindRecord(id);
        if (existing)
        {
            if (!name.empty())
                existing->name = name;
            return *existing;
        }

        LeaderboardStore::Record rec;
        rec.id = id;
        rec.name = name;
        rec.mmr = MmrRating::kDefaultMmr;
        rec.wins = 0;
        rec.losses = 0;
        rec.matches = 0;
        g_records.push_back(rec);
        return g_records.back();
    }

    bool StandingsLess(const LeaderboardStore::Record& a, const LeaderboardStore::Record& b)
    {
        if (a.mmr != b.mmr)
            return a.mmr > b.mmr;
        return a.name < b.name;
    }
}

namespace LeaderboardStore
{
    void EnsureLoaded()
    {
        std::string key;
        std::string path;
        if (!ResolveSavePaths(key, path))
        {
            if (g_loaded)
                ClearMemory();
            return;
        }

        if (g_loaded && g_loadedSaveKey == key && g_loadedFilePath == path)
            return;

        g_records.clear();
        LoadFromFile(path);
        g_loadedSaveKey = key;
        g_loadedFilePath = path;
        g_loaded = true;
        DebugLog(("Proving Grounds: rating store loaded for save " + key).c_str());
    }

    void ApplyFromSnapshot(SparPodium::Snapshot& snap)
    {
        if (!MmrRating::ShouldRate(snap.outcome))
            return;

        EnsureLoaded();
        if (!g_loaded)
        {
            DebugLog("Proving Grounds: rating skip - no active save key");
            return;
        }

        const int count = snap.fighterCount;
        if (count <= 0 || count > 16)
            return;

        float mmrBefore[16];
        int matchesBefore[16];
        bool rateMask[16];
        std::string ids[16];
        std::string names[16];

        for (int i = 0; i < count; ++i)
        {
            Character* c = SparSession::GetParticipant(snap.fighters[i].id);
            ids[i].clear();
            names[i] = snap.fighters[i].name;
            rateMask[i] = false;
            mmrBefore[i] = MmrRating::kDefaultMmr;
            matchesBefore[i] = 0;

            if (!c || !c->isValid())
                continue;

            ids[i] = CharacterKey(c);
            if (ids[i].empty())
                continue;

            Record& rec = EnsureRecord(ids[i], names[i]);
            mmrBefore[i] = rec.mmr;
            matchesBefore[i] = rec.matches;
            rateMask[i] = true;
        }

        bool any = false;
        for (int i = 0; i < count; ++i)
        {
            if (rateMask[i])
            {
                any = true;
                break;
            }
        }
        if (!any)
            return;

        float mmrAfter[16];
        int winsDelta[16];
        int lossesDelta[16];
        MmrRating::ApplyMatch(
            snap,
            mmrBefore,
            matchesBefore,
            rateMask,
            mmrAfter,
            winsDelta,
            lossesDelta);
        for (int i = 0; i < count; ++i)
        {
            if (!rateMask[i])
                continue;

            SparPodium::FighterRow& fighter = snap.fighters[i];
            fighter.ratingBefore = mmrBefore[i];
            fighter.ratingAfter = mmrAfter[i];
            fighter.ratingDelta = mmrAfter[i] - mmrBefore[i];
            fighter.ratingUpdated = true;

            // The podium stores FighterRow copies built before ratings apply.
            for (int p = 0; p < snap.podiumCount; ++p)
            {
                if (snap.podium[p].fighter.id != fighter.id)
                    continue;
                snap.podium[p].fighter.ratingBefore = fighter.ratingBefore;
                snap.podium[p].fighter.ratingAfter = fighter.ratingAfter;
                snap.podium[p].fighter.ratingDelta = fighter.ratingDelta;
                snap.podium[p].fighter.ratingUpdated = true;
                break;
            }
        }

        for (int i = 0; i < count; ++i)
        {
            if (!rateMask[i])
                continue;

            Record& rec = EnsureRecord(ids[i], names[i]);
            rec.mmr = mmrAfter[i];
            rec.wins += winsDelta[i];
            rec.losses += lossesDelta[i];
            rec.matches += 1;
            if (!names[i].empty())
                rec.name = names[i];
        }

        if (!SaveToFile(g_loadedFilePath))
            ErrorLog("Proving Grounds: rating persist failed after match");
        else
            DebugLog("Proving Grounds: ratings updated");
    }

    bool SetRating(Character* character, float rating)
    {
        if (!character || !character->isValid())
            return false;

        EnsureLoaded();
        if (!g_loaded)
            return false;

        const std::string id = CharacterKey(character);
        if (id.empty())
            return false;

        if (rating < 0.0f)
            rating = 0.0f;
        if (rating > 9999.0f)
            rating = 9999.0f;

        Record& record = EnsureRecord(id, character->getName());
        record.mmr = rating;
        if (!SaveToFile(g_loadedFilePath))
        {
            ErrorLog("Proving Grounds: debug rating persist failed");
            return false;
        }

        DebugLog("Proving Grounds: debug rating updated");
        return true;
    }

    void GetStandings(std::vector<Record>& out)
    {
        out.clear();
        EnsureLoaded();

        for (size_t i = 0; i < g_records.size(); ++i)
        {
            if (g_records[i].matches < 1)
                continue;

            Record copy = g_records[i];
            Character* live = FindRatedCharacter(copy.id);
            if (live && live->isValid())
            {
                const std::string liveName = live->getName();
                if (!liveName.empty())
                    copy.name = liveName;
            }
            out.push_back(copy);
        }

        std::sort(out.begin(), out.end(), StandingsLess);
    }

    float GetRating(Character* character)
    {
        if (!character)
            return MmrRating::kDefaultMmr;

        EnsureLoaded();
        Record* record = FindRecord(CharacterKey(character));
        return record ? record->mmr : MmrRating::kDefaultMmr;
    }

    Character* FindRatedCharacter(const std::string& id)
    {
        if (id.empty())
            return NULL;

        std::vector<Character*> squad;
        SquadUtil::CollectPlayerSquad(squad);
        for (size_t i = 0; i < squad.size(); ++i)
        {
            Character* c = squad[i];
            if (c && c->isValid() && CharacterKey(c) == id)
                return c;
        }

        const std::vector<Character*>& rosterPrisoners =
            PrisonerUtil::GetRosterPrisoners();
        for (size_t i = 0; i < rosterPrisoners.size(); ++i)
        {
            Character* c = rosterPrisoners[i];
            if (c && c->isValid() && CharacterKey(c) == id)
                return c;
        }

        const std::vector<Character*>& matchPrisoners =
            PrisonerUtil::GetMatchPrisoners();
        for (size_t i = 0; i < matchPrisoners.size(); ++i)
        {
            Character* c = matchPrisoners[i];
            if (c && c->isValid() && CharacterKey(c) == id)
                return c;
        }
        return NULL;
    }

    const std::string& GetActiveSaveKey()
    {
        EnsureLoaded();
        return g_loadedSaveKey;
    }

    bool HasActiveSave()
    {
        EnsureLoaded();
        return g_loaded;
    }
}
