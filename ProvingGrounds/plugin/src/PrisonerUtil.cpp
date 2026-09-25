#include "PrisonerUtil.h"

#include "ArenaIngress.h"
#include "ArenaMedical.h"
#include "FightStarter.h"
#include "FrameCadencePolicy.h"
#include "PrisonerMatchLogic.h"
#include "PrisonerUtilInternal.h"
#include "SparSession.h"

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <limits>
#include <windows.h>

#include "PGLog.h"

#pragma warning(push)
#pragma warning(disable: 4091)
#include <kenshi/Building/Building.h>
#include <kenshi/Character.h>
#include <kenshi/CharMovement.h>
#include <kenshi/Enums.h>
#include <kenshi/GameWorld.h>
#include <kenshi/Globals.h>
#include <kenshi/RootObject.h>
#include <kenshi/RootObjectBase.h>
#include <kenshi/util/hand.h>
#include <kenshi/util/lektor.h>
#include <ogre/OgreVector3.h>
#include <ogre/OgreLogManager.h>
#pragma warning(pop)

#ifndef NULL
#define NULL 0
#endif

// UseableStuff.h pulls InventoryLayout/MyGUI; declare the cage surface we need.
class UseableStuff : public Building
{
public:
    const hand& getOccupant() const;
};

namespace PrisonerUtil
{
    namespace
    {
        struct NearbyCage
        {
            Building* building;
            UseableStuff* useable;
        };

        struct MatchEntry
        {
            Character* prisoner;
            hand cageHand;
            UseableStuff* cageUseable;
            Character* handler;
            int originalCageIndex;
            bool wasChained;
            bool released;
            bool unlockPending;
            bool unlockArrived;
            float unlockElapsed;
            float unlockHoldElapsed;
            bool newlyReleased;
            UseableStuff* returnCage;
            Building* returnCageBuilding;
            hand returnCageHand;
            hand prisonerHand;
            hand handlerHand;
            bool lockPending;
            bool lockOrderIssued;
            DWORD lockStartedAt;
            bool returnPending;
            bool returnEscorting;
            bool returnCarrying;
            bool returnPickedUp;
            bool returnTreating;
            float returnElapsed;
            bool ringsideTreating;
            float ringsideTreatElapsed;
        };

        static std::vector<Character*> g_rosterPrisoners;
        static std::vector<NearbyCage> g_nearbyCages;
        static std::vector<MatchEntry> g_matchEntries;
        static std::vector<Character*> g_matchHandlers;
        static std::vector<Character*> g_matchPrisonersScratch;
        static bool g_matchHasPrisoner = false;
        static bool g_returning = false;
        static bool g_unlocking = false;
        static bool g_cageReturnFailed = false;
        static std::string g_cageReturnStatus;
        static bool g_handlerStatesRemembered = false;
        static DWORD g_returnLastTick = 0;
        static DWORD g_unlockLastTick = 0;
        static DWORD g_ringsideAidLastTick = 0;
        static DWORD g_matchUpkeepLastTick = 0;
        static float g_protectionElapsedSec = 0.0f;
        static float g_handlerElapsedSec = 0.0f;
        static bool g_protectionImmediate = false;

        static const int kSphereSearchMax = 512;
        // The building origin can be a long way from the cage's actual lock point.
        // Only finalise the native prison assignment once the prisoner/handler is
        // genuinely at the cage; otherwise Kenshi can leave the prisoner alongside it.
        static const float kReturnArriveRadius = 36.0f;
        static const float kReturnTimeoutSec = 60.0f;
        static const DWORD kLockTimeoutMs = 30000;
        static const float kUnlockArriveRadius = 120.0f;
        static const float kUnlockHoldSec = 0.45f;
        static const float kUnlockTimeoutSec = 45.0f;
        static const float kCarryPickupRetrySec = 2.5f;
        static const float kTreatmentTimeoutSec = 20.0f;
        static const float kTreatmentRetrySec = 3.0f;
        static const float kProtectionIntervalSec = 0.10f;
        static const float kHandlerReinforceIntervalSec = 0.25f;
        static const float kHandlerSideOffset = 28.0f;
        static const float kHandlerLeadOffset = 8.0f;
        static const char* kPrisonerFightLines[] = {
            "Finally. Let's make this count.",
            "Thought you'd never ask.",
            "Who am I fighting?",
            "Open the way. I'm ready.",
            "Try not to blink.",
            "Freedom first. Questions later.",
            "Let's get this over with.",
            "This should be interesting."
        };

        float AdvanceMatchUpkeepClock()
        {
            const DWORD now = GetTickCount();
            const float dt = g_matchUpkeepLastTick == 0 ? 0.0f :
                static_cast<float>(now - g_matchUpkeepLastTick) / 1000.0f;
            g_matchUpkeepLastTick = now;
            return dt > 1.0f ? 1.0f : dt;
        }

        void ResetMatchUpkeep()
        {
            g_matchUpkeepLastTick = 0;
            FrameCadencePolicy::Reset(g_protectionElapsedSec);
            FrameCadencePolicy::Reset(g_handlerElapsedSec);
            g_protectionImmediate = false;
        }

        float HorizontalDistSq(const Ogre::Vector3& a, const Ogre::Vector3& b)
        {
            const float dx = a.x - b.x;
            const float dz = a.z - b.z;
            return (dx * dx) + (dz * dz);
        }

        bool IsWithinRegistryRange(Building* registry, Building* cage)
        {
            if (!registry || !cage)
                return false;
            const float radiusSq = kCageNearRegistry * kCageNearRegistry;
            return HorizontalDistSq(registry->getPosition(), cage->getPosition()) <= radiusSq;
        }

        bool ContainsCharacter(const std::vector<Character*>& list, Character* c)
        {
            for (size_t i = 0; i < list.size(); ++i)
            {
                if (list[i] == c)
                    return true;
            }
            return false;
        }

        bool ContainsCageBuilding(const std::vector<NearbyCage>& cages, Building* building)
        {
            for (size_t i = 0; i < cages.size(); ++i)
            {
                if (cages[i].building == building)
                    return true;
            }
            return false;
        }

        UseableStuff* AsUseableCage(Building* building)
        {
            if (!building)
                return NULL;

            UseableStuff* useable = building->getUseableStuff();
            if (useable)
                return static_cast<UseableStuff*>(useable);

            // Cage furniture is often already the UseableStuff instance; base
            // getUseableStuff() can return null depending on how the pointer was typed.
            if (building->getSpecialFunction() == BF_CAGE)
                return reinterpret_cast<UseableStuff*>(building);

            return NULL;
        }

        bool IsPlayerCageBuilding(Building* building)
        {
            if (!building || !building->isValid())
                return false;
            if (building->isThePlayer())
                return true;
            return PrisonerUtilInternal::IsBuildingOwnedByPlayer(building);
        }

        void AddCageBuilding(
            Building* registry,
            Building* building,
            std::vector<NearbyCage>& cagesOut)
        {
            if (!registry || !building || !building->isValid())
                return;
            if (!IsWithinRegistryRange(registry, building))
                return;
            if (ContainsCageBuilding(cagesOut, building))
                return;

            UseableStuff* useable = AsUseableCage(building);
            if (!useable)
                return;

            NearbyCage entry;
            entry.building = building;
            entry.useable = useable;
            cagesOut.push_back(entry);
        }

