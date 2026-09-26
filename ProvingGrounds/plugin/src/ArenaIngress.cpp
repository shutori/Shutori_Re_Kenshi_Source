#include "ArenaIngress.h"
#include "ArenaRingGuard.h"
#include "TownArena.h"
#include "TownAnnouncer.h"
#include "TownAnnouncerPolicy.h"
#include "TownBookie.h"
#include "ArenaIngressPolicy.h"
#include "ArenaIdentity.h"
#include "ArenaUI.h"
#include "FightStarter.h"
#include "LeaderboardUI.h"
#include "PrisonerUtil.h"
#include "ResultsUI.h"
#include "SparSession.h"
#include "SquadUtil.h"

#include "PGLog.h"

#include <Windows.h>

#include <cmath>
#include <cstdio>
#include <vector>

#pragma warning(push)
#pragma warning(disable: 4091)
#include <kenshi/Building/Building.h>
#include <kenshi/Character.h>
#include <kenshi/CharMovement.h>
#include <kenshi/Enums.h>
#include <kenshi/GameWorld.h>
#include <kenshi/Globals.h>
#include <kenshi/RootObject.h>
#include <kenshi/util/hand.h>
#include <kenshi/util/lektor.h>
#include <ogre/OgreVector3.h>
#include <ogre/OgreQuaternion.h>
#pragma warning(pop)

#ifndef NULL
#define NULL 0
#endif

namespace
{
    // Pit entry relative to arena building (tune in-game if needed).
    static const Ogre::Vector3 kArenaMarkers[] = {
        Ogre::Vector3(-80.0f, 0.0f,  0.0f),
        Ogre::Vector3(-60.0f, 0.0f,  30.0f),
        Ogre::Vector3(-60.0f, 0.0f, -30.0f),
        Ogre::Vector3( 80.0f, 0.0f,  0.0f),
        Ogre::Vector3( 60.0f, 0.0f,  30.0f),
        Ogre::Vector3( 60.0f, 0.0f, -30.0f),
    };
    static const int kArenaMarkerCount = 6;
    // Further back than pit markers so waiters look like spectators.
    static const Ogre::Vector3 kArenaBenchA[] = {
        Ogre::Vector3(-120.0f, 0.0f,  40.0f),
        Ogre::Vector3(-120.0f, 0.0f,   0.0f),
        Ogre::Vector3(-120.0f, 0.0f, -40.0f),
    };
    static const Ogre::Vector3 kArenaBenchB[] = {
        Ogre::Vector3( 120.0f, 0.0f,  40.0f),
        Ogre::Vector3( 120.0f, 0.0f,   0.0f),
        Ogre::Vector3( 120.0f, 0.0f, -40.0f),
    };
    static const int kArenaBenchCount = 3;
    // Compact arena uses the same layout at half scale. Keep this separate
    // from the full arena values so it can be tuned against the placed model.
    static const float kSmallArenaScale = 0.5f;
    static const float kBannerGatherOffset = 20.0f;
    static const float kBannerBenchExtra = 18.0f;
    static const float kArenaLastStandingInnerRadius = 60.0f;
    static const float kArenaLastStandingOuterRadius = 155.0f;
    static const float kBannerLastStandingInnerRadius = 45.0f;
    static const float kBannerLastStandingOuterRadius = 115.0f;
    static const float kArenaTeamSideOffset = 85.0f;
    static const float kArenaTeamColumnSpacing = 35.0f;
    static const float kArenaTeamRowSpacing = 46.0f;
    static const float kBannerTeamSideOffset = 65.0f;
    static const float kBannerTeamColumnSpacing = 35.0f;
    static const float kBannerTeamRowSpacing = 42.0f;
    static const int kTeamRowsPerColumn = 5;
    static const float kEscortGatherTowardRegistry = 120.0f;
    static const float kEscortGatherFallbackLocalX = -140.0f;
    static const float kEscortSpacing = 18.0f;

    static const float kArrivalRadius = 60.0f;
    static const float kSettleRadius = 120.0f;
    // Fight formations need a tighter gate than walking up to the Registry.
    // Otherwise everyone can still be near the middle when combat is engaged.
    static const float kFormationArrivalRadius = 30.0f;
    static const float kSettleSpeed = 0.35f;
    static const float kSettleHoldSec = 0.8f;
    static const float kIngressTimeoutSec = 20.0f;
    static const float kNpcFormationTimeoutSec = 30.0f;
    // Extend only while every missing NPC is making measurable progress.
    static const float kNpcFormationHardCapSec = 90.0f;
    static const float kNpcFormationProgressLogSec = 10.0f;
    static const float kNpcFormationStallSec = 20.0f;
    static const float kNpcFormationProgressDistance = kFormationArrivalRadius;
    static const float kFightGatherHoldSec = 4.0f;
    static const float kFightCountdownSec = 2.25f;
    // RMB mouse-up cancels orders issued on the same click — wait it out, then keep re-issuing.
    static const float kApproachFirstMoveDelaySec = 0.35f;
    static const float kApproachMoveReissueSec = 0.40f;
    // FCS Instance usenode1024 on Proving Grounds Registry (building-local).
    static const Ogre::Vector3 kRegistryUseNodeLocal(-3.0f, 10.0f, 3.0f);
    // Node sits near mesh/board; nudge outward from building center onto walkable pad.
    static const float kRegistryUseNodeOutwardNudge = 30.0f;
    // Kenshi world units are huge (banner coords ~50k). "Nearby" visually is thousands of units.
    static const float kSiteSearchRadius = 25000.0f;
    static const int kSiteSearchMax = 512;

    enum PendingKind
    {
        PendingNone = 0,
        PendingPrisonerRelease = 1,
        PendingFormation = 2,
        PendingOpenUI = 3,
        PendingCountdown = 4
    };

    hand g_boundRegistry;
    hand g_boundLeaderboard;
    hand g_boundSite;
    ArenaIngress::LocationMode g_locationMode = ArenaIngress::LocationArena;
    bool g_pending = false;
    PendingKind g_pendingKind = PendingNone;
    float g_nextMoveIssueSec = 0.0f;
    float g_elapsedSec = 0.0f;
    float g_settleHoldSec = 0.0f;
    int g_countdownStage = -1;
    DWORD g_lastTick = 0;
    bool g_escortsParkIssued = false;
    MatchRules::MatchMode g_mode = MatchRules::ModeTeamAvB;
    bool g_walkInPending = false;
    Character* g_walkInFighter = NULL;
    Ogre::Vector3 g_walkInTarget = Ogre::Vector3(0.0f, 0.0f, 0.0f);
    float g_walkInNextMoveSec = 0.0f;
    float g_walkInElapsedSec = 0.0f;
    DWORD g_walkInLastTick = 0;
    std::vector<Character*> g_fighters;
    std::vector<MatchRules::MatchTeam> g_teams;
    std::vector<Character*> g_escorts;
    std::vector<Ogre::Vector3> g_targets;
    struct NpcFormationProgress
    {
        float checkpointDistance;
        float lastProgressSec;
        NpcFormationProgress(float distance, float elapsed)
            : checkpointDistance(distance), lastProgressSec(elapsed) {}
    };
    std::vector<NpcFormationProgress> g_npcFormationProgress;
    float g_nextNpcProgressLogSec = 0.0f;
    bool g_npcGraceLogged = false;
    Character* g_uiOpener = NULL;
    std::string g_status = "Idle";
    bool g_issuingManagedMove = false;

    float HorizontalDistSq(const Ogre::Vector3& a, const Ogre::Vector3& b)
    {
        const float dx = a.x - b.x;
        const float dz = a.z - b.z;
        return (dx * dx) + (dz * dz);
    }

    Ogre::Vector3 LocalToWorld(Building* building, const Ogre::Vector3& local)
    {
        Ogre::Vector3 pos = building->getPosition();
        Ogre::Quaternion ori = building->getOrientation();
        return pos + (ori * local);
    }

