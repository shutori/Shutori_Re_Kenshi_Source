#include "TownCorpseCleanup.h"
#include "TownCorpseCleanupPolicy.h"
#include "TownAftercare.h"
#include "ArenaMedical.h"
#include "TownSpectators.h"
#include "TownSpectatorPolicy.h"
#include "PGLog.h"
#include <kenshi/Character.h>
#include <kenshi/CharMovement.h>
#include <kenshi/GameData.h>
#include <kenshi/GameWorld.h>
#include <kenshi/Globals.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Inventory.h>
#include <kenshi/Building/UseableStuff.h>
#include <kenshi/util/hand.h>
#include <Windows.h>
#include <cstdio>
#include <vector>

namespace {
    struct Corpse {
        hand actor;
        bool resolved;
        Corpse() : resolved(false) {}
    };
    struct DeferredHandler {
        hand actor;
        void* receiver;
        bool hold, passive;
        MoveSpeed speed;
        DeferredHandler() : receiver(NULL), hold(false), passive(false), speed(WALK) {}
    };

    std::vector<Corpse> corpses;
    std::vector<DeferredHandler> deferredHandlers;
    hand activeArena, furnace, handler;
    int current = -1;
    bool active = false, sealed = false, directed = false;
    bool batchStarted = false, batchExpired = false;
    bool oldHold = false, oldPassive = false;
    MoveSpeed oldSpeed = WALK;
    void* reservedReceiver = NULL;
    DWORD lastTick = 0, lastPoll = 0, lastOrder = 0;
    float batchElapsed = 0.0f;
    TownCorpseCleanupPolicy::Action lastAction = TownCorpseCleanupPolicy::Wait;
    std::string status;

    bool Player(Character* c) {
        if (!ou || !ou->player) return true;
        const lektor<Character*>& players = ou->player->getAllPlayerCharacters();
        for (uint32_t i = 0; i < players.size(); ++i) if (players[i] == c) return true;
        return false;
    }

    bool IsCageHandler(Character* c) {
        if (!c || !c->isValid()) return false;
        GameData* data = c->getGameData();
        return TownSpectatorPolicy::RoleFor(data ? data->stringID.c_str() : NULL) ==
            TownSpectatorPolicy::CageHandler;
    }

    UseableStuff* Disposal(Building* b) {
        if (!b || !b->isValid() || b->getSpecialFunction() != BF_CORPSE_DISPOSAL) return NULL;
        Building::ConstructionState* construction = b->getBuildState();
        UseableStuff* result = b->getUseableStuff();
        if (!result) result = reinterpret_cast<UseableStuff*>(b);
        Building* arena = activeArena.getBuilding();
        if (!TownCorpseCleanupPolicy::FurnaceReady(true, arena && b->getTown() == arena->getTown(),
            !construction || construction->isComplete, result->isDisabled(), true)) return NULL;
        return result;
    }

    void SetStatus(const std::string& next) {
        if (status == next) return;
        status = next;
        PGLog::Debug(("Proving Grounds: corpse cleanup - " + status).c_str());
    }

    TownCorpseCleanupPolicy::ReleaseDisposition ReleaseDisposition(Character* c) {
        if (!c || !c->isValid()) return TownCorpseCleanupPolicy::ForgetRelease;
        if (Player(c)) return TownCorpseCleanupPolicy::ForgetRelease;
        return TownCorpseCleanupPolicy::ReleaseFor(true, false, c->isDead(),
            c->isUnconcious(), c->isInCombatMode(true, true), c->isBeingCarried(),
            ArenaMedical::NeedsStabilization(c), c->isCarryingSomething);
    }

    void RestoreNative(Character* c, void* receiver, bool hold, bool passive, MoveSpeed speed) {
        if (!c || !c->isValid() || Player(c)) {
            TownAftercare::ReleaseExternalOrders(receiver);
            return;
        }
        c->addOrder(NULL, MOVE_CUS_ORDERED, NULL, false, true, c->getPosition());
        c->removeJob(MOVE_CUS_ORDERED);
        c->clearAllAIGoals();
        c->setStandingOrder(MessageForB::M_SET_ORDER_HOLD, hold);
        c->setStandingOrder(MessageForB::M_SET_ORDER_PASSIVE, passive);
        CharMovement* movement = c->getMovement();
        if (movement) { movement->setDesiredSpeedOrders(speed); movement->restoreDesiredSpeed(); }
        // The ownership hook must stop suppressing chooseGoal before native AI
        // is asked to select its next town job.
        TownAftercare::ReleaseExternalOrders(receiver);
        c->reThinkCurrentAIAction();
    }

