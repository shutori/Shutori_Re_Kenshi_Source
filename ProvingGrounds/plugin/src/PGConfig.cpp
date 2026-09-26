#include "PGConfig.h"
#include "PGLog.h"
#include "ArenaRewards.h"
#include "../third_party/rapidjson/include/rapidjson/document.h"
#include "../third_party/rapidjson/include/rapidjson/error/en.h"
#include <Windows.h>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <float.h>
#include <map>
#include <string>

namespace PGConfig
{
    namespace
    {
        const char* const kFileName = "pg_config.json";
        const wchar_t* const kFileNameW = L"pg_config.json";
        // Resource guards, not balance limits: a price outside these ranges is a
        // stray digit rather than a deliberate setting.
        const int kMaxCost = 10000000;
        const int kMaxEditableCost = 1000000;
        const double kMaxScale = 100.0;
        // The shipped bookie-credit bound, and the odds ceiling the book is
        // clamped to in TownBettingPolicy::ReturnCats.
        const int kShippedBookieCreditCeiling = 50000;
        const double kBookOddsCeiling = 5.0;
        // Grade keys, indexed by ArenaRewards::Tier. The catalogue spells its
        // grades "Standard"/"High Grade"; these are the file's spelling.
        const char* const kTierNames[4] = { "standard", "high", "specialist", "masterwork" };
        const char* const kTierList = "standard, high, specialist or masterwork";

        // The shipped prices, each in one place. The globals below are built from
        // them, and ResetToShipped restores them.
        Recruitment ShippedRecruitment()
        {
            Recruitment shipped = { 25, 250, 5, 2.5 };
            return shipped;
        }
        Bookie ShippedBookie()
        {
            Bookie shipped = { 10, 10000 };
            return shipped;
        }
        Challenges ShippedChallenges()
        {
            Challenges shipped = { ChallengeNormal, 12, 500, 2.0, 24, 1.0, 1.0, 1.0 };
            return shipped;
        }
        int ShippedChallengeBase(int division)
        {
            return division == 0 ? 500 : division == 1 ? 1000 : 2500;
        }

        typedef std::map<std::string, int> Costs;

        // Namespace scope, so nothing is constructed on first use: Load runs on
        // the main thread before any hook is installed, and a worker thread may
        // read a value afterwards.
        Costs g_gear;
        Costs g_supplies;
        Costs g_licences;
        double g_multiplier = 1.0;
        Recruitment g_recruitment = ShippedRecruitment();
        Bookie g_bookie = ShippedBookie();
        Challenges g_challenges = ShippedChallenges();
        ArenaPerformance::Profile g_performanceProfile = ArenaPerformance::Normal;
        bool g_f8OpensDebugMenu = false;
        int g_challengeBase[3] =
        {
            ShippedChallengeBase(0), ShippedChallengeBase(1), ShippedChallengeBase(2)
        };
        bool g_loaded = false;
        int g_rejected = 0;
        int g_appliedGear = 0;
        int g_appliedSupplies = 0;
        int g_appliedLicences = 0;

        void Reject(const std::string& where, const std::string& reason)
        {
            ++g_rejected;
            PGLog::Error(std::string("Proving Grounds: ") + kFileName + " " + where +
                " ignored (" + reason + "); the shipped value stays in force");
        }

        int GradeIndex(const std::string& name)
        {
            for (int grade = 0; grade < 4; ++grade)
                if (name == kTierNames[grade])
                    return grade;
            return -1;
        }

        // 1 when the value is usable, -1 when it is not (already logged).
        int ReadWhole(const rapidjson::Value& value, const std::string& where,
            int minimum, int maximum, int& out)
        {
            if (!value.IsInt())
            {
                Reject(where, value.IsNumber() ?
                    "expected a whole number" : "expected a number");
                return -1;
            }
            const int number = value.GetInt();
            if (number < minimum || number > maximum)
            {
                char range[80];
                sprintf_s(range, "expected %d to %d", minimum, maximum);
                Reject(where, range);
                return -1;
            }
            out = number;
            return 1;
        }

        void OptionalWhole(const rapidjson::Value& object, const char* key,
            const std::string& path, int minimum, int maximum, int& target)
        {
            if (!object.HasMember(key))
                return;
            int value = 0;
            if (ReadWhole(object[key], path + "." + key, minimum, maximum, value) == 1)
                target = value;
        }

        void OptionalNumber(const rapidjson::Value& object, const char* key,
            const std::string& path, double minimum, double maximum, double& target)
        {
            if (!object.HasMember(key))
                return;
            const std::string where = path + "." + key;
            const rapidjson::Value& value = object[key];
            if (!value.IsNumber())
            {
                Reject(where, "expected a number");
                return;
            }
            const double number = value.GetDouble();
            if (!_finite(number) || number < minimum || number > maximum)
            {
                char range[96];
                sprintf_s(range, "expected %g to %g", minimum, maximum);
                Reject(where, range);
                return;
            }
            target = number;
        }

        void ReadGear(const rapidjson::Value& gear, const char* sectionName,
            const char* expectedPrefix)
        {
            for (rapidjson::Value::ConstMemberIterator piece = gear.MemberBegin();
                piece != gear.MemberEnd(); ++piece)
            {
                const std::string id = piece->name.GetString();
                const std::string itemPath = std::string(sectionName) + "." + id;
                if (expectedPrefix && id.find(expectedPrefix) != 0)
                {
                    Reject(itemPath, std::string("expected an item ID beginning with ") + expectedPrefix);
                    continue;
                }
                if (!piece->value.IsObject())
                {
                    Reject(itemPath, "expected an object of grade prices");
                    continue;
                }
                for (rapidjson::Value::ConstMemberIterator grade = piece->value.MemberBegin();
                    grade != piece->value.MemberEnd(); ++grade)
                {
                    const std::string name = grade->name.GetString();
                    const std::string where = itemPath + "." + name;
                    if (GradeIndex(name) < 0)
                    {
                        Reject(where, std::string("unknown grade; use ") + kTierList);
                        continue;
                    }
                    int cost = 0;
                    if (ReadWhole(grade->value, where, 0, kMaxCost, cost) != 1)
                        continue;
                    const std::string canonicalKey = std::string("gear.") + id + "." + name;
                    if (!g_gear.insert(std::make_pair(canonicalKey, cost)).second)
                        Reject(where, "duplicate entry");
                }
            }
        }

