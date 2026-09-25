#include "TownAnnouncer.h"
#include "TownAnnouncerPolicy.h"
#include "TownAftercare.h"
#include "ArenaIdentity.h"

#include "PGLog.h"
#include <Windows.h>

#include <cmath>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable: 4091)
#include <kenshi/Building/Building.h>
#include <kenshi/Character.h>
#include <kenshi/CharMovement.h>
#include <kenshi/GameData.h>
#include <kenshi/GameWorld.h>
#include <kenshi/Globals.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/util/hand.h>
#include <kenshi/util/lektor.h>
#include <ogre/OgreVector3.h>
#pragma warning(pop)

#ifndef NULL
#define NULL 0
#endif

namespace
{
    static const float kArrivalRadius = 15.0f;
    static const float kSpeakerSpacing = 16.0f;
    static const float kMoveRetrySec = 0.50f;
    static const float kSearchRadius = 2500.0f;

    struct ControlledSpeaker
    {
        hand actor;
        bool hold;
        bool passive;
        bool owned;
        bool directed;
        MoveSpeed speed;
        void* receiver;
        Ogre::Vector3 target;
        ControlledSpeaker() : hold(false), passive(false), owned(false),
            directed(false), speed(WALK),
            receiver(NULL), target(0.0f, 0.0f, 0.0f) {}
    };

    TownAnnouncerPolicy::Program g_program = TownAnnouncerPolicy::LegacyCountdown;
    std::vector<ControlledSpeaker> g_speakers;
    std::vector<ControlledSpeaker> g_pendingReleases;
    hand g_arena;
    hand g_spot;
    float g_approachElapsed = 0.0f;
    TownAnnouncerPolicy::ApproachProgress g_approachProgress;
    float g_nextMove = 0.0f;
    DWORD g_lastTick = 0;
    int g_lastCue = -1;
    int g_variant = 0;
    bool g_announcementStarted = false;
    std::string g_status = "Legacy fighter countdown";

    bool IsPlayer(Character* character)
    {
        if (!character || !ou || !ou->player)
            return false;
        const lektor<Character*>& players = ou->player->getAllPlayerCharacters();
        for (uint32_t i = 0; i < players.size(); ++i)
            if (players[i] == character)
                return true;
        return false;
    }

    bool IsFighter(Character* character, Character* const* fighters, int fighterCount)
    {
        for (int i = 0; character && fighters && i < fighterCount; ++i)
            if (fighters[i] == character)
                return true;
        return false;
    }

    bool CanAnnounce(Character* character, Building* arena,
        Character* const* fighters, int fighterCount)
    {
        return character && character->isValid() && arena && arena->isValid() &&
            character->getCurrentTownLocation() == arena->getTown() &&
            !IsPlayer(character) && !IsFighter(character, fighters, fighterCount) &&
            character->getOrdersReciever() &&
            !character->isDead() && !character->isUnconcious() &&
            !character->isInCombatMode(true, true) &&
            !character->isBeingCarried() && !character->isCarryingSomething;
    }

    bool CanContinue(Character* character)
    {
        return character && character->isValid() && !character->isDead() &&
            !character->isUnconcious() && !character->isInCombatMode(true, true) &&
            !character->isBeingCarried() && !character->isCarryingSomething;
    }

    bool ReleasePending(Character* character)
    {
        for (size_t i = 0; character && i < g_pendingReleases.size(); ++i)
            if (g_pendingReleases[i].actor == character)
                return true;
        return false;
    }

    Character* FindSpeaker(Building* arena, const char* role,
        Character* const* fighters, int fighterCount)
    {
        if (!ou || !arena || !role)
            return NULL;
        lektor<RootObject*> nearby;
        ou->getObjectsWithinSphere(nearby, arena->getPosition(), kSearchRadius,
            CHARACTER, 512, NULL);
        Character* nearest = NULL;
        float nearestDistance = 1.0e30f;
        for (uint32_t i = 0; i < nearby.size(); ++i)
        {
            RootObject* object = nearby[i];
            if (!object || !object->isValid())
                continue;
            Character* character = static_cast<Character*>(object);
            GameData* data = character->getGameData();
            if (!data || !TownAnnouncerPolicy::Equals(data->stringID.c_str(), role) ||
                !CanAnnounce(character, arena, fighters, fighterCount) ||
                ReleasePending(character))
                continue;
            const float distance = (character->getPosition() - arena->getPosition()).squaredLength();
            if (distance < nearestDistance)
            {
                nearest = character;
                nearestDistance = distance;
            }
        }
        return nearest;
    }

