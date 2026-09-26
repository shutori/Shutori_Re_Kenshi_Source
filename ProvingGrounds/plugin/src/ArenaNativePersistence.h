#pragma once
namespace ArenaNativePersistence {
    bool InstallHooks();
    void Disable();
    void AbandonWorld();
    bool IsLoading();
    bool IsReady();
    // Explicitly discard unmatched arena progress for the currently loaded slot.
    // Only offered after matching sidecar/backup/emergency recovery failed.
    bool CanResetUnmatchedSidecar();
    bool ResetUnmatchedSidecar();
    void Tick();
}