    Building* FindNearestMatching(
        const Ogre::Vector3& origin,
        bool (*matches)(RootObject*))
    {
        if (!ou || !matches)
            return NULL;

        lektor<RootObject*> results;
        ou->getObjectsWithinSphere(results, origin, kSiteSearchRadius, BUILDING, kSiteSearchMax, NULL);

        Building* best = NULL;
        float bestDist = 1.0e30f;
        int matchCount = 0;
        for (uint32_t i = 0; i < results.size(); ++i)
        {
            RootObject* obj = results[i];
            if (!obj || !obj->isValid() || !matches(obj))
                continue;

            ++matchCount;
            Building* b = static_cast<Building*>(obj);
            const float d = HorizontalDistSq(origin, b->getPosition());
            if (d < bestDist)
            {
                bestDist = d;
                best = b;
            }
        }

        char buf[192];
        sprintf_s(buf, "Proving Grounds: site search hits=%d matched=%d bestDistXZ=%.1f (radius=%.0f)",
            static_cast<int>(results.size()), matchCount,
            best ? sqrtf(bestDist) : -1.0f, kSiteSearchRadius);
        PGLog::Debug(buf);

        return best;
    }

    Ogre::Vector3 ResolveSearchOrigin(Character** fighters, int count)
    {
        if (g_boundRegistry.isValid())
        {
            Building* reg = g_boundRegistry.getBuilding();
            if (reg && reg->isValid())
                return reg->getPosition();
        }
        if (fighters && count > 0 && fighters[0] && fighters[0]->isValid())
            return fighters[0]->getPosition();
        return Ogre::Vector3(0.0f, 0.0f, 0.0f);
    }

    Building* FindSiteForMode(ArenaIngress::LocationMode mode, const Ogre::Vector3& origin)
    {
        if (mode == ArenaIngress::LocationBanner)
            return FindNearestMatching(origin, &ArenaIdentity::IsBanner);
        if (mode == ArenaIngress::LocationSmallArena)
            return FindNearestMatching(origin, &ArenaIdentity::IsSmallArena);
        return FindNearestMatching(origin, &ArenaIdentity::IsArena);
    }

    Ogre::Vector3 ScrambleOffset(
        int slot,
        int count,
        float innerRadius,
        float outerRadius)
    {
        // A golden-angle sunflower pattern fills the pit without obvious rows
        // or a predictable ring. Keeping the centre empty prevents one fighter
        // becoming the natural first target of everyone around them.
        static const float kGoldenAngle = 2.39996323f;
        const int safeCount = count > 0 ? count : 1;
        const float areaFraction =
            (static_cast<float>(slot) + 0.5f) / static_cast<float>(safeCount);
        const float innerSq = innerRadius * innerRadius;
        const float outerSq = outerRadius * outerRadius;
        const float radius = sqrtf(innerSq + areaFraction * (outerSq - innerSq));
        const float angle = (static_cast<float>(slot) * kGoldenAngle) +
            (static_cast<float>(safeCount) * 0.37f);
        return Ogre::Vector3(cosf(angle) * radius, 0.0f, sinf(angle) * radius);
    }

    Ogre::Vector3 TeamFormationOffset(
        MatchRules::MatchTeam team,
        int slot,
        int teamCount,
        float sideOffset,
        float columnSpacing,
        float rowSpacing)
    {
        const int column = slot / kTeamRowsPerColumn;
        const int row = slot % kTeamRowsPerColumn;
        const int firstInColumn = column * kTeamRowsPerColumn;
        const int remaining = teamCount - firstInColumn;
        const int rowsInColumn = remaining < kTeamRowsPerColumn
            ? remaining
            : kTeamRowsPerColumn;
        const float centeredRow = static_cast<float>(row) -
            (static_cast<float>(rowsInColumn - 1) * 0.5f);
        const float side = (team == MatchRules::TeamA) ? -1.0f : 1.0f;
        // Extra columns grow away from the centre. Growing inward made large
        // teams start almost on top of each other.
        const float x = side * (sideOffset + static_cast<float>(column) * columnSpacing);
        return Ogre::Vector3(x, 0.0f, centeredRow * rowSpacing);
    }

    void AssignArenaTargets(
        MatchRules::MatchMode mode,
        MatchRules::MatchTeam* teams,
        int count,
        Building* arena,
        std::vector<Ogre::Vector3>& targets,
        float scale = 1.0f)
    {
        targets.resize(static_cast<size_t>(count));
        const int half = kArenaMarkerCount / 2;

        if (mode == MatchRules::ModeTeams1v1)
        {
            int aSlot = 0;
            int bSlot = 0;
            for (int i = 0; i < count; ++i)
            {
                Ogre::Vector3 local;
                if (teams[i] == MatchRules::TeamA)
                {
                    if (aSlot == 0)
                        local = kArenaMarkers[0] * scale;
                    else
                        local = kArenaBenchA[(aSlot - 1) % kArenaBenchCount] * scale;
                    ++aSlot;
                }
                else
                {
                    if (bSlot == 0)
                        local = kArenaMarkers[half] * scale;
                    else
                        local = kArenaBenchB[(bSlot - 1) % kArenaBenchCount] * scale;
                    ++bSlot;
                }
                targets[static_cast<size_t>(i)] = LocalToWorld(arena, local);
            }
            return;
        }

        if (mode == MatchRules::ModeLastStanding)
        {
            for (int i = 0; i < count; ++i)
            {
                targets[static_cast<size_t>(i)] =
                    LocalToWorld(
                        arena,
                        ScrambleOffset(
                            i,
                            count,
                            kArenaLastStandingInnerRadius * scale,
                            kArenaLastStandingOuterRadius * scale));
            }
            return;
        }

        int teamACount = 0;
        int teamBCount = 0;
        for (int i = 0; i < count; ++i)
        {
            if (teams[i] == MatchRules::TeamA)
                ++teamACount;
            else if (teams[i] == MatchRules::TeamB)
                ++teamBCount;
        }

        int teamASlot = 0;
        int teamBSlot = 0;
        for (int i = 0; i < count; ++i)
        {
            const bool teamA = teams[i] == MatchRules::TeamA;
            const int slot = teamA ? teamASlot++ : teamBSlot++;
            const int teamCount = teamA ? teamACount : teamBCount;
            const Ogre::Vector3 local = TeamFormationOffset(
                teams[i],
                slot,
                teamCount,
                kArenaTeamSideOffset * scale,
                kArenaTeamColumnSpacing * scale,
                kArenaTeamRowSpacing * scale);
            targets[static_cast<size_t>(i)] = LocalToWorld(arena, local);
        }
    }

    void AssignBannerTargets(
        MatchRules::MatchMode mode,
        MatchRules::MatchTeam* teams,
        int count,
        Building* banner,
        std::vector<Ogre::Vector3>& targets)
    {
        targets.resize(static_cast<size_t>(count));
        const Ogre::Vector3 base = banner->getPosition();

        if (mode == MatchRules::ModeTeams1v1 && teams)
        {
            int aSlot = 0;
            int bSlot = 0;
            for (int i = 0; i < count; ++i)
            {
                const bool teamA = (teams[i] == MatchRules::TeamA);
                const float side = teamA ? -1.0f : 1.0f;
                const int slot = teamA ? aSlot : bSlot;
                const float radius = kBannerGatherOffset + (slot > 0 ? kBannerBenchExtra : 0.0f);
                targets[static_cast<size_t>(i)] = base + Ogre::Vector3(side * radius, 0.0f, static_cast<float>(slot) * 8.0f);
                if (teamA)
                    ++aSlot;
                else
                    ++bSlot;
            }
            return;
        }

        if (mode == MatchRules::ModeLastStanding)
        {
            for (int i = 0; i < count; ++i)
                targets[static_cast<size_t>(i)] =
                    base + ScrambleOffset(
                        i,
                        count,
                        kBannerLastStandingInnerRadius,
                        kBannerLastStandingOuterRadius);
            return;
        }

        int teamACount = 0;
        int teamBCount = 0;
        for (int i = 0; i < count; ++i)
        {
            if (teams[i] == MatchRules::TeamA)
                ++teamACount;
            else if (teams[i] == MatchRules::TeamB)
                ++teamBCount;
        }

        int teamASlot = 0;
        int teamBSlot = 0;
        for (int i = 0; i < count; ++i)
        {
            const bool teamA = teams[i] == MatchRules::TeamA;
            const int slot = teamA ? teamASlot++ : teamBSlot++;
            const int teamCount = teamA ? teamACount : teamBCount;
            targets[static_cast<size_t>(i)] = base + TeamFormationOffset(
                teams[i],
                slot,
                teamCount,
                kBannerTeamSideOffset,
                kBannerTeamColumnSpacing,
                kBannerTeamRowSpacing);
        }
    }

