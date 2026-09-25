#pragma once

namespace TownCorpseCleanupPolicy {
    enum Action { Wait, PickUp, Feed };
    enum ReleaseDisposition { ForgetRelease, DeferRelease, ResumeNative };

    inline bool EligibleCorpse(bool registeredFighter, bool valid, bool dead, bool consumed) {
        return registeredFighter && valid && dead && !consumed;
    }

    inline bool HandlerReady(bool valid, bool player, bool dead, bool unconscious,
        bool inCombat, bool carryingSomething) {
        return valid && !player && !dead && !unconscious && !inCombat && !carryingSomething;
    }

    inline bool FurnaceReady(bool valid, bool sameTown, bool complete, bool disabled,
        bool corpseDisposalFunction) {
        return valid && sameTown && complete && !disabled && corpseDisposalFunction;
    }

    inline Action NextAction(bool handlerReady, bool furnaceReady,
        bool carryingTarget, bool carryingOther) {
        if (!furnaceReady || carryingOther || (!handlerReady && !carryingTarget)) return Wait;
        return carryingTarget ? Feed : PickUp;
    }

    inline bool TimedOut(float elapsed, float limit) {
        return limit >= 0.0f && elapsed >= limit;
    }

    inline int SelectCorpse(const bool* eligible, const bool* resolved, int count, int current) {
        if (!eligible || !resolved || count <= 0) return -1;
        if (current >= 0 && current < count && eligible[current] && !resolved[current]) return current;
        for (int i = 0; i < count; ++i) if (eligible[i] && !resolved[i]) return i;
        return -1;
    }

    inline float AdvanceElapsed(float elapsed, float delta, bool paused) {
        if (paused || delta <= 0.0f) return elapsed;
        return elapsed + (delta > 1.0f ? 1.0f : delta);
    }

    inline bool BatchTimedOut(bool started, float elapsed, float limit) {
        return started && TimedOut(elapsed, limit);
    }

    inline ReleaseDisposition ReleaseFor(bool valid, bool player, bool dead,
        bool unconscious, bool inCombat, bool beingCarried, bool needsAid,
        bool carryingSomething) {
        if (!valid || player || dead) return ForgetRelease;
        if (unconscious || inCombat || beingCarried || needsAid || carryingSomething)
            return DeferRelease;
        return ResumeNative;
    }
}
