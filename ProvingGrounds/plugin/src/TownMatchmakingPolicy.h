#pragma once
#include "BalanceTuning.h"
#include "TownFighterCatalog.h"
#include "TownDiagnosticData.h"
#include "PGConfig.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace TownMatchmakingPolicy {
enum Division { Easy, Medium, Hard };
// id is a unique live persistent identity; role is the exact FCS character ID.
struct Fighter {
    std::string id, role;
    double ability, mmr;
    Fighter() : ability(0), mmr(100) {}
};
typedef std::vector<std::vector<std::string> > History;
struct Match {
    enum Failure { NoFailure, PlayersTooWeak };
    bool valid;
    std::vector<std::string> a, b;
    double scoreA, scoreB;
    bool diagnostic, diagnosticUnderdog, bestAvailable;
    Failure failure;
    int diagnosticModel, requestedCoverage, actualCoverage, relaxation;
    double modelScoreA[TownDiagnosticData::ModelCount];
    double modelScoreB[TownDiagnosticData::ModelCount];
    double modelProbabilityA[TownDiagnosticData::ModelCount];
    Match() : valid(false), scoreA(0), scoreB(0), diagnostic(false), diagnosticUnderdog(false), bestAvailable(false), failure(NoFailure),
        diagnosticModel(TownDiagnosticData::StatsOnly), requestedCoverage(TownDiagnosticData::Duel),
        actualCoverage(TownDiagnosticData::Duel), relaxation(0) {
        for (int i=0;i<TownDiagnosticData::ModelCount;++i) modelScoreA[i]=modelScoreB[i]=modelProbabilityA[i]=0;
    }
};
struct DiagnosticRequest {
    bool active, underdog, balanceStyleTier;
    bool enabledCoverage[TownDiagnosticData::CoverageCount];
    int model, coverage, desiredType, desiredTier;
    double influence, reducedInfluence;
    unsigned seed;
    DiagnosticRequest() : active(false), underdog(false), balanceStyleTier(true),
        model(TownDiagnosticData::StatsOnly), coverage(TownDiagnosticData::Duel),
        desiredType(0), desiredTier(0), influence(0), reducedInfluence(.25), seed(0) {
        for(int i=0;i<TownDiagnosticData::CoverageCount;++i)enabledCoverage[i]=true;
    }
};
inline bool Finite(double value) {
    return value == value && value <= (std::numeric_limits<double>::max)() &&
        value >= -(std::numeric_limits<double>::max)();
}
inline double Ability(double attack, double defence, double strength, double toughness, double dexterity) {
    if (!Finite(attack) || !Finite(defence) || !Finite(strength) || !Finite(toughness) || !Finite(dexterity)) return -1;
    const double score = 10 + .30 * attack + .30 * defence + .15 * strength + .15 * toughness + .10 * dexterity;
    return Finite(score) && score > 0 ? score : -1;
}
inline double RatingFactor(double mmr, double influence) {
    if (!Finite(mmr) || mmr < 0) return -1;
    if (!Finite(influence) || influence < 0 || influence > 1) return -1;
    const double factor = std::pow(10.0, influence * (mmr - 100.0) / 400.0);
    return Finite(factor) && factor > 0 ? factor : -1;
}
inline double RatingFactor(double mmr) { return RatingFactor(mmr, 1.0); }
// Live values from BalanceTuning, so a controlled diagnostic run can move them
// without a rebuild. Named rather than inline literals for the same reason.
// Shipped: exponent 2.5, full-model influence 1.0.
inline double AbilityExponent() { return BalanceTuning::Get().abilityExponent; }
inline double RatingInfluence() { return BalanceTuning::Get().ratingInfluence; }
inline double FighterPower(double ability, double mmr, double influence) {
    if (!Finite(ability) || ability <= 0) return -1;
    const double rating = RatingFactor(mmr, influence);
    if (rating <= 0) return -1;
    // Stats, equipment bonuses, damage and survivability compound in Kenshi;
    // MMR remains a separate, modest correction around the neutral 100 baseline.
    const double power = std::pow(ability, AbilityExponent()) * rating;
    return Finite(power) && power > 0 ? power : -1;
}
inline double FighterPower(double ability, double mmr) { return FighterPower(ability, mmr, RatingInfluence()); }
inline double TeamScore(const double* abilities, const double* ratings, int count, double influence) {
    if (!abilities || !ratings || count <= 0) return -1;
    double total = 0;
    for (int i = 0; i < count; ++i) {
        const double power = FighterPower(abilities[i], ratings[i], influence);
        if (power <= 0) return -1;
        total += power;
    }
    return Finite(total) && total > 0 ? total : -1;
}
inline double TeamScore(const double* abilities, const double* ratings, int count) {
    return TeamScore(abilities, ratings, count, RatingInfluence());
}
inline double TeamScore(const double* abilities, int count) {
    if (!abilities || count <= 0 || count > 4) return -1;
    double ratings[4] = {100, 100, 100, 100};
    return TeamScore(abilities, ratings, count);
}
inline void RecordHistory(History& history, const std::vector<std::string>& ids) {
    if (ids.empty()) return;
    std::vector<std::string> sorted(ids);
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    history.push_back(sorted);
    if (history.size() > 5) history.erase(history.begin(), history.end() - 5);
}

