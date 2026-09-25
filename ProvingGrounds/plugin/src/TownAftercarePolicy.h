#pragma once
namespace TownAftercarePolicy {
    enum Action { Done, Treat, WaitForBed, PickUp, Deliver, Gather, Hold, ReturnHospital, ApproachAid, ApproachPickup, WalkToBed };
    inline bool NeedsHospital(bool winner, bool unconscious, bool needsRecovery) {
        return unconscious || (!winner && needsRecovery);
    }
    inline bool StandbyOrderLost(bool moving, float destinationErrorSquared) {
        return moving && destinationErrorSquared > 18.0f * 18.0f;
    }
    // A patient who owes a hospital transfer but has no bed resolved holds a
    // medic (Intent Wait) for as long as the bed stays unavailable. Bound that
    // wait: past the budget the patient is handed back to the town AI instead of
    // pinning the bout, the medics and the autosave hold open indefinitely.
    // Sized above the observed legitimate bed waits (<= ~16s) and far below the
    // 300s bout cap in TownArenaRuntimePolicy::AftercareExpired.
    inline float TransferWaitBudget() { return 45.0f; }
    inline bool TransferWaitExpired(float age) { return age >= TransferWaitBudget(); }
    // The plugin is what puts arena patients into the hospital's beds, so the
    // plugin has to take them out again: the engine leaves a fighter lying there
    // once healed, and the per-bout patient list is gone by then. A bed is only
    // released for a healed patient, or for one that is awake and has outlasted
    // this stay limit (a destroyed limb can never reach the healed band).
    inline float BedStayLimit() { return 600.0f; }
    inline bool BedStayExpired(float age) { return age >= BedStayLimit(); }
    // A release that the engine refuses is retried, but not every tick: retries
    // are diagnostic, and one log line per 30s per stuck occupant is enough.
    inline float BedReleaseRetry() { return 30.0f; }
    // Bed recovery is a slow background sweep, not a frame-locked one.
    inline bool BedPollDue(unsigned long lastPoll, unsigned long now) {
        return lastPoll == 0 || now < lastPoll || now - lastPoll >= 1000;
    }
    inline int RequiredMedics(int fighters) { return fighters < 0 ? 0 : fighters > 10 ? 10 : fighters; }
    inline bool NeedsRecovery(bool unconscious, float minimumHealth, float blood) {
        return unconscious || minimumHealth < .99f || blood < .99f;
    }
    struct CommandProgress {
        Action action;
        bool issued;
        float bestDistance, bestAid, lastProgress, lastObserved;
        int stalledRetries;
        CommandProgress() { Reset(); }
        void Reset() {
            action = Done; issued = false;
            bestDistance = bestAid = lastProgress = lastObserved = 0; stalledRetries = 0;
        }
        bool Update(Action next, float elapsed, float distance, float aid) {
            const bool changed = !issued || action != next || elapsed < lastObserved;
            lastObserved = elapsed;
            if (!changed && (distance < bestDistance - 1.0f || aid < bestAid - .0001f)) {
                if (distance < bestDistance) bestDistance = distance;
                if (aid < bestAid) bestAid = aid;
                lastProgress = elapsed; stalledRetries = 0;
            }
            if (!changed && (next == Hold || next == WaitForBed || next == Done ||
                elapsed - lastProgress < (next == Gather ? 30.0f : 10.0f))) return false;
            if (changed) stalledRetries = 0; else ++stalledRetries;
            action = next; issued = true;
            bestDistance = distance; bestAid = aid; lastProgress = elapsed;
            return true;
        }
    };
    inline bool MedicPollDue(unsigned long lastTick, unsigned long now) {
        return lastTick == 0 || now < lastTick || now - lastTick >= 1000;
    }
    inline bool ClearExternalReservations(bool worldTeardown) { return worldTeardown; }
}
