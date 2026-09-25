#include "TownDiagnosticRun.h"
#include "LeaderboardStore.h"
#include "SparPodium.h"
#include "CombatBalanceLog.h"
#include "TownArena.h"
#include <Windows.h>
#include <cstdio>

namespace {
    bool Mutable(const TownDiagnosticData::State& s) {
        return s.status != TownDiagnosticData::Active && s.status != TownDiagnosticData::Paused;
    }
    int EnabledModels(const TownDiagnosticData::Config& c) {
        int n=0;for(int i=0;i<TownDiagnosticData::ModelCount;++i)if(c.models[i])++n;return n;
    }
    int EnabledCoverage(const TownDiagnosticData::Config& c) {
        int n=0;for(int i=0;i<TownDiagnosticData::CoverageCount;++i)if(c.coverage[i])++n;return n;
    }
    int ModelMask(const TownDiagnosticData::Config& c) {
        int mask=0;for(int i=0;i<TownDiagnosticData::ModelCount;++i)if(c.models[i])mask|=1<<i;return mask;
    }
    int CoverageMask(const TownDiagnosticData::Config& c) {
        int mask=0;for(int i=0;i<TownDiagnosticData::CoverageCount;++i)if(c.coverage[i])mask|=1<<i;return mask;
    }
    int LeastModel(const TownDiagnosticData::State& s) {
        int best=-1;
        for(int i=0;i<TownDiagnosticData::ModelCount;++i) if(s.config.models[i] &&
            (best<0 || s.counters.modelCompleted[i]<s.counters.modelCompleted[best])) best=i;
        return best;
    }
    int LeastCoverage(const TownDiagnosticData::State& s) {
        int best=-1;
        for(int i=0;i<TownDiagnosticData::CoverageCount;++i) if(s.config.coverage[i] &&
            (best<0 || s.counters.coverageCompleted[i]<s.counters.coverageCompleted[best])) best=i;
        return best;
    }
}