namespace Detail {
inline unsigned Next(unsigned& seed) {
    if (!seed) seed = 0x9e3779b9u;
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    return seed;
}
inline unsigned Hash(unsigned seed, const std::string& value) {
    unsigned hash = seed ^ 2166136261u;
    for (size_t i = 0; i < value.size(); ++i) { hash ^= static_cast<unsigned char>(value[i]); hash *= 16777619u; }
    return hash;
}
inline bool Named(const std::string& role) {
    return role == "616-Proving Grounds.mod" || role == "619-Proving Grounds.mod" ||
        role == "623-Proving Grounds.mod" || role == "169-Proving Grounds.mod" ||
        role == "168-Proving Grounds.mod" || role == "167-Proving Grounds.mod";
}
inline int NamedDivision(const std::string& role) {
    if (role == "616-Proving Grounds.mod") return Easy;
    if (role == "619-Proving Grounds.mod") return Medium;
    if (role == "623-Proving Grounds.mod" || role == "169-Proving Grounds.mod" ||
        role == "168-Proving Grounds.mod" || role == "167-Proving Grounds.mod") return Hard;
    return -1;
}
struct ById { bool operator()(const Fighter& a, const Fighter& b) const { return a.id < b.id; } };
struct ByPower {
    bool operator()(const Fighter& a, const Fighter& b) const {
        const double left = FighterPower(a.ability, a.mmr);
        const double right = FighterPower(b.ability, b.mmr);
        return left == right ? a.id < b.id : left < right;
    }
};
inline std::vector<Fighter> Roster(const std::vector<Fighter>& input, unsigned seed,
    const std::set<std::string>& excluded, const std::string& anchor) {
    std::map<std::string, int> counts;
    for (size_t i = 0; i < input.size(); ++i) ++counts[input[i].id];
    std::vector<Fighter> regular, named;
    for (size_t i = 0; i < input.size(); ++i) {
        const Fighter& f = input[i];
        if (f.id.empty() || counts[f.id] != 1 || excluded.count(f.id) || !Finite(f.ability) || f.ability <= 0) continue;
        if (Named(f.role)) {
            if (!anchor.empty() && f.role == anchor) named.push_back(f);
        }
        else if (TownFighterCatalog::Find(f.role.c_str())) regular.push_back(f);
    }
    std::sort(regular.begin(), regular.end(), ByPower());
    std::sort(named.begin(), named.end(), ById());
    // Keep the bounded search representative across the entire strength range.
    // An identity-contiguous sample could omit every weak opponent and falsely
    // report PlayersTooWeak even when an eligible fighter existed outside it.
    if (regular.size() > 64) {
        const unsigned rotation = Next(seed);
        std::vector<Fighter> sample;
        for (size_t i = 0; i < 64; ++i) {
            const size_t begin = i * regular.size() / 64;
            const size_t end = (i + 1) * regular.size() / 64;
            const size_t bucketSize = end - begin;
            size_t offset = (rotation + static_cast<unsigned>(i * 2654435761u)) % bucketSize;
            if (i == 0) offset = 0;
            if (i == 63) offset = bucketSize - 1;
            sample.push_back(regular[begin + offset]);
        }
        regular.swap(sample);
    }
    // A named encounter retains its anchor even with a full regular roster.
    if (!named.empty()) regular.push_back(named[Next(seed) % named.size()]);
    std::sort(regular.begin(), regular.end(), ById());
    return regular;
}
inline std::string Key(const std::vector<std::string>& ids) {
    std::string key;
    for (size_t i = 0; i < ids.size(); ++i) { key += ids[i]; key += '\0'; }
    return key;
}
inline double Novelty(const std::vector<std::string>& ids, const History& history,
    const std::map<std::string, std::string>& types) {
    double penalty = 0;
    const size_t first = history.size() > 5 ? history.size() - 5 : 0;
    for (size_t h = first; h < history.size(); ++h) {
        std::vector<std::string> old(history[h]); std::sort(old.begin(), old.end());
        const double weight = 1.0 + .1 * (h - first);
        if (old == ids) penalty += 100 * weight;
        std::set<std::string> previousTypes;
        for (size_t j = 0; j < old.size(); ++j) {
            std::map<std::string, std::string>::const_iterator type = types.find(old[j]);
            if (type != types.end()) previousTypes.insert(type->second);
        }
        for (size_t j = 0; j < ids.size(); ++j) {
            if (std::binary_search(old.begin(), old.end(), ids[j])) penalty += weight / ids.size();
            std::map<std::string, std::string>::const_iterator type = types.find(ids[j]);
            if (type != types.end() && previousTypes.count(type->second)) penalty += .05 * weight / ids.size();
        }
    }
    return penalty;
}
inline std::map<std::string, std::string> Types(const std::vector<Fighter>& roster) {
    std::map<std::string, std::string> types;
    for (size_t i = 0; i < roster.size(); ++i) {
        const TownFighterCatalog::Entry* entry = TownFighterCatalog::Find(roster[i].role.c_str());
        if (entry) types[roster[i].id] = entry->type;
    }
    return types;
}
struct Ranked {
    Match match;
    int fit;
    double novelty;
    unsigned tie;
    Ranked() : fit(0), novelty(0), tie(0) {}
    void Consider(const Match& candidate, double distance, double variety, unsigned tieBreak) {
        // Within a 2.5 percentage-point fit bucket, variety wins over tiny score differences.
        const int bucket = static_cast<int>(distance / .025);
        if (!match.valid || bucket < fit || (bucket == fit &&
            (variety < novelty || (variety == novelty && tieBreak < tie)))) {
            match = candidate; fit = bucket; novelty = variety; tie = tieBreak;
        }
    }
};
inline double Score(const std::vector<Fighter>& roster, const std::vector<int>& indices,
    std::vector<std::string>& ids, double influence = RatingInfluence()) {
    double values[4], ratings[4];
    for (size_t i = 0; i < indices.size(); ++i) {
        values[i] = roster[indices[i]].ability;
        ratings[i] = roster[indices[i]].mmr;
        ids.push_back(roster[indices[i]].id);
    }
    std::sort(ids.begin(), ids.end());
    return TeamScore(values, ratings, static_cast<int>(indices.size()), influence);
}
}