        void CollectCagesFromBuildingTree(
            Building* registry,
            Building* building,
            std::vector<NearbyCage>& cagesOut)
        {
            if (!registry || !building || !building->isValid())
                return;

            if (building->getSpecialFunction() == BF_CAGE && IsPlayerCageBuilding(building))
                AddCageBuilding(registry, building, cagesOut);

            // Kenshi cages are usually furniture inside a player building.
            lektor<Building*> furniture;
            building->findAllFurnitureWithFunction(furniture, BF_CAGE);
            for (uint32_t i = 0; i < furniture.size(); ++i)
            {
                Building* cage = furniture[i];
                if (!cage || !cage->isValid())
                    continue;
                if (!IsPlayerCageBuilding(cage) && !IsPlayerCageBuilding(building))
                    continue;
                AddCageBuilding(registry, cage, cagesOut);
            }
        }

        // Ownership furniture first, then nearby player buildings' cage furniture,
        // then standalone BF_CAGE hits in the sphere.
        void CollectCagesNearRegistry(Building* registry, std::vector<NearbyCage>& cagesOut)
        {
            cagesOut.clear();
            if (!registry || !registry->isValid())
                return;

            std::vector<Building*> ownedCages;
            PrisonerUtilInternal::CollectPlayerOwnedCageBuildings(ownedCages);
            for (size_t i = 0; i < ownedCages.size(); ++i)
                AddCageBuilding(registry, ownedCages[i], cagesOut);

            if (!ou)
            {
                char buf[160];
                sprintf_s(
                    buf,
                    "Proving Grounds: cage discovery ownership=%d sphere=skipped (no world)",
                    static_cast<int>(cagesOut.size()));
                PGLog::Debug(buf);
                return;
            }

            const Ogre::Vector3 origin = registry->getPosition();
            lektor<RootObject*> results;
            ou->getObjectsWithinSphere(
                results,
                origin,
                kCageNearRegistry,
                BUILDING,
                kSphereSearchMax,
                NULL);

            int sphereBuildings = 0;
            int playerBuildings = 0;
            for (uint32_t i = 0; i < results.size(); ++i)
            {
                RootObject* obj = results[i];
                if (!obj || !obj->isValid())
                    continue;
                if (obj->getDataType() != BUILDING &&
                    obj->getDataType() != BUILDING_FUNCTIONALITY &&
                    obj->getDataType() != FOLIAGE_BUILDING)
                    continue;

                Building* building = static_cast<Building*>(obj);
                ++sphereBuildings;

                if (IsPlayerCageBuilding(building) || building->isThePlayer())
                {
                    ++playerBuildings;
                    CollectCagesFromBuildingTree(registry, building, cagesOut);
                }
                else if (building->getSpecialFunction() == BF_CAGE &&
                    PrisonerUtilInternal::IsBuildingOwnedByPlayer(building))
                {
                    AddCageBuilding(registry, building, cagesOut);
                }
            }

            char buf[192];
            sprintf_s(
                buf,
                "Proving Grounds: cage discovery cages=%d ownership=%d sphereBuildings=%d playerBuildings=%d",
                static_cast<int>(cagesOut.size()),
                static_cast<int>(ownedCages.size()),
                sphereBuildings,
                playerBuildings);
            PGLog::Debug(buf);
        }

        // Most reliable roster source: characters already marked IN_PRISON near registry.
        void CollectImprisonedCharactersNearRegistry(
            Building* registry,
            std::vector<Character*>& out)
        {
            if (!registry || !registry->isValid() || !ou)
                return;

            const Ogre::Vector3 origin = registry->getPosition();
            lektor<RootObject*> results;
            // farRadius / nearRadius / always / maxFar / maxNear — take everyone in range.
            ou->getCharactersWithinSphere(
                results,
                origin,
                kCageNearRegistry,
                0.0f,
                kCageNearRegistry,
                kSphereSearchMax,
                kSphereSearchMax,
                NULL);

            int imprisonedHits = 0;
            for (uint32_t i = 0; i < results.size(); ++i)
            {
                RootObject* obj = results[i];
                if (!obj || !obj->isValid())
                    continue;

                const itemType type = obj->getDataType();
                if (type != CHARACTER && type != HUMAN_CHARACTER && type != ANIMAL_CHARACTER)
                    continue;

                Character* character = static_cast<Character*>(obj);
                if (!character || character->isDead())
                    continue;
                if (character->inSomething != IN_PRISON)
                    continue;

                ++imprisonedHits;

                Building* cageBuilding = NULL;
                if (character->inWhat.isValid())
                    cageBuilding = character->inWhat.getBuilding();

                // Prefer player cages; if inWhat is missing, still keep nearby
                // imprisoned characters (furniture handle resolution can be flaky).
                if (cageBuilding && !IsPlayerCageBuilding(cageBuilding))
                    continue;

                if (HorizontalDistSq(origin, character->getPosition()) >
                    (kCageNearRegistry * kCageNearRegistry))
                    continue;

                if (!ContainsCharacter(out, character))
                    out.push_back(character);

                if (cageBuilding)
                    AddCageBuilding(registry, cageBuilding, g_nearbyCages);
            }

            char buf[160];
            sprintf_s(
                buf,
                "Proving Grounds: imprisoned scan hits=%d kept=%d",
                imprisonedHits,
                static_cast<int>(out.size()));
            PGLog::Debug(buf);
        }

        bool IsCageFree(UseableStuff* useable)
        {
            if (!useable)
                return false;

            const hand& occupant = useable->getOccupant();
            if (!occupant.isValid())
                return true;

            Character* occupantChar = occupant.getCharacter();
            if (!occupantChar || !occupantChar->isValid() || occupantChar->isDead())
                return true;

            return false;
        }

        bool HasMatchPrisonerEntry(Character* c)
        {
            if (!c)
                return false;

            for (size_t i = 0; i < g_matchEntries.size(); ++i)
            {
                if (g_matchEntries[i].prisoner == c)
                    return true;
            }
            return false;
        }

        int FindCageIndex(UseableStuff* cage, const std::vector<NearbyCage>& cages)
        {
            if (!cage)
                return -1;

            for (size_t i = 0; i < cages.size(); ++i)
            {
                if (cages[i].useable == cage)
                    return static_cast<int>(i);
            }
            return -1;
        }