        // Flat "id": cost sections (supplies, licences).
        void ReadFlat(const rapidjson::Value& section, const std::string& prefix,
            Costs& target)
        {
            for (rapidjson::Value::ConstMemberIterator entry = section.MemberBegin();
                entry != section.MemberEnd(); ++entry)
            {
                const std::string where = prefix + "." + entry->name.GetString();
                int cost = 0;
                if (ReadWhole(entry->value, where, 0, kMaxCost, cost) != 1)
                    continue;
                if (!target.insert(std::make_pair(where, cost)).second)
                    Reject(where, "duplicate entry");
            }
        }

        void ReadRecruitment(const rapidjson::Value& section)
        {
            Recruitment candidate = g_recruitment;
            OptionalWhole(section, "minimum", "recruitment", 0, kMaxEditableCost, candidate.minimum);
            OptionalWhole(section, "maximum", "recruitment", 0, kMaxEditableCost, candidate.maximum);
            OptionalWhole(section, "step", "recruitment", 1, kMaxEditableCost, candidate.step);
            OptionalNumber(section, "combat_scale", "recruitment", 0.0, kMaxScale,
                candidate.combatScale);
            if (candidate.minimum > candidate.maximum)
            {
                Reject("recruitment", "minimum exceeds maximum; the shipped range is kept");
                return;
            }
            g_recruitment = candidate;
        }

        void ReadBookie(const rapidjson::Value& section)
        {
            Bookie candidate = g_bookie;
            OptionalWhole(section, "minimum", "bookie", 0, kMaxEditableCost, candidate.minimum);
            OptionalWhole(section, "maximum", "bookie", 0, kMaxEditableCost, candidate.maximum);
            // Wagers move in whole steps, so the reachable range is the step
            // multiples inside the configured one. Normalising here means the
            // stake validators never see a bound they would reject.
            candidate.minimum = ((candidate.minimum + kStakeStep - 1) / kStakeStep) * kStakeStep;
            candidate.maximum = (candidate.maximum / kStakeStep) * kStakeStep;
            if (candidate.minimum > candidate.maximum)
            {
                Reject("bookie", "minimum exceeds maximum; the shipped range is kept");
                return;
            }
            g_bookie = candidate;
        }

        void ReadChallenge(const rapidjson::Value& section)
        {
            const char* const names[3] = { "easy", "medium", "hard" };
            for (int division = 0; division < 3; ++division)
            {
                int base = g_challengeBase[division];
                OptionalWhole(section, names[division], "challenge_buy_in", 0, kMaxEditableCost, base);
                g_challengeBase[division] = base;
                // The quote scales its base by 0.5-2.0 and the wallet rejects a
                // buy-in outside 100-5000, so a base beyond this band prices
                // challenges nobody can accept.
                if (base < 200 || base > 2500)
                    PGLog::Error(std::string("Proving Grounds: ") + kFileName +
                        " challenge_buy_in." + names[division] + " is outside 200 to 2500;" +
                        " quotes built from it may be rejected as invalid");
            }
        }

        void ReadChallengeSettings(const rapidjson::Value& section)
        {
            Challenges candidate = g_challenges;
            if (section.HasMember("difficulty"))
            {
                const rapidjson::Value& value = section["difficulty"];
                if (!value.IsString()) Reject("challenges.difficulty", "expected easy, normal or hard");
                else if (strcmp(value.GetString(), "easy") == 0) candidate.difficulty = ChallengeEasy;
                else if (strcmp(value.GetString(), "normal") == 0) candidate.difficulty = ChallengeNormal;
                else if (strcmp(value.GetString(), "hard") == 0) candidate.difficulty = ChallengeHard;
                else Reject("challenges.difficulty", "expected easy, normal or hard");
            }
            OptionalWhole(section, "refresh_hours", "challenges", 1, 168, candidate.refreshHours);
            OptionalWhole(section, "refresh_base_cost_cats", "challenges", 1, 1000000, candidate.refreshBaseCostCats);
            OptionalNumber(section, "refresh_cost_multiplier", "challenges", 1.0, 10.0, candidate.refreshCostMultiplier);
            OptionalWhole(section, "cooldown_hours", "challenges", 0, 720, candidate.cooldownHours);
            if (section.HasMember("marks") && !section["marks"].IsObject())
                Reject("challenges.marks", "expected an object");
            else if (section.HasMember("marks"))
            {
                const rapidjson::Value& marks = section["marks"];
                OptionalNumber(marks, "win_multiplier", "challenges.marks", 0.0, kMaxScale, candidate.winMarksMultiplier);
                OptionalNumber(marks, "unique_bonus_multiplier", "challenges.marks", 0.0, kMaxScale, candidate.uniqueBonusMultiplier);
                OptionalNumber(marks, "skarn_bonus_multiplier", "challenges.marks", 0.0, kMaxScale, candidate.skarnBonusMultiplier);
            }
            g_challenges = candidate;
        }

