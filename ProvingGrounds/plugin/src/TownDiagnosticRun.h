#pragma once

#include "TownDiagnosticData.h"
#include "TownMatchmakingPolicy.h"
#include <string>

namespace SparPodium { struct Snapshot; }

namespace TownDiagnosticRun {
    TownDiagnosticData::State& Get();
    bool IsActive();
    bool StartPreset(bool resetTownRecords);
    bool StartConfigured(bool resetTownRecords);
    bool Pause();
    bool Resume();
    bool End();
    bool SetTarget(int target);
    bool ToggleModel(int model);
    bool ToggleCoverage(int coverage);
    bool CycleReducedInfluence();
    bool CycleUnderdogPercent();
    bool ToggleStyleTierBalance();
    TownMatchmakingPolicy::DiagnosticRequest NextRequest();
    void Assign(const TownMatchmakingPolicy::Match& match, double announcedHours);
    void CancelPending();
    void RetryPending();
    void RecoverPendingAfterLoad();
    void AbortPending();
    void CompletePending(const SparPodium::Snapshot& result, double completedHours);
    std::string StatusText();
    std::string ValidationError();
}
