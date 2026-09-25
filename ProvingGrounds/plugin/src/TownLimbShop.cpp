#include "TownLimbShop.h"
#include "TownLimbShopPolicy.h"
#include "TownAftercare.h"
#include "TownFighterCatalog.h"
#include "TownChallengePolicy.h"
#include "TownMatchmakingRuntime.h"
#include "LeaderboardStore.h"
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
#include <kenshi/GameData.h>
#include <kenshi/Gear.h>
#include <kenshi/Globals.h>
#include <kenshi/GameWorld.h>
#include <kenshi/Item.h>
#include <kenshi/MedicalSystem.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/RootObjectFactory.h>
#include <kenshi/util/hand.h>
#include <kenshi/util/lektor.h>
#include <ogre/OgreVector3.h>
#pragma warning(pop)

namespace
{
    static const char* kTraderId = "243-Proving Grounds.mod";
    static const float kSearchRadius = 2500.0f;
    static const float kServiceDistance = 12.0f;
    static const float kArrivalRadius = 15.0f;
    static const float kMoveRetrySeconds = 0.5f;
    static const float kSpeechDelaySeconds = 2.5f;
    static const float kInstallDelaySeconds = 2.5f;
    static const float kInstallRetrySeconds = 5.0f;
    static const DWORD kReadinessPollMilliseconds = 5000;

    static const char* kLimbData[4] = {
        "95747-Newwworld.mod",
        "95748-Newwworld.mod",
        "95872-rebirth.mod",
        "96022-rebirth.mod"
    };

    static const RobotLimbs::Limb kLimbs[4] = {
        RobotLimbs::LEFT_ARM,
        RobotLimbs::RIGHT_ARM,
        RobotLimbs::LEFT_LEG,
        RobotLimbs::RIGHT_LEG
    };

    enum Phase { Idle, Acquire, Approach, TraderSpeech, FighterSpeech, Install };

    struct Order
    {
        hand fighter;
        hand registry;
        std::string identity;
        unsigned limbMask;
        TownLimbShopPolicy::Grade grade;
        unsigned variant;
        float retryRemaining;
        Order() : limbMask(0), grade(TownLimbShopPolicy::Shoddy), variant(0), retryRemaining(0.0f) {}
    };

    struct ControlledActor
    {
        hand actor;
        void* receiver;
        bool hold, passive, owned, directed;
        MoveSpeed speed;
        ControlledActor() : receiver(NULL), hold(false), passive(false),
            owned(false), directed(false), speed(WALK) {}
    };

    std::vector<Order> g_orders;
    size_t g_current = 0;
    ControlledActor g_fighter;
    ControlledActor g_trader;
    Phase g_phase = Idle;
    Ogre::Vector3 g_target(0.0f, 0.0f, 0.0f);
    TownLimbShopPolicy::ApproachProgress g_progress;
    DWORD g_lastTick = 0;
    DWORD g_lastReadinessCheck = 0;
    float g_phaseElapsed = 0.0f;
    float g_nextMove = 0.0f;
    bool g_visible = false;
    std::string g_status = "Robotics visits idle";

    bool IsPlayer(Character* character)
    {
        if (!character || !ou || !ou->player) return false;
        const lektor<Character*>& players = ou->player->getAllPlayerCharacters();
        for (uint32_t i = 0; i < players.size(); ++i)
            if (players[i] == character) return true;
        return false;
    }

    const char* Role(Character* character)
    {
        GameData* data = character && character->isValid() ? character->getGameData() : NULL;
        return data ? data->stringID.c_str() : NULL;
    }

    unsigned MissingLimbs(Character* character)
    {
        MedicalSystem* medical = character && character->isValid() ? character->getMedical() : NULL;
        if (!medical) return 0;
        unsigned mask = 0;
        for (int i = 0; i < 4; ++i)
            if (medical->getLimbState(kLimbs[i]) == LIMB_STUMP) mask |= 1u << i;
        return mask;
    }