inline Match SelectPlayer(const std::vector<Fighter>& players, const std::vector<Fighter>& npcs,
    Division division, unsigned seed, const History& history, int maxEnemies = 4,
    int maxTotal = 6, const std::string& requiredRole = std::string(), int rarity = -1,
    bool forceSoloNamed = false, bool teams1v1 = false,
    PGConfig::ChallengeDifficulty challengeDifficulty = PGConfig::ChallengeNormal) {
    Match empty;
    if (players.empty() || players.size() > 3 || division < Easy || division > Hard ||
        rarity < -1 || rarity > 3 || maxEnemies < 1 || maxTotal < 2) return empty;
    if (!requiredRole.empty() && (!Detail::Named(requiredRole) || Detail::NamedDivision(requiredRole) != division)) return empty;
    std::set<std::string> playerIds;
    double values[3], ratings[3], strongest = 0;
    for (size_t i = 0; i < players.size(); ++i) {
        if (players[i].id.empty() || !playerIds.insert(players[i].id).second) return empty;
        values[i] = players[i].ability;
        ratings[i] = players[i].mmr;
        strongest = (std::max)(strongest, FighterPower(values[i], ratings[i]));
    }
    const double playerScore = TeamScore(values, ratings, static_cast<int>(players.size()));
    if (playerScore <= 0) return empty;
    const int limit = (std::min)(4, (std::min)(maxEnemies, maxTotal - static_cast<int>(players.size())));
    if (limit < 1) return empty;
    const std::vector<Fighter> roster = Detail::Roster(npcs, seed, playerIds, requiredRole);
    const std::map<std::string, std::string> types = Detail::Types(roster);
    static const double low[] = { .55, .85, 1.15 }, high[] = { .80, 1.10, 1.45 }, target[] = { .70, 1.00, 1.30 };
    // Challenge rarity describes actual relative team strength. Direct bookings
    // omit rarity and retain the original narrow division rules above.
    static const double rarityLow[3][4] = {
        {.25, .45, .70, 1.05}, {.50, .75, 1.00, 1.35}, {.85, 1.10, 1.40, 1.80}
    };
    static const double rarityHigh[3][4] = {
        {.45, .70, 1.05, 1.45}, {.75, 1.00, 1.35, 1.80}, {1.10, 1.40, 1.80, 2.30}
    };
    const double playerDifficulty = .80;
    const double selectionScale = challengeDifficulty == PGConfig::ChallengeEasy ? .85 :
        challengeDifficulty == PGConfig::ChallengeHard ? 1.15 : 1.0;
    const double minimum = (rarity < 0 ? low[division] : rarityLow[division][rarity]) * playerDifficulty * selectionScale;
    const double maximum = (rarity < 0 ? high[division] : rarityHigh[division][rarity]) * playerDifficulty * selectionScale;
    // A solo named challenge is the advertised duel. Do not add supporting
    // fighters merely because the model undervalues the unique's gear or combat
    // advantages; only reject the duel when the unique exceeds the safety cap.
    if (forceSoloNamed && !requiredRole.empty() && players.size() == 1)
    {
        for (size_t i = 0; i < roster.size(); ++i)
        {
            if (roster[i].role != requiredRole) continue;
            std::vector<int> anchor(1, static_cast<int>(i));
            Match duel;
            duel.scoreA = playerScore;
            duel.scoreB = Detail::Score(roster, anchor, duel.b);
            if (duel.scoreB <= 0) return empty;
            duel.valid = true;
            duel.a.assign(playerIds.begin(), playerIds.end());
            return duel;
        }
        return empty;
    }
    // If even the weakest eligible singleton is above the band, adding more
    // opponents can only worsen the mismatch. Stop before exploring teams.
    double weakestSingle = (std::numeric_limits<double>::max)();
    bool sawSingleton = false;
    for (size_t i = 0; i < roster.size(); ++i) {
        if (!requiredRole.empty() && roster[i].role != requiredRole) continue;
        const double score = FighterPower(roster[i].ability, roster[i].mmr);
        if (score <= 0) continue;
        sawSingleton = true;
        weakestSingle = (std::min)(weakestSingle, score / playerScore);
    }
    if (sawSingleton && weakestSingle > maximum + 1e-12) {
        empty.failure = Match::PlayersTooWeak;
        return empty;
    }
    unsigned varietySeed = seed ^ (0x9e3779b9u * static_cast<unsigned>(1 + division * 4 + rarity));
    const double desired = rarity < 0 ? target[division] * playerDifficulty * selectionScale :
        minimum + (maximum - minimum) * (Detail::Next(varietySeed) % 10001) / 10000.0;
    Detail::Ranked best;
    Detail::Ranked byCount[4];
    Detail::Ranked favorableByCount[2];
    Match fallback;
    double fallbackRatio = 0.0;
    double fallbackNovelty = 0.0;
    unsigned fallbackTie = 0;
    const int size = static_cast<int>(roster.size());
    // Sentinel -1 visits singleton/pair/triple/quad subsets exactly once.
    for (int i = 0; i < size; ++i) for (int j = -1; j < (limit >= 2 ? size : 0); ++j) {
        if (j >= 0 && j <= i) continue;
        for (int k = -1; k < (limit >= 3 && j >= 0 ? size : 0); ++k) {
            if (k >= 0 && k <= j) continue;
            for (int l = -1; l < (limit >= 4 && k >= 0 ? size : 0); ++l) {
                if (l >= 0 && l <= k) continue;
                std::vector<int> indices; indices.push_back(i);
                if (j >= 0) indices.push_back(j); if (k >= 0) indices.push_back(k); if (l >= 0) indices.push_back(l);
                if (requiredRole.empty() && indices.size() == 1 && rarity >= 0) {
                    const TownFighterCatalog::Entry* entry = TownFighterCatalog::Find(roster[i].role.c_str());
                    const int tierCeiling = division == Easy ? 0 :
                        division == Medium ? (rarity >= 2 ? 1 : 0) :
                        rarity == 3 ? 2 : rarity >= 1 ? 1 : 0;
                    if (!entry || entry->tier > tierCeiling) continue;
                }
                bool anchor = requiredRole.empty(), overwhelming = false;
                for (size_t n = 0; n < indices.size(); ++n) {
                    const Fighter& f = roster[indices[n]];
                    anchor = anchor || f.role == requiredRole;
                    overwhelming = overwhelming || FighterPower(f.ability, f.mmr) > strongest;
                }
                // Challenge bands already account for both teams and headcount;
                // one stronger NPC can still be easy for two or three players.
                if (!anchor || (division == Easy && rarity < 0 && overwhelming)) continue;
                Match candidate;
                candidate.scoreA = playerScore;
                candidate.scoreB = Detail::Score(roster, indices, candidate.b);
                if (candidate.scoreB <= 0) continue;
                const double rawRatio = candidate.scoreB / playerScore;
                const double ratio = rawRatio /
                    (teams1v1 && indices.size() > 1 ? 1.15 : 1.0);
                if (players.size() == 1 && indices.size() <= 2 &&
                    ratio < minimum - 1e-12 && ratio >= minimum * .80 - 1e-12)
                {
                    candidate.valid = true;
                    candidate.bestAvailable = true;
                    candidate.a.assign(playerIds.begin(), playerIds.end());
                    favorableByCount[indices.size() - 1].Consider(candidate,
                        minimum - ratio, Detail::Novelty(candidate.b, history, types),
                        Detail::Hash(seed, Detail::Key(candidate.b)));
                }
                if (rarity >= 2 && ratio < minimum - 1e-12)
                {
                    const bool stronger = !fallback.valid || ratio > fallbackRatio + 1e-12;
                    const bool tied = fallback.valid && std::fabs(ratio - fallbackRatio) <= 1e-12;
                    if (stronger || tied)
                    {
                        const double novelty = Detail::Novelty(candidate.b, history, types);
                        const unsigned tie = Detail::Hash(seed, Detail::Key(candidate.b));
                        if (stronger || novelty < fallbackNovelty ||
                            (novelty == fallbackNovelty && tie < fallbackTie))
                        {
                            candidate.valid = true;
                            candidate.bestAvailable = true;
                            candidate.a.assign(playerIds.begin(), playerIds.end());
                            fallback = candidate;
                            fallbackRatio = ratio;
                            fallbackNovelty = novelty;
                            fallbackTie = tie;
                        }
                    }
                }
                if (ratio < minimum - 1e-12 || ratio > maximum + 1e-12) continue;
                candidate.valid = true; candidate.a.assign(playerIds.begin(), playerIds.end());
                // Named Legendary duels are definitive: once the unique alone
                // fits the band, no supporting lineup can supersede it.
                if (!requiredRole.empty() && players.size() == 1 && indices.size() == 1) return candidate;
                Detail::Ranked& ranked = rarity < 0 ? best : byCount[indices.size() - 1];
                ranked.Consider(candidate, std::fabs(ratio - desired), Detail::Novelty(candidate.b, history, types), Detail::Hash(seed, Detail::Key(candidate.b)));
            }
        }
    }
    if (rarity < 0) return best.match;
    for (int i = 0; i < 2; ++i)
        if (!byCount[i].match.valid && favorableByCount[i].match.valid)
            byCount[i] = favorableByCount[i];
    // Solo challenges favor smaller opposing lineups while retaining every
    // feasible format. Slightly favorable 1v1/1v2 candidates may fill a gap.
    int available[4], count = 0;
    if (players.size() == 1) {
        static const int weights[] = {50, 30, 15, 5};
        int total = 0;
        for (int i = 0; i < 4; ++i) if (byCount[i].match.valid) total += weights[i];
        if (total) {
            int roll = static_cast<int>(Detail::Next(varietySeed) % total);
            for (int i = 0; i < 4; ++i) if (byCount[i].match.valid) {
                if (roll < weights[i]) return byCount[i].match;
                roll -= weights[i];
            }
        }
    }
    for (int i = 0; i < 4; ++i) if (byCount[i].match.valid) available[count++] = i;
    return count ? byCount[available[Detail::Next(varietySeed) % count]].match :
        fallback.valid ? fallback : empty;
}