    void IssueMove(Character* c, const Ogre::Vector3& loc)
    {
        if (!c || !c->isValid())
            return;

        // Town package goals can otherwise immediately reclaim a trial fighter.
        if (TownArena::IsFighter(c)) c->clearAllAIGoals();

        // Hold/Passive ignore move tasks; clear so pathing actually starts.
        c->setStandingOrder(MessageForB::M_SET_ORDER_HOLD, false);
        c->setStandingOrder(MessageForB::M_SET_ORDER_PASSIVE, false);

        CharMovement* movement = c->getMovement();
        if (movement)
            movement->setDestination(loc, HIGH_PRIORITY, true);

        g_issuingManagedMove = true;
        c->addOrder(NULL, MOVE_CUS_ORDERED, NULL, false, true, loc);
        g_issuingManagedMove = false;
        c->setDestination(loc, false);
        if (movement && TownArena::IsFighter(c))
        {
            movement->setDesiredSpeedOrders(RUN);
            movement->setDesiredSpeed(RUN);
        }
    }

    void IssueRun(Character* c, const Ogre::Vector3& loc)
    {
        IssueMove(c, loc);
        if (!c || !c->isValid())
            return;

        CharMovement* movement = c->getMovement();
        if (movement)
        {
            movement->setDesiredSpeedOrders(RUN);
            movement->setDesiredSpeed(RUN);
        }
    }

    void IssueApproachMoves()
    {
        const size_t count = g_fighters.size();
        for (size_t i = 0; i < count; ++i)
            IssueMove(g_fighters[i], g_targets[i]);

        PGLog::Debug("Proving Grounds: approach move (re)issued");
    }

    // Walk to interact point. Prefer Kenshi usage/position marker (exposed use node),
    // fall back to Registry-style local usenode offset + outward nudge.
    Ogre::Vector3 RegistryApproachPoint(Building* building, Character* from)
    {
        if (!building)
            return Ogre::Vector3(0.0f, 0.0f, 0.0f);

        if (from && from->isValid())
        {
            const Ogre::Vector3 marker = building->getPositionMarker(from->getPosition());
            const Ogre::Vector3 center = building->getPosition();
            const Ogre::Vector3 delta = marker - center;
            const float lenSq = (delta.x * delta.x) + (delta.y * delta.y) + (delta.z * delta.z);
            // Marker valid when it isn't stuck at the building origin.
            if (lenSq > 1.0f)
                return marker;
        }

        Ogre::Vector3 world = LocalToWorld(building, kRegistryUseNodeLocal);
        const Ogre::Vector3 center = building->getPosition();
        Ogre::Vector3 dir = world - center;
        dir.y = 0.0f;
        const float lenSq = (dir.x * dir.x) + (dir.z * dir.z);
        if (lenSq > 1.0f)
        {
            dir /= sqrtf(lenSq);
            world.x += dir.x * kRegistryUseNodeOutwardNudge;
            world.z += dir.z * kRegistryUseNodeOutwardNudge;
        }
        return world;
    }

    Building* GetBoundLeaderboard()
    {
        if (!g_boundLeaderboard.isValid())
            return NULL;
        Building* b = g_boundLeaderboard.getBuilding();
        if (b && ArenaIdentity::IsLeaderboard(b))
            return b;
        RootObject* obj = g_boundLeaderboard.getRootObject();
        if (obj && ArenaIdentity::IsLeaderboard(obj))
            return static_cast<Building*>(obj);
        return NULL;
    }

    bool HasValidLeaderboard()
    {
        Building* board = GetBoundLeaderboard();
        return board && board->isValid() && ArenaIdentity::IsLeaderboard(board);
    }

    bool HasValidApproachBuilding()
    {
        return (ArenaIngress::HasValidRegistry() &&
                ArenaIdentity::IsMatchUiOpenerFinished(ArenaIngress::GetBoundRegistry()))
            || (HasValidLeaderboard() &&
                ArenaIdentity::IsLeaderboardFinished(GetBoundLeaderboard()));
    }

    void BindLeaderboardBuilding(RootObject* board)
    {
        if (!board || !ArenaIdentity::IsLeaderboard(board))
            return;
        // Same assignment style as BindRegistry (RootObject* → hand).
        g_boundLeaderboard = board;
        g_boundRegistry.setNull();
    }

    Ogre::Vector3 EscortGatherBase(Building* site)
    {
        if (!site)
            return Ogre::Vector3(0.0f, 0.0f, 0.0f);

        const Ogre::Vector3 sitePos = site->getPosition();
        Building* registry = ArenaIngress::GetBoundRegistry();
        if (registry && registry->isValid())
        {
            Ogre::Vector3 dir = registry->getPosition() - sitePos;
            dir.y = 0.0f;
            const float lenSq = (dir.x * dir.x) + (dir.z * dir.z);
            if (lenSq > 1.0f)
            {
                const float invLen = 1.0f / sqrtf(lenSq);
                dir.x *= invLen;
                dir.z *= invLen;
                return sitePos + (dir * kEscortGatherTowardRegistry);
            }
        }

        if (g_locationMode == ArenaIngress::LocationBanner)
            return sitePos + Ogre::Vector3(0.0f, 0.0f, -kEscortGatherTowardRegistry);

        return LocalToWorld(site, Ogre::Vector3(kEscortGatherFallbackLocalX, 0.0f, 0.0f));
    }

    void IssueEscortMoves(Building* site, Character** escorts, int escortCount)
    {
        if (!escorts || escortCount <= 0)
            return;

        const Ogre::Vector3 base = EscortGatherBase(site);
        for (int i = 0; i < escortCount; ++i)
        {
            Character* escort = escorts[i];
            if (!escort || !escort->isValid())
                continue;

            const int col = i % 3;
            const int row = i / 3;
            const Ogre::Vector3 offset(
                (static_cast<float>(col) - 1.0f) * kEscortSpacing,
                0.0f,
                static_cast<float>(row) * kEscortSpacing);
            IssueMove(escort, base + offset);
        }
    }

    void ParkCharacterInPlace(Character* c)
    {
        if (!c || !c->isValid() || c->isDead())
            return;

        c->removeJob(MOVE_CUS_ORDERED);
        c->clearAllAIGoals();
        c->setStandingOrder(MessageForB::M_SET_ORDER_PASSIVE, true);
        c->setStandingOrder(MessageForB::M_SET_ORDER_HOLD, true);
        c->setStandingOrder(MessageForB::M_SET_ORDER_AGG, false);
        if (PrisonerUtil::IsMatchPrisoner(c) || TownArena::IsFighter(c))
        {
            // HOLD is only a player standing order; released NPC packages can
            // immediately rebuild Patrolling after the rethink below. Give
            // prisoner fighters an explicit active goal for staging instead.
            c->addGoal(STAND_STILL, NULL);
        }
        c->reThinkCurrentAIAction();
    }

    bool FighterAtMark(size_t index, float radius)
    {
        if (index >= g_fighters.size() || index >= g_targets.size())
            return false;
        Character* c = g_fighters[index];
        if (!c || !c->isValid())
            return false;
        const float radiusSq = radius * radius;
        return HorizontalDistSq(c->getPosition(), g_targets[index]) <= radiusSq;
    }