namespace TownDiagnosticRun {
TownDiagnosticData::State& Get() { return LeaderboardStore::GetTownDiagnostic(); }
bool IsActive() { return Get().status==TownDiagnosticData::Active; }
std::string ValidationError() {
    TownDiagnosticData::State& s=Get();
    if(!LeaderboardStore::HasActiveSave()) return "Load or start a game first.";
    if(TownArena::IsBusy() || TownArena::HasBooking()) return "Wait for the active bout to finish or cancel the player booking.";
    if(s.config.target<1 || s.config.target>10000) return "Target must be between 1 and 10000.";
    if(!EnabledModels(s.config)) return "Enable at least one MMR model.";
    if(!EnabledCoverage(s.config)) return "Enable at least one roster bucket.";
    return "";
}
bool StartConfigured(bool resetTownRecords) {
    TownDiagnosticData::State& s=Get();
    if(!Mutable(s) || !ValidationError().empty()) return false;
    // Idle ambient markets are created continuously by the normal scheduler.
    // Replace one atomically so it cannot race the debug action; active fights
    // and player-owned bookings remain protected by ValidationError above.
    if(TownArena::HasPlannedNpcBout())
        TownArena::CancelPlannedNpcBout("Replaced by controlled diagnostic run");
    if(TownArena::HasPlannedNpcBout()) return false;
    const TownDiagnosticData::Config config=s.config;
    const unsigned prior=s.runId;
    if(resetTownRecords && !LeaderboardStore::ResetTownStandings()) return false;
    s=TownDiagnosticData::State();s.config=config;s.status=TownDiagnosticData::Active;
    LeaderboardStore::ClearDiagnosticError();
    s.runId=(GetTickCount() ^ (prior*1664525u+1013904223u))|1u;
    char fields[512];sprintf_s(fields,",\"diagnostic_run_id\":%u,\"target\":%d,\"reset_town_records\":%d,\"model_mask\":%d,\"coverage_mask\":%d,\"reduced_influence\":%.6f,\"underdog_percent\":%d,\"balance_style_tier\":%d",
        s.runId,s.config.target,resetTownRecords?1:0,ModelMask(s.config),CoverageMask(s.config),s.config.reducedInfluence,s.config.underdogPercent,s.config.balanceStyleTier?1:0);
    CombatBalanceLog::DiagnosticEvent("diagnostic_run_started",fields);
    return true;
}
bool StartPreset(bool resetTownRecords) {
    TownDiagnosticData::State& s=Get();
    if(!Mutable(s)) return false;
    s.config=TownDiagnosticData::Config();s.config.target=150;
    return StartConfigured(resetTownRecords);
}
bool Pause(){TownDiagnosticData::State& s=Get();if(s.status!=TownDiagnosticData::Active || s.pending.active)return false;s.status=TownDiagnosticData::Paused;char f[96];sprintf_s(f,",\"diagnostic_run_id\":%u",s.runId);CombatBalanceLog::DiagnosticEvent("diagnostic_run_paused",f);return true;}
bool Resume(){TownDiagnosticData::State& s=Get();if(s.status!=TownDiagnosticData::Paused)return false;s.status=TownDiagnosticData::Active;char f[96];sprintf_s(f,",\"diagnostic_run_id\":%u",s.runId);CombatBalanceLog::DiagnosticEvent("diagnostic_run_resumed",f);return true;}
bool End(){TownDiagnosticData::State& s=Get();if((s.status!=TownDiagnosticData::Active && s.status!=TownDiagnosticData::Paused)||s.pending.active)return false;s.status=TownDiagnosticData::Ended;char f[128];sprintf_s(f,",\"diagnostic_run_id\":%u,\"completed\":%d",s.runId,s.counters.completed);CombatBalanceLog::DiagnosticEvent("diagnostic_run_ended",f);return true;}
bool SetTarget(int target){TownDiagnosticData::State& s=Get();if(!Mutable(s)||target<1||target>10000)return false;s.config.target=target;return true;}
bool ToggleModel(int model){TownDiagnosticData::State& s=Get();if(!Mutable(s)||model<0||model>=TownDiagnosticData::ModelCount)return false;s.config.models[model]=!s.config.models[model];if(!EnabledModels(s.config)){s.config.models[model]=true;return false;}return true;}
bool ToggleCoverage(int coverage){TownDiagnosticData::State& s=Get();if(!Mutable(s)||coverage<0||coverage>=TownDiagnosticData::CoverageCount)return false;s.config.coverage[coverage]=!s.config.coverage[coverage];if(!EnabledCoverage(s.config)){s.config.coverage[coverage]=true;return false;}return true;}
bool CycleReducedInfluence(){TownDiagnosticData::State& s=Get();if(!Mutable(s))return false;const double v=s.config.reducedInfluence;s.config.reducedInfluence=v<.24?.25:v<.49?.50:v<.74?.75:0;return true;}
bool CycleUnderdogPercent(){TownDiagnosticData::State& s=Get();if(!Mutable(s))return false;s.config.underdogPercent=(s.config.underdogPercent+10)%60;return true;}
bool ToggleStyleTierBalance(){TownDiagnosticData::State& s=Get();if(!Mutable(s))return false;s.config.balanceStyleTier=!s.config.balanceStyleTier;return true;}
TownMatchmakingPolicy::DiagnosticRequest NextRequest(){
    TownMatchmakingPolicy::DiagnosticRequest r;TownDiagnosticData::State& s=Get();
    if(s.status!=TownDiagnosticData::Active || s.pending.active || s.counters.completed>=s.config.target)return r;
    r.active=true;r.model=LeastModel(s);r.coverage=LeastCoverage(s);r.reducedInfluence=s.config.reducedInfluence;
    for(int i=0;i<TownDiagnosticData::CoverageCount;++i)r.enabledCoverage[i]=s.config.coverage[i];
    r.influence=r.model==TownDiagnosticData::StatsOnly?0:r.model==TownDiagnosticData::ReducedMmr?s.config.reducedInfluence:1;
    r.underdog=(static_cast<int>(((s.nextSequence-1)*37u)%100u)<s.config.underdogPercent);
    // Step every catalog type (see TypeNames), so a type added to the catalog is
    // targeted by the run instead of being skipped as a relaxation.
    const unsigned types=static_cast<unsigned>(TownMatchmakingPolicy::Detail::TypeCount());
    r.balanceStyleTier=s.config.balanceStyleTier;r.desiredType=static_cast<int>((s.nextSequence-1)%types);
    r.desiredTier=static_cast<int>(((s.nextSequence-1)/types)%3);r.seed=s.runId^s.nextSequence;return r;
}
void Assign(const TownMatchmakingPolicy::Match& m,double hours){
    TownDiagnosticData::State& s=Get();if(s.status!=TownDiagnosticData::Active || s.pending.active || !m.valid || !m.diagnostic)return;
    TownDiagnosticData::Pending p;p.active=true;p.sequence=s.nextSequence++;p.model=m.diagnosticModel;p.requestedCoverage=m.requestedCoverage;
    p.actualCoverage=m.actualCoverage;p.relaxation=m.relaxation;p.underdog=m.diagnosticUnderdog;p.sizeA=static_cast<int>(m.a.size());p.sizeB=static_cast<int>(m.b.size());p.announcedHours=hours;
    for(int i=0;i<TownDiagnosticData::ModelCount;++i){p.scoreA[i]=m.modelScoreA[i];p.scoreB[i]=m.modelScoreB[i];p.probabilityA[i]=m.modelProbabilityA[i];}
    if(s.counters.lastCompletedHours>0 && hours>=s.counters.lastCompletedHours){s.counters.schedulingHours+=hours-s.counters.lastCompletedHours;++s.counters.schedulingSamples;}
    s.pending=p;++s.counters.opened;
}
void CancelPending(){TownDiagnosticData::State& s=Get();if(!s.pending.active)return;++s.counters.cancelled;s.pending=TownDiagnosticData::Pending();}
void RetryPending(){
    TownDiagnosticData::State& s=Get();if(!s.pending.active)return;
    if(s.nextSequence==s.pending.sequence+1)s.nextSequence=s.pending.sequence;
    ++s.counters.cancelled;s.pending=TownDiagnosticData::Pending();
}
void RecoverPendingAfterLoad(){
    TownDiagnosticData::State& s=Get();if(!s.pending.active)return;
    const unsigned sequence=s.pending.sequence;++s.counters.cancelled;s.pending=TownDiagnosticData::Pending();
    char f[160];sprintf_s(f,",\"diagnostic_run_id\":%u,\"diagnostic_sequence\":%u,\"reason\":\"reload_does_not_restore_announced_bout\"",s.runId,sequence);
    CombatBalanceLog::DiagnosticEvent("diagnostic_assignment_cancelled",f);
}
void AbortPending(){TownDiagnosticData::State& s=Get();if(!s.pending.active)return;++s.counters.aborted;s.pending=TownDiagnosticData::Pending();}
void CompletePending(const SparPodium::Snapshot& result,double hours){
    TownDiagnosticData::State& s=Get();if(!s.pending.active)return;const TownDiagnosticData::Pending p=s.pending;s.pending=TownDiagnosticData::Pending();
    if(p.relaxation>=3)return;
    const bool a=result.outcome==SparPodium::OutcomeTeamAWins,b=result.outcome==SparPodium::OutcomeTeamBWins;if(!a&&!b)return;
    ++s.counters.completed;++s.counters.modelCompleted[p.model];++s.counters.coverageCompleted[p.actualCoverage];
    if(a)++s.counters.teamAWins;else ++s.counters.teamBWins;
    if(p.sizeA==p.sizeB)++s.counters.equalWins;else if((a&&p.sizeA>p.sizeB)||(b&&p.sizeB>p.sizeA))++s.counters.largerWins;else ++s.counters.smallerWins;
    if((p.probabilityA[p.model]>=.5)==a)++s.counters.favoriteWins;s.counters.lastCompletedHours=hours;
    if(s.counters.completed>=s.config.target){
        s.status=TownDiagnosticData::Complete;char f[512];
        sprintf_s(f,",\"diagnostic_run_id\":%u,\"completed\":%d,\"cancelled\":%d,\"aborted\":%d,\"model_stats\":%d,\"model_reduced_mmr\":%d,\"model_full_mmr\":%d,\"coverage_1v1\":%d,\"coverage_equal_group\":%d,\"coverage_difference_one\":%d,\"coverage_difference_large\":%d",
            s.runId,s.counters.completed,s.counters.cancelled,s.counters.aborted,
            s.counters.modelCompleted[TownDiagnosticData::StatsOnly],s.counters.modelCompleted[TownDiagnosticData::ReducedMmr],s.counters.modelCompleted[TownDiagnosticData::FullMmr],
            s.counters.coverageCompleted[TownDiagnosticData::Duel],s.counters.coverageCompleted[TownDiagnosticData::EqualGroup],s.counters.coverageCompleted[TownDiagnosticData::DifferenceOne],s.counters.coverageCompleted[TownDiagnosticData::DifferenceLarge]);
        CombatBalanceLog::DiagnosticEvent("diagnostic_run_completed",f);
    }
}
std::string StatusText(){
    const TownDiagnosticData::State& s=Get();char text[768];const double downtime=s.counters.schedulingSamples?s.counters.schedulingHours/s.counters.schedulingSamples:0;
    const std::string diagnosticError=LeaderboardStore::GetDiagnosticError();
    if(!diagnosticError.empty()) return std::string("Diagnostic disabled: ")+diagnosticError+"\nOrdinary arena matchmaking remains active. Configure and start a new run to replace the invalid diagnostic state.";
    sprintf_s(text,"Diagnostic: %s | Run %u | %d/%d complete\nModels: stats %d, reduced %d, full %d | Coverage: 1v1 %d, equal %d, diff-1 %d, diff-2+ %d\nMarkets: %d opened, %d cancelled, %d aborted | Wins A/B %d/%d | Larger/smaller %d/%d | Favorites %d | Avg downtime %.1f game hours",
        TownDiagnosticData::StatusName(s.status),s.runId,s.counters.completed,s.config.target,s.counters.modelCompleted[0],s.counters.modelCompleted[1],s.counters.modelCompleted[2],
        s.counters.coverageCompleted[0],s.counters.coverageCompleted[1],s.counters.coverageCompleted[2],s.counters.coverageCompleted[3],s.counters.opened,s.counters.cancelled,s.counters.aborted,
        s.counters.teamAWins,s.counters.teamBWins,s.counters.largerWins,s.counters.smallerWins,s.counters.favoriteWins,downtime);return text;
}
}