        void ReadEditableOverlay(const rapidjson::Value& section)
        {
            if (section.HasMember("recruitment"))
            {
                if (section["recruitment"].IsObject()) ReadRecruitment(section["recruitment"]);
                else Reject("ui_settings.recruitment", "expected an object");
            }
            if (section.HasMember("bookie"))
            {
                if (section["bookie"].IsObject()) ReadBookie(section["bookie"]);
                else Reject("ui_settings.bookie", "expected an object");
            }
            if (section.HasMember("challenge_buy_in"))
            {
                if (section["challenge_buy_in"].IsObject()) ReadChallenge(section["challenge_buy_in"]);
                else Reject("ui_settings.challenge_buy_in", "expected an object");
            }
            if (section.HasMember("challenges"))
            {
                if (section["challenges"].IsObject()) ReadChallengeSettings(section["challenges"]);
                else Reject("ui_settings.challenges", "expected an object");
            }
            if (section.HasMember("performance_profile"))
            {
                const rapidjson::Value& value = section["performance_profile"];
                ArenaPerformance::Profile profile = g_performanceProfile;
                if (!value.IsString()) Reject("ui_settings.performance_profile", "expected high, normal or potato");
                else if (!ArenaPerformance::Parse(value.GetString(), profile))
                    Reject("ui_settings.performance_profile", "expected high, normal or potato");
                else g_performanceProfile = profile;
            }
            if (section.HasMember("f8_opens_debug_menu"))
            {
                const rapidjson::Value& value = section["f8_opens_debug_menu"];
                if (!value.IsBool()) Reject("ui_settings.f8_opens_debug_menu", "expected true or false");
                else g_f8OpensDebugMenu = value.GetBool();
            }
        }

        void ResetToShipped()
        {
            g_gear.clear();
            g_supplies.clear();
            g_licences.clear();
            g_multiplier = 1.0;
            g_recruitment = ShippedRecruitment();
            g_bookie = ShippedBookie();
            g_challenges = ShippedChallenges();
            g_performanceProfile = ArenaPerformance::Normal;
            g_f8OpensDebugMenu = false;
            for (int division = 0; division < 3; ++division)
                g_challengeBase[division] = ShippedChallengeBase(division);
        }

        bool Parse(const std::string& text)
        {
            rapidjson::Document document;
            // A hand-edited file may carry annotations the JSON spec has no room
            // for; the generated file uses neither, so a file that does is still
            // read rather than rejected on punctuation.
            // A file saved from an editor may carry a UTF-8 BOM, which the reader
            // would otherwise treat as the start of the document.
            const size_t start = text.size() >= 3 &&
                static_cast<unsigned char>(text[0]) == 0xEF &&
                static_cast<unsigned char>(text[1]) == 0xBB &&
                static_cast<unsigned char>(text[2]) == 0xBF ? 3u : 0u;
            if (document.Parse<rapidjson::kParseCommentsFlag |
                rapidjson::kParseTrailingCommasFlag>(text.c_str() + start).HasParseError())
            {
                char error[320];
                sprintf_s(error, "Proving Grounds: %s is not valid JSON at offset %u (%s); it is left untouched and the shipped prices stay in force",
                    kFileName,
                    static_cast<unsigned>(document.GetErrorOffset()),
                    rapidjson::GetParseError_En(document.GetParseError()));
                PGLog::Error(error);
                ResetToShipped();
                return false;
            }
            if (!document.IsObject())
            {
                Reject("$", "expected an object");
                ResetToShipped();
                return false;
            }
            // A file stamped by another schema may mean something else by these
            // names, so nothing is taken from it.
            if (document.HasMember("version"))
            {
                const rapidjson::Value& version = document["version"];
                if (!version.IsInt() || version.GetInt() != 1)
                {
                    Reject("version", "expected 1; no prices were loaded");
                    ResetToShipped();
                    return false;
                }
            }

            static const char* const kSections[] =
            {
                "armour", "weapons", "gear", "supplies", "licences", "recruitment", "bookie", "challenge_buy_in", "challenges", "ui_settings"
            };
            static const char* const kKnown[] =
            {
                "version", "marks_multiplier", "armour", "weapons", "gear", "supplies", "licences",
                "recruitment", "bookie", "challenge_buy_in", "challenges", "ui_settings"
            };
            for (rapidjson::Value::ConstMemberIterator member = document.MemberBegin();
                member != document.MemberEnd(); ++member)
            {
                const std::string key = member->name.GetString();
                bool known = false;
                for (size_t i = 0; i < sizeof(kKnown) / sizeof(kKnown[0]); ++i)
                    if (key == kKnown[i])
                        known = true;
                if (!known)
                    Reject(key, "unknown section");
            }
            for (size_t i = 0; i < sizeof(kSections) / sizeof(kSections[0]); ++i)
                if (document.HasMember(kSections[i]) && !document[kSections[i]].IsObject())
                    Reject(kSections[i], "expected an object");

            OptionalNumber(document, "marks_multiplier", "$", 0.0, kMaxScale, g_multiplier);

            // The split sections take precedence when a config contains both
            // formats. Legacy mixed gear remains readable for existing installs.
            if (document.HasMember("armour") && document["armour"].IsObject())
                ReadGear(document["armour"], "armour", "armour.");
            if (document.HasMember("weapons") && document["weapons"].IsObject())
                ReadGear(document["weapons"], "weapons", "weapon.");
            if (document.HasMember("gear") && document["gear"].IsObject())
                ReadGear(document["gear"], "gear", NULL);
            if (document.HasMember("supplies") && document["supplies"].IsObject())
                ReadFlat(document["supplies"], "supplies", g_supplies);
            if (document.HasMember("licences") && document["licences"].IsObject())
                ReadFlat(document["licences"], "licences", g_licences);
            if (document.HasMember("recruitment") && document["recruitment"].IsObject())
                ReadRecruitment(document["recruitment"]);
            if (document.HasMember("bookie") && document["bookie"].IsObject())
                ReadBookie(document["bookie"]);
            if (document.HasMember("challenge_buy_in") &&
                document["challenge_buy_in"].IsObject())
                ReadChallenge(document["challenge_buy_in"]);
            if (document.HasMember("challenges") && document["challenges"].IsObject())
                ReadChallengeSettings(document["challenges"]);
            if (document.HasMember("ui_settings") && document["ui_settings"].IsObject())
                ReadEditableOverlay(document["ui_settings"]);
            return true;
        }

        enum FileState { FileMissing, FileRead, FileUnreadable };