    int LimbCount(unsigned mask)
    {
        int count = 0;
        for (int i = 0; i < 4; ++i) if (mask & (1u << i)) ++count;
        return count;
    }

    bool CanVisit(Character* character)
    {
        return character && character->isValid() && !character->isDead() &&
            !character->isUnconcious() && !character->isInCombatMode(true, true) &&
            !character->isBeingCarried() && !character->isCarryingSomething &&
            !TownAftercare::IsManaged(character) && character->getOrdersReciever();
    }

    void RestoreActor(ControlledActor& controlled)
    {
        Character* character = controlled.actor.getCharacter();
        if (controlled.directed && character && character->isValid())
        {
            character->addOrder(NULL, MOVE_CUS_ORDERED, NULL, false, true,
                character->getPosition());
            character->removeJob(MOVE_CUS_ORDERED);
            character->clearAllAIGoals();
            character->setStandingOrder(MessageForB::M_SET_ORDER_HOLD, controlled.hold);
            character->setStandingOrder(MessageForB::M_SET_ORDER_PASSIVE, controlled.passive);
            CharMovement* movement = character->getMovement();
            if (movement)
            {
                movement->setDesiredSpeedOrders(controlled.speed);
                movement->restoreDesiredSpeed();
            }
            if (!character->isDead()) character->reThinkCurrentAIAction();
        }
        if (controlled.owned)
            TownAftercare::ReleaseExternalOrders(controlled.receiver);
        controlled = ControlledActor();
    }

    void ReleaseControlled()
    {
        RestoreActor(g_fighter);
        RestoreActor(g_trader);
    }

    bool Capture(Character* character, ControlledActor& controlled)
    {
        if (!character || !character->isValid() || !character->getOrdersReciever()) return false;
        controlled.actor = character;
        controlled.receiver = character->getOrdersReciever();
        controlled.hold = character->getStandingOrder(MessageForB::M_SET_ORDER_HOLD);
        controlled.passive = character->getStandingOrder(MessageForB::M_SET_ORDER_PASSIVE);
        controlled.speed = character->getMovementSpeedOrders();
        if (!TownAftercare::ReserveExternalOrders(controlled.receiver))
        {
            controlled = ControlledActor();
            return false;
        }
        controlled.owned = true;
        return true;
    }

    void HoldFacing(ControlledActor& controlled, const Ogre::Vector3& target)
    {
        Character* character = controlled.actor.getCharacter();
        if (!character || !character->isValid()) return;
        character->removeJob(MOVE_CUS_ORDERED);
        character->clearAllAIGoals();
        character->setStandingOrder(MessageForB::M_SET_ORDER_HOLD, true);
        character->setStandingOrder(MessageForB::M_SET_ORDER_PASSIVE, true);
        character->addGoal(STAND_STILL, NULL);
        character->lookatPosition(target, true);
        controlled.directed = true;
    }

    void IssueMove()
    {
        Character* character = g_fighter.actor.getCharacter();
        if (!character || !character->isValid()) return;
        character->clearAllAIGoals();
        character->setStandingOrder(MessageForB::M_SET_ORDER_HOLD, false);
        character->setStandingOrder(MessageForB::M_SET_ORDER_PASSIVE, false);
        CharMovement* movement = character->getMovement();
        if (movement)
        {
            movement->setDestination(g_target, HIGH_PRIORITY, true);
            movement->setDesiredSpeedOrders(RUN);
            movement->setDesiredSpeed(RUN);
        }
        character->addOrder(NULL, MOVE_CUS_ORDERED, NULL, false, true, g_target);
        character->setDestination(g_target, false);
        g_fighter.directed = true;
    }

