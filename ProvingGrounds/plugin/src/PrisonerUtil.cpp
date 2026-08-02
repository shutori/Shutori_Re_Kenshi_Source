#include "PrisonerUtil.h"

#include "ArenaIngress.h"
#include "FightStarter.h"
#include "PrisonerMatchLogic.h"
#include "PrisonerUtilInternal.h"
#include "SparSession.h"

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <limits>
#include <windows.h>

#include <Debug.h>

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
            bool returnPending;
            bool returnEscorting;
            bool returnCarrying;
            bool returnPickedUp;
            float returnElapsed;
        };

        static std::vector<Character*> g_rosterPrisoners;
        static std::vector<NearbyCage> g_nearbyCages;
        static std::vector<MatchEntry> g_matchEntries;
        static std::vector<Character*> g_matchHandlers;
        static std::vector<Character*> g_matchPrisonersScratch;
        static bool g_matchHasPrisoner = false;
        static bool g_returning = false;
        static bool g_unlocking = false;
        static bool g_handlerStatesRemembered = false;
        static DWORD g_returnLastTick = 0;
        static DWORD g_unlockLastTick = 0;

        static const int kSphereSearchMax = 512;
        static const float kReturnArriveRadius = 120.0f;
        static const float kReturnTimeoutSec = 60.0f;
        static const float kUnlockArriveRadius = 120.0f;
        static const float kUnlockHoldSec = 0.45f;
        static const float kUnlockTimeoutSec = 45.0f;
        static const float kCarryPickupRetrySec = 2.5f;
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
                DebugLog(buf);
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
            DebugLog(buf);
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
            DebugLog(buf);
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
            c->setStandingOrder(MessageForB::M_SET_ORDER_PASSIVE, true);
            c->setStandingOrder(MessageForB::M_SET_ORDER_HOLD, true);
            c->setStandingOrder(MessageForB::M_SET_ORDER_AGG, false);
        }

        bool IsConsciousPrisoner(Character* c)
        {
            return c && c->isValid() && !c->isDead() && !c->isUnconcious();
        }

        void FinishCageEntry(MatchEntry& entry)
        {
            Character* prisoner = entry.prisoner;
            Character* handler = entry.handler;
            UseableStuff* cage = entry.returnCage;

            if (handler && handler->isValid() && handler->isCarryingSomething)
                handler->dropCarriedObject(false, false);

            if (prisoner && prisoner->isValid() && !prisoner->isDead() && cage)
            {
                prisoner->setPrisonMode(true, cage);
                if (entry.wasChained || prisoner->isSlave() == IS_SLAVE)
                {
                    prisoner->setChainedMode(
                        true,
                        hand(static_cast<RootObjectBase*>(prisoner)));
                }
            }
            entry.returnPending = false;
            entry.returnEscorting = false;
            entry.returnCarrying = false;
            entry.returnPickedUp = false;
        }

        void ReleasePrisonerFromCage(MatchEntry& entry)
        {
            Character* prisoner = entry.prisoner;
            if (!prisoner || !prisoner->isValid() || entry.released)
                return;

            entry.wasChained = prisoner->isChainedMode();
            prisoner->setPrisonMode(false, NULL);
            if (entry.wasChained)
                prisoner->setChainedMode(false, hand(static_cast<RootObjectBase*>(prisoner)));

            entry.released = true;
            entry.unlockPending = false;
            entry.newlyReleased = true;

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
            entry.returnPending = false;
            entry.returnEscorting = false;
            entry.returnCarrying = false;
            entry.returnPickedUp = false;
            entry.returnElapsed = 0.0f;
        }
    }

    void CollectNearbyCagePrisoners(Building* registry, std::vector<Character*>& out)
    {
        out.clear();
        g_rosterPrisoners.clear();
        g_nearbyCages.clear();

        if (!registry)
        {
            DebugLog("Proving Grounds: prisoner collect skipped — no bound registry");
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
        DebugLog(buf);
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
        ClearMatchState();

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

    bool AllMatchHandlersReady(float maxDistanceFromPrisoner)
    {
        if (g_matchEntries.empty())
            return true;
        const float maxDistanceSq = maxDistanceFromPrisoner * maxDistanceFromPrisoner;
        for (size_t i = 0; i < g_matchEntries.size(); ++i)
        {
            const MatchEntry& entry = g_matchEntries[i];
            if (!entry.released)
                return false;
            Character* prisoner = entry.prisoner;
            Character* handler = entry.handler;
            if (!prisoner || !prisoner->isValid() ||
                !handler || !handler->isValid() || handler->isDead())
                return false;
            if (HorizontalDistSq(prisoner->getPosition(), handler->getPosition()) > maxDistanceSq)
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

    void EscortHandlerBesidePrisoner(
        Character* prisoner, const Ogre::Vector3& prisonerTarget)
    {
        if (!prisoner || !prisoner->isValid())
            return;

        for (size_t i = 0; i < g_matchEntries.size(); ++i)
        {
            MatchEntry& entry = g_matchEntries[i];
            if (entry.prisoner != prisoner)
                continue;

            Character* handler = entry.handler;
            if (!handler || !handler->isValid() || handler->isDead())
                return;

            Ogre::Vector3 direction = prisonerTarget - prisoner->getPosition();
            direction.y = 0.0f;
            const float lenSq = (direction.x * direction.x) +
                (direction.z * direction.z);
            Ogre::Vector3 side(1.0f, 0.0f, 0.0f);
            if (lenSq > 1.0f)
            {
                const float invLen = 1.0f / sqrtf(lenSq);
                direction.x *= invLen;
                direction.z *= invLen;
                side.x = -direction.z;
                side.z = direction.x;
            }
            if ((i % 2) != 0)
                side = -side;

            const Ogre::Vector3 escortTarget = prisoner->getPosition() +
                (direction * kHandlerLeadOffset) +
                (side * kHandlerSideOffset);
            IssueMove(handler, escortTarget);
            CharMovement* movement = handler->getMovement();
            if (movement)
            {
                movement->setDesiredSpeedOrders(RUN);
                movement->setDesiredSpeed(RUN);
            }
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

    void ParkMatchHandlers()
    {
        for (size_t i = 0; i < g_matchHandlers.size(); ++i)
            ApplyParkOrders(g_matchHandlers[i]);
    }

    void BeginReturnToCages(std::string& statusOut)
    {
        statusOut.clear();
        g_returning = false;
        g_returnLastTick = 0;

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
                Character* prisoner = g_matchEntries[i].prisoner;
                if (!prisoner || !prisoner->isValid() || prisoner->isDead())
                    continue;

                if (!statusOut.empty())
                    statusOut += "\n";
                statusOut += "Could not return ";
                statusOut += prisoner->getName();
                statusOut += " to cage";
            }
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
            entry.returnElapsed = 0.0f;
            entry.returnCage = NULL;
            entry.returnCageBuilding = NULL;

            Character* prisoner = entry.prisoner;
            if (!prisoner || !prisoner->isValid() || prisoner->isDead())
                continue;

            // Still locked in — nothing to return.
            if (!entry.released)
                continue;

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
                if (!statusOut.empty())
                    statusOut += "\n";
                statusOut += "Could not return ";
                statusOut += prisoner->getName();
                statusOut += " to cage";
                continue;
            }

            alreadyReserved[pick.index] = true;
            UseableStuff* cage = cages[static_cast<size_t>(pick.index)].useable;
            Building* cageBuilding = cages[static_cast<size_t>(pick.index)].building;
            entry.returnCage = cage;
            entry.returnCageBuilding = cageBuilding;

            const PrisonerMatchLogic::ReturnAction action =
                PrisonerMatchLogic::ChooseReturnAction(
                    IsConsciousPrisoner(prisoner),
                    prisoner->isDead());

            Character* handler = entry.handler;
            if (handler && handler->isValid())
                ClearParkOrders(handler);

            if (action == PrisonerMatchLogic::ReturnEscortWalk)
            {
                entry.returnPending = true;
                entry.returnEscorting = true;
                entry.returnCarrying = false;
                entry.returnPickedUp = false;
                entry.returnElapsed = 0.0f;
                anyPending = true;

                if (cageBuilding)
                {
                    const Ogre::Vector3 cagePos = cageBuilding->getPosition();
                    IssueMove(prisoner, cagePos);
                    if (handler && handler->isValid() && !handler->isDead())
                        IssueMove(handler, cagePos);
                }
            }
            else if (action == PrisonerMatchLogic::ReturnCarry)
            {
                entry.returnPending = true;
                entry.returnEscorting = false;
                entry.returnCarrying = true;
                entry.returnPickedUp = false;
                entry.returnElapsed = 0.0f;
                anyPending = true;

                if (handler && handler->isValid() && !handler->isDead())
                    IssueLiftOrder(handler, prisoner);
                else
                    FinishCageEntry(entry);
            }
            else if (action == PrisonerMatchLogic::ReturnInstantCage)
            {
                FinishCageEntry(entry);
                if (handler && handler->isValid() && !handler->isDead() && cageBuilding)
                    IssueMove(handler, cageBuilding->getPosition());
            }
        }

        delete[] free;
        delete[] distToPrisonerSq;
        delete[] inRegistryRange;
        delete[] alreadyReserved;

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
            if (!entry.returnPending)
                continue;

            Character* prisoner = entry.prisoner;
            Character* handler = entry.handler;
            entry.returnElapsed += dt;

            if (!prisoner || !prisoner->isValid() || prisoner->isDead())
            {
                entry.returnPending = false;
                continue;
            }

            const bool timedOut = entry.returnElapsed >= kReturnTimeoutSec;

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
                if (handler && handler->isValid() && !handler->isDead() &&
                    entry.returnCageBuilding)
                {
                    IssueMove(handler, entry.returnCageBuilding->getPosition());
                }
                continue;
            }

            anyPending = true;
        }

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

    void ReturnAllToCages(std::string& statusOut)
    {
        // Force-complete path: instant cage everyone that still has a pickable cage.
        BeginReturnToCages(statusOut);

        for (size_t e = 0; e < g_matchEntries.size(); ++e)
        {
            MatchEntry& entry = g_matchEntries[e];
            if (!entry.returnPending)
                continue;
            FinishCageEntry(entry);
            Character* handler = entry.handler;
            if (handler && handler->isValid() && !handler->isDead() &&
                entry.returnCageBuilding)
            {
                IssueMove(handler, entry.returnCageBuilding->getPosition());
            }
        }

        g_returning = false;
        ClearMatchState();
    }

    void ClearMatchState()
    {
        if (g_handlerStatesRemembered && !g_matchHandlers.empty())
        {
            FightStarter::DisengageMatch(
                &g_matchHandlers[0], static_cast<int>(g_matchHandlers.size()));
        }

        g_matchEntries.clear();
        g_matchHandlers.clear();
        g_handlerStatesRemembered = false;
        g_matchHasPrisoner = false;
        g_returning = false;
        g_unlocking = false;
        g_returnLastTick = 0;
        g_unlockLastTick = 0;
    }
}
