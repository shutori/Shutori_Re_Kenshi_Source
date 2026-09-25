// LeaderboardSavePolicy.h - pure save/sidecar synchronization policy
#pragma once

#include <string>
#include <cctype>

namespace LeaderboardSavePolicy
{
    class WorldContext
    {
        enum Mode { Blocked, AwaitingLoad, Fresh, Loaded };
        Mode mode;
    public:
        WorldContext() : mode(AwaitingLoad) {}
        void Block() { mode = Blocked; }
        void CompleteTransition(bool newGame) { mode = newGame ? Fresh : AwaitingLoad; }
        void MarkLoaded() { mode = Loaded; }
        bool IsFresh() const { return mode == Fresh; }
        bool CanReadSidecar() const { return mode == AwaitingLoad; }
        bool CanUseMemory() const { return mode == Fresh || mode == Loaded; }
    };
    enum Event
    {
        GameplayMutation = 0,
        NativeSaveCompleted
    };

    // Arena data must advance only with Kenshi's world snapshot. Writing on a
    // purchase or match would make an older world save inherit newer data.
    inline bool ShouldWriteSidecar(Event event)
    {
        return event == NativeSaveCompleted;
    }

    // SaveManager::name is a pending save/load target and may remain stale.
    // The loaded snapshot is identified only by the active save directory.
    inline std::string ActiveSaveKey(
        const std::string& currentGame,
        const std::string& /*pendingName*/)
    {
        return currentGame;
    }

    // Kenshi save-folder names are case-insensitive on Windows. Keep the
    // normalization here, away from filesystem and game-runtime code, so the
    // sidecar owner check is directly unit-testable.
    inline std::string NormaliseSaveKey(const std::string& key)
    {
        std::string result = key;
        for (size_t i = 0; i < result.size(); ++i)
        {
            result[i] = static_cast<char>(std::tolower(
                static_cast<unsigned char>(result[i])));
        }
        return result;
    }

    inline bool SameSaveKey(const std::string& a, const std::string& b)
    {
        return !a.empty() && !b.empty() &&
            NormaliseSaveKey(a) == NormaliseSaveKey(b);
    }

    // Versions before 6 did not stamp an owner key. They are accepted only
    // from the requested folder and upgraded on its next native save. A v6+
    // sidecar with a different owner is foreign data and must be ignored.
    inline bool ShouldAcceptSidecar(
        const std::string& requestedSaveKey,
        const std::string& declaredSaveKey,
        int version)
    {
        if (requestedSaveKey.empty())
            return false;
        if (version < 6)
            return true;
        return SameSaveKey(requestedSaveKey, declaredSaveKey);
    }

    // Saving into another folder is a deliberate native world fork (Save As,
    // quicksave, or autosave), never a context switch. The active context
    // remains the source until Kenshi actually loads the target folder.
    inline bool IsForkSave(
        const std::string& activeSaveKey,
        const std::string& targetSaveKey)
    {
        return !activeSaveKey.empty() && !targetSaveKey.empty() &&
            !SameSaveKey(activeSaveKey, targetSaveKey);
    }

    inline bool HoldForPrisoners(bool activity, bool pending, bool failed) { return pending || (activity && !failed); }
    enum ManualSaveDecision { AllowManualSave, WarnUnsecuredSave, DeferManualSave,
        NoDeferredSave, CancelManualSave, ResumeManualSave };
    class ManualSaveGate {
        bool waiting;
    public:
        ManualSaveGate() : waiting(false) {}
        void Defer() { waiting=true; }
        void Abandon() { waiting=false; }
        bool Waiting() const { return waiting; }
        ManualSaveDecision Request(bool failed) const { return waiting ? DeferManualSave : (failed ? WarnUnsecuredSave : AllowManualSave); }
        ManualSaveDecision Poll(bool pending, bool failed) {
            if(!waiting) return NoDeferredSave;
            if(pending) return DeferManualSave;
            waiting=false;
            return failed ? CancelManualSave : ResumeManualSave;
        }
    };
    inline bool NativeDispatchReady(int delay, bool filesystemIdle) { return delay <= 0 && filesystemIdle; }
}