    void FaceOpposingFormation(size_t index)
    {
        if (index >= g_fighters.size() || g_teams.size() != g_targets.size())
            return;
        Character* fighter = g_fighters[index];
        if (!fighter || !fighter->isValid() || fighter->isDead())
            return;

        // Use the opponent's assigned mark even before they arrive; the marks
        // already account for the arena's orientation in world space.
        size_t closest = g_targets.size();
        float distance = 1.0e30f;
        for (size_t other = 0; other < g_targets.size(); ++other)
        {
            if (other == index ||
                (g_mode != MatchRules::ModeLastStanding && g_teams[other] == g_teams[index]))
                continue;
            const float candidate = HorizontalDistSq(fighter->getPosition(), g_targets[other]);
            if (candidate < distance)
            {
                distance = candidate;
                closest = other;
            }
        }
        if (closest == g_targets.size() || distance <= 1.0f)
            return;
        Ogre::Vector3 face = g_targets[closest];
        face.y = fighter->getPosition().y;
        fighter->lookatPosition(face, true);
    }

    void ParkArrivedFighters()
    {
        for (size_t i = 0; i < g_fighters.size(); ++i)
        {
            Character* fighter = g_fighters[i];
            if (!fighter || !fighter->isValid())
                continue;
            if (PrisonerUtil::IsMatchPrisoner(fighter) &&
                !PrisonerUtil::IsMatchPrisonerReleased(fighter))
            {
                continue;
            }
            if (!FighterAtMark(i, kFormationArrivalRadius))
                continue;

            ParkCharacterInPlace(fighter);
            FaceOpposingFormation(i);
        }
    }

    int CountFightersOutsideFormation()
    {
        int outside = 0;
        for (size_t i = 0; i < g_fighters.size(); ++i)
        {
            Character* fighter = g_fighters[i];
            if (!fighter || !fighter->isValid() ||
                !FighterAtMark(i, kFormationArrivalRadius))
            {
                ++outside;
            }
        }
        return outside;
    }

    void LogNpcFormationProgress(const char* phase)
    {
        for (size_t i = 0; i < g_fighters.size() && i < g_targets.size(); ++i)
        {
            Character* fighter = g_fighters[i];
            if (!fighter || !fighter->isValid()) continue;
            const Ogre::Vector3 position = fighter->getPosition();
            const float horizontal = sqrtf(HorizontalDistSq(position, g_targets[i]));
            CharMovement* movement = fighter->getMovement();
            const float progressAge = i < g_npcFormationProgress.size()
                ? g_elapsedSec - g_npcFormationProgress[i].lastProgressSec : g_elapsedSec;
            char detail[768];
            sprintf_s(detail,
                "Proving Grounds: NPC formation %s elapsed=%.1f slot=%u name=%s horizontal=%.1f vertical=%.1f atMark=%d progressAge=%.1f moving=%d speed=%.2f",
                phase, g_elapsedSec, static_cast<unsigned>(i + 1), fighter->getName().c_str(),
                horizontal, position.y - g_targets[i].y,
                FighterAtMark(i, kFormationArrivalRadius) ? 1 : 0, progressAge,
                movement && movement->isCurrentlyMoving() ? 1 : 0, fighter->getMovementSpeed());
            PGLog::Debug(detail);
        }
    }

    void BeginNpcFormationProgress()
    {
        g_npcFormationProgress.clear();
        if (!TownArena::IsBusy() || TownArena::IsPlayerMatch() ||
            g_pendingKind != PendingFormation) return;
        for (size_t i = 0; i < g_fighters.size(); ++i)
        {
            Character* fighter = g_fighters[i];
            const float distance = fighter && fighter->isValid()
                ? sqrtf(HorizontalDistSq(fighter->getPosition(), g_targets[i])) : 0.0f;
            g_npcFormationProgress.push_back(NpcFormationProgress(distance, g_elapsedSec));
        }
        g_nextNpcProgressLogSec = g_elapsedSec + kNpcFormationProgressLogSec;
        g_npcGraceLogged = false;
        LogNpcFormationProgress("start");
    }

    void UpdateNpcFormationProgress()
    {
        if (!TownArena::IsBusy() || TownArena::IsPlayerMatch() ||
            g_pendingKind != PendingFormation) return;
        if (g_npcFormationProgress.size() != g_fighters.size())
            BeginNpcFormationProgress();
        for (size_t i = 0; i < g_npcFormationProgress.size(); ++i)
        {
            Character* fighter = g_fighters[i];
            if (!fighter || !fighter->isValid()) continue;
            const float distance = sqrtf(HorizontalDistSq(fighter->getPosition(), g_targets[i]));
            NpcFormationProgress& progress = g_npcFormationProgress[i];
            if (distance <= progress.checkpointDistance - kNpcFormationProgressDistance)
            {
                progress.checkpointDistance = distance;
                progress.lastProgressSec = g_elapsedSec;
            }
        }
        if (g_elapsedSec >= g_nextNpcProgressLogSec)
        {
            LogNpcFormationProgress("sample");
            g_nextNpcProgressLogSec = g_elapsedSec + kNpcFormationProgressLogSec;
        }
    }

    void IssueFormationMoves()
    {
        for (size_t i = 0; i < g_fighters.size(); ++i)
        {
            Character* fighter = g_fighters[i];
            if (!fighter || !fighter->isValid() || fighter->isDead())
                continue;

            const bool prisoner = PrisonerUtil::IsMatchPrisoner(fighter);
            if (prisoner && !PrisonerUtil::IsMatchPrisonerReleased(fighter))
                continue;
            if (FighterAtMark(i, kFormationArrivalRadius))
                continue;

            if (prisoner)
            {
                PrisonerUtil::SendPairToArena(fighter, g_targets[i]);
            }
            else
            {
                IssueMove(fighter, g_targets[i]);
            }
        }
    }

    void ClearPendingState(bool restoreFighterOrders = true)
    {
        const bool restoreFightState =
            restoreFighterOrders &&
            (g_pendingKind == PendingPrisonerRelease ||
             g_pendingKind == PendingFormation ||
             g_pendingKind == PendingCountdown);
        for (size_t i = 0; i < g_fighters.size(); ++i)
        {
            Character* c = g_fighters[i];
            if (c && c->isValid())
                c->removeJob(MOVE_CUS_ORDERED);
        }
        for (size_t i = 0; i < g_escorts.size(); ++i)
        {
            Character* c = g_escorts[i];
            if (c && c->isValid())
                c->removeJob(MOVE_CUS_ORDERED);
        }
        if (restoreFightState && !g_fighters.empty())
        {
            FightStarter::DisengageMatch(
                g_fighters.data(),
                static_cast<int>(g_fighters.size()));
        }
        TownAnnouncer::Release();
        g_pending = false;
        g_pendingKind = PendingNone;
        g_nextMoveIssueSec = 0.0f;
        g_elapsedSec = 0.0f;
        g_settleHoldSec = 0.0f;
        g_countdownStage = -1;
        g_lastTick = 0;
        g_escortsParkIssued = false;
        g_boundSite.setNull();
        g_fighters.clear();
        g_teams.clear();
        g_escorts.clear();
        g_targets.clear();
    }

    void ShowFighterCountdown(int stage, const char* text)
    {
        if (stage == g_countdownStage || !text)
            return;

        g_countdownStage = stage;
        const std::string line(text);
        for (size_t i = 0; i < g_fighters.size(); ++i)
        {
            Character* fighter = g_fighters[i];
            if (fighter && fighter->isValid())
                fighter->say(line);
        }
    }