    void TickDeferredHandlers() {
        for (size_t i = 0; i < deferredHandlers.size();) {
            DeferredHandler& pending = deferredHandlers[i];
            Character* c = pending.actor.getCharacter();
            TownCorpseCleanupPolicy::ReleaseDisposition disposition = ReleaseDisposition(c);
            if (disposition == TownCorpseCleanupPolicy::DeferRelease && c &&
                !c->isUnconcious() && !c->isInCombatMode(true, true) && !c->isBeingCarried() &&
                !ArenaMedical::NeedsStabilization(c) && c->isCarryingSomething) {
                Character* carried = c->getCarryingObject().getCharacter();
                if (carried && carried->isDead()) c->dropCarriedObject(true, false);
                disposition = ReleaseDisposition(c);
            }
            if (disposition == TownCorpseCleanupPolicy::DeferRelease) { ++i; continue; }
            if (disposition == TownCorpseCleanupPolicy::ResumeNative)
                RestoreNative(c, pending.receiver, pending.hold, pending.passive, pending.speed);
            else TownAftercare::ReleaseExternalOrders(pending.receiver);
            deferredHandlers.erase(deferredHandlers.begin() + i);
        }
    }

    void ResumeHandler() {
        Character* c = handler.getCharacter();
        if (directed) {
            const TownCorpseCleanupPolicy::ReleaseDisposition disposition = ReleaseDisposition(c);
            if (disposition == TownCorpseCleanupPolicy::ResumeNative)
                RestoreNative(c, reservedReceiver, oldHold, oldPassive, oldSpeed);
            else if (disposition == TownCorpseCleanupPolicy::DeferRelease) {
                DeferredHandler pending;
                pending.actor = handler; pending.receiver = reservedReceiver;
                pending.hold = oldHold; pending.passive = oldPassive; pending.speed = oldSpeed;
                deferredHandlers.push_back(pending);
            } else TownAftercare::ReleaseExternalOrders(reservedReceiver);
        } else TownAftercare::ReleaseExternalOrders(reservedReceiver);
        reservedReceiver = NULL;
        directed = false;
        handler.setNull();
        lastOrder = 0;
        lastAction = TownCorpseCleanupPolicy::Wait;
    }

    void ResolveCurrent(const char* message) {
        if (current >= 0 && current < static_cast<int>(corpses.size())) corpses[current].resolved = true;
        ResumeHandler();
        current = -1;
        SetStatus(message);
    }

    void DiscoverFurnace() {
        if (Disposal(furnace.getBuilding())) return;
        furnace.setNull();
        Building* arena = activeArena.getBuilding();
        if (!ou || !arena || !arena->isValid()) return;
        lektor<RootObject*> nearby;
        ou->getObjectsWithinSphere(nearby, arena->getPosition(), 2500, BUILDING, 1024, NULL);
        float best = 2500.0f * 2500.0f;
        for (uint32_t i = 0; i < nearby.size(); ++i) {
            Building* candidate = nearby[i] && nearby[i]->isValid() ? static_cast<Building*>(nearby[i]) : NULL;
            if (!Disposal(candidate)) continue;
            const float distance = (candidate->getPosition() - arena->getPosition()).squaredLength();
            if (distance < best) { best = distance; furnace = candidate; }
        }
    }

    bool HandlerOperational(Character* c, bool carryingTarget) {
        if (!c || !c->isValid() || Player(c)) return false;
        return c->getOrdersReciever() && !c->isBeingCarried() &&
            !ArenaMedical::NeedsStabilization(c) && TownCorpseCleanupPolicy::HandlerReady(
                true, false, c->isDead(), c->isUnconcious(), c->isInCombatMode(true, true),
                c->isCarryingSomething && !carryingTarget);
    }