    Character* FindTrader(Building* registry)
    {
        if (!ou || !registry || !registry->isValid() || !registry->getTown()) return NULL;
        lektor<RootObject*> nearby;
        ou->getObjectsWithinSphere(nearby, registry->getPosition(), kSearchRadius,
            CHARACTER, 512, NULL);
        Character* result = NULL;
        float nearest = 1.0e30f;
        for (uint32_t i = 0; i < nearby.size(); ++i)
        {
            Character* candidate = static_cast<Character*>(nearby[i]);
            if (!candidate || !candidate->isValid() || candidate->getCurrentTownLocation() != registry->getTown() ||
                !Role(candidate) || std::string(Role(candidate)) != kTraderId || !CanVisit(candidate)) continue;
            const float distance = (candidate->getPosition() - registry->getPosition()).squaredLength();
            if (distance < nearest) { nearest = distance; result = candidate; }
        }
        return result;
    }

    void BeginSilent(const char* reason)
    {
        ReleaseControlled();
        g_visible = false;
        g_phase = Install;
        g_phaseElapsed = 0.0f;
        g_status = "Scratch mechanic fitting replacement limbs";
        PGLog::Debug((std::string("Proving Grounds: limb shop silent fallback - ") +
            (reason ? reason : "scene unavailable")).c_str());
    }

    void WaitForFighter()
    {
        ReleaseControlled();
        g_visible = false;
        g_phase = Acquire;
        g_phaseElapsed = 0.0f;
        g_lastReadinessCheck = GetTickCount();
        g_status = "Robotics visit pending until the fighter is available after medical care";
    }

    void CompleteQueue()
    {
        // Called only after the last pending order was removed. No scanning or
        // visit ticking resumes until the next match-end check queues an amputee.
        g_current = 0;
        g_phase = Idle;
        g_lastTick = 0;
        g_lastReadinessCheck = 0;
        g_phaseElapsed = 0.0f;
        g_nextMove = 0.0f;
        g_visible = false;
        g_status = "Robotics visits complete";
        PGLog::Debug("Proving Grounds: limb shop queue stopped; pending=0; awaiting next match-end limb check");
    }

    void Advance()
    {
        ReleaseControlled();
        if (g_current < g_orders.size())
            g_orders.erase(g_orders.begin() + g_current);
        g_current = 0;
        g_phaseElapsed = 0.0f;
        g_nextMove = 0.0f;
        g_lastReadinessCheck = 0;
        g_visible = false;
        if (g_orders.empty()) { CompleteQueue(); return; }
        g_phase = Acquire;
        g_status = "Next fighter heading to Scratch Robotics";
    }

    bool InstallCurrent()
    {
        if (g_current >= g_orders.size()) return false;
        Order& order = g_orders[g_current];
        Character* fighter = order.fighter.getCharacter();
        if (!fighter || !fighter->isValid() || fighter->isDead()) return false;
        MedicalSystem* medical = fighter->getMedical();
        if (!medical || !ou || !ou->theFactory) return false;

        const unsigned mask = MissingLimbs(fighter);
        order.limbMask = mask;
        if (!mask) return true;
        Item* created[4] = { NULL, NULL, NULL, NULL };
        const int quality = TownLimbShopPolicy::QualityLevel(order.grade);
        for (int i = 0; i < 4; ++i)
        {
            if (!(mask & (1u << i))) continue;
            GameData* data = ou->gamedata.getData(kLimbData[i]);
            if (!data || data->type != LIMB_REPLACEMENT)
            {
                char line[256];
                sprintf_s(line, "Proving Grounds: limb shop missing replacement data limb=%d id=%s",
                    i, kLimbData[i]);
                PGLog::Error(line);
                for (int j = 0; j < 4; ++j)
                    if (created[j]) ou->destroy(created[j], false, "Proving Grounds limb order rollback");
                return false;
            }
            created[i] = ou->theFactory->createItem(data, hand(), NULL, NULL, quality, NULL);
            if (!created[i] || created[i]->getClassType() != LIMB_REPLACEMENT)
            {
                char line[256];
                sprintf_s(line, "Proving Grounds: limb shop item creation failed limb=%d id=%s",
                    i, kLimbData[i]);
                PGLog::Error(line);
                for (int j = 0; j < 4; ++j)
                    if (created[j]) ou->destroy(created[j], false, "Proving Grounds limb order rollback");
                return false;
            }
            Gear* gear = created[i]->isGear();
            if (gear)
            {
                gear->level_0_100 = quality;
                gear->level = static_cast<float>(quality) / 100.0f;
            }
        }

        for (int i = 0; i < 4; ++i)
            if (created[i]) medical->setRobotLimbItem(kLimbs[i], created[i], false);

        bool installed = true;
        for (int i = 0; i < 4; ++i)
            if ((mask & (1u << i)) && medical->getLimbState(kLimbs[i]) != LIMB_REPLACED)
                installed = false;
        char line[384];
        sprintf_s(line, "Proving Grounds: limb shop purchase fighter=%s identity=%s grade=%s quality=%d limbs=%u count=%d scene=%s installed=%d",
            fighter->getName().c_str(), order.identity.c_str(),
            TownLimbShopPolicy::GradeName(order.grade), quality, mask,
            LimbCount(mask), g_visible ? "visible" : "silent", installed ? 1 : 0);
        if (installed) PGLog::Debug(line); else PGLog::Error(line);
        return installed;
    }

