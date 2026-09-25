#pragma once

#include <cmath>

class Building;

// Keeps fighters inside the arena they are fighting in.
//
// The engine's crowding repulsion (FlockingTools, reached through CharMovement)
// presses a fighter against the wall until the collision step loses them and they
// pass through it -- seen live as a weaker fighter ground into the wall by a
// stronger one. So a fighter pressed against the edge is steered back in while
// still fighting, and one already through it is returned to the ring.
//
// Covers every arena a bout can be run in -- the town Crucible and the player-run
// arena, which are the same size -- because they run the same SparSession. It is
// not town-specific. The banner is excluded: a banner is boundless, with no wall
// to contain anyone.
namespace ArenaRingGuard
{
    // ---- pure arithmetic: no engine calls, so a reader can check the geometry ----

    enum RingZone { RingInside, RingNearEdge, RingOutside };

    inline RingZone ClassifyRing(float distance, float nearRadius, float outRadius)
    {
        if (distance >= outRadius) return RingOutside;
        if (distance >= nearRadius) return RingNearEdge;
        return RingInside;
    }

    // Pull a fighter straight in along their own radius. Aiming at the centre
    // itself would stack the whole bout on one point and reset the fight's
    // geometry; keeping the angle preserves where they were standing in the ring.
    inline void RingReturnPoint(float dx, float dz, float targetRadius, float& x, float& z)
    {
        const float length = std::sqrt(dx * dx + dz * dz);
        if (length > 1.0f)
        {
            x = dx * targetRadius / length;
            z = dz * targetRadius / length;
        }
        else
        {
            // Exactly on the centre: any direction is equally inward.
            x = targetRadius;
            z = 0.0f;
        }
    }

    // Both corrections are throttled, so a fighter held against the wall is not
    // re-ordered every frame. Wrap-safe: the unsigned difference handles rollover.
    inline bool RingCorrectionDue(unsigned int now, unsigned int last, unsigned int intervalMs)
    {
        return last == 0 || (now - last) >= intervalMs;
    }

    // ---- engine surface ----

    // An arena registers itself when it arms a bout, because only the arming code
    // knows which building the ring is. The radii scale by the ARENA's size, which
    // is a property of the site and not of its staging layout: the player-run arena
    // places its formation at a tighter layout factor than the town Crucible while
    // being the same size, so scaling the ring by the layout factor would draw the
    // ring 30% too small there. Only the small arena is a different size.
    //
    // Nothing is re-derived from the participants on the tick: recomputing the site
    // from them would drift from the one the bout was actually staged on.
    void SetArena(Building* site);

    void Tick();
}