        void IssueMove(Character* c, const Ogre::Vector3& loc)
        {
            if (!c || !c->isValid())
                return;

            c->setStandingOrder(MessageForB::M_SET_ORDER_HOLD, false);
            c->setStandingOrder(MessageForB::M_SET_ORDER_PASSIVE, false);

            CharMovement* movement = c->getMovement();
            if (movement)
                movement->setDestination(loc, HIGH_PRIORITY, true);

            c->addOrder(NULL, MOVE_CUS_ORDERED, NULL, false, true, loc);
            c->setDestination(loc, false);
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

        void ClearHandlerOrderQueue(Character* handler)
        {
            if (!handler || !handler->isValid())
                return;
            const Ogre::Vector3 pos = handler->getPosition();
            handler->addOrder(NULL, MOVE_CUS_ORDERED, NULL, false, true, pos);
            handler->removeJob(MOVE_CUS_ORDERED);
        }

        Ogre::Vector3 HandlerTransitTarget(
            const MatchEntry& entry,
            const Ogre::Vector3& destination,
            size_t entryIndex)
        {
            Ogre::Vector3 origin = destination;
            if (entry.cageUseable)
                origin = static_cast<Building*>(entry.cageUseable)->getPosition();
            else if (entry.prisoner && entry.prisoner->isValid())
                origin = entry.prisoner->getPosition();
            Ogre::Vector3 direction = destination - origin;
            direction.y = 0.0f;
            const float lenSq = direction.x * direction.x + direction.z * direction.z;
            Ogre::Vector3 side(1.0f, 0.0f, 0.0f);
            if (lenSq > 1.0f)
            {
                const float invLen = 1.0f / sqrtf(lenSq);
                direction.x *= invLen;
                direction.z *= invLen;
                side.x = -direction.z;
                side.z = direction.x;
            }
            if ((entryIndex % 2) != 0)
                side = -side;
            return destination - (direction * kHandlerLeadOffset) +
                (side * kHandlerSideOffset);
        }

        void ClearParkOrders(Character* c)
        {
            if (!c || !c->isValid())
                return;
            c->setStandingOrder(MessageForB::M_SET_ORDER_HOLD, false);
            c->setStandingOrder(MessageForB::M_SET_ORDER_PASSIVE, false);
        }

        void ApplyParkOrders(Character* c)
        {
            if (!c || !c->isValid() || c->isDead())
                return;
            // Drop lingering sit-around / job AI before freezing in place.
            c->clearAllAIGoals();
            c->setStandingOrder(MessageForB::M_SET_ORDER_PASSIVE, true);
            c->setStandingOrder(MessageForB::M_SET_ORDER_HOLD, true);
            c->setStandingOrder(MessageForB::M_SET_ORDER_AGG, false);
            c->reThinkCurrentAIAction();
        }

        void ReinforceParkOrders(Character* c)
        {
            if (!c || !c->isValid() || c->isDead())
                return;
            // Re-assert hold without wiping medical / escort AI mid-match.
            c->setStandingOrder(MessageForB::M_SET_ORDER_PASSIVE, true);
            c->setStandingOrder(MessageForB::M_SET_ORDER_HOLD, true);
            c->setStandingOrder(MessageForB::M_SET_ORDER_AGG, false);
        }

        bool IsConsciousPrisoner(Character* c)
        {
            return c && c->isValid() && !c->isDead() && !c->isUnconcious();
        }

        void IssueLiftOrder(Character* handler, Character* prisoner);

        typedef bool (*NativeLockPredicate)(AI*, const hand&, const Ogre::Vector3&);

        NativeLockPredicate GetNativeLockVerifier()
        {
            // The SDK omits this declaration. The KenshiLib export preserves the
            // native predicate, including lock strength and broken-lock checks.
            static NativeLockPredicate verify = NULL;
            if (!verify)
            {
                HMODULE library = GetModuleHandleA("KenshiLib.dll");
                if (library)
                    verify = reinterpret_cast<NativeLockPredicate>(GetProcAddress(
                        library, "?isDoorLocked@AI@@QEAA_NAEBVhand@@AEBVVector3@Ogre@@@Z"));
            }
            return verify;
        }

        void ReportCageReturnFailure(Character* prisoner, const char* reason)
        {
            std::string message = "Could not secure ";
            message += prisoner && prisoner->isValid() ? prisoner->getName() : "prisoner";
            message += ": ";
            message += reason;
            g_cageReturnFailed = true;
            if (!g_cageReturnStatus.empty()) g_cageReturnStatus += "\n";
            g_cageReturnStatus += message;
            PGLog::Debug(("Proving Grounds: " + message).c_str());
            if (ou) ou->showPlayerAMessage(message, true);
        }

        const char* CageLockFailureReason(PrisonerMatchLogic::CageLockAction action)
        {
            using namespace PrisonerMatchLogic;
            switch (action)
            {
            case CageLockMissingDestination: return "destination cage or pole is missing or destroyed";
            case CageLockWrongOccupant: return "destination does not contain the expected prisoner";
            case CageLockMissingLock: return "destination has no operable lock";
            case CageLockBroken: return "destination cage or pole is broken";
            case CageLockNoVerifier: return "native cage lock verification is unavailable";
            case CageLockNoHandler: return "no conscious handler can lock the destination";
            case CageLockOrderFailed: return "native cage lock order was rejected";
            case CageLockTimedOut: return "native lock did not engage before timeout (blocked or broken lock)";
            default: return "native cage lock failed";
            }
        }

        // Resolves each handle afresh so destroyed/unloaded furniture or actors
        // cannot leave a pending operation dereferencing its old raw pointer.
        void ResolveReturnReferences(MatchEntry& entry)
        {
            entry.prisoner = entry.prisonerHand.getCharacter();
            entry.handler = entry.handlerHand.getCharacter();
            entry.returnCageBuilding = entry.returnCageHand.getBuilding();
            Building* building = entry.returnCageBuilding;
            if (building && (!building->isValid() || building->destroyed)) building = NULL;
            entry.returnCageBuilding = building;
            entry.returnCage = building ? AsUseableCage(building) : NULL;
        }

        bool SecureReturnedPrisoner(MatchEntry& returning, std::string& statusOut)
        {
            using namespace PrisonerMatchLogic;
            statusOut.clear();
            MatchEntry* entry = &returning;
            Character* prisoner = entry->prisoner;
            UseableStuff* cage = entry->returnCage;
            Character* handler = entry->handler;
            CageLockObservation observation = {};
            observation.destinationValid = cage && cage->isValid() && !cage->destroyed;
            observation.correctOccupant = observation.destinationValid && prisoner &&
                prisoner->isValid() && !prisoner->isDead() &&
                cage->getOccupant().getCharacter() == prisoner;
            observation.hasLock = observation.destinationValid && cage->getDoorLock() != NULL;
            observation.broken = observation.destinationValid && cage->isBroken();
            observation.handlerAvailable = IsConsciousPrisoner(handler) && handler->getAI();
            NativeLockPredicate verify = GetNativeLockVerifier();
            AI* ai = observation.handlerAvailable ? handler->getAI() :
                (prisoner && prisoner->isValid() ? prisoner->getAI() : NULL);
            observation.verifierAvailable = verify && ai;
            observation.locked = observation.destinationValid && observation.hasLock &&
                observation.verifierAvailable &&
                verify(ai, entry->returnCageHand, cage->getPosition());
            observation.orderIssued = entry->lockOrderIssued;
            observation.timedOut = GetTickCount() - entry->lockStartedAt >= kLockTimeoutMs;
            CageLockAction action = ChooseCageLockAction(observation);
            if (action == CageLockIssueOrder)
            {
                // addOrder copies SUBJECT into the queued hand (native 0x5D1799).
                // Native factory 0x32EB90 maps LOCK_DOOR (0x4D) to Task_LockDoor;
                // its update 0x337630 resolves that building and calls its virtual
                // getDoorLock. HERE predicates use the actor's current building.
                // Never send a null/broad cage target.
                ClearParkOrders(handler);
                observation.orderRejected = handler->checkPlayerOrderForProblems(LOCK_DOOR, cage);
                if (observation.orderRejected)
                    action = ChooseCageLockAction(observation);
                else
                {
                    handler->addOrder(cage, LOCK_DOOR, cage, false, true, cage->getPosition());
                    entry->lockOrderIssued = true;
                    action = CageLockWait;
                }
            }
            if (action == CageLockSecured)
                return true;
            if (action != CageLockWait)
                statusOut = CageLockFailureReason(action);
            return false;
        }

        void AdvanceCageLock(MatchEntry& entry)
        {
            ResolveReturnReferences(entry);
            std::string failure;
            const bool secured = SecureReturnedPrisoner(entry, failure);
            if (secured || !failure.empty())
            {
                entry.lockPending = false;
                entry.returnPending = false;
                if (entry.lockOrderIssued) ClearHandlerOrderQueue(entry.handler);
                if (!failure.empty()) ReportCageReturnFailure(entry.prisoner, failure.c_str());
            }
        }

        void FinishCageEntry(MatchEntry& entry)
        {
            if (entry.lockPending) return;
            ResolveReturnReferences(entry);
            Character* prisoner = entry.prisoner;
            Character* handler = entry.handler;
            UseableStuff* cage = entry.returnCage;

            if (handler && handler->isValid())
            {
                if (handler->isCarryingSomething)
                    handler->dropCarriedObject(false, false);
                ClearHandlerOrderQueue(handler);
            }

            // Never replace a different live occupant during emergency placement.
            const bool canPlace = cage && !cage->isBroken() &&
                (IsCageFree(cage) || cage->getOccupant().getCharacter() == prisoner);
            if (prisoner && prisoner->isValid() && !prisoner->isDead() && canPlace)
            {
                prisoner->setPrisonMode(true, cage);
                prisoner->setChainedMode(true, hand(static_cast<RootObjectBase*>(prisoner)));
            }
            entry.returnEscorting = false;
            entry.returnCarrying = false;
            entry.returnPickedUp = false;
            entry.returnTreating = false;
            entry.returnPending = true;
            entry.lockPending = true;
            entry.lockOrderIssued = false;
            entry.lockStartedAt = GetTickCount();
            AdvanceCageLock(entry);
        }

        void BeginReturnMovement(MatchEntry& entry)
        {
            Character* prisoner = entry.prisoner;
            Character* handler = entry.handler;
            entry.returnTreating = false;
            entry.returnElapsed = 0.0f;

            if (prisoner && prisoner->isValid())
            {
                prisoner->clearAllAIGoals();
                prisoner->reThinkCurrentAIAction();
            }
            if (handler && handler->isValid() && !handler->isDead())
            {
                handler->clearAllAIGoals();
                handler->reThinkCurrentAIAction();
            }

            if (entry.returnEscorting)
            {
                if (entry.returnCageBuilding)
                {
                    const Ogre::Vector3 cagePos = entry.returnCageBuilding->getPosition();
                    IssueRun(prisoner, cagePos);
                    IssueRun(handler, HandlerTransitTarget(entry, cagePos, 0));
                }
                return;
            }

            if (entry.returnCarrying)
            {
                if (handler && handler->isValid() && !handler->isDead())
                    IssueLiftOrder(handler, prisoner);
                else
                    FinishCageEntry(entry);
                return;
            }

            FinishCageEntry(entry);
        }

        void ReleasePrisonerFromCage(MatchEntry& entry)
        {
            Character* prisoner = entry.prisoner;
            if (!prisoner || !prisoner->isValid() || entry.released)
                return;

            entry.wasChained = prisoner->isChainedMode();
            ClearHandlerOrderQueue(entry.handler);
            prisoner->setPrisonMode(false, NULL);
            if (entry.wasChained)
                prisoner->setChainedMode(false, hand(static_cast<RootObjectBase*>(prisoner)));

            entry.released = true;
            entry.unlockPending = false;
            entry.newlyReleased = true;
            g_protectionImmediate = true;

            const int lineCount = static_cast<int>(
                sizeof(kPrisonerFightLines) / sizeof(kPrisonerFightLines[0]));
            prisoner->say(kPrisonerFightLines[rand() % lineCount]);
        }

        void IssueLiftOrder(Character* handler, Character* prisoner)
        {
            if (!handler || !handler->isValid() || !prisoner || !prisoner->isValid())
                return;

            ClearParkOrders(handler);
            const Ogre::Vector3 loc = prisoner->getPosition();
            handler->addOrder(
                NULL,
                LIFT_PERSON_PLAYER_ORDER,
                prisoner,
                false,
                true,
                loc);
            // Let LIFT_PERSON_PLAYER_ORDER walk to and pick up the prisoner.
            // Calling pickupObject here relocates a KO'd prisoner immediately.
        }

        void DisengageOutsiderFromPrisoner(Character* outsider, Character* prisoner)
        {
            if (!outsider || !outsider->isValid() || !prisoner || !prisoner->isValid())
                return;
            if (SparSession::IsSparringOpponent(outsider, prisoner))
                return;

            outsider->clearTempEnemyStatus(prisoner);
            prisoner->clearTempEnemyStatus(outsider);
            outsider->removeJob(FOCUSED_MELEE_ATTACK);
            outsider->removeJob(UNPROVOKED_FOCUSED_MELEE_ATTACK);
            hand target = outsider->getAttackTarget();
            if (target.isValid() && target.getCharacter() == prisoner)
                outsider->endCombatMode();
        }

        void MakeEmptyEntry(MatchEntry& entry)
        {
            entry.prisoner = NULL;
            entry.cageUseable = NULL;
            entry.handler = NULL;
            entry.originalCageIndex = -1;
            entry.wasChained = false;
            entry.released = false;
            entry.unlockPending = false;
            entry.unlockArrived = false;
            entry.unlockElapsed = 0.0f;
            entry.unlockHoldElapsed = 0.0f;
            entry.newlyReleased = false;
            entry.returnCage = NULL;
            entry.returnCageBuilding = NULL;
            entry.lockPending = false;
            entry.lockOrderIssued = false;
            entry.lockStartedAt = 0;
            entry.returnPending = false;
            entry.returnEscorting = false;
            entry.returnCarrying = false;
            entry.returnPickedUp = false;
            entry.returnTreating = false;
            entry.returnElapsed = 0.0f;
            entry.ringsideTreating = false;
            entry.ringsideTreatElapsed = 0.0f;
        }
    }