        FileState ReadText(const std::wstring& path, std::string& text)
        {
            FILE* file = NULL;
            if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || !file)
            {
                const DWORD attributes = GetFileAttributesW(path.c_str());
                if (attributes != INVALID_FILE_ATTRIBUTES)
                    return FileUnreadable;
                const DWORD error = GetLastError();
                return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ?
                    FileMissing : FileUnreadable;
            }
            bool ok = fseek(file, 0, SEEK_END) == 0;
            const long size = ok ? ftell(file) : -1;
            // The arena snapshot reader's resource guard, for the same reason: a
            // hand-edited file must not be allocated whole before it is bounded.
            if (size < 0 || static_cast<unsigned long>(size) > 1024u * 1024u)
                ok = false;
            if (ok)
                ok = fseek(file, 0, SEEK_SET) == 0;
            if (ok && size > 0)
            {
                text.resize(static_cast<size_t>(size));
                ok = fread(&text[0], 1, static_cast<size_t>(size), file) ==
                    static_cast<size_t>(size);
            }
            fclose(file);
            return ok ? FileRead : FileUnreadable;
        }

        void FormatNumber(char* out, size_t size, double value)
        {
            sprintf_s(out, size, "%g", value);
            // A whole number would print as "1", which reads as an integer in a
            // file where the user is about to type fractional values.
            if (strchr(out, '.') == NULL && strchr(out, 'e') == NULL)
                strcat_s(out, size, ".0");
        }

        void AppendGearSection(std::string& out, char* line,
            const char* sectionName, bool weapons)
        {
            out += "    \"";
            out += sectionName;
            out += "\": {\n";
            int itemCount = 0;
            for (int index = 0; index < static_cast<int>(ArenaRewards::PieceCount); ++index)
            {
                const ArenaRewards::Piece piece = static_cast<ArenaRewards::Piece>(index);
                if (ArenaRewards::IsWeapon(piece) == weapons && ArenaRewards::TiersFor(piece) > 0)
                    ++itemCount;
            }
            int emitted = 0;
            for (int index = 0; index < static_cast<int>(ArenaRewards::PieceCount); ++index)
            {
                const ArenaRewards::Piece piece = static_cast<ArenaRewards::Piece>(index);
                const int tiers = ArenaRewards::TiersFor(piece);
                if (tiers <= 0 || ArenaRewards::IsWeapon(piece) != weapons)
                    continue;
                ++emitted;
                const int first = static_cast<int>(ArenaRewards::FirstTier(piece));
                out += "        ";
                out += PGLog::Quote(ArenaRewards::StableId(piece));
                out += ": {\n";
                for (int offset = 0; offset < tiers; ++offset)
                {
                    const int tier = first + offset;
                    sprintf_s(line, 2048, "            \"%s\": %d%s\n",
                        kTierNames[tier],
                        ArenaRewards::Get(piece, static_cast<ArenaRewards::Tier>(tier)).cost,
                        offset + 1 < tiers ? "," : "");
                    out += line;
                }
                out += emitted < itemCount ? "        },\n" : "        }\n";
            }
            out += "    },\n";
        }

        // The shipped settings, as the file a user edits. Written only when no
        // file exists, so an edited file survives a rebuild. The parser accepts
        // comments to keep the generated settings self-documenting.
        bool WriteDefaults(const std::wstring& path)
        {
            std::string out;
            // The challenge settings block is now the longest formatted line (over 1 KiB).
            char line[2048];
            char number[32];

            out += "{\n    \"version\": 1,\n";
            out += "    // Prices and Marks costs\n";
            FormatNumber(number, sizeof(number), g_multiplier);
            sprintf_s(line, "    \"marks_multiplier\": %s,\n", number);
            out += line;

            AppendGearSection(out, line, "armour", false);
            AppendGearSection(out, line, "weapons", true);

            out += "    \"supplies\": {\n";
            for (int item = 0; item < static_cast<int>(ArenaRewards::GeneralItemCount); ++item)
            {
                const ArenaRewards::GeneralReward& reward = ArenaRewards::GetGeneral(
                    static_cast<ArenaRewards::GeneralItem>(item));
                sprintf_s(line, "        %s: %d%s\n", PGLog::Quote(reward.stableId).c_str(),
                    reward.cost,
                    item + 1 < static_cast<int>(ArenaRewards::GeneralItemCount) ? "," : "");
                out += line;
            }
            out += "    },\n";

            const ArenaRewards::ArmourSet kSets[3] =
            {
                ArenaRewards::ArmourSetT1, ArenaRewards::ArmourSetT2, ArenaRewards::ArmourSetT3
            };
            out += "    \"licences\": {\n";
            for (int set = 0; set < 3; ++set)
            {
                const ArenaRewards::Licence& licence = ArenaRewards::GetLicence(kSets[set]);
                sprintf_s(line, "        %s: %d,\n",
                    PGLog::Quote(licence.stableId).c_str(), licence.cost);
                out += line;
            }
            const ArenaRewards::Licence& weapons = ArenaRewards::GetWeaponLicence();
            sprintf_s(line, "        %s: %d\n", PGLog::Quote(weapons.stableId).c_str(),
                weapons.cost);
            out += line;
            out += "    },\n";

            FormatNumber(number, sizeof(number), g_recruitment.combatScale);
            sprintf_s(line, "    \"recruitment\": {\n"
                "        \"minimum\": %d,\n"
                "        \"maximum\": %d,\n"
                "        \"step\": %d,\n"
                "        \"combat_scale\": %s\n"
                "    },\n",
                g_recruitment.minimum, g_recruitment.maximum, g_recruitment.step, number);
            out += line;
            sprintf_s(line, "    \"bookie\": {\n"
                "        \"minimum\": %d,\n"
                "        \"maximum\": %d\n"
                "    },\n",
                g_bookie.minimum, g_bookie.maximum);
            out += line;
            sprintf_s(line, "    // Cats paid to book a challenge\n"
                "    \"challenge_buy_in\": {\n"
                "        \"easy\": %d,\n"
                "        \"medium\": %d,\n"
                "        \"hard\": %d\n"
                "    },\n"
                "    // Matchmaking and rewards for player challenges\n"
                "    \"challenges\": {\n"
                "        // easy = weaker opponents, normal = shipped balance, hard = stronger opponents\n"
                "        \"difficulty\": \"normal\",\n"
                "        // Challenge offer refresh interval in in-game hours; 12 keeps the shipped 06:00 / 18:00 schedule\n"
                "        \"refresh_hours\": 12,\n"
                "        // First paid refresh costs this many Cats (1-1,000,000)\n"
                "        \"refresh_base_cost_cats\": 500,\n"
                "        // Price multiplier per paid refresh (1.0-10.0); whole-Cat prices round to nearest; 2.0 doubles\n"
                "        \"refresh_cost_multiplier\": 2.0,\n"
                "        // In-game hours after a Skarn challenge attempt before another Skarn challenge; 0 disables the cooldown\n"
                "        \"cooldown_hours\": 24,\n"
                "        \"marks\": {\n"
                "            // Multipliers: 1.0 keeps the shipped reward\n"
                "            \"win_multiplier\": 1.0,\n"
                "            // Unique-fighter bonus: Ressa Vane 20, Bum-Per 40, Senn / Torka / Veyr 50 Marks\n"
                "            \"unique_bonus_multiplier\": 1.0,\n"
                "            // Skarn victory bonus: 100 Marks\n"
                "            \"skarn_bonus_multiplier\": 1.0\n"
                "        }\n"
                "    },\n",
                g_challengeBase[0], g_challengeBase[1], g_challengeBase[2]);
            out += line;
            out += "    // Arena performance and plugin UI\n"
                "    \"ui_settings\": {\n"
                "        \"performance_profile\": \"normal\",\n"
                "        // true lets F8 toggle the debug menu; false disables the F8 action\n"
                "        \"f8_opens_debug_menu\": false\n"
                "    }\n}\n";

            FILE* file = NULL;
            if (_wfopen_s(&file, path.c_str(), L"wb") != 0 || !file)
                return false;
            const bool written = fwrite(out.data(), 1, out.size(), file) == out.size();
            const bool closed = fclose(file) == 0;
            return written && closed;
        }

