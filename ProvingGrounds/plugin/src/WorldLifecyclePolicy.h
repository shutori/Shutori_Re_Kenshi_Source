#pragma once

namespace WorldLifecyclePolicy
{
    struct ArenaActivity
    {
        bool ingressPending;
        bool walkInPending;
        bool matchActive;
        bool prisonerActivity;
        bool townActivity;
    };

    inline bool ShouldPostponeAutosave(const ArenaActivity& activity)
    {
        return activity.ingressPending ||
            activity.walkInPending ||
            activity.matchActive ||
            activity.prisonerActivity || activity.townActivity;
    }
}