    void AcquireCurrent()
    {
        for (size_t i = 0; i < g_orders.size();)
        {
            Character* pending = g_orders[i].fighter.getCharacter();
            if (!pending || !pending->isValid() || pending->isDead() || !MissingLimbs(pending))
                g_orders.erase(g_orders.begin() + i);
            else
                ++i;
        }
        if (g_orders.empty()) { CompleteQueue(); return; }

        size_t ready = g_orders.size();
        for (size_t i = 0; i < g_orders.size(); ++i)
        {
            Character* pending = g_orders[i].fighter.getCharacter();
            if (g_orders[i].retryRemaining <= 0.0f && CanVisit(pending)) { ready = i; break; }
        }
        if (ready == g_orders.size())
        {
            g_status = "Robotics visits pending until queued fighters finish medical care and can visit";
            return;
        }

        g_current = ready;
        Order& order = g_orders[g_current];
        Character* fighter = order.fighter.getCharacter();
        order.limbMask = MissingLimbs(fighter);
        Building* registry = order.registry.getBuilding();
        Character* trader = FindTrader(registry);
        if (!trader) { BeginSilent("Scratch Robotics Trader unavailable"); return; }
        if (!Capture(fighter, g_fighter)) { BeginSilent("fighter AI ownership unavailable"); return; }
        if (!Capture(trader, g_trader)) { BeginSilent("trader AI ownership unavailable"); return; }

        Ogre::Vector3 direction = fighter->getPosition() - trader->getPosition();
        direction.y = 0.0f;
        if (direction.squaredLength() <= 1.0f) direction = Ogre::Vector3(1.0f, 0.0f, 0.0f);
        else direction.normalise();
        g_target = trader->getPosition() + direction * kServiceDistance;
        const float distance = (fighter->getPosition() - g_target).length();
        TownLimbShopPolicy::ResetApproach(g_progress, distance);
        HoldFacing(g_trader, fighter->getPosition());
        IssueMove();
        g_visible = true;
        g_phase = Approach;
        g_phaseElapsed = 0.0f;
        g_nextMove = kMoveRetrySeconds;
        g_status = fighter->getName() + " is heading to Scratch Robotics";
    }
}