    Building* FindSpot(Building* arena)
    {
        if (!ou || !arena)
            return NULL;
        lektor<RootObject*> nearby;
        ou->getObjectsWithinSphere(nearby, arena->getPosition(), kSearchRadius,
            BUILDING, 1024, NULL);
        Building* nearest = NULL;
        float nearestDistance = 1.0e30f;
        for (uint32_t i = 0; i < nearby.size(); ++i)
        {
            RootObject* object = nearby[i];
            if (!object || !object->isValid() ||
                !TownAnnouncerPolicy::IsSpot(ArenaIdentity::GetStringId(object)))
                continue;
            Building* spot = static_cast<Building*>(object);
            if (spot->getTown() != arena->getTown())
                continue;
            Building::ConstructionState* state = spot->getBuildState();
            if (state && !state->isComplete)
                continue;
            const float distance = (spot->getPosition() - arena->getPosition()).squaredLength();
            if (distance < nearestDistance)
            {
                nearest = spot;
                nearestDistance = distance;
            }
        }
        return nearest;
    }

    Ogre::Vector3 SpeakerTarget(Building* arena, Building* spot, int index, int count)
    {
        const Ogre::Vector3 center = spot->getPosition();
        if (count <= 1)
            return center;
        Ogre::Vector3 towardArena = arena->getPosition() - center;
        towardArena.y = 0.0f;
        const float lengthSq = towardArena.squaredLength();
        if (lengthSq <= 1.0f)
            towardArena = Ogre::Vector3(1.0f, 0.0f, 0.0f);
        else
            towardArena /= sqrtf(lengthSq);
        const Ogre::Vector3 sideways(-towardArena.z, 0.0f, towardArena.x);
        const float offset = (static_cast<float>(index) -
            (static_cast<float>(count - 1) * 0.5f)) * kSpeakerSpacing;
        return center + sideways * offset;
    }

    bool Arrived(const ControlledSpeaker& speaker)
    {
        Character* character = speaker.actor.getCharacter();
        if (!character || !character->isValid())
            return false;
        return (character->getPosition() - speaker.target).squaredLength() <=
            kArrivalRadius * kArrivalRadius;
    }

    float FarthestDistanceFromTarget()
    {
        float farthest = 0.0f;
        for (size_t i = 0; i < g_speakers.size(); ++i)
        {
            Character* character = g_speakers[i].actor.getCharacter();
            if (!character || !character->isValid())
                continue;
            const float distance = (character->getPosition() -
                g_speakers[i].target).length();
            if (distance > farthest)
                farthest = distance;
        }
        return farthest;
    }

    void FaceArena(Character* character)
    {
        Building* arena = g_arena.getBuilding();
        if (character && character->isValid() && arena && arena->isValid())
            character->lookatPosition(arena->getPosition(), true);
    }

    void HoldAtStand(ControlledSpeaker& speaker)
    {
        Character* character = speaker.actor.getCharacter();
        if (!character || !character->isValid())
            return;
        character->removeJob(MOVE_CUS_ORDERED);
        character->clearAllAIGoals();
        character->setStandingOrder(MessageForB::M_SET_ORDER_HOLD, true);
        character->setStandingOrder(MessageForB::M_SET_ORDER_PASSIVE, true);
        character->addGoal(STAND_STILL, NULL);
        FaceArena(character);
    }