        // Every key the catalogue did not claim names something this build does
        // not sell: a mistyped stable ID, or a grade the piece does not have.
        void ReportLeftovers()
        {
            Costs* const sections[3] = { &g_gear, &g_supplies, &g_licences };
            for (int i = 0; i < 3; ++i)
                for (Costs::const_iterator entry = sections[i]->begin();
                    entry != sections[i]->end(); ++entry)
                    Reject(entry->first, "does not name an entry in the shipped catalogue");
        }

        // For the log only: the module directory may hold characters the active
        // code page cannot represent, and a mangled path in a debug line is worse
        // than none only if it is taken for the real one.
        std::string Narrow(const std::wstring& path)
        {
            if (path.empty())
                return std::string();
            const int size = WideCharToMultiByte(CP_ACP, 0, path.c_str(),
                static_cast<int>(path.size()), NULL, 0, NULL, NULL);
            if (size <= 0)
                return std::string();
            std::string narrow(static_cast<size_t>(size), '\0');
            WideCharToMultiByte(CP_ACP, 0, path.c_str(), static_cast<int>(path.size()),
                &narrow[0], size, NULL, NULL);
            return narrow;
        }

        bool SkipString(const std::string& text, size_t& pos)
        {
            if (pos >= text.size() || text[pos] != '"') return false;
            ++pos;
            while (pos < text.size())
            {
                if (text[pos] == '\\') { pos += 2; continue; }
                if (text[pos++] == '"') return true;
            }
            return false;
        }

        size_t SkipTrivia(const std::string& text, size_t pos)
        {
            while (pos < text.size())
            {
                if (text[pos] == ' ' || text[pos] == '\t' || text[pos] == 13 || text[pos] == '\n')
                { ++pos; continue; }
                if (pos + 1 < text.size() && text[pos] == '/' && text[pos + 1] == '/')
                {
                    pos += 2;
                    while (pos < text.size() && text[pos] != '\n' && text[pos] != 13) ++pos;
                    continue;
                }
                if (pos + 1 < text.size() && text[pos] == '/' && text[pos + 1] == '*')
                {
                    pos += 2;
                    while (pos + 1 < text.size() && !(text[pos] == '*' && text[pos + 1] == '/')) ++pos;
                    if (pos + 1 < text.size()) pos += 2;
                    continue;
                }
                break;
            }
            return pos;
        }

        bool ScanValue(const std::string& text, size_t start, size_t& end)
        {
            if (start >= text.size()) return false;
            if (text[start] == '"')
            {
                end = start;
                if (!SkipString(text, end)) return false;
                return true;
            }
            if (text[start] != '{' && text[start] != '[')
            {
                end = start;
                while (end < text.size() && text[end] != ',' && text[end] != '}' &&
                    text[end] != ']' && text[end] != ' ' && text[end] != '\t' &&
                    text[end] != 13 && text[end] != '\n') ++end;
                return end > start;
            }
            char stack[128];
            size_t depth = 0;
            for (size_t pos = start; pos < text.size(); ++pos)
            {
                if (pos + 1 < text.size() && text[pos] == '/' && text[pos + 1] == '/')
                {
                    pos += 2;
                    while (pos < text.size() && text[pos] != '\n' && text[pos] != 13) ++pos;
                    if (pos >= text.size()) return false;
                    continue;
                }
                if (pos + 1 < text.size() && text[pos] == '/' && text[pos + 1] == '*')
                {
                    pos += 2;
                    while (pos + 1 < text.size() && !(text[pos] == '*' && text[pos + 1] == '/')) ++pos;
                    if (pos + 1 >= text.size()) return false;
                    ++pos;
                    continue;
                }
                if (text[pos] == '"')
                {
                    size_t quote = pos;
                    if (!SkipString(text, quote)) return false;
                    pos = quote - 1;
                    continue;
                }
                if (text[pos] == '{' || text[pos] == '[')
                {
                    if (depth >= sizeof(stack)) return false;
                    stack[depth++] = text[pos] == '{' ? '}' : ']';
                }
                else if (text[pos] == '}' || text[pos] == ']')
                {
                    if (!depth || text[pos] != stack[depth - 1]) return false;
                    if (--depth == 0) { end = pos + 1; return true; }
                }
            }
            return false;
        }