namespace TownLimbShop
{
    void Begin(Building* registry, Character* const* fighters, int count)
    {
        if (!registry || !registry->isValid() || !fighters || count <= 0) return;
        const bool wasIdle = g_phase == Idle;
        unsigned queued = 0;
        for (int i = 0; i < count; ++i)
        {
            Character* fighter = fighters[i];
            if (!fighter || !fighter->isValid() || fighter->isDead() || IsPlayer(fighter)) continue;
            bool duplicate = false;
            for (size_t j = 0; j < g_orders.size(); ++j)
                if (g_orders[j].fighter == fighter) duplicate = true;
            if (duplicate) continue;
            const unsigned mask = MissingLimbs(fighter);
            if (!mask) continue;
            const char* role = Role(fighter);
            const TownFighterCatalog::Entry* regular = TownFighterCatalog::Find(role);
            const int namedDivision = TownChallengePolicy::NamedRoleDivision(role ? role : "");
            const int division = regular ? regular->tier : namedDivision >= 0 ? namedDivision : 0;
            const bool legendary = !regular && namedDivision == 2;
            Order order;
            order.fighter = fighter;
            order.registry = registry;
            order.identity = TownMatchmakingRuntime::Identity(fighter);
            if (order.identity.empty()) order.identity = order.fighter.toString();
            order.limbMask = mask;
            const unsigned seed = TownLimbShopPolicy::Seed(order.identity,
                LeaderboardStore::GetMatchCount(fighter, LeaderboardData::Town), mask);
            order.grade = TownLimbShopPolicy::GradeFor(division, legendary, seed);
            order.variant = (seed >> 8) % 3u;
            g_orders.push_back(order);
            ++queued;
        }
        char checked[192];
        sprintf_s(checked, "Proving Grounds: limb shop match-end check participants=%d newlyQueued=%u pending=%u",
            count, queued, static_cast<unsigned>(g_orders.size()));
        PGLog::Debug(checked);
        if (!queued) return;
        if (wasIdle)
        {
            g_current = 0;
            g_phase = Acquire;
            g_lastTick = GetTickCount();
            g_lastReadinessCheck = 0;
            g_status = "Fighters queued for Scratch Robotics";
            PGLog::Debug("Proving Grounds: limb shop queue started after match-end limb loss check");
        }
        char line[128];
        sprintf_s(line, "Proving Grounds: limb shop added %u fighter visits; %u pending",
            queued, static_cast<unsigned>(g_orders.size()));
        PGLog::Debug(line);
    }