    void IssueMove(ControlledSpeaker& speaker)
    {
        Character* character = speaker.actor.getCharacter();
        if (!character || !character->isValid())
            return;
        character->clearAllAIGoals();
        character->setStandingOrder(MessageForB::M_SET_ORDER_HOLD, false);
        // Kenshi ignores explicit movement while either standing order is set.
        // Passive is restored after arrival when the speaker is parked.
        character->setStandingOrder(MessageForB::M_SET_ORDER_PASSIVE, false);
        CharMovement* movement = character->getMovement();
        if (movement)
        {
            movement->setDestination(speaker.target, HIGH_PRIORITY, true);
            movement->setDesiredSpeedOrders(RUN);
            movement->setDesiredSpeed(RUN);
        }
        character->addOrder(NULL, MOVE_CUS_ORDERED, NULL, false, true, speaker.target);
        character->setDestination(speaker.target, false);
        speaker.directed = true;
    }

    void PrepareNativeResume(ControlledSpeaker& speaker, Character* character)
    {
        character->addOrder(NULL, MOVE_CUS_ORDERED, NULL, false, true,
            character->getPosition());
        character->removeJob(MOVE_CUS_ORDERED);
        character->clearAllAIGoals();
        character->setStandingOrder(MessageForB::M_SET_ORDER_HOLD, speaker.hold);
        character->setStandingOrder(MessageForB::M_SET_ORDER_PASSIVE, speaker.passive);
        CharMovement* movement = character->getMovement();
        if (movement)
        {
            movement->setDesiredSpeedOrders(speaker.speed);
            movement->restoreDesiredSpeed();
        }
    }

    void FallBack(const char* reason)
    {
        std::string message = "Proving Grounds: announcer fallback - ";
        message += reason ? reason : "unavailable";
        PGLog::Debug(message.c_str());
        TownAnnouncer::Release();
        g_status = "Legacy fighter countdown";
    }
}

namespace TownAnnouncer
{
    void Begin(Building* arena, bool playerMatch, int challengeSlot,
        const char* const* opponentRoles, int opponentCount,
        Character* const* fighters, int fighterCount)
    {
        Release();
        const TownAnnouncerPolicy::Program requested = TownAnnouncerPolicy::Select(
            playerMatch, challengeSlot, opponentRoles, opponentCount);
        if (requested == TownAnnouncerPolicy::LegacyCountdown)
            return;

        Building* spot = FindSpot(arena);
        std::vector<Character*> actors;
        TownAnnouncerPolicy::Program candidate = requested;
        while (spot && candidate != TownAnnouncerPolicy::LegacyCountdown)
        {
            actors.clear();
            const int candidateCount = TownAnnouncerPolicy::RequiredSpeakers(candidate);
            for (int i = 0; i < candidateCount; ++i)
            {
                Character* actor = FindSpeaker(arena,
                    TownAnnouncerPolicy::SpeakerRole(candidate, i), fighters, fighterCount);
                if (actor)
                    actors.push_back(actor);
            }
            if (static_cast<int>(actors.size()) == candidateCount)
                break;
            candidate = TownAnnouncerPolicy::NextFallback(candidate);
        }
        g_program = TownAnnouncerPolicy::Resolve(candidate, spot != NULL,
            static_cast<int>(actors.size()));
        if (g_program == TownAnnouncerPolicy::LegacyCountdown)
        {
            PGLog::Debug(!spot
                ? "Proving Grounds: announcer fallback - spot 261 not found near arena"
                : "Proving Grounds: announcer fallback - required cast unavailable");
            return;
        }

        g_arena = arena;
        g_spot = spot;
        const int required = TownAnnouncerPolicy::RequiredSpeakers(g_program);
        for (int i = 0; i < required; ++i)
        {
            ControlledSpeaker speaker;
            speaker.actor = actors[static_cast<size_t>(i)];
            speaker.hold = actors[static_cast<size_t>(i)]->getStandingOrder(MessageForB::M_SET_ORDER_HOLD);
            speaker.passive = actors[static_cast<size_t>(i)]->getStandingOrder(MessageForB::M_SET_ORDER_PASSIVE);
            speaker.speed = actors[static_cast<size_t>(i)]->getMovementSpeedOrders();
            speaker.receiver = actors[static_cast<size_t>(i)]->getOrdersReciever();
            speaker.target = SpeakerTarget(arena, spot, i, required);
            g_speakers.push_back(speaker);
        }
        for (size_t i = 0; i < g_speakers.size(); ++i)
        {
            if (!TownAftercare::ReserveExternalOrders(g_speakers[i].receiver))
            {
                FallBack("speaker AI ownership could not be reserved");
                return;
            }
            g_speakers[i].owned = true;
        }
        const unsigned timeSeed = GetTickCount() ^
            static_cast<unsigned>(ou ? ou->getTimeStamp_inGameHours().getTotalHours() * 60.0 : 0.0);
        g_variant = TownAnnouncerPolicy::Variant(g_program, timeSeed);
        g_approachElapsed = 0.0f;
        TownAnnouncerPolicy::ResetApproachProgress(g_approachProgress,
            FarthestDistanceFromTarget());
        g_nextMove = 0.0f;
        g_lastTick = GetTickCount();
        g_lastCue = -1;
        g_announcementStarted = false;
        g_status = "Announcer walking to the viewing stand";
        for (size_t i = 0; i < g_speakers.size(); ++i)
            IssueMove(g_speakers[i]);
        PGLog::Debug("Proving Grounds: announcer cast walking to spot 261");
    }