namespace Detail {
// The ambient band boundaries used to live here as kCloseRatioCap (1.15) and
// kParityRatioCap (1.30). They are now BalanceTuning::ambientCloseCap /
// ambientRatioCap, so the band can be A/B'd without a rebuild; the shipped
// 1.15 / 0.15 / 1.30 / 0.15 set is recorded on the lever itself. A candidate's
// category must be read against the LIVE values, never against a constant, or a
// band would be classified against one shape and ranked against another.
//
// Ratio cap while the showcase band is enabled. ratio 1.80 -> the favourite is
// priced 0.236 (fair 4.24x), so the widest possible edge goes from 12.8 pp to
// 26.4 pp. NOTE: the TownBettingPolicy clamps at 1.10/5.00 are unreachable under
// the shipped square pricing, since the 5.00 clamp needs ratio >= 2.00 (fair
// 5.00x exactly). Once BalanceTuning::pricingKnee is set they become reachable
// in principle -- with the fitted knee 1.10 / exponent 6.65 the favourite's
// price hits the 1.10 floor and the underdog's the 5.00 ceiling near ratio 1.29
// -- but the narrowed ambient band caps an ambient bout at ratio 1.15, where the
// fit prices 1.26 / 3.18, so both clamps are dead again for ambient play. They
// return only if the band is widened or showcasePercent is turned on; re-check
// them whenever either moves, since both clamps leave the book positive.
static const double kShowcaseRatioHigh = 1.80;
inline void AmbientCandidate(const std::vector<Fighter>& roster, const std::vector<int>& a,
    const std::vector<int>& b, const History& history, const std::map<std::string, std::string>& types,
    double closeTarget, double underdogTarget, double showcaseTarget,
    double closeCap, double ratioCap,
    int attendance, unsigned seed, std::set<std::string>& seen, Ranked* best) {
    Match candidate;
    candidate.scoreA = Score(roster, a, candidate.a); candidate.scoreB = Score(roster, b, candidate.b);
    if (candidate.scoreA <= 0 || candidate.scoreB <= 0) return;
    const double ratio = (std::max)(candidate.scoreA, candidate.scoreB) / (std::min)(candidate.scoreA, candidate.scoreB);
    if (ratio > ratioCap + 1e-12) return;
    if (candidate.b < candidate.a) { candidate.a.swap(candidate.b); std::swap(candidate.scoreA, candidate.scoreB); }
    std::string key = Key(candidate.a); key += '\1'; key += Key(candidate.b);
    if (!seen.insert(key).second) return;
    std::vector<std::string> members(candidate.a); members.insert(members.end(), candidate.b.begin(), candidate.b.end());
    std::sort(members.begin(), members.end());
    candidate.valid = true;
    // 0 close (<= closeCap), 1 underdog ((closeCap, ratioCap]), 2 showcase
    // (> ratioCap, reachable only while showcasePercent > 0). closeCap and
    // ratioCap are the live BalanceTuning values, so both the boundary and the
    // target below come from the same band shape.
    const int category = ratio <= closeCap + 1e-12 ? 0 :
        ratio <= ratioCap + 1e-12 ? 1 : 2;
    const double variety = Novelty(members, history, types) + .01 * std::abs(static_cast<int>(members.size()) - attendance);
    const double target = category == 2 ? showcaseTarget : category == 1 ? underdogTarget : closeTarget;
    best[category].Consider(candidate, std::fabs(ratio - target), variety, Hash(seed, key));
}
}
inline Match SelectAmbient(const std::vector<Fighter>& npcs, unsigned& seed,
    const History& history, int maxTotal = 8) {
    // Exactly one saved-stream step per attempt; search uses a local stream so
    // capacity and roster size cannot change the next attempt's random state.
    const unsigned attemptSeed = Detail::Next(seed);
    unsigned random = attemptSeed;
    const int capacity = (std::min)(8, maxTotal);
    Match empty;
    if (capacity < 2) return empty;
    const std::set<std::string> excluded;
    const std::vector<Fighter> roster = Detail::Roster(npcs, random, excluded, std::string());
    const int size = static_cast<int>(roster.size());
    if (size < 2) return empty;
    // Exactly one draw here, as before. With the showcase band disabled the
    // classification is identical to the shipped "% 5 == 0 -> underdog" and the
    // saved stream is untouched, so shipped matchups are bit-for-bit unchanged.
    const unsigned categoryRoll = Detail::Next(random);
    // One read of the live lever set: the band shape must not change between the
    // draws, the classification and the cap.
    const BalanceTuning::Values& tuning = BalanceTuning::Get();
    const int showcasePercent = tuning.showcasePercent;
    const double closeCap = tuning.ambientCloseCap;
    int category;
    if (showcasePercent > 0) {
        const unsigned bucket = categoryRoll % 100u;
        category = bucket < static_cast<unsigned>(showcasePercent) ? 2 :
            bucket < static_cast<unsigned>(showcasePercent + 20) ? 1 : 0;
    } else {
        category = (categoryRoll % 5u) == 0 ? 1 : 0;
    }
    // base + rand * SPAN, with the span an explicit literal rather than
    // (cap - base): deriving it would move the drawn target by one ULP on a
    // third of all draws and break the bit-identical shipped baseline.
    const double closeTarget = 1 + (Detail::Next(random) % 10000) * tuning.ambientCloseSpan / 10000;
    const double underdogTarget = closeCap + (Detail::Next(random) % 10000) * tuning.ambientUnderdogSpan / 10000;
    // Drawn only when the band is enabled, so the stream does not shift for the
    // shipped configuration. The showcase band starts at the LIVE ambient cap, so
    // narrowing the ambient band widens showcase rather than leaving a gap
    // between them.
    double showcaseTarget = 0.0;
    if (showcasePercent > 0)
        showcaseTarget = tuning.ambientRatioCap + (Detail::Next(random) % 10000) *
            (Detail::kShowcaseRatioHigh - tuning.ambientRatioCap) / 10000;
    const double ratioCap = showcasePercent > 0 ? Detail::kShowcaseRatioHigh : tuning.ambientRatioCap;
    const int attendance = 2 + Detail::Next(random) % ((std::min)(capacity, size) - 1);
    const std::map<std::string, std::string> types = Detail::Types(roster);
    Detail::Ranked best[3]; std::set<std::string> seen;
    for (int i = 0; i < size; ++i) for (int j = i + 1; j < size; ++j) {
        const std::vector<int> a(1, i), b(1, j);
        Detail::AmbientCandidate(roster, a, b, history, types, closeTarget, underdogTarget, showcaseTarget, closeCap, ratioCap, attendance, attemptSeed, seen, best);
    }
    // A fixed work budget; invalid size draws cost an attempt too.
    for (int attempt = 0; attempt < 4096 && capacity > 2 && size > 2; ++attempt) {
        const int countA = 1 + Detail::Next(random) % 4, countB = 1 + Detail::Next(random) % 4;
        if (countA + countB > capacity || countA + countB > size || countA + countB == 2) continue;
        std::vector<int> available;
        for (int i = 0; i < size; ++i) available.push_back(i);
        std::vector<int> a, b;
        for (int i = 0; i < countA + countB; ++i) {
            const int chosen = i + Detail::Next(random) % (size - i);
            std::swap(available[i], available[chosen]);
            (i < countA ? a : b).push_back(available[i]);
        }
        Detail::AmbientCandidate(roster, a, b, history, types, closeTarget, underdogTarget, showcaseTarget, closeCap, ratioCap, attendance, attemptSeed, seen, best);
    }
    Match match = best[category].match.valid ? best[category].match
        : best[category == 0 ? 1 : 0].match;
    if (match.valid && (Detail::Next(random) & 1u)) { match.a.swap(match.b); std::swap(match.scoreA, match.scoreB); }
    return match;
}

