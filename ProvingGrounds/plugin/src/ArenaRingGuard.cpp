#include "ArenaRingGuard.h"
#include "ArenaIdentity.h"
#include "BalanceTuning.h"
#include "PGLog.h"
#include "SparSession.h"

#include <cstdio>

#pragma warning(push)
#pragma warning(disable: 4091)
#include <Windows.h>
#include <kenshi/Building/Building.h>
#include <kenshi/Character.h>
#include <kenshi/CharMovement.h>
#include <kenshi/util/hand.h>
#include <ogre/OgreVector3.h>
#pragma warning(pop)

namespace
{
    // The small arena is the only arena of a different size. This is an arena-size
    // factor and deliberately NOT ArenaIngress::kSmallArenaScale (0.5), which is the
    // factor its staging formation is placed at -- the two are independent, exactly
    // as the player-run arena's 0.70 layout factor is independent of its size.
    const float kSmallArenaRingScale = 0.6f;

    // A hand, not a raw pointer: the site can be destroyed while a bout is being
    // torn down, and a dangling Building* would crash the tick rather than skip it.
    hand g_site;
    float g_ringScale = 0.0f;
    unsigned int g_nearAt = 0;
    unsigned int g_outAt = 0;

    // One correction per second per kind, throttled per PASS rather than per fighter
    // so two fighters out at once are both corrected in the same pass.
    const unsigned int kCorrectionIntervalMs = 1000u;

    // The arena's size, from the site itself. 0 means "no ring here", which makes
    // the guard inert rather than guessing a radius for a site it does not know.
    float RingScaleForSite(Building* site)
    {
        if (!site) return 0.0f;
        if (ArenaIdentity::IsBanner(site)) return 0.0f;
        if (ArenaIdentity::IsSmallArena(site)) return kSmallArenaRingScale;
        // The town Crucible and the player-run arena are the same size, so the
        // tuned radii apply to both unchanged.
        return 1.0f;
    }
}

namespace ArenaRingGuard
{
    void SetArena(Building* site)
    {
        g_site = site;
        g_ringScale = RingScaleForSite(site);
        g_nearAt = 0;
        g_outAt = 0;
    }

    void Tick()
    {
        // A bout is live in either arena at the moment combat starts -- the town bout
        // from its own OnMatchStarted, the player-run arena from FinishIngressAndStart
        // once the walk-in completes -- so this one predicate covers both, and excludes
        // staging, when fighters approach from outside the ring by design.
        if (!SparSession::IsActive()) return;
        if (g_ringScale <= 0.0f) return;
        if (!g_site.isValid()) return;
        Building* site = g_site.getBuilding();
        if (!site || !site->isValid()) return;

        const BalanceTuning::Values& tuning = BalanceTuning::Get();
        const float nearRadius = static_cast<float>(tuning.ringNearRadius) * g_ringScale;
        const float outRadius = static_cast<float>(tuning.ringOutRadius) * g_ringScale;
        const float returnRadius = static_cast<float>(tuning.ringReturnRadius) * g_ringScale;
        // Equal near/out thresholds mean the guard is off. The other condition keeps
        // the corrections ordered: the return must land inside the near line or the
        // fighter would be steered again the moment they arrive.
        if (outRadius <= nearRadius || returnRadius >= nearRadius) return;

        const Ogre::Vector3 centre = site->getPosition();
        const unsigned int now = static_cast<unsigned int>(GetTickCount());
        const bool nearDue = RingCorrectionDue(now, g_nearAt, kCorrectionIntervalMs);
        const bool outDue = RingCorrectionDue(now, g_outAt, kCorrectionIntervalMs);
        if (!nearDue && !outDue) return;

        const int count = SparSession::GetParticipantCount();
        bool didNear = false, didOut = false;
        for (int i = 0; i < count; ++i)
        {
            // Participants, not an arena's own fighter list: this is what both arenas
            // share, and it keeps medics and spectators out of scope by construction.
            Character* c = SparSession::GetParticipant(i);
            if (!c || !c->isValid()) continue;
            // An eliminated fighter is aftercare's patient; moving them would fight
            // the medics for the body.
            if (SparSession::IsEliminated(c)) continue;

            CharMovement* movement = c->getMovement();
            if (!movement) continue;

            const Ogre::Vector3 pos = c->getPosition();
            const float dx = pos.x - centre.x;
            const float dz = pos.z - centre.z;
            const float distance = std::sqrt(dx * dx + dz * dz);
            const RingZone zone = ClassifyRing(distance, nearRadius, outRadius);
            if (zone == RingInside) continue;
            const bool outside = zone == RingOutside;
            if (outside ? !outDue : !nearDue) continue;

            float rx = 0.0f, rz = 0.0f;
            RingReturnPoint(dx, dz, returnRadius, rx, rz);
            // Their own height, so a correction never changes floor level.
            const Ogre::Vector3 target(centre.x + rx, pos.y, centre.z + rz);
            char line[224];
            if (outside)
            {
                // Already through the wall: a move order cannot be trusted to path back
                // out of geometry, so set the position directly. Clear the forced
                // waypoint first, so a stale inward steer cannot fight the correction it
                // was a preparation for.
                movement->combatMover.hasForcedWP = false;
                movement->_setPositionSimple(target);
                didOut = true;
                sprintf_s(line, "Proving Grounds: ring guard returned fighter to the ring (dist=%.0f out=%.0f -> %.0f scale=%.2f)",
                    distance, outRadius, returnRadius, g_ringScale);
                PGLog::Debug(line);
            }
            else
            {
                // Still inside: steer, so they keep fighting. A waypoint rather than a
                // move order, which would disengage them from combat.
                movement->combatMover.setForcedWP(target);
                didNear = true;
                sprintf_s(line, "Proving Grounds: ring guard steering fighter inward (dist=%.0f near=%.0f -> %.0f scale=%.2f)",
                    distance, nearRadius, returnRadius, g_ringScale);
                PGLog::Debug(line);
            }
        }
        if (didNear) g_nearAt = now;
        if (didOut) g_outAt = now;
    }
}