        bool FindUiOverlay(const std::string& text, size_t& rootOpen,
            size_t& rootClose, size_t& valueStart, size_t& valueEnd, size_t& memberCount)
        {
            size_t pos = text.size() >= 3 &&
                static_cast<unsigned char>(text[0]) == 0xEF &&
                static_cast<unsigned char>(text[1]) == 0xBB &&
                static_cast<unsigned char>(text[2]) == 0xBF ? 3u : 0u;
            pos = SkipTrivia(text, pos);
            if (pos >= text.size() || text[pos] != '{') return false;
            rootOpen = pos++;
            memberCount = 0;
            valueStart = valueEnd = static_cast<size_t>(-1);
            while (pos < text.size())
            {
                pos = SkipTrivia(text, pos);
                if (pos < text.size() && text[pos] == '}') { rootClose = pos; return true; }
                if (pos >= text.size() || text[pos] != '"') return false;
                const size_t keyStart = pos;
                size_t keyEnd = keyStart;
                if (!SkipString(text, keyEnd)) return false;
                const std::string encodedKey = text.substr(keyStart, keyEnd - keyStart);
                rapidjson::Document keyDocument;
                if (keyDocument.Parse<rapidjson::kParseCommentsFlag |
                    rapidjson::kParseTrailingCommasFlag>(encodedKey.c_str()).HasParseError() ||
                    !keyDocument.IsString()) return false;
                const std::string key = keyDocument.GetString();
                pos = keyEnd;
                pos = SkipTrivia(text, pos);
                if (pos >= text.size() || text[pos++] != ':') return false;
                pos = SkipTrivia(text, pos);
                const size_t start = pos;
                size_t end = start;
                if (!ScanValue(text, start, end)) return false;
                if (key == "ui_settings")
                {
                    if (valueStart != static_cast<size_t>(-1)) return false;
                    valueStart = start;
                    valueEnd = end;
                }
                ++memberCount;
                pos = SkipTrivia(text, end);
                if (pos < text.size() && text[pos] == ',') { ++pos; continue; }
                if (pos < text.size() && text[pos] == '}') { rootClose = pos; return true; }
                return false;
            }
            return false;
        }