    bool Tick()
    {
        if (g_phase == Idle) return false;
        const DWORD now = GetTickCount();
        float dt = g_lastTick ? static_cast<float>(now - g_lastTick) / 1000.0f : 0.0f;
        g_lastTick = now;
        if (!ou || ou->isPaused()) return true;
        if (dt < 0.0f) dt = 0.0f;
        if (dt > 1.0f) dt = 1.0f;
        g_phaseElapsed += dt;
        for (size_t i = 0; i < g_orders.size(); ++i)
            if (g_orders[i].retryRemaining > 0.0f)
                g_orders[i].retryRemaining -= dt;

        if (g_phase == Acquire)
        {
            if (g_lastReadinessCheck && now - g_lastReadinessCheck < kReadinessPollMilliseconds)
                return true;
            g_lastReadinessCheck = now;
            AcquireCurrent();
            return g_phase != Idle;
        }

        Character* fighter = g_current < g_orders.size() ?
            g_orders[g_current].fighter.getCharacter() : NULL;
        if (!fighter || !fighter->isValid() || fighter->isDead())
        {
            Advance();
            return g_phase != Idle;
        }

        if (g_visible && g_phase != Install)
        {
            Character* trader = g_trader.actor.getCharacter();
            if (!CanVisit(trader))
            {
                BeginSilent("trader interrupted during visit");
                return true;
            }
            if (!CanVisit(fighter))
            {
                WaitForFighter();
                return true;
            }
        }

        if (g_phase == Approach)
        {
            Character* trader = g_trader.actor.getCharacter();
            const float distance = (fighter->getPosition() - g_target).length();
            TownLimbShopPolicy::UpdateApproach(g_progress, distance, dt);
            if (distance <= kArrivalRadius)
            {
                HoldFacing(g_fighter, trader->getPosition());
                HoldFacing(g_trader, fighter->getPosition());
                const Order& order = g_orders[g_current];
                trader->say(TownLimbShopPolicy::TraderLine(order.grade, order.variant,
                    TownLimbShopPolicy::KindFor(order.limbMask)));
                g_phase = TraderSpeech;
                g_phaseElapsed = 0.0f;
                g_status = "Scratch Robotics is quoting a replacement order";
                return true;
            }
            if (TownLimbShopPolicy::ApproachStalled(g_progress))
            {
                BeginSilent("shop approach stalled for 20 seconds");
                return true;
            }
            if (g_phaseElapsed >= g_nextMove)
            {
                IssueMove();
                g_nextMove = g_phaseElapsed + kMoveRetrySeconds;
            }
            return true;
        }

        if (g_phase == TraderSpeech && g_phaseElapsed >= kSpeechDelaySeconds)
        {
            const Order& order = g_orders[g_current];
            fighter->say(TownLimbShopPolicy::FighterLine(order.grade, order.variant,
                TownLimbShopPolicy::KindFor(order.limbMask)));
            g_phase = FighterSpeech;
            g_phaseElapsed = 0.0f;
            g_status = fighter->getName() + " is buying replacement limbs";
            return true;
        }

        if (g_phase == FighterSpeech && g_phaseElapsed >= kInstallDelaySeconds)
        {
            g_phase = Install;
            g_phaseElapsed = 0.0f;
        }

        if (g_phase == Install)
        {
            if (!CanVisit(fighter)) { WaitForFighter(); return true; }
            if (InstallCurrent()) Advance();
            else {
                ReleaseControlled();
                // Keep the failed order, but let other fighters and saves proceed.
                Order retry = g_orders[g_current];
                retry.retryRemaining = kInstallRetrySeconds;
                g_orders.erase(g_orders.begin() + g_current);
                g_orders.push_back(retry);
                g_current = 0;
                g_phase = Acquire;
                g_visible = false;
                g_phaseElapsed = 0.0f;
                g_lastReadinessCheck = 0;
                g_status = "Replacement limb installation failed; queued order will retry";
                PGLog::Error("Proving Grounds: limb shop installation incomplete; order retained; retry in 5 unpaused seconds");
            }
        }
        return g_phase != Idle;
    }

    bool IsActive() { return g_phase != Idle; }

    bool IsManaged(Character* fighter)
    {
        if (!fighter) return false;
        for (size_t i = 0; i < g_orders.size(); ++i)
            if (g_orders[i].fighter == fighter) return true;
        return false;
    }

    bool IsVisitActive()
    {
        return g_visible || g_fighter.owned || g_trader.owned || g_phase == Install;
    }

    void SuspendVisit()
    {
        if (g_phase == Idle) return;
        ReleaseControlled();
        g_visible = false;
        g_current = 0;
        g_phase = Acquire;
        g_phaseElapsed = 0.0f;
        g_nextMove = 0.0f;
        g_lastReadinessCheck = 0;
        g_status = "Robotics visits resume after saving";
    }

    void Release()
    {
        ReleaseControlled();
        g_orders.clear();
        g_current = 0;
        g_phase = Idle;
        g_lastTick = 0;
        g_lastReadinessCheck = 0;
        g_phaseElapsed = 0.0f;
        g_nextMove = 0.0f;
        g_visible = false;
        g_status = "Robotics visits idle";
    }

    void AbandonWorldState()
    {
        // TownAftercare owns and clears the shared reservation table during
        // world teardown. Actor/receiver pointers may already be invalid here.
        g_fighter = ControlledActor();
        g_trader = ControlledActor();
        g_orders.clear();
        g_current = 0;
        g_phase = Idle;
        g_lastTick = 0;
        g_lastReadinessCheck = 0;
        g_phaseElapsed = 0.0f;
        g_nextMove = 0.0f;
        g_visible = false;
        g_status = "Robotics visits idle";
    }

    const std::string& GetStatus() { return g_status; }
}