    void BeginFightCountdown()
    {
        g_pendingKind = PendingCountdown;
        g_elapsedSec = 0.0f;
        g_countdownStage = -1;
        g_lastTick = GetTickCount();
        g_status = TownAnnouncer::HasAnnouncement()
            ? "The announcer takes the rail"
            : "Fight starts in 3...";
        ParkArrivedFighters();
        for (size_t i = 0; i < g_fighters.size(); ++i)
        {
            ParkCharacterInPlace(g_fighters[i]);
            FaceOpposingFormation(i);
        }
        if (g_pendingKind == PendingPrisonerRelease)
            PrisonerUtil::ParkMatchHandlers();
        if (TownAnnouncer::HasAnnouncement())
            TownAnnouncer::BeginAnnouncement();
        else
            ShowFighterCountdown(3, "3");
        PGLog::Debug("Proving Grounds: pre-fight countdown started");
    }

    ArenaIngressPolicy::IngressObservation BuildIngressObservation()
    {
        ArenaIngressPolicy::IngressObservation observation = {};
        observation.allPrisonersReleased = PrisonerUtil::AllMatchPrisonersReleased();
        observation.allFightersValid = true;
        observation.allAtArrivalRadius = observation.allPrisonersReleased;
        observation.allAtSettleRadius = observation.allPrisonersReleased;
        observation.allAtFormationRadius = observation.allPrisonersReleased;
        observation.allSettled = observation.allPrisonersReleased;

        const float arrivalRadiusSq = kArrivalRadius * kArrivalRadius;
        const float settleRadiusSq = kSettleRadius * kSettleRadius;
        const float formationRadiusSq =
            kFormationArrivalRadius * kFormationArrivalRadius;
        for (size_t i = 0; i < g_fighters.size(); ++i)
        {
            Character* c = g_fighters[i];
            if (!c || !c->isValid())
            {
                observation.allFightersValid = false;
                observation.allAtArrivalRadius = false;
                observation.allAtSettleRadius = false;
                observation.allAtFormationRadius = false;
                observation.allSettled = false;
                continue;
            }

            const float distanceSq = HorizontalDistSq(c->getPosition(), g_targets[i]);
            if (distanceSq > arrivalRadiusSq)
                observation.allAtArrivalRadius = false;
            if (distanceSq > settleRadiusSq)
                observation.allAtSettleRadius = false;
            if (distanceSq > formationRadiusSq)
                observation.allAtFormationRadius = false;
            if (c->getMovementSpeed() > kSettleSpeed)
                observation.allSettled = false;
        }
        return observation;
    }

    bool AnyFighterInvalid()
    {
        const int count = static_cast<int>(g_fighters.size());
        for (int i = 0; i < count; ++i)
        {
            Character* c = g_fighters[static_cast<size_t>(i)];
            if (!c || !c->isValid())
                return true;
        }
        return false;
    }

    bool HasValidSite()
    {
        if (!g_boundSite.isValid())
            return false;
        Building* b = g_boundSite.getBuilding();
        return b && b->isValid();
    }

    void FinishIngressAndStart()
    {
        // Ingress ticks before TownArena. Revalidate the locked NPCs before
        // clearing staging ownership or allowing StartMatch to begin combat.
        if (!TownArena::ValidatePreparation()) return;
        const int count = static_cast<int>(g_fighters.size());
        MatchRules::MatchMode mode = g_mode;
        std::vector<Character*> fighters = g_fighters;
        std::vector<MatchRules::MatchTeam> teams = g_teams;

        ClearPendingState(false);

        if (count < 2 || fighters.empty() || teams.empty())
        {
            g_status = "Ingress failed: no fighters";
            return;
        }

        const bool ok = SparSession::StartMatch(mode, fighters.data(), teams.data(), count);
        g_status = SparSession::GetStatus();
        if (!ok)
        {
            // ClearPendingState(false) transferred ownership to StartMatch.
            // A rejected start must still release the saved staging orders.
            FightStarter::DisengageMatch(fighters.data(), count);
            PGLog::Debug(("Proving Grounds: ingress StartMatch failed: " + g_status).c_str());
        }
        else if (PrisonerUtil::MatchIncludesPrisoner())
            PrisonerUtil::ParkMatchHandlers();
    }

    void FinishApproachAndOpenUI()
    {
        Building* registry = ArenaIngress::GetBoundRegistry();
        Building* board = GetBoundLeaderboard();
        ClearPendingState();

        if (registry && ArenaIdentity::IsMatchUiOpenerFinished(registry))
        {
            ArenaUI::ShowFromRegistry(registry);
            g_status = ArenaIngress::GetStatus();
            PGLog::Debug("Proving Grounds: approach complete -> Arena UI");
            return;
        }

        LeaderboardData::Kind leaderboard = LeaderboardData::Player;
        if (board && ArenaIdentity::GetFinishedLeaderboardKind(
                board, leaderboard))
        {
            LeaderboardUI::Show(leaderboard, board);
            g_status = leaderboard == LeaderboardData::Town
                ? "Town leaderboard open"
                : "Player leaderboard open";
            PGLog::Debug(leaderboard == LeaderboardData::Town
                ? "Proving Grounds: approach complete -> Town Leaderboard UI"
                : "Proving Grounds: approach complete -> Player Leaderboard UI");
            return;
        }

        g_status = "Interact building lost during approach";
    }

    bool OpenInteractUiImmediately(Building* building)
    {
        if (ArenaIdentity::IsMatchUiOpenerFinished(building))
        {
            ArenaUI::ShowFromRegistry(
                building,
                ArenaIngressPolicy::ShouldBindInteractSource(true));
            PGLog::Debug("Proving Grounds: busy match Registry/Banner UI opened immediately");
            return true;
        }

        LeaderboardData::Kind leaderboard = LeaderboardData::Player;
        if (ArenaIdentity::GetFinishedLeaderboardKind(building, leaderboard))
        {
            LeaderboardUI::Show(leaderboard, building);
            PGLog::Debug(leaderboard == LeaderboardData::Town
                ? "Proving Grounds: busy match Town Leaderboard UI opened immediately"
                : "Proving Grounds: busy match Player Leaderboard UI opened immediately");
            return true;
        }
        return false;
    }
}

namespace ArenaIngress
{
    void AbandonWorldState()
    {
        TownAnnouncer::AbandonWorldState();
        g_boundRegistry.setNull();
        g_boundLeaderboard.setNull();
        g_boundSite.setNull();
        g_pending = false;
        g_pendingKind = PendingNone;
        g_nextMoveIssueSec = 0.0f;
        g_elapsedSec = 0.0f;
        g_settleHoldSec = 0.0f;
        g_countdownStage = -1;
        g_lastTick = 0;
        g_escortsParkIssued = false;
        g_walkInPending = false;
        g_walkInFighter = NULL;
        g_walkInTarget = Ogre::Vector3(0.0f, 0.0f, 0.0f);
        g_walkInNextMoveSec = 0.0f;
        g_walkInElapsedSec = 0.0f;
        g_walkInLastTick = 0;
        g_fighters.clear();
        g_teams.clear();
        g_escorts.clear();
        g_targets.clear();
        g_uiOpener = NULL;
        g_issuingManagedMove = false;
        g_status = "Idle";
    }

    void BindRegistry(RootObject* registry)
    {
        if (!ArenaIdentity::IsMatchUiOpener(registry))
            return;

        g_boundRegistry = registry;
        g_boundLeaderboard.setNull();
        if (ArenaIdentity::IsBanner(registry))
        {
            g_locationMode = LocationBanner;
            g_status = "Bound to banner";
        }
        else
        {
            g_status = "Bound to registry";
        }
    }

    void ClearBind()
    {
        g_boundRegistry.setNull();
        g_boundLeaderboard.setNull();
        g_boundSite.setNull();
        g_status = "Unbound";
    }

    bool HasValidRegistry()
    {
        if (!g_boundRegistry.isValid())
            return false;
        Building* building = g_boundRegistry.getBuilding();
        if (!building || !building->isValid())
            return false;
        return ArenaIdentity::IsMatchUiOpener(building);
    }

    Building* GetBoundRegistry()
    {
        if (!HasValidRegistry())
            return NULL;
        return g_boundRegistry.getBuilding();
    }

    Character* GetUiOpener()
    {
        if (g_uiOpener && g_uiOpener->isValid())
            return g_uiOpener;
        return NULL;
    }