    bool HasRuntimeActivity()
    {
        return g_unlocking || g_returning || !g_matchEntries.empty();
    }

    void AbandonWorldState()
    {
        g_cageReturnFailed = false;
        g_cageReturnStatus.clear();
        g_rosterPrisoners.clear();
        g_nearbyCages.clear();
        g_matchEntries.clear();
        g_matchHandlers.clear();
        g_matchPrisonersScratch.clear();
        g_matchHasPrisoner = false;
        g_returning = false;
        g_unlocking = false;
        g_handlerStatesRemembered = false;
        g_returnLastTick = 0;
        g_unlockLastTick = 0;
        g_ringsideAidLastTick = 0;
        ResetMatchUpkeep();
    }

    void CollectNearbyCagePrisoners(Building* registry, std::vector<Character*>& out)
    {
        out.clear();
        g_rosterPrisoners.clear();
        g_nearbyCages.clear();

        if (!registry)
        {
            PGLog::Debug("Proving Grounds: prisoner collect skipped — no bound registry");
            return;
        }

        CollectCagesNearRegistry(registry, g_nearbyCages);

        // Occupant path (cage → prisoner).
        for (size_t i = 0; i < g_nearbyCages.size(); ++i)
        {
            UseableStuff* useable = g_nearbyCages[i].useable;
            if (!useable)
                continue;

            const hand& occupantHand = useable->getOccupant();
            if (!occupantHand.isValid())
                continue;

            Character* occupant = occupantHand.getCharacter();
            if (!occupant || !occupant->isValid() || occupant->isDead())
                continue;

            // Prefer IN_PRISON; also accept any living occupant of a player cage.
            if (!ContainsCharacter(out, occupant))
                out.push_back(occupant);
        }

        // Character path (prisoner → cage). Catches cases where furniture
        // ownership / getOccupant fails but IN_PRISON is set.
        CollectImprisonedCharactersNearRegistry(registry, out);

        g_rosterPrisoners = out;

        char buf[128];
        sprintf_s(
            buf,
            "Proving Grounds: prisoner roster size=%d cages=%d",
            static_cast<int>(out.size()),
            static_cast<int>(g_nearbyCages.size()));
        PGLog::Debug(buf);
    }