        bool FormatEditableOverlay(const EditableSettings& settings, std::string& out)
        {
            char number[32];
            char line[2048];
            FormatNumber(number, sizeof(number), settings.recruitment.combatScale);
            const char* difficulty = settings.challenges.difficulty == ChallengeEasy ? "easy" :
                settings.challenges.difficulty == ChallengeHard ? "hard" : "normal";
            char refreshScale[32], winMarks[32], uniqueMarks[32], skarnMarks[32];
            const char* performanceProfile = ArenaPerformance::PersistedName(settings.performanceProfile);
            FormatNumber(refreshScale, sizeof(refreshScale), settings.challenges.refreshCostMultiplier);
            FormatNumber(winMarks, sizeof(winMarks), settings.challenges.winMarksMultiplier);
            FormatNumber(uniqueMarks, sizeof(uniqueMarks), settings.challenges.uniqueBonusMultiplier);
            FormatNumber(skarnMarks, sizeof(skarnMarks), settings.challenges.skarnBonusMultiplier);
            sprintf_s(line, "{\n"
                "        \"recruitment\": {\"minimum\": %d, \"maximum\": %d, \"step\": %d, \"combat_scale\": %s},\n"
                "        \"bookie\": {\"minimum\": %d, \"maximum\": %d},\n"
                "        \"challenge_buy_in\": {\"easy\": %d, \"medium\": %d, \"hard\": %d},\n"
                "        \"challenges\": {\"difficulty\": \"%s\", \"refresh_hours\": %d, \"refresh_base_cost_cats\": %d, \"refresh_cost_multiplier\": %s, \"cooldown_hours\": %d, \"marks\": {\"win_multiplier\": %s, \"unique_bonus_multiplier\": %s, \"skarn_bonus_multiplier\": %s}},\n"
                "        \"performance_profile\": \"%s\",\n"
                "        // true lets F8 toggle the debug menu; false disables the F8 action\n"
                "        \"f8_opens_debug_menu\": %s\n"
                "    }",
                settings.recruitment.minimum, settings.recruitment.maximum, settings.recruitment.step, number,
                settings.bookie.minimum, settings.bookie.maximum,
                settings.challengeBase[0], settings.challengeBase[1], settings.challengeBase[2],
                difficulty, settings.challenges.refreshHours, settings.challenges.refreshBaseCostCats,
                refreshScale, settings.challenges.cooldownHours, winMarks, uniqueMarks, skarnMarks,
                performanceProfile, settings.f8OpensDebugMenu ? "true" : "false");
            out = line;
            return true;
        }
    }

    void Load()
    {
        if (g_loaded)
            return;
        g_loaded = true;

        if (PGLog::Directory().empty())
        {
            PGLog::Error(std::string("Proving Grounds: the plugin directory could not be resolved; ") + kFileName + " cannot be read and the shipped prices stay in force");
            return;
        }

        const std::wstring path = PGLog::Directory() + kFileNameW;
        std::string text;
        const FileState state = ReadText(path, text);
        const char* origin = "could not be read, shipped prices used";
        if (state == FileRead)
        {
            origin = "read";
            Parse(text);
        }
        else if (state == FileUnreadable)
            PGLog::Error(std::string("Proving Grounds: ") + kFileName + " exists but could not be read; it is left untouched and the shipped prices stay in force");
        else if (WriteDefaults(path))
        {
            origin = "written from the shipped defaults";
            PGLog::Debug(std::string("Proving Grounds: wrote ") + kFileName + " beside ProvingGrounds.dll from the shipped values; edit that file to change one");
        }
        else
        {
            origin = "absent and not writable, shipped prices used";
            PGLog::Error(std::string("Proving Grounds: no ") + kFileName + " and it could not be written; the shipped prices stay in force and there is no file to edit");
        }

        // Counted as the catalogue consumes them, so the summary reports what
        // took effect rather than what the file offered.
        ArenaRewards::ApplyPriceOverrides();
        if (state == FileRead)
            ReportLeftovers();

        char summary[768];
        const char* const difficultyName = g_challenges.difficulty == ChallengeEasy ? "easy" :
            g_challenges.difficulty == ChallengeHard ? "hard" : "normal";
        sprintf_s(summary,
            "Proving Grounds: %s %s (%s); applied catalogue=%d supplies=%d licences=%d marks_multiplier=%g; "
            "recruitment=%d/%d/%d combat_scale=%g; bookie=%d/%d; challenge_base=%d/%d/%d; "
            "challenge_difficulty=%s refresh_hours=%d refresh_base_cats=%d refresh_multiplier=%g "
            "cooldown_hours=%d marks=%g/%g/%g; performance=%s f8_debug=%s; rejected=%d",
            kFileName,
            origin,
            Narrow(path).c_str(),
            g_appliedGear, g_appliedSupplies, g_appliedLicences, g_multiplier,
            g_recruitment.minimum, g_recruitment.maximum, g_recruitment.step,
            g_recruitment.combatScale,
            g_bookie.minimum, g_bookie.maximum,
            g_challengeBase[0], g_challengeBase[1], g_challengeBase[2],
            difficultyName, g_challenges.refreshHours, g_challenges.refreshBaseCostCats,
            g_challenges.refreshCostMultiplier, g_challenges.cooldownHours, g_challenges.winMarksMultiplier,
            g_challenges.uniqueBonusMultiplier, g_challenges.skarnBonusMultiplier,
            ArenaPerformance::PersistedName(g_performanceProfile),
            g_f8OpensDebugMenu ? "enabled" : "disabled",
            g_rejected);
        PGLog::Debug(summary);
        if (g_rejected)
            PGLog::Error(std::string("Proving Grounds: ") + kFileName + " was not applied in full; every rejected entry is named above");
    }

    int GearCost(const char* pieceStableId, int tier, int defaultCost)
    {
        if (!pieceStableId || tier < 0 || tier > 3)
            return ScaleMarks(defaultCost);
        Costs::iterator entry = g_gear.find(
            std::string("gear.") + pieceStableId + "." + kTierNames[tier]);
        if (entry == g_gear.end())
            return ScaleMarks(defaultCost);
        const int cost = entry->second;
        g_gear.erase(entry);
        ++g_appliedGear;
        return ScaleMarks(cost);
    }

    int SupplyCost(const char* stableId, int defaultCost)
    {
        if (!stableId)
            return ScaleMarks(defaultCost);
        Costs::iterator entry = g_supplies.find(std::string("supplies.") + stableId);
        if (entry == g_supplies.end())
            return ScaleMarks(defaultCost);
        const int cost = entry->second;
        g_supplies.erase(entry);
        ++g_appliedSupplies;
        return ScaleMarks(cost);
    }

    int LicenceCost(const char* stableId, int defaultCost)
    {
        if (!stableId)
            return ScaleMarks(defaultCost);
        Costs::iterator entry = g_licences.find(std::string("licences.") + stableId);
        if (entry == g_licences.end())
            return ScaleMarks(defaultCost);
        const int cost = entry->second;
        g_licences.erase(entry);
        ++g_appliedLicences;
        return ScaleMarks(cost);
    }

    int ScaleMarks(int marks)
    {
        // Exactly the shipped price at the shipped multiplier: the default build
        // never takes a path that could re-round a value.
        if (g_multiplier == 1.0)
            return marks;
        const double scaled = static_cast<double>(marks) * g_multiplier;
        if (scaled >= static_cast<double>(INT_MAX))
            return INT_MAX;
        if (!(scaled > 0.0))
            return 0;
        return static_cast<int>(scaled + 0.5);
    }

    const Recruitment& RecruitmentValues()
    {
        return g_recruitment;
    }

    const Bookie& BookieValues()
    {
        return g_bookie;
    }

    const Challenges& ChallengeValues()
    {
        return g_challenges;
    }

    EditableSettings ShippedEditableSettings()
    {
        EditableSettings settings;
        settings.recruitment = ShippedRecruitment();
        settings.bookie = ShippedBookie();
        settings.challenges = ShippedChallenges();
        for (int i = 0; i < 3; ++i) settings.challengeBase[i] = ShippedChallengeBase(i);
        settings.performanceProfile = ArenaPerformance::Normal;
        settings.f8OpensDebugMenu = false;
        return settings;
    }

    ArenaPerformance::Profile PerformanceProfile()
    {
        return g_performanceProfile;
    }

    EditableSettings GetEditableSettings()
    {
        EditableSettings settings;
        settings.recruitment = g_recruitment;
        settings.bookie = g_bookie;
        settings.challenges = g_challenges;
        for (int i = 0; i < 3; ++i) settings.challengeBase[i] = g_challengeBase[i];
        settings.performanceProfile = g_performanceProfile;
        settings.f8OpensDebugMenu = g_f8OpensDebugMenu;
        return settings;
    }

    bool SaveEditableSettings(const EditableSettings& settings, std::string& error)
    {
        if (settings.recruitment.minimum < 0 || settings.recruitment.maximum > kMaxEditableCost ||
            settings.recruitment.minimum > settings.recruitment.maximum ||
            settings.recruitment.step < 1 || settings.recruitment.step > kMaxEditableCost ||
            !_finite(settings.recruitment.combatScale) || settings.recruitment.combatScale < 0.0 ||
            settings.recruitment.combatScale > kMaxScale)
        { error = "Recruitment settings are outside their supported ranges."; return false; }
        if (settings.bookie.minimum < 0 || settings.bookie.maximum > kMaxEditableCost ||
            settings.bookie.minimum > settings.bookie.maximum ||
            settings.bookie.minimum % kStakeStep || settings.bookie.maximum % kStakeStep)
        { error = "Bookie limits must be ordered multiples of 10 Cats."; return false; }
        if (settings.challenges.difficulty != ChallengeEasy &&
            settings.challenges.difficulty != ChallengeNormal &&
            settings.challenges.difficulty != ChallengeHard)
        { error = "Challenge difficulty is invalid."; return false; }
        if (!ArenaPerformance::IsValid(static_cast<int>(settings.performanceProfile)))
        { error = "Crowd performance profile is invalid."; return false; }
        for (int i = 0; i < 3; ++i)
            if (settings.challengeBase[i] < 0 || settings.challengeBase[i] > kMaxEditableCost)
            { error = "Challenge buy-in is outside its supported range."; return false; }
        if (settings.challenges.refreshHours < 1 || settings.challenges.refreshHours > 168 ||
            settings.challenges.refreshBaseCostCats < 1 || settings.challenges.refreshBaseCostCats > 1000000 ||
            !_finite(settings.challenges.refreshCostMultiplier) || settings.challenges.refreshCostMultiplier < 1.0 ||
            settings.challenges.refreshCostMultiplier > 10.0 || settings.challenges.cooldownHours < 0 ||
            settings.challenges.cooldownHours > 720 ||
            !_finite(settings.challenges.winMarksMultiplier) || settings.challenges.winMarksMultiplier < 0.0 ||
            settings.challenges.winMarksMultiplier > kMaxScale ||
            !_finite(settings.challenges.uniqueBonusMultiplier) || settings.challenges.uniqueBonusMultiplier < 0.0 ||
            settings.challenges.uniqueBonusMultiplier > kMaxScale ||
            !_finite(settings.challenges.skarnBonusMultiplier) || settings.challenges.skarnBonusMultiplier < 0.0 ||
            settings.challenges.skarnBonusMultiplier > kMaxScale)
        { error = "Challenge settings are outside their supported ranges."; return false; }

        if (PGLog::Directory().empty())
        { error = "The Proving Grounds mod folder could not be resolved."; return false; }
        const std::wstring path = PGLog::Directory() + kFileNameW;
        std::string text;
        if (ReadText(path, text) != FileRead)
        { error = "pg_config.json could not be read; it was not changed."; return false; }
        rapidjson::Document document;
        size_t parseStart = text.size() >= 3 &&
            static_cast<unsigned char>(text[0]) == 0xEF &&
            static_cast<unsigned char>(text[1]) == 0xBB &&
            static_cast<unsigned char>(text[2]) == 0xBF ? 3u : 0u;
        if (document.Parse<rapidjson::kParseCommentsFlag |
            rapidjson::kParseTrailingCommasFlag>(text.c_str() + parseStart).HasParseError() ||
            !document.IsObject())
        { error = "pg_config.json is invalid; it was left untouched."; return false; }

        size_t rootOpen = 0, rootClose = 0, valueStart = 0, valueEnd = 0, memberCount = 0;
        if (!FindUiOverlay(text, rootOpen, rootClose, valueStart, valueEnd, memberCount))
        { error = "pg_config.json could not be safely updated; it was left untouched."; return false; }
        std::string overlay;
        FormatEditableOverlay(settings, overlay);
        if (valueStart != static_cast<size_t>(-1))
            text.replace(valueStart, valueEnd - valueStart, overlay);
        else
        {
            std::string insertion = "\n    \"ui_settings\": " + overlay;
            if (memberCount) insertion += ",";
            insertion += "\n    ";
            text.insert(rootOpen + 1, insertion);
        }
        (void)rootClose;
        rapidjson::Document savedDocument;
        if (savedDocument.Parse<rapidjson::kParseCommentsFlag |
            rapidjson::kParseTrailingCommasFlag>(text.c_str() + parseStart).HasParseError() ||
            !savedDocument.IsObject() || !savedDocument.HasMember("ui_settings") ||
            !savedDocument["ui_settings"].IsObject())
        { error = "The updated settings did not validate; pg_config.json was left untouched."; return false; }

        const std::wstring temporaryPath = path + L".tmp";
        FILE* file = NULL;
        if (_wfopen_s(&file, temporaryPath.c_str(), L"wb") != 0 || !file)
        { error = "Could not create a temporary config file."; return false; }
        const bool written = fwrite(text.data(), 1, text.size(), file) == text.size();
        const bool closed = fclose(file) == 0;
        if (!written || !closed || !MoveFileExW(temporaryPath.c_str(), path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            DeleteFileW(temporaryPath.c_str());
            error = "Could not replace pg_config.json; the previous file was kept.";
            return false;
        }
        g_performanceProfile = settings.performanceProfile;
        g_f8OpensDebugMenu = settings.f8OpensDebugMenu;
        PGLog::Debug("Proving Grounds: saved in-game settings overlay to pg_config.json; crowd performance and F8 apply immediately");
        error.clear();
        return true;
    }

    bool F8OpensDebugMenu()
    {
        return g_f8OpensDebugMenu;
    }

    int ChallengeBase(int division)
    {
        if (division < 0 || division > 2)
            division = 0;
        return g_challengeBase[division];
    }

    int ClampStake(int stake)
    {
        if (stake < g_bookie.minimum)
            return g_bookie.minimum;
        if (stake > g_bookie.maximum)
            return g_bookie.maximum;
        return stake;
    }

    int BookieCreditCeiling()
    {
        // Betting is blocked while a credit is pending, so the largest credit a
        // save can hold is one winning payout at the book's odds ceiling.
        const double ceiling = static_cast<double>(g_bookie.maximum) * kBookOddsCeiling;
        return ceiling > kShippedBookieCreditCeiling ?
            static_cast<int>(ceiling + 0.5) : kShippedBookieCreditCeiling;
    }
}