    void DiscoverHandler() {
        Character* existing = handler.getCharacter();
        if (HandlerOperational(existing, false) && IsCageHandler(existing)) return;
        ResumeHandler();
        Building* arena = activeArena.getBuilding();
        if (!ou || !arena || !arena->isValid()) return;
        lektor<RootObject*> nearby;
        ou->getObjectsWithinSphere(nearby, arena->getPosition(), 2500, CHARACTER, 512, NULL);
        for (uint32_t i = 0; i < nearby.size(); ++i) {
            Character* candidate = nearby[i] && nearby[i]->isValid() ? static_cast<Character*>(nearby[i]) : NULL;
            if (!IsCageHandler(candidate) || candidate->getCurrentTownLocation() != arena->getTown() ||
                !HandlerOperational(candidate, false) || TownAftercare::IsManaged(candidate)) continue;
            TownSpectators::ReleaseCharacter(candidate);
            if (!TownAftercare::ReserveExternalOrders(candidate->getOrdersReciever())) continue;
            handler = candidate;
            reservedReceiver = candidate->getOrdersReciever();
            oldHold = candidate->getStandingOrder(MessageForB::M_SET_ORDER_HOLD);
            oldPassive = candidate->getStandingOrder(MessageForB::M_SET_ORDER_PASSIVE);
            oldSpeed = candidate->getMovementSpeedOrders();
            candidate->setStandingOrder(MessageForB::M_SET_ORDER_HOLD, false);
            candidate->setStandingOrder(MessageForB::M_SET_ORDER_PASSIVE, false);
            directed = true;
            return;
        }
    }

    bool CorpsePending(int index) {
        if (index < 0 || index >= static_cast<int>(corpses.size()) || corpses[index].resolved) return false;
        Character* c = corpses[index].actor.getCharacter();
        if (!c || !c->isValid()) return false;
        return TownCorpseCleanupPolicy::EligibleCorpse(true, true, c->isDead(), false);
    }

    int NextCorpse() {
        if (CorpsePending(current)) return current;
        for (size_t i = 0; i < corpses.size(); ++i)
            if (CorpsePending(static_cast<int>(i))) return static_cast<int>(i);
        return -1;
    }
}

namespace TownCorpseCleanup {
    void Begin(Building* arena, const std::vector<Character*>& fighters) {
        Release();
        if (!arena || !arena->isValid()) return;
        activeArena = arena;
        for (size_t i = 0; i < fighters.size(); ++i) if (fighters[i]) {
            Corpse corpse; corpse.actor = fighters[i]; corpses.push_back(corpse);
        }
        active = !corpses.empty();
        DiscoverFurnace();
        SetStatus(furnace.isNull() ? "waiting for a nearby corpse furnace" : "watching registered fighters");
    }

    void Seal() { sealed = true; }

    int AdmitArenaDeaths() {
        Building* arena = activeArena.getBuilding();
        if (!ou || !arena || !arena->isValid()) return 0;

        // Match the medical final sweep: include bodies against the edge of
        // the 155-unit combat layout without reaching the medic standby line.
        const float sweepRadius = 175.0f;
        lektor<RootObject*> nearby;
        ou->getObjectsWithinSphere(nearby, arena->getPosition(), sweepRadius,
            CHARACTER, 256, NULL);
        int admitted = 0;
        for (uint32_t i = 0; i < nearby.size(); ++i) {
            RootObject* object = nearby[i];
            if (!object || !object->isValid()) continue;
            Character* corpse = static_cast<Character*>(object);
            if (!corpse->isDead() || Player(corpse) || corpse->isBeingCarried()) continue;

            bool known = false;
            for (size_t existing = 0; existing < corpses.size(); ++existing)
                if (corpses[existing].actor == corpse) { known = true; break; }
            if (known) continue;

            Corpse entry;
            entry.actor = corpse;
            corpses.push_back(entry);
            ++admitted;
            char line[384];
            sprintf_s(line, "Proving Grounds: final arena death sweep admitted name=%s actor=%s slot=%u position=(%.1f,%.1f,%.1f)",
                corpse->getName().c_str(), entry.actor.toString().c_str(),
                static_cast<unsigned>(corpses.size()), corpse->getPosition().x,
                corpse->getPosition().y, corpse->getPosition().z);
            PGLog::Debug(line);
        }
        if (admitted > 0) {
            active = true;
            if (batchExpired) {
                batchExpired = false;
                batchStarted = false;
                batchElapsed = 0.0f;
                current = -1;
                lastOrder = 0;
                lastAction = TownCorpseCleanupPolicy::Wait;
            }
            DiscoverFurnace();
            char next[160];
            sprintf_s(next, "final arena sweep found %d dead NPC%s", admitted,
                admitted == 1 ? "" : "s");
            SetStatus(next);
        }
        return admitted;
    }

    bool HasPending() {
        if (!active || batchExpired) return false;
        for (size_t i = 0; i < corpses.size(); ++i) if (CorpsePending(static_cast<int>(i))) return true;
        return false;
    }