    void TickApproach()
    {
        if (g_program == TownAnnouncerPolicy::LegacyCountdown || g_announcementStarted)
            return;
        Building* arena = g_arena.getBuilding();
        Building* spot = g_spot.getBuilding();
        if (!arena || !arena->isValid() || !spot || !spot->isValid())
        {
            FallBack("arena or spot became invalid");
            return;
        }
        const DWORD now = GetTickCount();
        const float dt = static_cast<float>(now - g_lastTick) / 1000.0f;
        g_lastTick = now;
        g_approachElapsed = TownAnnouncerPolicy::AdvanceApproach(
            g_approachElapsed, dt, ou && ou->isPaused());
        if (ou && ou->isPaused())
            return;
        bool allArrived = true;
        for (size_t i = 0; i < g_speakers.size(); ++i)
        {
            Character* character = g_speakers[i].actor.getCharacter();
            if (!CanContinue(character))
            {
                FallBack("speaker became unavailable");
                return;
            }
            if (Arrived(g_speakers[i]))
                HoldAtStand(g_speakers[i]);
            else
                allArrived = false;
        }
        if (allArrived)
        {
            g_status = "Announcer ready at the viewing stand";
            return;
        }
        if (TownAnnouncerPolicy::ApproachStalled(g_approachProgress,
            FarthestDistanceFromTarget(), dt, false))
        {
            FallBack("viewing stand approach made no progress for 20 seconds");
            return;
        }
        if (g_approachElapsed >= g_nextMove)
        {
            for (size_t i = 0; i < g_speakers.size(); ++i)
                if (!Arrived(g_speakers[i]))
                    IssueMove(g_speakers[i]);
            g_nextMove = g_approachElapsed + kMoveRetrySec;
        }
    }

    void TickReleases()
    {
        if (ou && ou->isPaused())
            return;
        for (size_t i = 0; i < g_pendingReleases.size();)
        {
            ControlledSpeaker& speaker = g_pendingReleases[i];
            Character* character = speaker.actor.getCharacter();
            const bool valid = character && character->isValid();
            const bool dead = valid && character->isDead();
            const TownAnnouncerPolicy::PendingReleaseDisposition disposition =
                TownAnnouncerPolicy::PendingReleaseFor(valid, dead,
                    CanContinue(character));
            if (disposition == TownAnnouncerPolicy::WaitPendingRelease)
            {
                ++i;
                continue;
            }
            if (disposition == TownAnnouncerPolicy::ResumePendingRelease)
            {
                PrepareNativeResume(speaker, character);
                character->reThinkCurrentAIAction();
            }
            g_pendingReleases.erase(g_pendingReleases.begin() + i);
        }
    }