    bool IsRosterPrisoner(Character* c)
    {
        if (!c)
            return false;
        if (HasMatchPrisonerEntry(c))
            return true;
        return ContainsCharacter(g_rosterPrisoners, c);
    }

    bool IsMatchPrisoner(Character* c)
    {
        return HasMatchPrisonerEntry(c);
    }

    UseableStuff* FindCageForOccupant(Character* c)
    {
        if (!c || !c->isValid())
            return NULL;

        if (c->inSomething == IN_PRISON && c->inWhat.isValid())
        {
            Building* building = c->inWhat.getBuilding();
            UseableStuff* useable = AsUseableCage(building);
            if (useable)
                return useable;
        }

        for (size_t i = 0; i < g_nearbyCages.size(); ++i)
        {
            UseableStuff* useable = g_nearbyCages[i].useable;
            if (!useable)
                continue;

            const hand& occupantHand = useable->getOccupant();
            if (!occupantHand.isValid())
                continue;

            Character* occupant = occupantHand.getCharacter();
            if (occupant == c)
                return useable;
        }

        for (size_t i = 0; i < g_matchEntries.size(); ++i)
        {
            if (g_matchEntries[i].prisoner == c && g_matchEntries[i].cageUseable)
                return g_matchEntries[i].cageUseable;
        }

        return NULL;
    }

    int CountHandlerCandidates(
        const std::vector<Character*>& squadInRange,
        const std::vector<Character*>& matchFighters)
    {
        int count = 0;
        for (size_t i = 0; i < squadInRange.size(); ++i)
        {
            Character* candidate = squadInRange[i];
            if (!candidate)
                continue;

            bool isFighter = false;
            for (size_t f = 0; f < matchFighters.size(); ++f)
            {
                if (matchFighters[f] == candidate)
                {
                    isFighter = true;
                    break;
                }
            }
            if (!isFighter)
                ++count;
        }
        return count;
    }

    bool PrepareMatch(
        const std::vector<Character*>& prisonerFighters,
        const std::vector<Character*>& handlerCandidates,
        std::string& statusOut)
    {
        if (g_returning || HasPendingCageLocks())
        {
            statusOut = "Wait for prisoner return and cage locking to finish";
            return false;
        }
        ClearMatchState();
        g_cageReturnFailed = false;
        g_cageReturnStatus.clear();

        if (handlerCandidates.size() < prisonerFighters.size())
        {
            statusOut = "Not enough handlers for prisoners";
            return false;
        }

        for (size_t i = 0; i < prisonerFighters.size(); ++i)
        {
            if (!handlerCandidates[i])
            {
                statusOut = "Not enough handlers for prisoners";
                return false;
            }
        }

        Building* registry = ArenaIngress::GetBoundRegistry();
        std::vector<NearbyCage> cages;
        CollectCagesNearRegistry(registry, cages);
        g_nearbyCages = cages;

        std::vector<MatchEntry> planned;
        planned.reserve(prisonerFighters.size());

        for (size_t i = 0; i < prisonerFighters.size(); ++i)
        {
            Character* prisoner = prisonerFighters[i];
            UseableStuff* cage = FindCageForOccupant(prisoner);
            if (!cage)
            {
                statusOut = "Prisoner has no cage";
                return false;
            }

            MatchEntry entry;
            MakeEmptyEntry(entry);
            entry.prisoner = prisoner;
            entry.cageUseable = cage;
            entry.cageHand = hand(static_cast<RootObjectBase*>(cage));
            entry.handler = handlerCandidates[i];
            entry.prisonerHand = hand(static_cast<RootObjectBase*>(prisoner));
            entry.handlerHand = hand(static_cast<RootObjectBase*>(entry.handler));
            entry.originalCageIndex = FindCageIndex(cage, cages);
            planned.push_back(entry);
        }

        for (size_t i = 0; i < planned.size(); ++i)
        {
            // Stay caged until the handler arrives to "unlock" them.
            g_matchEntries.push_back(planned[i]);
            g_matchHandlers.push_back(planned[i].handler);
        }

        g_matchHasPrisoner = !prisonerFighters.empty();
        statusOut.clear();
        return true;
    }

    const std::vector<Character*>& GetMatchHandlers()
    {
        return g_matchHandlers;
    }

    const std::vector<Character*>& GetMatchPrisoners()
    {
        g_matchPrisonersScratch.clear();
        for (size_t i = 0; i < g_matchEntries.size(); ++i)
            g_matchPrisonersScratch.push_back(g_matchEntries[i].prisoner);
        return g_matchPrisonersScratch;
    }

    const std::vector<Character*>& GetRosterPrisoners()
    {
        return g_rosterPrisoners;
    }

    void ForgetRosterPrisoner(Character* prisoner)
    {
        for (size_t i = 0; i < g_rosterPrisoners.size(); )
        {
            if (g_rosterPrisoners[i] == prisoner)
                g_rosterPrisoners.erase(g_rosterPrisoners.begin() + i);
            else
                ++i;
        }
    }

    bool MatchIncludesPrisoner()
    {
        return g_matchHasPrisoner;
    }

    bool IsMatchPrisonerReleased(Character* c)
    {
        if (!c)
            return false;
        for (size_t i = 0; i < g_matchEntries.size(); ++i)
        {
            if (g_matchEntries[i].prisoner == c)
                return g_matchEntries[i].released;
        }
        return false;
    }

    bool AllMatchPrisonersReleased()
    {
        if (!g_matchHasPrisoner)
            return true;
        for (size_t i = 0; i < g_matchEntries.size(); ++i)
        {
            if (!g_matchEntries[i].released)
                return false;
        }
        return true;
    }

