#include "CombatBalanceLog.h"
#include "BalanceTuning.h"
#include "PGLog.h"
#include "ArenaCombatProfile.h"
#include "LeaderboardStore.h"
#include "TownMatchmakingRuntime.h"
#include "TownBettingPolicy.h"
#include "TownArena.h"
#include "TownChallengePolicy.h"
#include "TownDiagnosticRun.h"
#include "TownFighterCatalog.h"
#include "RosterStatus.h"
#include "SparSession.h"
#include <kenshi/Character.h>
#include <kenshi/GameWorld.h>
#include <kenshi/Globals.h>
#include <sstream>
#include <iomanip>
#include <locale>
#include <float.h>
#include <Windows.h>

namespace {
    unsigned matchId = 0, marketId = 0, challengeId = 0;
    // Monotonic per-process record counter. Unlike the id families it is written
    // on EVERY event, so a reader can key a join on (session, record) and never
    // meet the cross-session id collisions described in
    // balancing/matchmaking-balance-insights.md.
    unsigned record = 0;
    bool active = false;
    double startedHours = 0;
    ULONGLONG startedWall = 0;
    // Bout-start model scores. Written for a controlled diagnostic bout AND for a
    // player challenge, so it is no longer diagnostic-only even though both feed
    // the same start_* field names; match_result repeats it so a result line still
    // carries the chance its bout was measured against.
    std::string startFields;
    // Which channel the live match belongs to, and which challenge it came from.
    // matchChallengeId is deliberately held across ChallengeSettled, because that
    // call clears challengeId and match_result must still be able to name its
    // challenge whichever of the two fires first.
    bool playerMatch = false;
    unsigned matchChallengeId = 0;
    int challengeStake = 0, challengePayout = 0;
    double Hours() { return ou ? ou->getTimeStamp_inGameHours().getTotalHours() : 0; }
    std::string Number(double value) {
        if (!_finite(value)) return "null";
        std::ostringstream out; out.imbue(std::locale::classic()); out << std::setprecision(10) << value; return out.str();
    }
    // The live balance levers, stamped onto the events a balance read depends on
    // so a captured log always describes the parameters that produced it.
    std::string TuningFields() {
        char buffer[512];
        BalanceTuning::WriteJson(buffer, static_cast<int>(sizeof(buffer)));
        return std::string(",") + buffer;
    }
    std::string Line(const char* event, const std::string& fields) {
        return "\"event\":" + PGLog::Quote(event) + ",\"record\":" + Number(++record) +
            ",\"game_hours\":" + Number(Hours()) +
            ",\"save\":" + PGLog::Quote(LeaderboardStore::GetActiveSaveKey()) + fields;
    }
    // NPC bouts, their markets and the diagnostic run: the ambient population.
    void Event(const char* event, const std::string& fields) { PGLog::Combat(Line(event, fields)); }
    // Player challenges, in their own file, so a balance read of either population
    // is never diluted by the other. Both channels share the session id and the
    // record counter and can be interleaved on (session, record).
    void ChallengeEvent(const char* event, const std::string& fields) { PGLog::Challenge(Line(event, fields)); }
    std::string Field(const char* name, double value) { return "," + PGLog::Quote(name) + ":" + Number(value); }
    std::string DiagnosticFields() {
        const TownDiagnosticData::State& d=TownDiagnosticRun::Get();
        if(!d.pending.active)return "";
        const TownDiagnosticData::Pending& p=d.pending;
        std::string out=Field("diagnostic_run_id",d.runId)+Field("diagnostic_sequence",p.sequence)+
            ",\"diagnostic_model\":"+PGLog::Quote(TownDiagnosticData::ModelName(p.model))+
            ",\"diagnostic_requested_coverage\":"+PGLog::Quote(TownDiagnosticData::CoverageName(p.requestedCoverage))+
            ",\"diagnostic_actual_coverage\":"+PGLog::Quote(TownDiagnosticData::CoverageName(p.actualCoverage))+
            Field("diagnostic_relaxation",p.relaxation)+Field("diagnostic_underdog",p.underdog)+
            Field("diagnostic_size_a",p.sizeA)+Field("diagnostic_size_b",p.sizeB);
        for(int i=0;i<TownDiagnosticData::ModelCount;++i) {
            const char* suffix=i==0?"stats":i==1?"reduced":"full";
            out+=Field((std::string("score_a_")+suffix).c_str(),p.scoreA[i]);
            out+=Field((std::string("score_b_")+suffix).c_str(),p.scoreB[i]);
            out+=Field((std::string("predicted_p_a_")+suffix).c_str(),p.probabilityA[i]);
        }
        return out;
    }
    // Per-side ability and rating, read from the live fighters. Shared by the
    // diagnostic's three models and a player challenge's single one, so both quote
    // the same underlying quantity.
    bool StartAbility(Character* const* fighters,const MatchRules::MatchTeam* teams,int count,
        std::vector<double>& abilityA,std::vector<double>& ratingA,std::vector<double>& abilityB,std::vector<double>& ratingB) {
        if(!fighters || !teams || count<2)return false;
        for(int i=0;i<count;++i) {
            const ArenaCombatProfile::Profile profile=ArenaCombatProfile::Read(fighters[i]);
            const double ability=ArenaCombatProfile::Evaluate(profile);
            const double rating=LeaderboardStore::GetRating(fighters[i],LeaderboardData::Town);
            if(ability<=0 || rating<0) return false;
            if(teams[i]==MatchRules::TeamA){abilityA.push_back(ability);ratingA.push_back(rating);}
            else if(teams[i]==MatchRules::TeamB){abilityB.push_back(ability);ratingB.push_back(rating);}
        }
        return !abilityA.empty() && !abilityB.empty();
    }
    // A player challenge's model chance, under the same start_* names the
    // diagnostic uses for its full model, so one reader covers both populations.
    // The full model only: the reduced-influence variant is a property of that
    // run's config, and a booked challenge has no such config.
    std::string PlayerStartScores(Character* const* fighters,const MatchRules::MatchTeam* teams,int count) {
        std::vector<double> abilityA,ratingA,abilityB,ratingB;
        if(!StartAbility(fighters,teams,count,abilityA,ratingA,abilityB,ratingB))
            return Field("start_scores_available",0);
        const double scoreA=TownMatchmakingPolicy::TeamScore(&abilityA[0],&ratingA[0],static_cast<int>(abilityA.size()),1.0);
        const double scoreB=TownMatchmakingPolicy::TeamScore(&abilityB[0],&ratingB[0],static_cast<int>(abilityB.size()),1.0);
        if(scoreA<=0 || scoreB<=0)return Field("start_scores_available",0);
        return Field("start_scores_available",1)+Field("start_score_a_full",scoreA)+Field("start_score_b_full",scoreB)+
            Field("start_predicted_p_a_full",TownBettingPolicy::WinProbability(scoreA,scoreB));
    }
    std::string DiagnosticStartScores(Character* const* fighters,const MatchRules::MatchTeam* teams,int count) {
        const TownDiagnosticData::State& d=TownDiagnosticRun::Get();
        if(!d.pending.active)return "";
        std::vector<double> abilityA,ratingA,abilityB,ratingB;
        if(!StartAbility(fighters,teams,count,abilityA,ratingA,abilityB,ratingB))
            return Field("start_scores_available",0);
        std::string out=Field("start_scores_available",1);
        const double influences[TownDiagnosticData::ModelCount]={0,d.config.reducedInfluence,1};
        for(int i=0;i<TownDiagnosticData::ModelCount;++i) {
            const char* suffix=i==0?"stats":i==1?"reduced":"full";
            const double scoreA=TownMatchmakingPolicy::TeamScore(&abilityA[0],&ratingA[0],static_cast<int>(abilityA.size()),influences[i]);
            const double scoreB=TownMatchmakingPolicy::TeamScore(&abilityB[0],&ratingB[0],static_cast<int>(abilityB.size()),influences[i]);
            if(scoreA<=0 || scoreB<=0)return Field("start_scores_available",0);
            out+=Field((std::string("start_score_a_")+suffix).c_str(),scoreA);
            out+=Field((std::string("start_score_b_")+suffix).c_str(),scoreB);
            out+=Field((std::string("start_predicted_p_a_")+suffix).c_str(),TownBettingPolicy::WinProbability(scoreA,scoreB));
        }
        return out;
    }
    std::string Fighter(Character* c, int slot, MatchRules::MatchTeam team, LeaderboardData::Kind kind) {
        const bool valid = c && c->isValid();
        const ArenaCombatProfile::Profile p = ArenaCombatProfile::Read(c);
        const std::string id = TownMatchmakingRuntime::Identity(c);
        std::string out = "{\"id\":" + PGLog::Quote(id) + ",\"template\":" + PGLog::Quote(TownMatchmakingRuntime::Role(c)) +
            ",\"name\":" + PGLog::Quote(valid ? c->getName() : "Unavailable") + Field("slot", slot) + Field("team", team) +
            ",\"style\":" + PGLog::Quote(p.style) + Field("profile_valid", p.valid) +
            Field("attack", p.attack) + Field("defence", p.defence) + Field("strength", p.strength) +
            Field("toughness", p.toughness) + Field("dexterity", p.dexterity) + Field("weapon_skill", p.weaponSkill) +
            Field("ability", ArenaCombatProfile::Evaluate(p));
        const TownFighterCatalog::Entry* catalog=TownFighterCatalog::Find(TownMatchmakingRuntime::Role(c).c_str());
        out += ",\"catalog_type\":" + PGLog::Quote(catalog ? catalog->type : "") + Field("catalog_tier", catalog ? catalog->tier : -1);
        std::vector<LeaderboardStore::Record> standings; LeaderboardStore::GetStandings(kind, standings);
        int wins = 0, losses = 0, matches = 0;
        for (size_t i = 0; i < standings.size(); ++i) if (!id.empty() && standings[i].id == id) {
            wins = standings[i].wins; losses = standings[i].losses; matches = standings[i].matches; break;
        }
        out += Field("mmr", valid ? LeaderboardStore::GetRating(c, kind) : 0) + Field("wins", wins) + Field("losses", losses) + Field("matches", matches);
        RosterStatus::Snapshot health = {};
        if (RosterStatus::Read(c, health)) out += Field("lowest_limb", health.lowestLimb) + Field("blood", health.blood) + Field("recovery", health.recovery);
        return out + "}";
    }
    std::string Fighters(Character* const* fighters, const MatchRules::MatchTeam* teams, int count, LeaderboardData::Kind kind) {
        std::string out = ",\"fighters\":[";
        for (int i = 0; i < count; ++i) { if (i) out += ','; out += Fighter(fighters[i], i, teams[i], kind); }
        return out + "]";
    }
}
namespace CombatBalanceLog {
    void StartMatch(MatchRules::MatchMode mode, Character** fighters, MatchRules::MatchTeam* teams, int count) {
        AbortMatch("superseded");
        matchId = PGLog::NextId(); active = true; startedHours = Hours(); startedWall = GetTickCount64();
        const bool town = TownArena::OwnsMatch(), player = TownArena::IsPlayerMatch();
        // A player challenge is its own population: it goes to the challenge file,
        // and carries no market and no diagnostic block. It does now carry a model
        // chance, which before this only the ambient path had.
        playerMatch = town && player;
        matchChallengeId = playerMatch ? challengeId : 0;
        startFields = !town ? "" : player ? PlayerStartScores(fighters, teams, count) : DiagnosticStartScores(fighters, teams, count);
        const std::string body = Field("match_id", matchId) + Field("market_id", town && !player ? marketId : 0) +
            Field("challenge_id", town && player ? challengeId : 0) + Field("mode", mode) +
            ",\"kind\":" + PGLog::Quote(town ? player ? "town_player" : "town_npc" : "arena") + TuningFields() +
            (town && !player ? DiagnosticFields() : "") + startFields +
            Fighters(fighters, teams, count, LeaderboardData::ForMatch(town));
        if (playerMatch) ChallengeEvent("match_start", body); else Event("match_start", body);
    }
    void FinishMatch(const SparPodium::Snapshot& snapshot, int stopReason) {
        if (!active) return;
        // Damage concentration: the single strongest predictor of the winner in
        // the recorded sample was the top damage dealer's side (27/36, p=0.002),
        // not the additive team score. Logged so it is measurable directly.
        float totalDamage = 0.0f, teamDamageA = 0.0f, teamDamageB = 0.0f, topDamage = 0.0f;
        for (int i = 0; i < snapshot.fighterCount; ++i) {
            const float dealt = snapshot.fighters[i].damageDealt;
            totalDamage += dealt;
            if (dealt > topDamage) topDamage = dealt;
            if (snapshot.fighters[i].team == MatchRules::TeamA) teamDamageA += dealt;
            else teamDamageB += dealt;
        }
        // challenge_id leads, so a player result names its own challenge even when
        // ChallengeSettled has already cleared the live id.
        std::string fields = Field("match_id", matchId) + Field("challenge_id", matchChallengeId) + Field("outcome", snapshot.outcome) + Field("stop_reason", stopReason) +
            ",\"end_kind\":" + PGLog::Quote(MatchRules::EndKindName(SparSession::GetPendingEndKind())) +
            Field("game_minutes", (Hours() - startedHours) * 60) + Field("wall_seconds", (GetTickCount64() - startedWall) / 1000.0) +
            Field("last_standing_slot", snapshot.lastStandingId) + ",\"outcome_name\":" + PGLog::Quote(
                snapshot.outcome == SparPodium::OutcomeTeamAWins ? "team_a_wins" :
                snapshot.outcome == SparPodium::OutcomeTeamBWins ? "team_b_wins" :
                snapshot.outcome == SparPodium::OutcomeLastStanding ? "last_standing" :
                snapshot.outcome == SparPodium::OutcomeDraw ? "draw" : "stopped") +
            Field("team_damage_a", teamDamageA) + Field("team_damage_b", teamDamageB) +
            Field("top_damage_share", totalDamage > 0 ? topDamage / totalDamage : 0) +
            Field("total_damage", totalDamage) + ",\"fighters\":[";
        for (int i = 0; i < snapshot.fighterCount; ++i) {
            const SparPodium::FighterRow& r = snapshot.fighters[i];
            Character* c = SparSession::GetParticipant(r.id);
            if (i) fields += ',';
            fields += "{\"id\":" + PGLog::Quote(TownMatchmakingRuntime::Identity(c)) + ",\"name\":" + PGLog::Quote(r.name) + Field("slot", r.id) + Field("team", r.team) +
                Field("damage_dealt", r.damageDealt) + Field("damage_taken", r.damageTaken) + Field("damage_mitigated", r.damageMitigated) +
                Field("damage_share", totalDamage > 0 ? r.damageDealt / totalDamage : 0) +
                Field("hits", r.hitsLanded) + Field("blocks", r.blocks) + Field("misses", r.misses) + Field("dodges", r.dodges) +
                Field("elimination_order", r.eliminationIndex) + Field("rating_updated", r.ratingUpdated) +
                Field("mmr_before", r.ratingBefore) + Field("mmr_after", r.ratingAfter);
            RosterStatus::Snapshot health = {};
            if (RosterStatus::Read(c, health)) fields += Field("lowest_limb", health.lowestLimb) + Field("blood", health.blood) + Field("recovery", health.recovery);
            if (c && c->isValid()) fields += Field("dead", c->isDead()) + Field("unconscious", c->isUnconcious());
            fields += "}";
        }
        const std::string body = fields + "]" + Field("damage_mitigated_available",0) + TuningFields() + DiagnosticFields() + startFields;
        if (playerMatch) ChallengeEvent("match_result", body); else Event("match_result", body);
        active = false; startFields.clear();
    }
    void AbortMatch(const char* reason) {
        if (!active) return;
        // Teardown-safe: no Character or world dereferences, no fabricated result.
        const std::string body = "\"event\":\"match_aborted\"" + Field("record", ++record) + Field("match_id", matchId) +
            ",\"reason\":" + PGLog::Quote(reason) + DiagnosticFields() + startFields;
        if (playerMatch) PGLog::Challenge(body); else PGLog::Combat(body);
        active = false; startFields.clear();
    }
    void OpenMarket(Character* const* fighters, const MatchRules::MatchTeam* teams, int count, double scoreA, double scoreB, bool eligible) {
        marketId = PGLog::NextId();
        Event("market_open", Field("market_id", marketId) + Field("eligible", eligible) + Field("ability_a", scoreA) + Field("ability_b", scoreB) + Field("scheduled_game_hours", TownArena::GetPlannedNpcStartHours()) +
            Field("predicted_p_a", TownBettingPolicy::WinProbability(scoreA, scoreB)) +
            Field("return_per_1000_a", TownBettingPolicy::ReturnCats(1000, scoreA, scoreB)) +
            Field("return_per_1000_b", TownBettingPolicy::ReturnCats(1000, scoreB, scoreA)) + TuningFields() + DiagnosticFields() +
            Fighters(fighters, teams, count, LeaderboardData::Town));
    }
    void MarketEvent(const char* event, const std::string& fields) {
        if (marketId) Event(event, Field("market_id", marketId) + fields + DiagnosticFields());
    }
    void CloseMarket(int winner, int side, int stake, int quotedReturn, int credit, bool hadTicket) {
        MarketEvent("market_settled", Field("winner_side", winner) + Field("had_ticket", hadTicket) + Field("bet_side", side) +
            Field("stake", stake) + Field("quoted_return", quotedReturn) + Field("credited", credit) + Field("player_net", hadTicket ? credit - stake : 0));
        marketId = 0;
    }
    void ChallengeAccepted(const ChallengeCard& card) {
        challengeId = PGLog::NextId();
        challengeStake = card.stake; challengePayout = card.payout;
        // odds / implied_p are the book's own view of the bout (payout over
        // buy-in). model_p is the same model the ambient book prices from, on the
        // same TeamScore quantity, so the two chances sit on one line and can be
        // compared against each other and against the realised result.
        std::string fields = Field("challenge_id", challengeId) + Field("slot", card.slot) +
            Field("division", card.division) + ",\"division_name\":" + PGLog::Quote(TownChallengePolicy::DivisionName(card.division)) +
            Field("rarity", card.rarity) + ",\"rarity_name\":" + PGLog::Quote(TownChallengePolicy::RarityName(card.rarity)) +
            Field("encounter", card.encounter) + ",\"encounter_title\":" + PGLog::Quote(TownChallengePolicy::Title(card.encounter)) +
            Field("format", card.format) + Field("mode", card.mode) + ",\"mode_name\":" + PGLog::Quote(TownChallengePolicy::ModeName(card.mode)) +
            Field("players", card.players) + Field("enemies", card.enemies) +
            Field("own_score", card.ownScore) + Field("enemy_score", card.enemyScore) +
            Field("stake", card.stake) + Field("quoted_return", card.payout) +
            Field("odds", card.stake > 0 ? static_cast<double>(card.payout) / card.stake : 0) +
            Field("implied_p", card.payout > 0 ? static_cast<double>(card.stake) / card.payout : 0) +
            Field("model_p", TownBettingPolicy::WinProbability(card.ownScore, card.enemyScore)) + TuningFields() +
            ",\"opponents\":[";
        for (int i = 0; i < card.enemies && i < 4; ++i) {
            if (i) fields += ',';
            fields += "{\"id\":" + PGLog::Quote(card.opponents[i]) + ",\"name\":" + PGLog::Quote(card.opponentNames[i]) +
                ",\"role\":" + PGLog::Quote(card.opponentRoles[i]) + Field("slot", i) + "}";
        }
        ChallengeEvent("challenge_accepted", fields + "]");
    }
    void ChallengeSettled(int outcome, int credit) {
        // stake / quoted_return are repeated here so the challenge ledger is
        // self-contained: a reader never has to join back to the accept line to
        // work out what the bet was.
        if (challengeId) ChallengeEvent("challenge_settled", Field("challenge_id", challengeId) + Field("outcome", outcome) +
            Field("credited", credit) + Field("stake", challengeStake) + Field("quoted_return", challengePayout) +
            Field("player_net", credit - challengeStake));
        challengeId = 0; challengeStake = challengePayout = 0;
    }
    void DiagnosticEvent(const char* event, const std::string& fields) { Event(event, fields); }
    void Abandon() {
        AbortMatch("world_teardown");
        if (marketId) PGLog::Combat("\"event\":\"market_abandoned\"" + Field("record", ++record) + Field("market_id", marketId) + DiagnosticFields());
        if (challengeId) PGLog::Challenge("\"event\":\"challenge_abandoned\"" + Field("record", ++record) + Field("challenge_id", challengeId) + Field("stake", challengeStake));
        marketId = challengeId = challengeStake = challengePayout = 0;
    }
}