    bool WaitingForPosition()
    {
        if (g_program == TownAnnouncerPolicy::LegacyCountdown)
            return false;
        for (size_t i = 0; i < g_speakers.size(); ++i)
            if (!Arrived(g_speakers[i]))
                return true;
        return false;
    }

    bool HasAnnouncement()
    {
        return g_program != TownAnnouncerPolicy::LegacyCountdown;
    }

    void BeginAnnouncement()
    {
        if (!HasAnnouncement())
            return;
        g_announcementStarted = true;
        g_lastCue = -1;
        g_status = "The announcer takes the rail";
        TickAnnouncement(0.0f);
    }

    void TickAnnouncement(float elapsed)
    {
        if (!HasAnnouncement() || !g_announcementStarted)
            return;
        for (size_t i = 0; i < g_speakers.size(); ++i)
        {
            Character* character = g_speakers[i].actor.getCharacter();
            if (!CanContinue(character))
            {
                FallBack("speaker lost during announcement");
                return;
            }
            HoldAtStand(g_speakers[i]);
        }
        const int due = TownAnnouncerPolicy::CuesDue(g_program, g_lastCue, elapsed, g_variant);
        for (int delivered = 0; delivered < due; ++delivered)
        {
            const int cue = g_lastCue + 1;
            const int speaker = TownAnnouncerPolicy::CueSpeaker(g_program, cue, g_variant);
            const char* line = TownAnnouncerPolicy::CueText(g_program, g_variant, cue);
            if (speaker >= 0 && speaker < static_cast<int>(g_speakers.size()) && line)
            {
                Character* actor = g_speakers[static_cast<size_t>(speaker)].actor.getCharacter();
                FaceArena(actor);
                actor->say(std::string(line));
                g_status = cue == TownAnnouncerPolicy::CueCount(g_program, g_variant) - 1
                    ? "Fight!"
                    : "The fight is being announced";
            }
            g_lastCue = cue;
        }
    }

    float AnnouncementDuration()
    {
        return TownAnnouncerPolicy::Duration(g_program, g_variant);
    }

    const char* GetStatus()
    {
        return g_status.c_str();
    }

    void Release()
    {
        for (size_t i = 0; i < g_speakers.size(); ++i)
        {
            ControlledSpeaker& speaker = g_speakers[i];
            Character* character = speaker.actor.getCharacter();
            const bool safeToResume = CanContinue(character);
            const TownAnnouncerPolicy::ReleasePlan plan =
                TownAnnouncerPolicy::PlanRelease(speaker.owned,
                    speaker.directed, safeToResume);
            if (plan.resumeNative)
                PrepareNativeResume(speaker, character);
            if (plan.releaseOwnership)
                TownAftercare::ReleaseExternalOrders(speaker.receiver);
            if (plan.resumeNative)
                character->reThinkCurrentAIAction();
            else if (plan.deferNative)
            {
                speaker.owned = false;
                g_pendingReleases.push_back(speaker);
            }
        }
        g_speakers.clear();
        g_arena.setNull();
        g_spot.setNull();
        g_program = TownAnnouncerPolicy::LegacyCountdown;
        g_approachElapsed = 0.0f;
        TownAnnouncerPolicy::ResetApproachProgress(g_approachProgress, 0.0f);
        g_nextMove = 0.0f;
        g_lastTick = 0;
        g_lastCue = -1;
        g_announcementStarted = false;
        g_status = "Legacy fighter countdown";
    }

    void AbandonWorldState()
    {
        for (size_t i = 0; i < g_speakers.size(); ++i)
            if (g_speakers[i].owned)
                TownAftercare::ReleaseExternalOrders(g_speakers[i].receiver);
        g_speakers.clear();
        g_pendingReleases.clear();
        g_arena.setNull();
        g_spot.setNull();
        g_program = TownAnnouncerPolicy::LegacyCountdown;
        g_approachElapsed = 0.0f;
        TownAnnouncerPolicy::ResetApproachProgress(g_approachProgress, 0.0f);
        g_nextMove = 0.0f;
        g_lastTick = 0;
        g_lastCue = -1;
        g_announcementStarted = false;
        g_status = "Legacy fighter countdown";
    }
}