namespace Detail {
inline int CoverageFor(int a, int b) {
    if (a==1 && b==1) return TownDiagnosticData::Duel;
    if (a==b) return TownDiagnosticData::EqualGroup;
    return std::abs(a-b)==1 ? TownDiagnosticData::DifferenceOne : TownDiagnosticData::DifferenceLarge;
}
// Catalog type names, in the order the diagnostic's round-robin steps through
// them. ONE table behind both the index and the count, so the two can never
// disagree. Adding a type to the catalog means adding it here: DesiredType()
// walks (nextSequence-1) % TypeCount and HasTarget matches on TypeIndex, which
// returns -1 for an unknown type, so a catalog type missing from this list can
// never be targeted by a controlled run and its bouts read as relaxations.
inline const char* const* TypeNames(int& count) {
    static const char* const names[] = {"Polearm","Sabre","Katana","Champion","Mace","Greatsword","Martial","Hacker"};
    count = static_cast<int>(sizeof(names) / sizeof(names[0]));
    return names;
}
inline int TypeCount() {
    int count = 0;
    TypeNames(count);
    return count;
}
inline int TypeIndex(const char* type) {
    int count = 0;
    const char* const* names = TypeNames(count);
    for (int i = 0; i < count; ++i) if (type && std::strcmp(type, names[i]) == 0) return i;
    return -1;
}
inline bool HasTarget(const std::vector<Fighter>& roster,const std::vector<int>& a,const std::vector<int>& b,int type,int tier) {
    for (int side=0;side<2;++side) {
        const std::vector<int>& members=side?b:a;
        for(size_t i=0;i<members.size();++i) {
            const TownFighterCatalog::Entry* e=TownFighterCatalog::Find(roster[members[i]].role.c_str());
            if(e && TypeIndex(e->type)==type && e->tier==tier) return true;
        }
    }
    return false;
}
inline double ScoreIds(const std::vector<Fighter>& roster,const std::vector<std::string>& ids,double influence) {
    double total=0;int found=0;
    for(size_t i=0;i<ids.size();++i) for(size_t j=0;j<roster.size();++j) if(roster[j].id==ids[i]) {
        const double power=FighterPower(roster[j].ability,roster[j].mmr,influence);
        if(power<=0) return -1; total+=power;++found; break;
    }
    return found==static_cast<int>(ids.size()) && total>0 && Finite(total) ? total : -1;
}
inline void FillModels(Match& match,const std::vector<Fighter>& roster,double reduced) {
    const double influences[TownDiagnosticData::ModelCount]={0,reduced,1};
    for(int i=0;i<TownDiagnosticData::ModelCount;++i) {
        match.modelScoreA[i]=ScoreIds(roster,match.a,influences[i]);
        match.modelScoreB[i]=ScoreIds(roster,match.b,influences[i]);
        const double aa=match.modelScoreA[i]*match.modelScoreA[i], bb=match.modelScoreB[i]*match.modelScoreB[i];
        match.modelProbabilityA[i]=aa>0 && bb>0 ? aa/(aa+bb) : 0;
    }
}
struct DiagnosticRanked {
    Match match;
    int coveragePenalty, stylePenalty, fit;
    double novelty;
    unsigned tie;
    DiagnosticRanked() : coveragePenalty(9),stylePenalty(9),fit(0),novelty(0),tie(0) {}
    void Consider(const Match& candidate,int cp,int sp,double distance,double variety,unsigned t) {
        const int bucket=static_cast<int>(distance/.025);
        if(!match.valid || cp<coveragePenalty || (cp==coveragePenalty &&
            (sp<stylePenalty || (sp==stylePenalty && (bucket<fit || (bucket==fit &&
            (variety<novelty || (variety==novelty && t<tie)))))))) {
            match=candidate;coveragePenalty=cp;stylePenalty=sp;fit=bucket;novelty=variety;tie=t;
        }
    }
};
inline void DiagnosticCandidate(const std::vector<Fighter>& roster,const std::vector<int>& a,const std::vector<int>& b,
    const History& history,const std::map<std::string,std::string>& types,const DiagnosticRequest& request,
    int attendance,unsigned seed,std::set<std::string>& seen,DiagnosticRanked& best) {
    Match candidate; candidate.scoreA=Score(roster,a,candidate.a,request.influence); candidate.scoreB=Score(roster,b,candidate.b,request.influence);
    if(candidate.scoreA<=0 || candidate.scoreB<=0) return;
    const double ratio=(std::max)(candidate.scoreA,candidate.scoreB)/(std::min)(candidate.scoreA,candidate.scoreB);
    if(ratio>1.30+1e-12 || (request.underdog ? ratio<=1.15+1e-12 : ratio>1.15+1e-12)) return;
    if(candidate.b<candidate.a) {candidate.a.swap(candidate.b);std::swap(candidate.scoreA,candidate.scoreB);}
    std::string key=Key(candidate.a);key+='\1';key+=Key(candidate.b);if(!seen.insert(key).second)return;
    std::vector<std::string> members(candidate.a);members.insert(members.end(),candidate.b.begin(),candidate.b.end());std::sort(members.begin(),members.end());
    candidate.valid=true; const int coverage=CoverageFor(static_cast<int>(a.size()),static_cast<int>(b.size()));
    if(!request.enabledCoverage[coverage])return;
    const int cp=coverage==request.coverage?0:1;
    const int sp=!request.balanceStyleTier || HasTarget(roster,a,b,request.desiredType,request.desiredTier)?0:1;
    const double target=request.underdog?1.225:1.075;
    const double variety=Novelty(members,history,types)+.01*std::abs(static_cast<int>(members.size())-attendance);
    candidate.diagnostic=true;candidate.diagnosticUnderdog=request.underdog;candidate.diagnosticModel=request.model;
    candidate.requestedCoverage=request.coverage;candidate.actualCoverage=coverage;candidate.relaxation=cp?2:sp?1:0;
    best.Consider(candidate,cp,sp,std::fabs(ratio-target),variety,Hash(seed,key));
}
}