    void SetLocationMode(LocationMode mode)
    {
        g_locationMode = mode;
    }

    LocationMode GetLocationMode()
    {
        return g_locationMode;
    }

    bool HasLocationSite()
    {
        Ogre::Vector3 origin(0.0f, 0.0f, 0.0f);
        if (HasValidRegistry())
        {
            origin = GetBoundRegistry()->getPosition();
        }
        else
        {
            std::vector<Character*> squad;
            SquadUtil::CollectPlayerSquad(squad);
            if (squad.empty() || !squad[0])
                return false;
            origin = squad[0]->getPosition();
        }
        return FindSiteForMode(g_locationMode, origin) != NULL;
    }

    const char* GetMissingLocationMessage()
    {
        if (g_locationMode == LocationBanner)
            return "No banner placed near registry";
        if (g_locationMode == LocationSmallArena)
            return "No small arena placed near registry";
        return "No arena placed near registry";
    }

    bool IsPending()
    {
        return g_pending;
    }

    const std::string& GetStatus()
    {
        return g_status;
    }

    void NotifyExternalOrder(Character* fighter)
    {
        if (g_issuingManagedMove || !g_pending || g_pendingKind != PendingOpenUI)
            return;

        if (fighter)
        {
            bool isApproaching = false;
            for (size_t i = 0; i < g_fighters.size(); ++i)
            {
                if (g_fighters[i] == fighter)
                {
                    isApproaching = true;
                    break;
                }
            }
            if (!isApproaching)
                return;
        }

        ClearPendingState();
        g_status = "Approach cancelled by new order";
        PGLog::Debug("Proving Grounds: UI approach cancelled by player order");
    }

    bool BeginApproachForUI(Building* building)
    {
        const bool isMatchUiOpener = ArenaIdentity::IsMatchUiOpenerFinished(building);
        const bool isBanner = isMatchUiOpener && ArenaIdentity::IsBanner(building);
        const bool isLeaderboard = ArenaIdentity::IsLeaderboardFinished(building);
        if (!isMatchUiOpener && !isLeaderboard)
        {
            g_status = "Interact building not finished";
            return false;
        }
        const bool fightIngressPending = g_pending &&
            (g_pendingKind == PendingPrisonerRelease ||
             g_pendingKind == PendingFormation ||
             g_pendingKind == PendingCountdown);
        if (ArenaIngressPolicy::ShouldOpenInteractUiImmediately(
                SparSession::IsActive(), fightIngressPending, g_walkInPending))
        {
            return OpenInteractUiImmediately(building);
        }
        // Multi-select use fires addOrder once per character — do not restart.
        if (g_pending && g_pendingKind == PendingOpenUI)
        {
            Building* boundRegistry = GetBoundRegistry();
            Building* boundBoard = GetBoundLeaderboard();
            if (boundRegistry == building || boundBoard == building)
            {
                g_status = isLeaderboard
                    ? "Walking to leaderboard..."
                    : (isBanner ? "Walking to banner..." : "Walking to registry...");
                return true;
            }
            ClearPendingState();
        }

        std::vector<Character*> selected;
        SquadUtil::CollectSelectedCharacters(selected);
        if (selected.empty())
        {
            g_status = "No characters selected";
            return false;
        }

        if (isMatchUiOpener)
            BindRegistry(building);
        else
            BindLeaderboardBuilding(building);

        g_fighters = selected;
        g_uiOpener = selected[0];
        g_teams.clear();
        g_targets.resize(selected.size());
        for (size_t i = 0; i < selected.size(); ++i)
            g_targets[i] = RegistryApproachPoint(building, selected[i]);

        g_pending = true;
        g_pendingKind = PendingOpenUI;
        // Don't move on the RMB frame — Kenshi cancels that order on mouse-up (~0.1s).
        g_elapsedSec = 0.0f;
        g_nextMoveIssueSec = kApproachFirstMoveDelaySec;
        g_settleHoldSec = 0.0f;
        g_lastTick = GetTickCount();
        g_status = isLeaderboard
            ? "Walking to leaderboard..."
            : (isBanner ? "Walking to banner..." : "Walking to registry...");
        PGLog::Debug(isLeaderboard
            ? "Proving Grounds: approach Leaderboard for UI started"
            : (isBanner
                ? "Proving Grounds: approach Banner for UI started"
                : "Proving Grounds: approach Registry for UI started"));

        if (ArenaIngressPolicy::CanOpenApproachUi(BuildIngressObservation()))
        {
            FinishApproachAndOpenUI();
            return true;
        }

        return true;
    }

    bool Begin(MatchRules::MatchMode mode, Character** fighters, MatchRules::MatchTeam* teams, int count)
    {
        return Begin(mode, fighters, teams, count, NULL, 0);
    }

    bool Begin(
        MatchRules::MatchMode mode,
        Character** fighters,
        MatchRules::MatchTeam* teams,
        int count,
        Character** escorts,
        int escortCount,
        Building* siteOverride)
    {
        if (TownArena::IsAftercare())
        {
            g_status = "Town medics are clearing the arena";
            return false;
        }
        if (!fighters || !teams || count < 2)
        {
            g_status = "Need at least two fighters";
            return false;
        }

        for (int i = 0; i < count; ++i)
        {
            if (!fighters[i])
            {
                g_status = "Invalid fighter selection";
                return false;
            }
        }

        if (SparSession::IsActive())
        {
            g_status = "Already sparring";
            return false;
        }

        if (g_pending && g_pendingKind == PendingOpenUI)
            Cancel();

        if (g_pending)
        {
            g_status = "Already walking to fight site";
            return false;
        }

        const Ogre::Vector3 origin = ResolveSearchOrigin(fighters, count);
        Building* site = siteOverride ? siteOverride : FindSiteForMode(g_locationMode, origin);
        if (siteOverride && (!siteOverride->isValid() || !ArenaIdentity::IsArena(siteOverride)))
        {
            g_status = "Invalid town arena";
            return false;
        }
        if (!site)
        {
            g_status = GetMissingLocationMessage();
            return false;
        }

        g_mode = mode;
        g_fighters.assign(fighters, fighters + count);
        g_teams.assign(teams, teams + count);
        g_escorts.clear();
        if (escorts && escortCount > 0)
        {
            for (int i = 0; i < escortCount; ++i)
            {
                Character* escort = escorts[i];
                if (escort && escort->isValid())
                    g_escorts.push_back(escort);
            }
        }
        g_boundSite = site;
        // Jobs off as soon as match start begins walking people in.
        FightStarter::RememberMatchFighters(fighters, count);
        if (escorts && escortCount > 0)
            FightStarter::RememberMatchFighters(escorts, escortCount);

        ResultsUI::Cancel();
        ResultsUI::Close();

        if (g_locationMode == LocationBanner)
            AssignBannerTargets(mode, teams, count, site, g_targets);
        else {
            const float arenaScale = g_locationMode == LocationSmallArena
                ? kSmallArenaScale
                : (!TownArena::IsBusy() ? 0.70f : 1.0f);
            AssignArenaTargets(
                mode, teams, count, site, g_targets,
                arenaScale);
        }

        // Register the ring the fighters will be contained in. This is the one place
        // that knows both the site and whether the bout is a banner, so the guard
        // never has to re-derive either. A banner is boundless: SetArena finds no ring
        // there and the guard goes inert. Note the staging scale above is the
        // LAYOUT's, not the arena's size -- the player-run arena stages at 0.70 while
        // being the same size as the town Crucible.
        ArenaRingGuard::SetArena(site);

        for (int i = 0; i < count; ++i)
        {
            Character* c = g_fighters[static_cast<size_t>(i)];
            // Keep caged prisoners put until their handler unlocks them.
            if (PrisonerUtil::IsMatchPrisoner(c) &&
                !PrisonerUtil::IsMatchPrisonerReleased(c))
            {
                continue;
            }
            // One move order — jobs are already off so they won't wander to sit.
            IssueMove(g_fighters[static_cast<size_t>(i)], g_targets[static_cast<size_t>(i)]);
        }

        if (PrisonerUtil::MatchIncludesPrisoner())
        {
            PrisonerUtil::BeginHandlerUnlocks();
            g_status = "Handlers releasing prisoners...";
        }
        else if (!g_escorts.empty())
        {
            IssueEscortMoves(site, &g_escorts[0], static_cast<int>(g_escorts.size()));
            g_status = (g_locationMode == LocationBanner)
                ? "Walking to banner..."
                : "Walking into arena...";
        }
        else
        {
            g_status = (g_locationMode == LocationBanner)
                ? "Walking to banner..."
                : "Walking into arena...";
        }

        g_pending = true;
        g_pendingKind = PrisonerUtil::MatchIncludesPrisoner()
            ? PendingPrisonerRelease
            : PendingFormation;
        g_elapsedSec = 0.0f;
        g_settleHoldSec = 0.0f;
        g_escortsParkIssued = false;
        g_lastTick = GetTickCount();
        PGLog::Debug("Proving Grounds: location ingress started");
        BeginNpcFormationProgress();
        if (!TownArena::IsBusy())
            TownArena::CancelPlannedNpcBout("Player match took priority");
        return true;
    }