    void Tick() {
        if (!ou) return;
        TickDeferredHandlers();
        if (!active || batchExpired) return;
        const DWORD now = GetTickCount();
        if (ou->isPaused()) { lastTick = now; return; }
        const float delta = lastTick ? (now - lastTick) / 1000.0f : 0.0f;
        lastTick = now;
        if (batchStarted)
            batchElapsed = TownCorpseCleanupPolicy::AdvanceElapsed(batchElapsed, delta, false);
        if (lastPoll && now - lastPoll < 500) return;
        lastPoll = now;
        const int next = NextCorpse();
        if (next < 0) {
            if (current >= 0) ResolveCurrent("corpse accepted by furnace");
            if (sealed) SetStatus("corpse cleanup complete");
            return;
        }
        if (current != next) {
            ResumeHandler();
            current = next;
            batchStarted = true;
            lastOrder = 0;
            Character* corpse = corpses[current].actor.getCharacter();
            SetStatus(std::string("dispatching cage handler for ") + (corpse ? corpse->getName() : "fighter"));
        }
        Character* corpse = corpses[current].actor.getCharacter();
        if (!corpse || !corpse->isValid()) { ResolveCurrent("corpse no longer present"); return; }
        if (TownCorpseCleanupPolicy::BatchTimedOut(batchStarted, batchElapsed, 180.0f)) {
            batchExpired = true;
            for (size_t i = 0; i < corpses.size(); ++i) corpses[i].resolved = true;
            ResumeHandler();
            current = -1;
            SetStatus("corpse cleanup timed out; remaining bodies released to town AI");
            return;
        }
        DiscoverFurnace();
        if (!Disposal(furnace.getBuilding())) { SetStatus("dead fighter waiting: no usable corpse furnace nearby"); return; }
        Character* c = handler.getCharacter();
        bool carryingTarget = c && c->isValid() && c->isCarryingSomething && c->getCarryingObject() == corpse;
        if (!HandlerOperational(c, carryingTarget) || !IsCageHandler(c)) {
            DiscoverHandler();
            c = handler.getCharacter();
            carryingTarget = c && c->isValid() && c->isCarryingSomething && c->getCarryingObject() == corpse;
        }
        if (!c || !directed) { SetStatus("dead fighter waiting: no cage handler available"); return; }
        const bool carryingOther = c->isCarryingSomething && !carryingTarget;
        const TownCorpseCleanupPolicy::Action action = TownCorpseCleanupPolicy::NextAction(
            HandlerOperational(c, carryingTarget), true, carryingTarget, carryingOther);
        if (action == TownCorpseCleanupPolicy::Wait) return;
        if (action != lastAction) { lastAction = action; lastOrder = 0; }
        CharMovement* movement = c->getMovement();
        if (movement) { movement->setDesiredSpeedOrders(RUN); movement->setDesiredSpeed(RUN); }
        if (action == TownCorpseCleanupPolicy::PickUp) {
            if (!lastOrder || now - lastOrder >= 15000) {
                lastOrder = now;
                c->addOrder(NULL, LIFT_PERSON_PLAYER_ORDER, corpse, false, true, corpse->getPosition());
                SetStatus(std::string("cage handler collecting ") + corpse->getName());
            }
            return;
        }
        if (!lastOrder || now - lastOrder >= 15000) {
            lastOrder = now;
            Building* target = furnace.getBuilding();
            c->addOrder(target, FEED_CORPSE_INTO_MACHINE, target, false, true, target->getPosition());
            SetStatus(std::string("cage handler taking ") + corpse->getName() + " to the corpse furnace");
        }
    }

    const std::string& GetStatus() { return status; }

    void Release() {
        ResumeHandler();
        corpses.clear();
        activeArena.setNull();
        furnace.setNull();
        current = -1;
        active = sealed = batchStarted = batchExpired = false;
        reservedReceiver = NULL;
        lastTick = lastPoll = lastOrder = 0;
        batchElapsed = 0.0f;
        lastAction = TownCorpseCleanupPolicy::Wait;
        status.clear();
    }

    void AbandonWorldState() {
        corpses.clear(); deferredHandlers.clear();
        activeArena.setNull(); furnace.setNull(); handler.setNull();
        current = -1;
        active = sealed = directed = batchStarted = batchExpired = false;
        reservedReceiver = NULL;
        lastTick = lastPoll = lastOrder = 0;
        batchElapsed = 0.0f;
        lastAction = TownCorpseCleanupPolicy::Wait;
        status.clear();
    }
}