inline Match SelectAmbientDiagnostic(const std::vector<Fighter>& npcs,unsigned& seed,const History& history,
    const DiagnosticRequest& request,int maxTotal=8) {
    if(!request.active) return SelectAmbient(npcs,seed,history,maxTotal);
    const unsigned attemptSeed=Detail::Next(seed);unsigned random=attemptSeed;
    const int capacity=(std::min)(8,maxTotal);Match empty;if(capacity<2)return empty;
    const std::set<std::string> excluded;const std::vector<Fighter> roster=Detail::Roster(npcs,random,excluded,std::string());
    const int size=static_cast<int>(roster.size());if(size<2)return empty;
    const int attendance=2+Detail::Next(random)%((std::min)(capacity,size)-1);
    const std::map<std::string,std::string> types=Detail::Types(roster);
    Detail::DiagnosticRanked best;std::set<std::string> seen;
    for(int i=0;i<size;++i)for(int j=i+1;j<size;++j){const std::vector<int>a(1,i),b(1,j);Detail::DiagnosticCandidate(roster,a,b,history,types,request,attendance,attemptSeed,seen,best);}
    for(int attempt=0;attempt<4096 && capacity>2 && size>2;++attempt){
        const int countA=1+Detail::Next(random)%4,countB=1+Detail::Next(random)%4;
        if(countA+countB>capacity || countA+countB>size || countA+countB==2)continue;
        std::vector<int> available;for(int i=0;i<size;++i)available.push_back(i);std::vector<int>a,b;
        for(int i=0;i<countA+countB;++i){const int chosen=i+Detail::Next(random)%(size-i);std::swap(available[i],available[chosen]);(i<countA?a:b).push_back(available[i]);}
        Detail::DiagnosticCandidate(roster,a,b,history,types,request,attendance,attemptSeed,seen,best);
    }
    Match match=best.match;
    if(!match.valid){
        unsigned fallbackSeed=attemptSeed;match=SelectAmbient(npcs,fallbackSeed,history,maxTotal);
        if(!match.valid)return match;match.diagnostic=true;match.diagnosticModel=request.model;match.requestedCoverage=request.coverage;
        match.actualCoverage=Detail::CoverageFor(static_cast<int>(match.a.size()),static_cast<int>(match.b.size()));match.relaxation=3;
        match.diagnosticUnderdog=(std::max)(match.scoreA,match.scoreB)/(std::min)(match.scoreA,match.scoreB)>1.15;
    }
    if(Detail::Next(random)&1u){match.a.swap(match.b);std::swap(match.scoreA,match.scoreB);}
    Detail::FillModels(match,npcs,request.reducedInfluence);
    match.scoreA=match.modelScoreA[request.model];match.scoreB=match.modelScoreB[request.model];
    return match;
}
}