    void Cancel()
    {
        if (!g_pending)
            return;

        ClearPendingState();

        if (PrisonerUtil::MatchIncludesPrisoner())
        {
            std::string prisonerStatus;
            PrisonerUtil::BeginReturnToCages(prisonerStatus);
            g_status = prisonerStatus.empty() ? "Ingress cancelled" : prisonerStatus;
        }
        else
        {
            g_status = "Ingress cancelled";
        }

        PGLog::Debug("Proving Grounds: arena ingress cancelled");
    }

    void Tick()
    {
        if (!g_pending)
            return;
        if (TownArena::IsBusy() && ou && ou->isPaused())
        {
            g_lastTick = GetTickCount();
            TownAnnouncer::TickApproach();
            return;
        }

        if (g_pendingKind == PendingCountdown)
        {
            if (!HasValidSite() || AnyFighterInvalid())
            {
                ClearPendingState();
                g_status = "Countdown cancelled: fighter or site invalid";
                return;
            }

            const DWORD now = GetTickCount();
            const float dt = static_cast<float>(now - g_lastTick) / 1000.0f;
            g_lastTick = now;
            g_elapsedSec += dt;

            // Keep everyone frozen so sit-around jobs can't pull them away.
            for (size_t i = 0; i < g_fighters.size(); ++i)
            {
                ParkCharacterInPlace(g_fighters[i]);
                FaceOpposingFormation(i);
            }
            if (PrisonerUtil::MatchIncludesPrisoner())
                PrisonerUtil::ReinforceParkedHandlers();

            if (TownAnnouncer::HasAnnouncement())
            {
                TownAnnouncer::TickAnnouncement(g_elapsedSec);
                if (TownAnnouncer::HasAnnouncement())
                {
                    g_status = TownAnnouncer::GetStatus();
                    if (g_elapsedSec >= TownAnnouncer::AnnouncementDuration())
                        FinishIngressAndStart();
                    return;
                }
                // A lost speaker switches to the legacy countdown so the
                // staged fight cannot remain blocked.
                g_elapsedSec = TownAnnouncerPolicy::CountdownElapsedAfterFallback(
                    true, g_elapsedSec);
                g_countdownStage = -1;
            }

            if (g_elapsedSec < 0.60f)
            {
                g_status = "Fight starts in 3...";
                ShowFighterCountdown(3, "3");
            }
            else if (g_elapsedSec < 1.20f)
            {
                g_status = "Fight starts in 2...";
                ShowFighterCountdown(2, "2");
            }
            else if (g_elapsedSec < 1.80f)
            {
                g_status = "Fight starts in 1...";
                ShowFighterCountdown(1, "1");
            }
            else
            {
                g_status = "Fight!";
                ShowFighterCountdown(0, "FIGHT!");
            }

            if (g_elapsedSec >= kFightCountdownSec)
                FinishIngressAndStart();
            return;
        }

        if (g_pendingKind == PendingOpenUI)
        {
            if (!HasValidApproachBuilding())
            {
                ClearPendingState();
                g_status = "Interact building invalid during approach";
                return;
            }
            if (AnyFighterInvalid())
            {
                ClearPendingState();
                g_status = "Selection invalid during approach";
                return;
            }

            const DWORD now = GetTickCount();
            const float dt = static_cast<float>(now - g_lastTick) / 1000.0f;
            g_lastTick = now;
            g_elapsedSec += dt;

            // Keep re-applying the move so RMB / use leftovers can't leave them idle.
            if (g_elapsedSec >= g_nextMoveIssueSec)
            {
                IssueApproachMoves();
                g_nextMoveIssueSec = g_elapsedSec + kApproachMoveReissueSec;
            }

            const ArenaIngressPolicy::IngressObservation observation =
                BuildIngressObservation();
            if (!observation.allFightersValid)
            {
                ClearPendingState();
                g_status = "Selection invalid during approach";
                return;
            }

            if (ArenaIngressPolicy::CanOpenApproachUi(observation))
            {
                PGLog::Debug("Proving Grounds: approach arrival (radius)");
                FinishApproachAndOpenUI();
                return;
            }

            if (observation.allAtSettleRadius && observation.allSettled)
            {
                g_settleHoldSec += dt;
                if (g_settleHoldSec >= kSettleHoldSec)
                {
                    PGLog::Debug("Proving Grounds: approach arrival (settled)");
                    FinishApproachAndOpenUI();
                    return;
                }
            }
            else
            {
                g_settleHoldSec = 0.0f;
            }

            if (g_elapsedSec >= kIngressTimeoutSec)
            {
                PGLog::Debug("Proving Grounds: approach timeout — opening UI anyway");
                FinishApproachAndOpenUI();
            }
            return;
        }

        if (!HasValidSite())
        {
            ClearPendingState();
            g_status = "Fight site destroyed during ingress";
            PGLog::Debug("Proving Grounds: fight site destroyed mid-ingress");
            return;
        }

        ArenaIngressPolicy::IngressObservation observation = BuildIngressObservation();
        if (!observation.allFightersValid)
        {
            Cancel();
            g_status = "Fighter invalid during ingress";
            return;
        }

        const DWORD now = GetTickCount();
        const float dt = static_cast<float>(now - g_lastTick) / 1000.0f;
        g_lastTick = now;
        g_elapsedSec += dt;
        TownAnnouncer::TickApproach();

        if (PrisonerUtil::MatchIncludesPrisoner())
        {
            PrisonerUtil::TickHandlerUnlocks();
            for (size_t i = 0; i < g_fighters.size(); ++i)
            {
                Character* fighter = g_fighters[i];
                if (PrisonerUtil::ConsumeNewlyReleased(fighter))
                {
                    PrisonerUtil::SendPairToArena(fighter, g_targets[i]);
                    PGLog::Debug("Proving Grounds: prisoner unlocked — walking to arena");
                }
            }

            if (g_elapsedSec >= g_nextMoveIssueSec)
            {
                IssueFormationMoves();
                g_nextMoveIssueSec = g_elapsedSec + kApproachMoveReissueSec;
            }
            ParkArrivedFighters();

            const bool allPrisonersReleased =
                PrisonerUtil::AllMatchPrisonersReleased();
            if (!allPrisonersReleased)
            {
                g_status = "Handlers releasing prisoners...";
                g_settleHoldSec = 0.0f;
                return;
            }

            if (ArenaIngressPolicy::ShouldAdvancePrisonerRelease(
                    g_pendingKind == PendingPrisonerRelease,
                    PrisonerUtil::MatchIncludesPrisoner(),
                    allPrisonersReleased))
            {
                g_pendingKind = PendingFormation;
                g_elapsedSec = 0.0f;
                g_settleHoldSec = 0.0f;
                g_nextMoveIssueSec = 0.0f;
                IssueFormationMoves();
                BeginNpcFormationProgress();
                PGLog::Debug("Proving Grounds: all prisoners released - formation phase started");
            }
        }

        if (g_pendingKind != PendingFormation)
            return;

        if (g_elapsedSec >= g_nextMoveIssueSec)
        {
            IssueFormationMoves();
            g_nextMoveIssueSec = g_elapsedSec + kApproachMoveReissueSec;
        }
        ParkArrivedFighters();
        UpdateNpcFormationProgress();
        observation = BuildIngressObservation();
        if (ArenaIngressPolicy::CanBeginCountdown(observation))
        {
            if (TownAnnouncer::WaitingForPosition())
            {
                g_settleHoldSec = 0.0f;
                g_status = TownAnnouncer::GetStatus();
                return;
            }
            if (TownBookie::HoldForBets())
            {
                g_settleHoldSec = 0.0f;
                g_status = "NPC fighters ready - bookie accepting bets";
                return;
            }
            g_settleHoldSec += dt;
            if (g_settleHoldSec < kFightGatherHoldSec)
            {
                g_status = "Fighters ready - crowd settling before the match";
                return;
            }
            PGLog::Debug("Proving Grounds: ingress arrival (radius)");
            BeginFightCountdown();
            return;
        }

        g_settleHoldSec = 0.0f;
        const int outside = CountFightersOutsideFormation();
        char waiting[128];
        sprintf_s(waiting, "Waiting for %d fighter%s to reach formation...",
            outside, outside == 1 ? "" : "s");
        g_status = waiting;

        // Give fighters making real progress time to reach their marks, but do
        // not let disconnected navigation hold the wager indefinitely.
        if (TownArena::IsBusy() && !TownArena::IsPlayerMatch() &&
            g_elapsedSec >= kNpcFormationTimeoutSec)
        {
            bool stalled = false;
            for (size_t i = 0; i < g_fighters.size(); ++i)
            {
                if (!FighterAtMark(i, kFormationArrivalRadius) &&
                    (i >= g_npcFormationProgress.size() ||
                     g_elapsedSec - g_npcFormationProgress[i].lastProgressSec >= kNpcFormationStallSec))
                    stalled = true;
            }
            const bool hardCap = g_elapsedSec >= kNpcFormationHardCapSec;
            if (!stalled && !hardCap)
            {
                if (!g_npcGraceLogged)
                {
                    PGLog::Debug("Proving Grounds: NPC formation extended while missing fighters approach their marks");
                    g_npcGraceLogged = true;
                }
                return;
            }
            const bool wagerPending = TownBookie::HasPendingWager();
            for (size_t i = 0; i < g_fighters.size(); ++i)
            {
                Character* fighter = g_fighters[i];
                if (!fighter || !fighter->isValid()) continue;
                const Ogre::Vector3 position = fighter->getPosition();
                const Ogre::Vector3 target = g_targets[i];
                const float horizontal = std::sqrt(HorizontalDistSq(position, target));
                const bool blocked = !FighterAtMark(i, kFormationArrivalRadius);
                const float progressAge = i < g_npcFormationProgress.size()
                    ? g_elapsedSec - g_npcFormationProgress[i].lastProgressSec : g_elapsedSec;
                const bool stuck = blocked && progressAge >= kNpcFormationStallSec;
                CharMovement* movement = fighter->getMovement();
                char detail[768];
                sprintf_s(detail,
                    "Proving Grounds: ingress timeout fighter slot=%u name=%s blocked=%d stalled=%d progressAge=%.1f horizontal=%.1f vertical=%.1f moving=%d speed=%.2f position=(%.1f,%.1f,%.1f) target=(%.1f,%.1f,%.1f) destination=(%.1f,%.1f,%.1f)",
                    static_cast<unsigned>(i + 1), fighter->getName().c_str(), blocked ? 1 : 0,
                    stuck ? 1 : 0, progressAge, horizontal, position.y - target.y,
                    movement && movement->isCurrentlyMoving() ? 1 : 0, fighter->getMovementSpeed(),
                    position.x, position.y, position.z, target.x, target.y, target.z,
                    movement ? movement->destination.x : 0.0f,
                    movement ? movement->destination.y : 0.0f,
                    movement ? movement->destination.z : 0.0f);
                PGLog::Debug(detail);
                if (stuck) TownArena::QuarantineIngressFighter(fighter);
            }
            Cancel();
            g_status = hardCap ? "NPC formation hard cap reached; original lineup cancelled" :
                "NPC formation stalled; original lineup cancelled";
            if (wagerPending) g_status += "; wager refunded";
            PGLog::Debug(("Proving Grounds: " + g_status).c_str());
        }
    }