    void BeginHandlerUnlocks()
    {
        if (g_matchEntries.empty())
        {
            g_unlocking = false;
            return;
        }

        g_unlocking = true;
        g_unlockLastTick = GetTickCount();

        if (!g_handlerStatesRemembered && !g_matchHandlers.empty())
        {
            FightStarter::RememberMatchFighters(
                &g_matchHandlers[0], static_cast<int>(g_matchHandlers.size()));
            g_handlerStatesRemembered = true;
        }
        for (size_t i = 0; i < g_matchEntries.size(); ++i)
        {
            MatchEntry& entry = g_matchEntries[i];
            entry.released = false;
            entry.unlockPending = true;
            entry.unlockArrived = false;
            entry.unlockElapsed = 0.0f;
            entry.unlockHoldElapsed = 0.0f;
            entry.newlyReleased = false;

            Character* handler = entry.handler;
            Building* cageBuilding = entry.cageUseable
                ? static_cast<Building*>(entry.cageUseable)
                : NULL;
            if (handler && handler->isValid() && !handler->isDead() && cageBuilding)
                IssueMove(handler, cageBuilding->getPosition());
        }
    }

    void TickHandlerUnlocks()
    {
        if (!g_unlocking)
            return;

        const DWORD now = GetTickCount();
        float dt = 0.0f;
        if (g_unlockLastTick != 0)
            dt = static_cast<float>(now - g_unlockLastTick) / 1000.0f;
        g_unlockLastTick = now;
        if (dt < 0.0f)
            dt = 0.0f;
        if (dt > 1.0f)
            dt = 1.0f;

        const float arriveSq = kUnlockArriveRadius * kUnlockArriveRadius;
        bool anyPending = false;

        for (size_t i = 0; i < g_matchEntries.size(); ++i)
        {
            MatchEntry& entry = g_matchEntries[i];
            if (entry.released || !entry.unlockPending)
                continue;

            anyPending = true;
            entry.unlockElapsed += dt;

            Character* handler = entry.handler;
            Building* cageBuilding = entry.cageUseable
                ? static_cast<Building*>(entry.cageUseable)
                : NULL;

            bool handlerNear = false;
            if (handler && handler->isValid() && !handler->isDead() && cageBuilding)
            {
                handlerNear = HorizontalDistSq(
                    handler->getPosition(),
                    cageBuilding->getPosition()) <= arriveSq;
            }

            const bool timedOut = entry.unlockElapsed >= kUnlockTimeoutSec;
            if (handlerNear)
            {
                if (!entry.unlockArrived)
                {
                    entry.unlockArrived = true;
                    entry.unlockHoldElapsed = 0.0f;
                    // Fake unlock moment at the cage.
                    if (handler && cageBuilding)
                    {
                        handler->addOrder(
                            cageBuilding,
                            RELEASE_PRISONER,
                            entry.prisoner,
                            false,
                            true,
                            cageBuilding->getPosition());
                    }
                }
                entry.unlockHoldElapsed += dt;
                if (entry.unlockHoldElapsed >= kUnlockHoldSec || timedOut)
                    ReleasePrisonerFromCage(entry);
            }
            else if (timedOut)
            {
                ReleasePrisonerFromCage(entry);
            }
            else if (handler && handler->isValid() && !handler->isDead() && cageBuilding)
            {
                IssueMove(handler, cageBuilding->getPosition());
            }
        }

        if (!anyPending)
            g_unlocking = false;
    }

    bool ConsumeNewlyReleased(Character* prisoner)
    {
        if (!prisoner)
            return false;
        for (size_t i = 0; i < g_matchEntries.size(); ++i)
        {
            if (g_matchEntries[i].prisoner != prisoner)
                continue;
            if (!g_matchEntries[i].newlyReleased)
                return false;
            g_matchEntries[i].newlyReleased = false;
            return true;
        }
        return false;
    }

    void SendPairToArena(
        Character* prisoner, const Ogre::Vector3& prisonerTarget)
    {
        if (!prisoner || !prisoner->isValid())
            return;

        for (size_t i = 0; i < g_matchEntries.size(); ++i)
        {
            MatchEntry& entry = g_matchEntries[i];
            if (entry.prisoner != prisoner || !entry.released)
                continue;

            IssueRun(prisoner, prisonerTarget);
            Character* handler = entry.handler;
            if (handler && handler->isValid() && !handler->isDead())
                IssueRun(handler, HandlerTransitTarget(entry, prisonerTarget, i));
            return;
        }
    }

    void ProtectMatchPrisoners()
    {
        if (!g_matchHasPrisoner)
            return;

        for (size_t i = 0; i < g_matchEntries.size(); ++i)
        {
            MatchEntry& entry = g_matchEntries[i];
            if (g_returning)
            {
                ResolveReturnReferences(entry);
                if (entry.lockPending || !entry.returnPending) continue;
            }
            Character* prisoner = entry.prisoner;
            if (!prisoner || !prisoner->isValid() || prisoner->isDead())
                continue;
            if (!entry.released)
                continue;

            lektor<hand> attackers;
            prisoner->getAllAttackers(attackers);
            for (size_t a = 0; a < attackers.size(); ++a)
            {
                Character* outsider = attackers[static_cast<unsigned int>(a)].getCharacter();
                DisengageOutsiderFromPrisoner(outsider, prisoner);
            }

            // If the prisoner is chasing a non-match target, break it off.
            hand targetHand = prisoner->getAttackTarget();
            if (targetHand.isValid())
            {
                Character* target = targetHand.getCharacter();
                if (target && !SparSession::IsSparringOpponent(prisoner, target))
                {
                    prisoner->clearTempEnemyStatus(target);
                    prisoner->removeJob(FOCUSED_MELEE_ATTACK);
                    prisoner->removeJob(UNPROVOKED_FOCUSED_MELEE_ATTACK);
                    prisoner->endCombatMode();
                }
            }

            // Mark nearby squad handlers as temp-allies of the prisoner so AI
            // is less eager to pull them into world fights.
            Character* handler = entry.handler;
            if (handler && handler->isValid())
            {
                handler->rememberCharacter(prisoner, ST_TEMPORARY_ALLY);
                prisoner->rememberCharacter(handler, ST_TEMPORARY_ALLY);
            }
        }
    }

    void TickRingsideAid()
    {
        if (!SparSession::IsActive() || !g_matchHasPrisoner ||
            !ArenaMedical::ShouldStabilizePrisoner(ArenaMedical::GetProtocol()))
        {
            g_ringsideAidLastTick = 0;
            return;
        }

        const DWORD now = GetTickCount();
        float dt = g_ringsideAidLastTick == 0 ? 0.0f :
            static_cast<float>(now - g_ringsideAidLastTick) / 1000.0f;
        g_ringsideAidLastTick = now;
        if (dt < 0.0f) dt = 0.0f;
        if (dt > 1.0f) dt = 1.0f;

        for (size_t i = 0; i < g_matchEntries.size(); ++i)
        {
            MatchEntry& entry = g_matchEntries[i];
            Character* prisoner = entry.prisoner;
            Character* handler = entry.handler;
            const bool eligible = prisoner && prisoner->isValid() &&
                !prisoner->isDead() && SparSession::IsEliminated(prisoner) &&
                ArenaMedical::NeedsStabilization(prisoner);
            if (!eligible)
            {
                if (entry.ringsideTreating)
                {
                    entry.ringsideTreating = false;
                    entry.ringsideTreatElapsed = 0.0f;
                    ApplyParkOrders(handler);
                }
                continue;
            }
            if (!handler || !handler->isValid() || handler->isDead())
                continue;

            const float previous = entry.ringsideTreatElapsed;
            entry.ringsideTreatElapsed += dt;
            if (!entry.ringsideTreating)
            {
                entry.ringsideTreating = true;
                entry.ringsideTreatElapsed = 0.0f;
                handler->clearAllAIGoals();
                handler->reThinkCurrentAIAction();
                ClearParkOrders(handler);
                ArenaMedical::IssueStabilizeOrder(handler, prisoner);
                continue;
            }
            const int retry = static_cast<int>(entry.ringsideTreatElapsed / kTreatmentRetrySec);
            const int previousRetry = static_cast<int>(previous / kTreatmentRetrySec);
            if (retry != previousRetry)
            {
                ClearParkOrders(handler);
                ArenaMedical::IssueStabilizeOrder(handler, prisoner);
            }
        }
    }