    bool BeginWalkIn(Character* fighter, MatchRules::MatchTeam team)
    {
        if (!fighter || !fighter->isValid())
            return false;
        if (g_walkInPending)
            return false;
        if (team != MatchRules::TeamA && team != MatchRules::TeamB)
            return false;

        Building* site = FindSiteForMode(g_locationMode, fighter->getPosition());
        if (!site)
            return false;

        if (g_locationMode == LocationBanner)
        {
            const Ogre::Vector3 base = site->getPosition();
            const float side = (team == MatchRules::TeamA) ? -1.0f : 1.0f;
            g_walkInTarget = base + Ogre::Vector3(side * kBannerGatherOffset, 0.0f, 0.0f);
        }
        else
        {
            const int half = kArenaMarkerCount / 2;
            Ogre::Vector3 local = (team == MatchRules::TeamA)
                ? kArenaMarkers[0]
                : kArenaMarkers[half];
            if (g_locationMode == LocationSmallArena)
                local = local * kSmallArenaScale;
            g_walkInTarget = LocalToWorld(site, local);
        }

        g_walkInFighter = fighter;
        g_walkInPending = true;
        g_walkInElapsedSec = 0.0f;
        g_walkInNextMoveSec = 0.0f;
        g_walkInLastTick = GetTickCount();
        IssueMove(fighter, g_walkInTarget);
        PGLog::Debug("Proving Grounds: Teams 1v1 walk-in started");
        return true;
    }

    Character* GetWalkInFighter()
    {
        return g_walkInPending ? g_walkInFighter : NULL;
    }

    bool IsWalkInPending()
    {
        return g_walkInPending;
    }

    bool IsWalkInArrived()
    {
        if (!g_walkInPending || !g_walkInFighter || !g_walkInFighter->isValid())
            return false;
        return HorizontalDistSq(g_walkInFighter->getPosition(), g_walkInTarget)
            <= (kArrivalRadius * kArrivalRadius);
    }

    void TickWalkIn()
    {
        if (!g_walkInPending)
            return;

        if (!g_walkInFighter || !g_walkInFighter->isValid())
        {
            ClearWalkIn();
            return;
        }

        const DWORD now = GetTickCount();
        const float dt = static_cast<float>(now - g_walkInLastTick) / 1000.0f;
        g_walkInLastTick = now;
        g_walkInElapsedSec += dt;

        if (g_walkInElapsedSec >= g_walkInNextMoveSec)
        {
            IssueMove(g_walkInFighter, g_walkInTarget);
            g_walkInNextMoveSec = g_walkInElapsedSec + kApproachMoveReissueSec;
        }

        // Timeout: treat as arrived so the bout can continue.
        if (g_walkInElapsedSec >= kIngressTimeoutSec)
            g_walkInTarget = g_walkInFighter->getPosition();
    }

    void ClearWalkIn()
    {
        if (g_walkInFighter && g_walkInFighter->isValid())
            g_walkInFighter->removeJob(MOVE_CUS_ORDERED);
        g_walkInPending = false;
        g_walkInFighter = NULL;
        g_walkInTarget = Ogre::Vector3(0.0f, 0.0f, 0.0f);
        g_walkInNextMoveSec = 0.0f;
        g_walkInElapsedSec = 0.0f;
        g_walkInLastTick = 0;
    }
}