    void ParkMatchHandlers()
    {
        for (size_t i = 0; i < g_matchHandlers.size(); ++i)
            ApplyParkOrders(g_matchHandlers[i]);
    }

    void ReinforceParkedHandlers()
    {
        for (size_t i = 0; i < g_matchHandlers.size(); ++i)
            ReinforceParkOrders(g_matchHandlers[i]);
    }

    void TickMatchUpkeep()
    {
        if (!g_matchHasPrisoner)
        {
            ResetMatchUpkeep();
            return;
        }

        const float dt = AdvanceMatchUpkeepClock();
        if (g_protectionImmediate ||
            FrameCadencePolicy::Advance(
                g_protectionElapsedSec, dt, kProtectionIntervalSec))
        {
            ProtectMatchPrisoners();
            g_protectionImmediate = false;
        }

        if (SparSession::IsActive() &&
            FrameCadencePolicy::Advance(
                g_handlerElapsedSec, dt, kHandlerReinforceIntervalSec))
        {
            ReinforceParkedHandlers();
        }
    }

    void BeginReturnToCages(std::string& statusOut)
    {
        statusOut = g_cageReturnStatus;
        // Cancellation/pre-save may revisit a return. Never replace its pending
        // destination, handler, native order, or timeout with a fresh attempt.
        if (g_returning || HasPendingCageLocks()) return;
        g_returning = false;
        g_returnLastTick = 0;
        g_unlocking = false;
        for (size_t i = 0; i < g_matchEntries.size(); ++i)
            g_matchEntries[i].unlockPending = false;

        if (g_matchEntries.empty())
        {
            ClearMatchState();
            return;
        }

        Building* registry = ArenaIngress::GetBoundRegistry();
        std::vector<NearbyCage> cages;
        CollectCagesNearRegistry(registry, cages);

        const int count = static_cast<int>(cages.size());
        if (count <= 0)
        {
            for (size_t i = 0; i < g_matchEntries.size(); ++i)
            {
                Character* prisoner = g_matchEntries[i].prisonerHand.getCharacter();
                ReportCageReturnFailure(prisoner, "no destination cage or pole is available");
            }
            statusOut = g_cageReturnStatus;
            ClearMatchState();
            return;
        }

        bool* free = new bool[static_cast<size_t>(count)];
        float* distToPrisonerSq = new float[static_cast<size_t>(count)];
        bool* inRegistryRange = new bool[static_cast<size_t>(count)];
        bool* alreadyReserved = new bool[static_cast<size_t>(count)];

        for (int i = 0; i < count; ++i)
        {
            inRegistryRange[i] = registry
                ? IsWithinRegistryRange(registry, cages[static_cast<size_t>(i)].building)
                : false;
            free[i] = IsCageFree(cages[static_cast<size_t>(i)].useable);
            alreadyReserved[i] = false;
        }

        bool anyPending = false;

        for (size_t e = 0; e < g_matchEntries.size(); ++e)
        {
            MatchEntry& entry = g_matchEntries[e];
            entry.returnPending = false;
            entry.returnEscorting = false;
            entry.returnCarrying = false;
            entry.returnPickedUp = false;
            entry.returnTreating = false;
            entry.returnElapsed = 0.0f;
            entry.returnCage = NULL;
            entry.returnCageBuilding = NULL;

            entry.prisoner = entry.prisonerHand.getCharacter();
            entry.handler = entry.handlerHand.getCharacter();
            Character* prisoner = entry.prisoner;
            if (!prisoner || !prisoner->isValid() || prisoner->isDead())
            {
                ReportCageReturnFailure(prisoner, "prisoner is missing or dead");
                continue;
            }

            // An aborted unlock may leave an occupied yet unlocked cage.
            // Verify it through the same placement/lock path as ordinary return.
            if (!entry.released)
            {
                entry.returnCageHand = entry.cageHand;
                FinishCageEntry(entry);
                if (entry.returnPending) anyPending = true;
                continue;
            }

            // The fresh cage scan may have a different order after destruction.
            entry.originalCageIndex = FindCageIndex(
                AsUseableCage(entry.cageHand.getBuilding()), cages);
            const Ogre::Vector3 prisonerPos = prisoner->getPosition();
            for (int i = 0; i < count; ++i)
            {
                distToPrisonerSq[i] = HorizontalDistSq(
                    prisonerPos,
                    cages[static_cast<size_t>(i)].building->getPosition());
            }

            // Original cage is free again once the prisoner left it.
            if (entry.originalCageIndex >= 0 &&
                entry.originalCageIndex < count)
            {
                free[entry.originalCageIndex] = IsCageFree(
                    cages[static_cast<size_t>(entry.originalCageIndex)].useable);
            }

            PrisonerMatchLogic::CagePick pick = PrisonerMatchLogic::ChooseReturnCage(
                entry.originalCageIndex,
                free,
                distToPrisonerSq,
                inRegistryRange,
                alreadyReserved,
                count);

            if (pick.index < 0)
            {
                ReportCageReturnFailure(prisoner, "no free destination cage or pole is available");
                continue;
            }

            alreadyReserved[pick.index] = true;
            UseableStuff* cage = cages[static_cast<size_t>(pick.index)].useable;
            entry.returnCage = cage;
            entry.returnCageBuilding = static_cast<Building*>(cage);
            entry.returnCageHand = hand(static_cast<RootObjectBase*>(cage));

            const PrisonerMatchLogic::ReturnAction action =
                PrisonerMatchLogic::ChooseReturnAction(
                    IsConsciousPrisoner(prisoner),
                    prisoner->isDead());

            Character* handler = entry.handler;
            if (handler && handler->isValid())
                ClearParkOrders(handler);

            entry.returnEscorting = action == PrisonerMatchLogic::ReturnEscortWalk;
            entry.returnCarrying = action == PrisonerMatchLogic::ReturnCarry;
            entry.returnPending = true;

            const bool canTreat = handler && handler->isValid() && !handler->isDead();
            if (ArenaMedical::ShouldStabilizePrisoner(ArenaMedical::GetProtocol()) &&
                canTreat && ArenaMedical::NeedsStabilization(prisoner))
            {
                entry.returnTreating = true;
                entry.returnElapsed = 0.0f;
                anyPending = true;
                ArenaMedical::IssueStabilizeOrder(handler, prisoner);
            }
            else
            {
                BeginReturnMovement(entry);
                if (entry.returnPending)
                    anyPending = true;
            }
        }

        delete[] free;
        delete[] distToPrisonerSq;
        delete[] inRegistryRange;
        delete[] alreadyReserved;

        if (g_cageReturnFailed) statusOut = g_cageReturnStatus;
        if (anyPending)
        {
            g_returning = true;
            g_returnLastTick = GetTickCount();
            if (statusOut.empty())
                statusOut = "Returning prisoners to cages...";
        }
        else
        {
            ClearMatchState();
        }
    }

    void TickReturn()
    {
        TickPendingCageLocks();
        if (!g_returning)
            return;

        const DWORD now = GetTickCount();
        float dt = 0.0f;
        if (g_returnLastTick != 0)
            dt = static_cast<float>(now - g_returnLastTick) / 1000.0f;
        g_returnLastTick = now;
        if (dt < 0.0f)
            dt = 0.0f;
        if (dt > 1.0f)
            dt = 1.0f;

        const float arriveSq = kReturnArriveRadius * kReturnArriveRadius;
        bool anyPending = false;

        for (size_t e = 0; e < g_matchEntries.size(); ++e)
        {
            MatchEntry& entry = g_matchEntries[e];
            if (!entry.returnPending || entry.lockPending)
                continue;

            ResolveReturnReferences(entry);
            Character* prisoner = entry.prisoner;
            Character* handler = entry.handler;
            entry.returnElapsed += dt;

            if (!prisoner || !prisoner->isValid() || prisoner->isDead())
            {
                entry.returnPending = false;
                ReportCageReturnFailure(prisoner, "prisoner is missing or dead");
                continue;
            }
            if (!entry.returnCage || entry.returnCage->isBroken())
            {
                FinishCageEntry(entry);
                continue;
            }

            const bool timedOut = entry.returnElapsed >= kReturnTimeoutSec;

            if (entry.returnTreating)
            {
                const bool treatmentTimedOut = entry.returnElapsed >= kTreatmentTimeoutSec;
                const bool medicUnavailable = !handler || !handler->isValid() || handler->isDead();
                if (!ArenaMedical::NeedsStabilization(prisoner) ||
                    treatmentTimedOut || medicUnavailable)
                {
                    BeginReturnMovement(entry);
                    if (entry.returnPending)
                        anyPending = true;
                    continue;
                }

                const int slot = static_cast<int>(entry.returnElapsed / kTreatmentRetrySec);
                const int prevSlot = static_cast<int>((entry.returnElapsed - dt) / kTreatmentRetrySec);
                if (slot != prevSlot)
                    ArenaMedical::IssueStabilizeOrder(handler, prisoner);
                anyPending = true;
                continue;
            }

            if (entry.returnCarrying)
            {
                const bool carrying =
                    (handler && handler->isValid() &&
                        (handler->isCarryingSomething || prisoner->isBeingCarried()));

                if (!entry.returnPickedUp)
                {
                    if (carrying)
                    {
                        entry.returnPickedUp = true;
                        if (entry.returnCageBuilding && handler)
                            IssueMove(handler, entry.returnCageBuilding->getPosition());
                    }
                    else if (timedOut)
                    {
                        FinishCageEntry(entry);
                        continue;
                    }
                    else if (handler && handler->isValid() && !handler->isDead())
                    {
                        const int slot = static_cast<int>(
                            entry.returnElapsed / kCarryPickupRetrySec);
                        const int prevSlot = static_cast<int>(
                            (entry.returnElapsed - dt) / kCarryPickupRetrySec);
                        if (slot != prevSlot)
                            IssueLiftOrder(handler, prisoner);
                    }
                    anyPending = true;
                    continue;
                }

                bool arrived = false;
                if (handler && handler->isValid() && entry.returnCageBuilding)
                {
                    arrived = HorizontalDistSq(
                        handler->getPosition(),
                        entry.returnCageBuilding->getPosition()) <= arriveSq;
                }

                if (arrived || timedOut)
                {
                    FinishCageEntry(entry);
                    continue;
                }

                if (handler && handler->isValid() && entry.returnCageBuilding)
                    IssueMove(handler, entry.returnCageBuilding->getPosition());
                anyPending = true;
                continue;
            }

            // Walking escort path.
            bool arrived = false;
            if (entry.returnCageBuilding)
            {
                arrived = HorizontalDistSq(
                    prisoner->getPosition(),
                    entry.returnCageBuilding->getPosition()) <= arriveSq;
            }

            if (arrived || timedOut)
            {
                FinishCageEntry(entry);
                continue;
            }

            if (entry.returnCageBuilding)
            {
                const Ogre::Vector3 cagePos = entry.returnCageBuilding->getPosition();
                IssueRun(prisoner, cagePos);
                IssueRun(handler, HandlerTransitTarget(entry, cagePos, e));
            }
            anyPending = true;
        }

        for (size_t e = 0; e < g_matchEntries.size(); ++e)
            if (g_matchEntries[e].returnPending) anyPending = true;
        if (!anyPending)
        {
            g_returning = false;
            ClearMatchState();
        }
    }

    bool IsReturning()
    {
        return g_returning;
    }

    bool HasPendingCageLocks()
    {
        for (size_t e = 0; e < g_matchEntries.size(); ++e)
            if (g_matchEntries[e].lockPending) return true;
        return false;
    }

    bool HasCageReturnFailure() { return g_cageReturnFailed; }
    const std::string& GetCageReturnStatus() { return g_cageReturnStatus; }

    void TickPendingCageLocks()
    {
        bool anyPending = false;
        for (size_t e = 0; e < g_matchEntries.size(); ++e)
        {
            MatchEntry& entry = g_matchEntries[e];
            if (entry.lockPending) AdvanceCageLock(entry);
            if (entry.returnPending) anyPending = true;
        }
        if (g_returning && !anyPending)
        {
            g_returning = false;
            ClearMatchState();
        }
    }

    void ReturnAllToCages(std::string& statusOut)
    {
        // Force placement, then retain native lock work until verified/failed.
        BeginReturnToCages(statusOut);
        bool anyPending = false;
        for (size_t e = 0; e < g_matchEntries.size(); ++e)
        {
            MatchEntry& entry = g_matchEntries[e];
            if (entry.returnPending && !entry.lockPending) FinishCageEntry(entry);
            if (entry.returnPending) anyPending = true;
        }
        g_returning = anyPending;
        if (!anyPending) ClearMatchState();
        if (g_cageReturnFailed) statusOut = g_cageReturnStatus;
        else if (anyPending) statusOut = "Waiting for native cage locks before return completes...";
        else statusOut.clear();
    }

    void ClearMatchState()
    {
        // UI cancellation must not cancel the handler's queued native lock or
        // erase the handles required by the GUI/lifecycle tick.
        if (HasPendingCageLocks()) return;
        if (g_handlerStatesRemembered && !g_matchHandlers.empty())
        {
            std::vector<Character*> survivingHandlers;
            for (size_t i = 0; i < g_matchEntries.size(); ++i)
            {
                Character* handler = g_matchEntries[i].handlerHand.getCharacter();
                if (handler && handler->isValid()) survivingHandlers.push_back(handler);
            }
            if (!survivingHandlers.empty())
                FightStarter::DisengageMatch(&survivingHandlers[0],
                    static_cast<int>(survivingHandlers.size()));
        }

        g_matchEntries.clear();
        g_matchHandlers.clear();
        g_handlerStatesRemembered = false;
        g_matchHasPrisoner = false;
        g_returning = false;
        g_unlocking = false;
        g_returnLastTick = 0;
        g_unlockLastTick = 0;
        g_ringsideAidLastTick = 0;
        ResetMatchUpkeep();
    }
}
