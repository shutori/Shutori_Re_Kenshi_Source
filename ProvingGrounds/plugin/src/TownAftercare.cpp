#include "TownAftercare.h"
#include "SparSession.h"
#include "TownAftercarePolicy.h"
#include "TownMedicCoordinator.h"
#include "TownMedicAIPolicy.h"
#include "TownFurniturePolicy.h"
#include "TownArenaRuntimePolicy.h"
#include "ArenaMedical.h"
#include "ArenaIdentity.h"
#include "RosterStatus.h"
#include "PGLog.h"
#include <core/Functions.h>
#include <kenshi/Character.h>
#include <kenshi/CharMovement.h>
#include <kenshi/GameWorld.h>
#include <kenshi/Globals.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/RaceData.h>
#include <kenshi/MedicalSystem.h>
#include <kenshi/Inventory.h>
#include <kenshi/Item.h>
#include <kenshi/Building/UseableStuff.h>
#include <kenshi/util/hand.h>
#include <Windows.h>
#include <cstdio>

namespace {
    SRWLOCK ownershipLock = SRWLOCK_INIT;
    TownMedicAIPolicy::Reservations reservations;
    TownMedicAIPolicy::PatientReservations patientReservations;
    TownMedicAIPolicy::PatientReservations benchReservations;
    TownMedicAIPolicy::ExternalReservations externalReservations;
    TownMedicAIPolicy::Deliveries deliveries;
    struct TreatmentTrace {
        Character* actor;
        Character* patient;
        void* receiver;
        unsigned firstAid, doctoring, blocked, completed, orderChecks;
        bool nativeHandled;
        unsigned workTicks;
        float aidTime, doctorTime;
        TreatmentTrace() : actor(NULL), patient(NULL), receiver(NULL), firstAid(0),
            doctoring(0), blocked(0), completed(0), orderChecks(0), nativeHandled(false), workTicks(0), aidTime(0), doctorTime(0) {}
        void ClearSamples() {
            patient = NULL; firstAid = doctoring = blocked = completed = orderChecks = 0;
            aidTime = doctorTime = 0;
        }
    } treatmentTraces[10];
    typedef bool (*CheckOrdersFunction)(void*);
    CheckOrdersFunction checkOrdersOriginal = NULL;
    typedef void (*ClearOrdersFunction)(void*);
    ClearOrdersFunction clearOrders = NULL;
    typedef void (*TaskSystemRequest)(void*, bool);
    TaskSystemRequest chooseGoal = NULL, callForUpdate = NULL;
    typedef void* (*CurrentGoalFunction)(void*);
    CurrentGoalFunction currentGoal = NULL;
    typedef void (*SetBedModeFunction)(Character*, bool, UseableStuff*);
    SetBedModeFunction setBedModeOriginal = NULL;
    // Beds the arena has filled and not yet taken back. A delivered patient stops
    // being managed while still lying in the bed, so this registry outlives the
    // per-bout patient list and is only dropped on a real world teardown.
    struct BedOccupant {
        hand actor, bed;
        float entered;
        float lastAttempt;
        BedOccupant() : entered(0), lastAttempt(-1000) {}
    };
    BedOccupant bedOccupants[TownMedicCoordinator::MaxBeds];
    int bedOccupantCount = 0;
    float bedRecoveryElapsed = 0;
    float lastBedSweepTrace = -1000;
    DWORD lastBedSweep = 0;
    void RememberBedOccupant(Character* patient, UseableStuff* bed);
    bool IsArenaHospitalBed(UseableStuff* bed);
    void RecordBedEntry(Character* patient, bool on, UseableStuff* bed) {
        setBedModeOriginal(patient, on, bed);
        if (!on || !bed || patient->inSomething != IN_BED) return;
        AcquireSRWLockExclusive(&ownershipLock);
        deliveries.Entered(patient, bed);
        ReleaseSRWLockExclusive(&ownershipLock);
        // Only the arena hospital's beds are ours to give back; beds elsewhere in
        // the world (inns, a player's own bed) stay the engine's business.
        if (IsArenaHospitalBed(bed)) RememberBedOccupant(patient, bed);
    }
    struct NativeMedicResume {
        Character* actor;
        void* receiver;
        NativeMedicResume(Character* c) : actor(c), receiver(c->getOrdersReciever()) {}
        void ClearOrders() { clearOrders(receiver); }
        void Rethink() { actor->reThinkCurrentAIAction(); }
        void RestoreGoals() { actor->clearAllAIGoals(); }
        void ChooseGoal() { chooseGoal(receiver, false); }
        void Schedule() { callForUpdate(receiver, true); }
    };
    volatile LONG preventedFallbacks = 0;
    bool controlAvailable = false;
    bool CheckMedicOrders(void* receiver) {
        // Keep native order completion and GOAP execution intact. Only suppress
        // chooseGoal's autonomous fallback, including gaps between our orders.
        const bool nativeResult = checkOrdersOriginal(receiver);
        AcquireSRWLockShared(&ownershipLock);
        const bool medicReserved = reservations.Contains(receiver);
        const bool patientReserved = patientReservations.Contains(receiver) || benchReservations.Contains(receiver);
        const bool externalReserved = externalReservations.Contains(receiver);
        ReleaseSRWLockShared(&ownershipLock);
        const bool reserved = medicReserved || patientReserved || externalReserved;
        AcquireSRWLockExclusive(&ownershipLock);
        for (int i = 0; i < 10; ++i) if (treatmentTraces[i].receiver == receiver) {
            ++treatmentTraces[i].orderChecks; treatmentTraces[i].nativeHandled = nativeResult; break;
        }
        ReleaseSRWLockExclusive(&ownershipLock);
        if (reserved && !nativeResult) InterlockedIncrement(&preventedFallbacks);
        return TownMedicAIPolicy::OrdersHandled(medicReserved, patientReserved,
            externalReserved, nativeResult);
    }
    void ReleaseReceiver(void* receiver) {
        AcquireSRWLockExclusive(&ownershipLock);
        reservations.Remove(receiver);
        for (int i = 0; i < 10; ++i)
            if (treatmentTraces[i].receiver == receiver) treatmentTraces[i] = TreatmentTrace();
        ReleaseSRWLockExclusive(&ownershipLock);
    }
    struct Medic {
        hand actor, patient, bed;
        bool directed;
        TownMedicCoordinator::Kind lastKind;
        void* receiver;
        MoveSpeed speed;
        float lastRecall, lastRecallLog, lastTrace;
        unsigned lastWorkTicks;
        TownAftercarePolicy::CommandProgress command;
        Medic() : directed(false), lastKind(TownMedicCoordinator::Wait), receiver(NULL), speed(WALK),
            lastRecall(-1000), lastRecallLog(-1000), lastTrace(-1000), lastWorkTicks(0) {}
    };
    struct Patient {
        hand actor;
        bool directed, hold, passive, winner, releaseLogged, earlyAdmitted, sweepAdmitted, speedDirected;
        MoveSpeed speed;
        void* receiver;
        float lastTrace;
        Patient() : directed(false), hold(false), passive(false), winner(false), releaseLogged(false), earlyAdmitted(false),
            sweepAdmitted(false), speedDirected(false), speed(WALK), receiver(NULL), lastTrace(-1000) {}
    };
    TownMedicCoordinator::Coordinator coordinator;
    std::vector<Medic> medics;
    std::vector<Patient> patients;
    std::vector<hand> beds;
    hand arenaSite;
    hand returnHospital;
    Ogre::Vector3 standbyPoint(0,0,0);
    Ogre::Vector3 standbySide(1,0,0);
    DWORD lastStandby = 0;
    float standbyElapsed = 0;
    float lastSummaryTrace = -1000;
    std::string status;
    unsigned careId = 0;
    float postFightStart = -1;
    bool Player(Character* c) {
        if (!ou || !ou->player) return true;
        const lektor<Character*>& all = ou->player->getAllPlayerCharacters();
        for (uint32_t i = 0; i < all.size(); ++i) if (all[i] == c) return true;
        return false;
    }
    bool Available(Character* c) {
        return c && c->isValid() && c->getOrdersReciever() && !Player(c) && !c->isDead() && !c->isUnconcious() &&
            !c->isBeingCarried() && !c->isInCombatMode(true, true) && !ArenaMedical::NeedsStabilization(c);
    }
    void Run(Character* c) {
        CharMovement* movement = c->getMovement();
        if (movement) { movement->setDesiredSpeedOrders(RUN); movement->setDesiredSpeed(RUN); }
    }
    void Direct(Medic& m) {
        Character* c = m.actor.getCharacter();
        if (!m.directed) {
            m.receiver = c->getOrdersReciever();
            AcquireSRWLockExclusive(&ownershipLock);
            reservations.Add(m.receiver);
            for (int i = 0; i < 10; ++i) if (!treatmentTraces[i].actor) {
                treatmentTraces[i].actor = c; treatmentTraces[i].receiver = m.receiver; break;
            }
            ReleaseSRWLockExclusive(&ownershipLock);
            m.speed = c->getMovementSpeedOrders();
            m.directed = true;
        }
    }
    void Reassert(Medic& m) {
        Character* c = m.actor.getCharacter();
        if (!c || !c->isValid()) return;
        Direct(m);
        NativeMedicResume native(c);
        TownMedicAIPolicy::PrepareOwnedOrder(native);
    }
    void DropPatient(Medic& m) {
        Character* c = m.actor.getCharacter();
        if (c && c->isValid() && !Player(c) && !m.patient.isNull() && c->getCarryingObject().hand::operator==(m.patient))
            c->dropCarriedObject(true, false);
    }
    void ReleaseMedic(Medic& m, bool resume = true) {
        ReleaseReceiver(m.receiver);
        m.receiver = NULL;
        Character* c = m.actor.getCharacter();
        if (m.directed && c && c->isValid() && !Player(c)) {
            DropPatient(m);
            CharMovement* movement = c->getMovement();
            if (movement) { movement->setDesiredSpeedOrders(m.speed); movement->restoreDesiredSpeed(); }
            if (resume && !c->isDead() && !c->isUnconcious() && !c->isInCombatMode(true, true) && !c->isCarryingSomething) {
                NativeMedicResume native(c);
                TownMedicAIPolicy::ResumeNative(native);
                char resumed[192];
                sprintf_s(resumed, "Proving Grounds: medic native handoff actor=%s goalSelected=%d scheduled=1",
                    m.actor.toString().c_str(), currentGoal(native.receiver) ? 1 : 0);
                PGLog::Debug(resumed);
            }
            PGLog::Debug(("Proving Grounds: medic returned to town AI actor=" + m.actor.toString()).c_str());
        }
        m.directed = false;
        m.patient.setNull(); m.bed.setNull(); m.command.Reset();
    }
    void RestorePatient(Patient& patient) {
        const bool npcOwned = patient.receiver != NULL;
        AcquireSRWLockExclusive(&ownershipLock);
        patientReservations.Remove(patient.receiver);
        ReleaseSRWLockExclusive(&ownershipLock);
        patient.receiver = NULL;
        Character* p = patient.actor.getCharacter();
        bool nativeRestarted = false;
        if (patient.directed && p && p->isValid()) {
            // Do not cancel bed occupancy when returning the standing orders.
            p->setStandingOrder(MessageForB::M_SET_ORDER_HOLD, patient.hold);
            p->setStandingOrder(MessageForB::M_SET_ORDER_PASSIVE, patient.passive);
            if (patient.speedDirected) {
                CharMovement* movement = p->getMovement();
                if (movement) { movement->setDesiredSpeedOrders(patient.speed); movement->restoreDesiredSpeed(); }
            }
            if (npcOwned && !p->isDead() && !p->isUnconcious() && !p->isBeingCarried() &&
                p->inSomething != IN_BED && !p->isInCombatMode(true, true)) {
                clearOrders(p->getOrdersReciever());
                p->clearAllAIGoals(); p->reThinkCurrentAIAction();
                nativeRestarted = true;
            }
        }
        if (!patient.releaseLogged && p && p->isValid()) {
            MedicalSystem* medical = p->getMedical();
            RosterStatus::Snapshot recovery = {};
            std::string recoveryError;
            const bool recoveryRead = RosterStatus::Read(p, recovery, &recoveryError);
            char line[768];
            sprintf_s(line, "Proving Grounds: aftercare fighter released name=%s actor=%s npcOwned=%d nativeRestarted=%d dead=%d ko=%d combat=%d carried=%d inBed=%d hold=%d passive=%d aidFlesh=%.3f aidRepair=%.3f bleed=%.3f wounds=%u blood=%.3f lowestLimb=%.3f recoveryRead=%d nativeGoal=%d position=(%.1f,%.1f,%.1f)",
                p->getName().c_str(), patient.actor.toString().c_str(), npcOwned ? 1 : 0, nativeRestarted ? 1 : 0,
                p->isDead() ? 1 : 0, p->isUnconcious() ? 1 : 0, p->isInCombatMode(true, true) ? 1 : 0,
                p->isBeingCarried() ? 1 : 0, p->inSomething == IN_BED ? 1 : 0,
                p->getStandingOrder(MessageForB::M_SET_ORDER_HOLD) ? 1 : 0,
                p->getStandingOrder(MessageForB::M_SET_ORDER_PASSIVE) ? 1 : 0,
                ArenaMedical::ActionableAidNeed(p, false), ArenaMedical::ActionableAidNeed(p, true),
                medical ? medical->currentBleedRate : 0.0f,
                medical ? static_cast<unsigned>(medical->wounds.size()) : 0,
                recoveryRead ? recovery.blood : -1.0f, recoveryRead ? recovery.lowestLimb : -1.0f,
                recoveryRead ? 1 : 0, currentGoal && p->getOrdersReciever() && currentGoal(p->getOrdersReciever()) ? 1 : 0,
                p->getPosition().x, p->getPosition().y, p->getPosition().z);
            PGLog::Debug(line);
        }
        else if (!patient.releaseLogged) {
            PGLog::Debug(("Proving Grounds: aftercare fighter released actor=" + patient.actor.toString() +
                " valid=0").c_str());
        }
        patient.releaseLogged = true;
        patient.directed = false;
        patient.speedDirected = false;
    }
    void DirectPatient(Character* p, bool hold) {
        if (!p || !p->isValid() || p->isUnconcious() || p->isBeingCarried() || p->inSomething == IN_BED) return;
        for (size_t i = 0; i < patients.size(); ++i) {
            Patient& patient = patients[i];
            if (patient.actor != p) continue;
            if (!patient.directed) {
                if (!Player(p)) {
                    patient.receiver = p->getOrdersReciever();
                    AcquireSRWLockExclusive(&ownershipLock);
                    const bool claimed = patientReservations.Add(patient.receiver);
                    ReleaseSRWLockExclusive(&ownershipLock);
                    if (!claimed) {
                        char line[256];
                        sprintf_s(line, "Proving Grounds: aftercare fighter ownership failed name=%s actor=%s receiver=%p",
                            p->getName().c_str(), patient.actor.toString().c_str(), patient.receiver);
                        PGLog::Debug(line);
                        patient.receiver = NULL; return;
                    }
                    clearOrders(patient.receiver);
                }
                patient.hold = p->getStandingOrder(MessageForB::M_SET_ORDER_HOLD);
                patient.passive = p->getStandingOrder(MessageForB::M_SET_ORDER_PASSIVE);
                patient.directed = true;
                patient.releaseLogged = false;
                // Replace old work once. Do not enqueue rethink/body completion:
                // it can cancel the replacement native action on the next update.
                p->clearAllAIGoals();
                if (hold) {
                    const Ogre::Vector3 position = p->getPosition();
                    p->addOrder(NULL, MOVE_CUS_ORDERED, NULL, false, true, position);
                    CharMovement* movement = p->getMovement();
                    if (movement) movement->setDestination(position, HIGH_PRIORITY, true);
                    p->setDestination(position, false);
                }
                PGLog::Debug(("Proving Grounds: aftercare fighter assigned name=" + p->getName() + " actor=" + patient.actor.toString()).c_str());
            }
            p->setStandingOrder(MessageForB::M_SET_ORDER_HOLD, hold);
            p->setStandingOrder(MessageForB::M_SET_ORDER_PASSIVE, true);
            if (!hold && patient.receiver && !Player(p)) {
                if (!patient.speedDirected) {
                    patient.speed = p->getMovementSpeedOrders();
                    patient.speedDirected = true;
                }
                // Self-moving patients need the same run order as carrying medics.
                // Leaving their town WALK order in place holds the whole bout open.
                Run(p);
            }
            return;
        }
    }
    UseableStuff* HospitalBed(Building* b) {
        if (!b || !b->isValid()) return NULL;
        const BuildingFunction function = b->getSpecialFunction();
        if (function != BF_BED && function != BF_SKELETON_BED) return NULL;
        Building* hospital = b->furnitureParentBuilding();
        if (!hospital || !hospital->isValid() || !TownArenaRuntimePolicy::Equals(ArenaIdentity::GetStringId(hospital), "163-Proving Grounds.mod")) return NULL;
        Building::ConstructionState* state = b->getBuildState();
        if (state && !state->isComplete) return NULL;
        UseableStuff* bed = b->getUseableStuff();
        if (!bed) bed = reinterpret_cast<UseableStuff*>(b);
        return bed->numOperatorsMax == 1 && !bed->isDisabled() ? bed : NULL;
    }
    bool Compatible(UseableStuff* bed, Character* p) {
        RaceData* race = p ? p->getRace() : NULL;
        return bed && race && (race->robot ? bed->getSpecialFunction() == BF_SKELETON_BED : bed->getSpecialFunction() == BF_BED);
    }
    bool Occupied(UseableStuff* bed) {
        Character* occupant = bed ? bed->getOccupant().getCharacter() : NULL;
        return bed && TownFurniturePolicy::Occupied(bed->getOccupant().isValid(), occupant != NULL, occupant && occupant->isValid());
    }
    bool IsArenaHospitalBed(UseableStuff* bed) {
        return bed && HospitalBed(static_cast<Building*>(bed)) == bed;
    }
    void ForgetBedOccupant(const hand& actor) {
        AcquireSRWLockExclusive(&ownershipLock);
        for (int i = bedOccupantCount - 1; i >= 0; --i)
            // Qualified call: v100 places this overload in slot 0, but Kenshi's
            // native hand vtable has operator==(bool) there and handle equality at 8.
            if (bedOccupants[i].actor.hand::operator==(actor)) bedOccupants[i] = bedOccupants[--bedOccupantCount];
        ReleaseSRWLockExclusive(&ownershipLock);
    }
    void RememberBedOccupant(Character* patient, UseableStuff* bed) {
        if (!patient || !patient->isValid() || !bed || Player(patient)) return;
        hand actor; actor = patient;
        AcquireSRWLockExclusive(&ownershipLock);
        for (int i = 0; i < bedOccupantCount; ++i)
            if (bedOccupants[i].actor.hand::operator==(actor)) {
                // Moving to a different bed restarts the recovery clock.
                if (bedOccupants[i].bed.getBuilding() != static_cast<Building*>(bed)) {
                    bedOccupants[i].bed = bed;
                    bedOccupants[i].entered = bedRecoveryElapsed;
                }
                ReleaseSRWLockExclusive(&ownershipLock);
                return;
            }
        if (bedOccupantCount < TownMedicCoordinator::MaxBeds) {
            BedOccupant& entry = bedOccupants[bedOccupantCount++];
            entry.actor = actor; entry.bed = bed; entry.entered = bedRecoveryElapsed;
        }
        ReleaseSRWLockExclusive(&ownershipLock);
    }
    // Leave the bed through the engine's own bed call (the inverse of the entry
    // the hook recorded), falling back to releasing the bed's operator slot, then
    // hand the character back to native AI so they walk off instead of standing in
    // the ward waiting for a job that never comes. The log records which of the
    // two calls actually freed the bed, so a refusal is diagnosable in game.
    struct BedReleaseOutcome {
        bool leftBed, bedFree;
        const char* path;
        BedReleaseOutcome() : leftBed(false), bedFree(false), path("none") {}
    };
    BedReleaseOutcome ReleaseBedOccupant(Character* p, UseableStuff* bed) {
        BedReleaseOutcome result;
        if (!p || !p->isValid() || !bed || !setBedModeOriginal) return result;
        result.path = "set-bed-mode";
        setBedModeOriginal(p, false, bed);
        if (p->inSomething == IN_BED) {
            // The bed-mode call alone sometimes leaves the character seated; the
            // operator slot is the engine's other half of the same state.
            hand actor; actor = p;
            bed->stopOperating(actor);
            result.path = "stop-operating";
        }
        result.leftBed = p->inSomething != IN_BED;
        result.bedFree = !Occupied(bed);
        if (!result.leftBed && !result.bedFree) result.path = "none";
        // A corpse has no native AI to resume; corpse cleanup owns the body.
        if (result.leftBed && !p->isDead()) {
            NativeMedicResume native(p);
            TownMedicAIPolicy::ResumeNative(native);
        }
        return result;
    }
    void DeferBedOccupant(const hand& actor, float attempt) {
        AcquireSRWLockExclusive(&ownershipLock);
        for (int i = 0; i < bedOccupantCount; ++i)
            if (bedOccupants[i].actor.hand::operator==(actor)) bedOccupants[i].lastAttempt = attempt;
        ReleaseSRWLockExclusive(&ownershipLock);
    }
    bool NeedsRecovery(Character* p) {
        if (!p || !p->isValid() || p->isDead()) return false;
        MedicalSystem* medical = p->getMedical();
        if (!medical) return p->isUnconcious();
        float minimumHealth = 1.0f;
        for (uint32_t i = 0; i < medical->anatomy.size(); ++i) {
            MedicalSystem::HealthPartStatus* part = medical->anatomy[i];
            if (!part || part->isDead() || part->maxHealth() <= 0) continue;
            const float health = (part->flesh - part->fleshStun) / part->maxHealth();
            if (health < minimumHealth) minimumHealth = health;
        }
        const float maximumBlood = medical->getMaxBlood();
        return TownAftercarePolicy::NeedsRecovery(p->isUnconcious(), minimumHealth,
            maximumBlood > 0 ? medical->blood / maximumBlood : 1.0f);
    }
    bool NeedsHospital(Character* p) {
        bool winner = false;
        for (size_t i = 0; i < patients.size(); ++i)
            if (patients[i].actor == p) { winner = patients[i].winner && !patients[i].earlyAdmitted; break; }
        return p && p->isValid() && TownAftercarePolicy::NeedsHospital(winner, p->isUnconcious(), NeedsRecovery(p));
    }
    float NativeAidNeed(Character* p, bool robot) {
        MedicalSystem* medical = p && p->isValid() && !p->isDead() ? p->getMedical() : NULL;
        const float score = medical ? medical->scoreFirstAidNeed(robot) : 0;
        return score > 0 ? score : 0;
    }
    float AidRemaining(Character* p) {
        return NativeAidNeed(p, false) + NativeAidNeed(p, true);
    }
    bool HasAidKit(Character* c, bool robot) {
        Inventory* inventory = c ? c->getInventory() : NULL;
        Item* kit = inventory ? inventory->getBestItemWithFunction(robot ? ITEM_ROBOTREPAIR : ITEM_FIRSTAID) : NULL;
        return kit && kit->chargesLeft > 0;
    }
    void StageMedic(Medic& m, size_t slot, float elapsed) {
        Character* c = m.actor.getCharacter();
        if (!Available(c) || c->isCarryingSomething) return;
        Direct(m);
        const float offset = (static_cast<float>(slot) - (static_cast<float>(medics.size()) - 1.0f) * .5f) * 16.0f;
        const Ogre::Vector3 point = standbyPoint + standbySide * offset;
        const Ogre::Vector3 delta = c->getPosition() - point;
        const float distance = std::sqrt(delta.x * delta.x + delta.z * delta.z);
        // Hysteresis keeps nearby medics from alternating between hold/move.
        const bool arrived = distance <= (m.command.action == TownAftercarePolicy::Hold ? 18.0f : 8.0f);
        const TownAftercarePolicy::Action action = arrived ? TownAftercarePolicy::Hold : TownAftercarePolicy::Gather;
        CharMovement* movement = c->getMovement();
        if (movement && (elapsed < m.lastRecall || elapsed - m.lastRecall >= 1.0f)) {
            const Ogre::Vector3 error = movement->destination - point;
            if (TownAftercarePolicy::StandbyOrderLost(movement->isCurrentlyMoving(), error.x * error.x + error.z * error.z)) {
                m.command.Reset();
                m.lastRecall = elapsed;
                if (elapsed < m.lastRecallLog || elapsed - m.lastRecallLog >= 30.0f) {
                    m.lastRecallLog = elapsed;
                    char recall[144];
                    sprintf_s(recall, "Proving Grounds: medic repeatedly recalled slot=%u destinationError=%.1f",
                        static_cast<unsigned>(slot + 1), error.length());
                    PGLog::Debug(recall);
                }
            }
        }
        if (m.command.Update(action, elapsed, distance, 0)) {
            Reassert(m);
            c = m.actor.getCharacter();
            if (!c || !c->isValid()) return;
            c->addOrder(NULL, MOVE_CUS_ORDERED, NULL, false, true, arrived ? c->getPosition() : point);
            // Match the working handler's movement handoff. Removing the move
            // job here handed an arrived medic straight back to their town AI.
            movement = c->getMovement();
            const Ogre::Vector3 destination = arrived ? c->getPosition() : point;
            if (movement) movement->setDestination(destination, HIGH_PRIORITY, true);
            c->setDestination(destination, false);
        }
        if (!arrived) Run(c);
    }
    const char* IntentName(TownMedicCoordinator::Kind kind) {
        using namespace TownMedicCoordinator;
        switch (kind) {
        case Gather: return "gather"; case Wait: return "wait"; case Approach: return "approach";
        case ApproachCarry: return "approach-pickup"; case WalkBed: return "walk-bed";
        case Bandage: return "bandage"; case Repair: return "repair"; case PickUp: return "pickup";
        case Deliver: return "deliver"; case GoHospital: return "hospital"; case Release: return "release";
        default: return "putdown";
        }
    }
    const char* ActionName(TownAftercarePolicy::Action action) {
        using namespace TownAftercarePolicy;
        switch (action) {
        case ApproachPickup: return "approach-pickup"; case WalkToBed: return "walk-bed";
        case Treat: return "treat"; case WaitForBed: return "wait-bed"; case PickUp: return "pickup";
        case Deliver: return "deliver"; case Gather: return "gather"; case Hold: return "hold";
        case ReturnHospital: return "hospital"; case ApproachAid: return "approach";
        default: return "done";
        }
    }
    const char* MedicBlocker(Character* c) {
        if (!c || !c->isValid()) return "invalid";
        if (!c->getOrdersReciever()) return "no-order-receiver";
        if (Player(c)) return "player-owned";
        if (c->isDead()) return "dead";
        if (c->isUnconcious()) return "unconscious";
        if (c->isBeingCarried()) return "being-carried";
        if (c->isInCombatMode(true, true)) return "combat";
        if (ArenaMedical::NeedsStabilization(c)) return "needs-aid";
        return "ready";
    }
    void TraceMedic(Medic& m, size_t slot, float elapsed, const char* phase,
        const TownMedicCoordinator::Intent* intent = NULL) {
        if (elapsed >= m.lastTrace && elapsed - m.lastTrace < 5) return;
        m.lastTrace = elapsed;
        Character* c = m.actor.getCharacter();
        if (!c || !c->isValid()) return;
        Character* p = m.patient.getCharacter();
        if (p && !p->isValid()) p = NULL;
        const CharMovement* movement = c->getMovement();
        AcquireSRWLockShared(&ownershipLock);
        const bool owned = reservations.Contains(m.receiver);
        ReleaseSRWLockShared(&ownershipLock);
        UseableStuff* targetBed = HospitalBed(m.bed.getBuilding());
        const float patientDistance = p ? (c->getPosition() - p->getPosition()).length() : -1.0f;
        const float bedDistance = targetBed ? (c->getPosition() - targetBed->getPosition()).length() : -1.0f;
        const float progressAge = m.command.issued ? elapsed - m.command.lastProgress : 0.0f;
        char line[1024];
        sprintf_s(line, "Proving Grounds: aftercare medic slot=%u name=%s actor=%s phase=%s elapsed=%.1f intent=%s action=%s issued=%d progressAge=%.1f bestDistance=%.1f available=%d blocker=%s owned=%d nativeGoal=%d preventedFallbacks=%ld moving=%d carrying=%d assignedCarry=%d patient=%s patientId=%s patientDistance=%.1f patientKO=%d aid=%.3f bedResolved=%d bedOccupied=%d bedDistance=%.1f reservedBed=%u bandages=%d repairKit=%d position=(%.1f,%.1f,%.1f) destination=(%.1f,%.1f,%.1f)",
            static_cast<unsigned>(slot + 1), c->getName().c_str(), m.actor.toString().c_str(), phase, elapsed,
            intent ? IntentName(intent->kind) : "standby", ActionName(m.command.action), m.command.issued ? 1 : 0,
            progressAge, m.command.bestDistance, Available(c) ? 1 : 0, MedicBlocker(c), owned ? 1 : 0,
            currentGoal && c->getOrdersReciever() && currentGoal(c->getOrdersReciever()) ? 1 : 0,
            InterlockedCompareExchange(&preventedFallbacks, 0, 0),
            movement && movement->isCurrentlyMoving() ? 1 : 0, c->isCarryingSomething ? 1 : 0,
            p && c->getCarryingObject() == p ? 1 : 0, p ? p->getName().c_str() : "none",
            m.patient.toString().c_str(), patientDistance, p && p->isUnconcious() ? 1 : 0, AidRemaining(p),
            targetBed ? 1 : 0, targetBed && Occupied(targetBed) ? 1 : 0, bedDistance,
            static_cast<unsigned>(coordinator.ReservedBed(static_cast<int>(slot)) + 1),
            HasAidKit(c, false) ? 1 : 0, HasAidKit(c, true) ? 1 : 0,
            c->getPosition().x, c->getPosition().y, c->getPosition().z,
            movement ? movement->destination.x : 0.0f, movement ? movement->destination.y : 0.0f,
            movement ? movement->destination.z : 0.0f);
        PGLog::Debug(line);
        TreatmentTrace trace;
        AcquireSRWLockExclusive(&ownershipLock);
        for (int i = 0; i < 10; ++i) if (treatmentTraces[i].actor == c) {
            trace = treatmentTraces[i]; treatmentTraces[i].ClearSamples(); break;
        }
        ReleaseSRWLockExclusive(&ownershipLock);
        if (p) {
            sprintf_s(line, "Proving Grounds: aftercare medic callbacks slot=%u firstAid=%u doctoring=%u blocked=%u complete=%u aidTime=%.3f doctorTime=%.3f lastPatientMatches=%d orderChecks=%u nativeHandled=%d",
                static_cast<unsigned>(slot + 1), trace.firstAid, trace.doctoring, trace.blocked,
                trace.completed, trace.aidTime, trace.doctorTime, trace.patient == p ? 1 : 0, trace.orderChecks, trace.nativeHandled ? 1 : 0);
            PGLog::Debug(line);
        }
        if (p) {
            MedicalSystem* medical = p->getMedical();
            float fleshGap = 0, repairGap = 0;
            if (medical) for (uint32_t partIndex = 0; partIndex < medical->anatomy.size(); ++partIndex) {
                MedicalSystem::HealthPartStatus* part = medical->anatomy[partIndex];
                if (!part || part->isDead()) continue;
                const float gap = part->maxHealth() - part->flesh - part->bandaging;
                if (gap > 0) { if (part->isRobotic()) repairGap += gap; else fleshGap += gap; }
            }
            // tryToGetCurrentGoal searches goal arrays, not explicit orders.
            // A null result is not evidence that native treatment has stopped.
            sprintf_s(line, "Proving Grounds: aftercare medic aid slot=%u patientId=%s flesh=%.3f repair=%.3f bandages=%d repairKit=%d distance=%.1f nativeGoalArrayMatch=%d fleshGap=%.3f repairGap=%.3f bleed=%.3f wounds=%u",
                static_cast<unsigned>(slot + 1), m.patient.toString().c_str(),
                medical ? medical->scoreFirstAidNeed(false) : 0, medical ? medical->scoreFirstAidNeed(true) : 0,
                HasAidKit(c, false) ? 1 : 0, HasAidKit(c, true) ? 1 : 0, (c->getPosition() - p->getPosition()).length(),
                currentGoal(c->getOrdersReciever()) ? 1 : 0, fleshGap, repairGap, medical ? medical->currentBleedRate : 0,
                medical ? static_cast<unsigned>(medical->wounds.size()) : 0);
            PGLog::Debug(line);
        }
    }
}
namespace {
    TownMedicCoordinator::Point Point(const Ogre::Vector3& p) {
        TownMedicCoordinator::Point result; result.x = p.x; result.y = p.y; result.z = p.z; return result;
    }
    Ogre::Vector3 Position(const TownMedicCoordinator::Point& p) { return Ogre::Vector3(p.x, p.y, p.z); }
    int PatientIndex(RootObject* actor) {
        if (!actor) return -1;
        for (size_t i = 0; i < patients.size(); ++i) if (patients[i].actor == actor) return static_cast<int>(i);
        return -1;
    }
    int PatientIndex(const hand& actor) {
        // Qualify the call: v100 places this overload in slot 0, but Kenshi's
        // native hand vtable has operator==(bool) there and handle equality at 8.
        for (size_t i = 0; i < patients.size(); ++i) if (patients[i].actor.hand::operator==(actor)) return static_cast<int>(i);
        return -1;
    }
    int AdmitArenaStragglers() {
        Building* arena = arenaSite.getBuilding();
        if (!ou || !arena || !arena->isValid() ||
            patients.size() >= TownMedicCoordinator::MaxPatients) return 0;

        // The full-size pit's combat layout reaches 155 units from centre.
        // Leave a small margin for bodies that fall against the arena edge.
        const float sweepRadius = 175.0f;
        lektor<RootObject*> nearby;
        ou->getObjectsWithinSphere(nearby, arena->getPosition(), sweepRadius,
            CHARACTER, 256, NULL);
        int admitted = 0;
        for (uint32_t i = 0; i < nearby.size() &&
            patients.size() < TownMedicCoordinator::MaxPatients; ++i) {
            RootObject* object = nearby[i];
            if (!object || !object->isValid()) continue;
            Character* fighter = static_cast<Character*>(object);
            if (fighter->isDead() || !fighter->isUnconcious() ||
                fighter->inSomething == IN_BED || fighter->isBeingCarried() ||
                TownArenaRuntimePolicy::IsMedic(ArenaIdentity::GetStringId(fighter))) continue;

            bool known = PatientIndex(fighter) >= 0;
            for (size_t medic = 0; !known && medic < medics.size(); ++medic)
                known = medics[medic].actor == fighter;
            if (known) continue;

            Patient patient;
            patient.actor = fighter;
            patient.earlyAdmitted = true;
            patient.sweepAdmitted = true;
            patients.push_back(patient);
            ++admitted;
            char line[384];
            sprintf_s(line, "Proving Grounds: final arena sweep admitted name=%s actor=%s slot=%u position=(%.1f,%.1f,%.1f)",
                fighter->getName().c_str(), patient.actor.toString().c_str(),
                static_cast<unsigned>(patients.size()), fighter->getPosition().x,
                fighter->getPosition().y, fighter->getPosition().z);
            PGLog::Debug(line);
        }
        char summary[192];
        sprintf_s(summary, "Proving Grounds: final arena sweep care=%u candidates=%u admitted=%d radius=%.1f",
            careId, static_cast<unsigned>(nearby.size()), admitted, sweepRadius);
        PGLog::Debug(summary);
        return admitted;
    }
    TownMedicCoordinator::Observation Observe(bool eliminatedOnly = false) {
        TownMedicCoordinator::Observation o;
        o.medicCount = static_cast<int>(medics.size());
        o.patientCount = static_cast<int>(patients.size());
        o.bedCount = static_cast<int>(beds.size()) < TownMedicCoordinator::MaxBeds ?
            static_cast<int>(beds.size()) : TownMedicCoordinator::MaxBeds;
        for (int i = 0; i < o.medicCount; ++i) {
            Character* c = medics[i].actor.getCharacter();
            if (!c || !c->isValid()) continue;
            TownMedicCoordinator::MedicObservation& m = o.medics[i];
            m.ready = !coordinator.MedicDone(i) && Available(c); m.position = Point(c->getPosition());
            m.bandages = HasAidKit(c, false); m.repairs = HasAidKit(c, true);
            if (c->isCarryingSomething) {
                m.carrying = PatientIndex(c->getCarryingObject());
                if (m.carrying < 0) { m.carrying = -2; m.ready = false; }
            }
        }
        for (int i = 0; i < o.patientCount; ++i) {
            Character* c = patients[i].actor.getCharacter();
            TownMedicCoordinator::PatientObservation& p = o.patients[i];
            p.eligible = !eliminatedOnly || patients[i].earlyAdmitted;
            if (!c || !c->isValid() || c->isDead()) continue;
            p.valid = true; p.hospital = NeedsHospital(c);
            p.position = Point(c->getPosition()); p.inBed = c->inSomething == IN_BED;
            // A downed fighter can retain a stale native combat flag after an
            // older bout. Final-sweep patients are safe to transport regardless.
            p.combat = !patients[i].sweepAdmitted && c->isInCombatMode(true, true);
            RaceData* race = c->getRace(); p.robot = race && race->robot;
            p.externalCarry = c->isBeingCarried();
            for (int j = 0; j < o.medicCount; ++j) if (o.medics[j].carrying == i) p.externalCarry = false;
            p.flesh = NativeAidNeed(c, false);
            p.repair = NativeAidNeed(c, true);
            MedicalSystem* medical = c->getMedical();
            p.bleed = medical ? medical->currentBleedRate : 0;
            p.canWalk = !Player(c) && !c->isUnconcious() && !c->isCrippled() &&
                c->getOrdersReciever() && !c->isBeingCarried() && !c->isCarryingSomething;
            AcquireSRWLockShared(&ownershipLock);
            p.delivered = deliveries.Completed(c);
            ReleaseSRWLockShared(&ownershipLock);
        }
        for (int i = 0; i < o.bedCount; ++i) {
            UseableStuff* bed = HospitalBed(beds[i].getBuilding());
            if (!bed) continue;
            o.beds[i].available = !Occupied(bed);
            o.beds[i].robot = bed->getSpecialFunction() == BF_SKELETON_BED;
            o.beds[i].occupant = PatientIndex(bed->getOccupant().getCharacter());
            o.beds[i].position = Point(bed->getPosition());
        }
        Building* hospital = returnHospital.getBuilding();
        if (hospital && hospital->isValid()) {
            o.hospitalAvailable = true; o.hospital = Point(hospital->getPosition());
            for (int i = 0; i < o.bedCount; ++i) if (HospitalBed(beds[i].getBuilding())) {
                o.hospital = o.beds[i].position; break;
            }
        }
        return o;
    }
    const char* PhaseName(TownMedicCoordinator::Phase phase) {
        switch (phase) {
        case TownMedicCoordinator::Care: return "care";
        case TownMedicCoordinator::Gathering: return "gather";
        case TownMedicCoordinator::Treatment: return "treatment";
        case TownMedicCoordinator::Transport: return "transport";
        case TownMedicCoordinator::Returning: return "return";
        default: return "complete";
        }
    }
    const char* PatientState(const TownMedicCoordinator::PatientObservation& p, bool done) {
        if (done) return "done";
        if (!p.eligible) return "not-eliminated-still-in-match";
        if (!p.valid) return "invalid-or-dead";
        if (p.combat) return "combat";
        if (p.flesh > .01f || p.repair > .01f) return "needs-aid";
        if (p.externalCarry) return "external-carry";
        if (p.inBed || p.delivered) return "delivered";
        if (p.hospital) return "needs-transport";
        return "waiting";
    }
    void TracePatient(Patient& patient, int index, const TownMedicCoordinator::Observation& o,
        float elapsed, bool force) {
        if (!force && elapsed >= patient.lastTrace && elapsed - patient.lastTrace < 5) return;
        patient.lastTrace = elapsed;
        Character* c = patient.actor.getCharacter();
        const TownMedicCoordinator::PatientObservation& observed = o.patients[index];
        const bool done = coordinator.PatientDone(index);
        int assignments = 0;
        const char* firstIntent = "none";
        for (int i = 0; i < o.medicCount; ++i) {
            const TownMedicCoordinator::Intent& intent = coordinator.Command(i);
            if (intent.patient == index || o.medics[i].carrying == index) {
                if (!assignments) firstIntent = IntentName(intent.kind);
                ++assignments;
            }
        }
        AcquireSRWLockShared(&ownershipLock);
        const bool owned = patientReservations.Contains(patient.receiver);
        ReleaseSRWLockShared(&ownershipLock);
        if (!c || !c->isValid()) {
            char invalid[384];
            sprintf_s(invalid, "Proving Grounds: aftercare fighter slot=%d actor=%s elapsed=%.1f state=%s valid=0 done=%d directed=%d owned=%d assignments=%d firstIntent=%s",
                index + 1, patient.actor.toString().c_str(), elapsed, PatientState(observed, done),
                done ? 1 : 0, patient.directed ? 1 : 0, owned ? 1 : 0, assignments, firstIntent);
            PGLog::Debug(invalid);
            return;
        }
        MedicalSystem* medical = c->getMedical();
        RosterStatus::Snapshot recovery = {};
        std::string recoveryError;
        const bool recoveryRead = RosterStatus::Read(c, recovery, &recoveryError);
        char line[1280];
        sprintf_s(line, "Proving Grounds: aftercare fighter slot=%d name=%s actor=%s elapsed=%.1f state=%s done=%d winner=%d player=%d directed=%d owned=%d assignments=%d firstIntent=%s dead=%d ko=%d combat=%d carried=%d externalCarry=%d inBed=%d delivered=%d hospital=%d robot=%d hold=%d passive=%d aidFlesh=%.3f aidRepair=%.3f bleed=%.3f wounds=%u blood=%.3f lowestLimb=%.3f recovery=%.3f recoveryRead=%d nativeGoal=%d bedWait=%.1f waitExpired=%d position=(%.1f,%.1f,%.1f)",
            index + 1, c->getName().c_str(), patient.actor.toString().c_str(), elapsed,
            PatientState(observed, done), done ? 1 : 0, patient.winner ? 1 : 0, Player(c) ? 1 : 0,
            patient.directed ? 1 : 0, owned ? 1 : 0, assignments, firstIntent,
            c->isDead() ? 1 : 0, c->isUnconcious() ? 1 : 0, c->isInCombatMode(true, true) ? 1 : 0,
            c->isBeingCarried() ? 1 : 0, observed.externalCarry ? 1 : 0, observed.inBed ? 1 : 0,
            observed.delivered ? 1 : 0, observed.hospital ? 1 : 0, observed.robot ? 1 : 0,
            c->getStandingOrder(MessageForB::M_SET_ORDER_HOLD) ? 1 : 0,
            c->getStandingOrder(MessageForB::M_SET_ORDER_PASSIVE) ? 1 : 0,
            observed.flesh, observed.repair, medical ? medical->currentBleedRate : 0.0f,
            medical ? static_cast<unsigned>(medical->wounds.size()) : 0,
            recoveryRead ? recovery.blood : -1.0f, recoveryRead ? recovery.lowestLimb : -1.0f,
            recoveryRead ? recovery.recovery : -1.0f, recoveryRead ? 1 : 0,
            currentGoal && c->getOrdersReciever() && currentGoal(c->getOrdersReciever()) ? 1 : 0,
            coordinator.BedWait(index), coordinator.WaitExpired(index) ? 1 : 0,
            c->getPosition().x, c->getPosition().y, c->getPosition().z);
        PGLog::Debug(line);
        const CharMovement* movement = c->getMovement();
        sprintf_s(line, "Proving Grounds: aftercare patient motion care=%u patient=%s canWalk=%d moving=%d destination=(%.1f,%.1f,%.1f) filteredFlesh=%.5f filteredRepair=%.5f",
            careId, patient.actor.toString().c_str(), observed.canWalk ? 1 : 0,
            movement && movement->isCurrentlyMoving() ? 1 : 0,
            movement ? movement->destination.x : 0, movement ? movement->destination.y : 0,
            movement ? movement->destination.z : 0, ArenaMedical::ActionableAidNeed(c, false),
            ArenaMedical::ActionableAidNeed(c, true));
        PGLog::Debug(line);
    }
    // Bed state is the one input the coordinator cannot verify for itself: it
    // only learns 'available' from Observe(). Log the table on a slow cadence
    // plus on every change, next to the occupant handle and the engine's own
    // operator set, so a bed that reads occupied while the hospital looks empty
    // is visible as a stale handle rather than as a full hospital.
    const void* bedOccupant[TownMedicCoordinator::MaxBeds];
    bool bedAvailable[TownMedicCoordinator::MaxBeds];
    float lastBedTrace = -1000;
    void TraceBeds(const TownMedicCoordinator::Observation& o, float elapsed, bool force) {
        const bool dumpAll = force || elapsed < lastBedTrace || elapsed - lastBedTrace >= 10.0f;
        if (dumpAll) lastBedTrace = elapsed;
        for (int i = 0; i < o.bedCount && i < TownMedicCoordinator::MaxBeds; ++i) {
            UseableStuff* bed = HospitalBed(beds[i].getBuilding());
            if (!bed) continue;
            const hand& occupantHand = bed->getOccupant();
            Character* occupant = occupantHand.getCharacter();
            const bool available = o.beds[i].available;
            if (!dumpAll && bedOccupant[i] == occupant && bedAvailable[i] == available) continue;
            bedOccupant[i] = occupant; bedAvailable[i] = available;
            unsigned reservedBy = 0;
            for (int m = 0; m < o.medicCount; ++m)
                if (coordinator.ReservedBed(m) == i) { reservedBy = static_cast<unsigned>(m + 1); break; }
            char line[768];
            sprintf_s(line, "Proving Grounds: aftercare bed care=%u index=%u skeleton=%d available=%d occupied=%d handleValid=%d resolved=%d occupant=%s inBed=%d dead=%d ko=%d operators=%u reservedBy=%u position=(%.1f,%.1f,%.1f)",
                careId, static_cast<unsigned>(i + 1), o.beds[i].robot ? 1 : 0, available ? 1 : 0,
                Occupied(bed) ? 1 : 0, occupantHand.isValid() ? 1 : 0, occupant ? 1 : 0,
                occupant ? occupant->getName().c_str() : "none",
                occupant && occupant->inSomething == IN_BED ? 1 : 0,
                occupant && occupant->isDead() ? 1 : 0,
                occupant && occupant->isUnconcious() ? 1 : 0,
                static_cast<unsigned>(bed->currentOperators.size()), reservedBy,
                bed->getPosition().x, bed->getPosition().y, bed->getPosition().z);
            PGLog::Debug(line);
        }
    }
    void TraceSummary(const TownMedicCoordinator::Observation& o, float elapsed,
        const char* outcome, bool force) {
        TraceBeds(o, elapsed, force);
        if (!force && elapsed >= lastSummaryTrace && elapsed - lastSummaryTrace < 5) return;
        lastSummaryTrace = elapsed;
        int pending = 0, aid = 0, transport = 0, invalid = 0, combat = 0, delivered = 0, deferred = 0;
        for (int i = 0; i < o.patientCount; ++i) {
            const TownMedicCoordinator::PatientObservation& p = o.patients[i];
            if (!p.eligible) { ++deferred; continue; }
            if (!p.valid) ++invalid;
            if (p.combat) ++combat;
            if (p.delivered || p.inBed) ++delivered;
            if (!coordinator.PatientDone(i)) {
                ++pending;
                if (p.flesh > .01f || p.repair > .01f) ++aid;
                else if (p.hospital && !p.delivered && !p.inBed) ++transport;
            }
        }
        int ready = 0, idle = 0, carrying = 0;
        for (int i = 0; i < o.medicCount; ++i) {
            if (o.medics[i].ready) ++ready;
            if (o.medics[i].carrying >= 0) ++carrying;
            const TownMedicCoordinator::Intent& intent = coordinator.Command(i);
            if (o.medics[i].ready && o.medics[i].carrying < 0 &&
                (intent.kind == TownMedicCoordinator::Wait || intent.kind == TownMedicCoordinator::Release)) ++idle;
        }
        int freeBeds = 0;
        for (int i = 0; i < o.bedCount; ++i) if (o.beds[i].available) ++freeBeds;
        char line[640];
        sprintf_s(line, "Proving Grounds: aftercare summary care=%u phase=%s elapsed=%.1f outcome=%s patients=%d notAdmitted=%d pending=%d aidPending=%d transportPending=%d delivered=%d invalid=%d combat=%d medics=%d ready=%d idle=%d carrying=%d beds=%d freeBeds=%d missingBed=%d bedWaits=%d preventedFallbacks=%ld",
            careId, PhaseName(coordinator.GetPhase()), elapsed, outcome ? outcome : "heartbeat", o.patientCount, deferred,
            pending, aid, transport, delivered, invalid, combat, o.medicCount, ready, idle, carrying,
            o.bedCount, freeBeds, coordinator.MissingBed() ? 1 : 0, coordinator.ExpiredWaits(),
            InterlockedCompareExchange(&preventedFallbacks, 0, 0));
        PGLog::Debug(line);
    }
    const char* OutcomeName(TownMedicCoordinator::Outcome outcome) {
        using namespace TownMedicCoordinator;
        switch (outcome) {
        case Pending: return "pending"; case Treated: return "treated";
        case Delivered: return "delivered"; case Recovered: return "recovered";
        case Unavailable: return "dead-or-unavailable"; default: return "unresolved-handoff";
        }
    }
    void LogOutcome(int p, float elapsed, const char* reason) {
        char line[512];
        sprintf_s(line, "Proving Grounds: aftercare outcome care=%u elapsed=%.1f patient=%s outcome=%s reason=%s",
            careId, elapsed, patients[p].actor.toString().c_str(),
            OutcomeName(coordinator.PatientOutcome(p)), reason);
        PGLog::Debug(line);
    }
    void Execute(Medic& m, size_t slot, const TownMedicCoordinator::Intent& intent,
        const TownMedicCoordinator::Observation& o, float elapsed) {
        using namespace TownMedicCoordinator;
        Character* c = m.actor.getCharacter();
        // Remember actual carried patients even when the assignment was revoked.
        if (c && c->isValid() && c->isCarryingSomething && PatientIndex(c->getCarryingObject()) >= 0)
            m.patient = c->getCarryingObject();
        if (intent.kind == PutDown) { DropPatient(m); m.command.Reset(); return; }
        if (intent.kind == GoHospital && Available(c) && !c->isCarryingSomething && o.hospitalAvailable) {
            // Remove our ownership before issuing the final native move. The
            // subsequent bout cleanup must not cancel this return order.
            ReleaseMedic(m, false);
            const Ogre::Vector3 destination = Position(o.hospital);
            c->addOrder(NULL, MOVE_CUS_ORDERED, NULL, false, true, destination);
            CharMovement* movement = c->getMovement();
            if (movement) movement->setDestination(destination, HIGH_PRIORITY, true);
            c->setDestination(destination, false);
            char line[384];
            sprintf_s(line, "Proving Grounds: aftercare hospital return care=%u slot=%u actor=%s destination=(%.1f,%.1f,%.1f) distance=%.1f nativeHandoff=1",
                careId, static_cast<unsigned>(slot+1), m.actor.toString().c_str(), destination.x, destination.y,
                destination.z, (c->getPosition()-destination).length());
            PGLog::Debug(line);
            return;
        }
        if (intent.kind == Release || intent.kind == GoHospital || !Available(c)) {
            if (m.directed) {
                char line[384];
                sprintf_s(line, "Proving Grounds: aftercare native release care=%u slot=%u actor=%s reason=%s",
                    careId, static_cast<unsigned>(slot+1), m.actor.toString().c_str(),
                    Available(c) ? "no-active-assignment" : MedicBlocker(c));
                PGLog::Debug(line);
            }
            ReleaseMedic(m); return;
        }
        hand patient, bed;
        if (intent.patient >= 0) patient = patients[intent.patient].actor;
        if (intent.bed >= 0) bed = beds[intent.bed];
        const bool changed = !m.patient.hand::operator==(patient) ||
            !m.bed.hand::operator==(bed) || m.lastKind != intent.kind;
        if (changed) {
            char line[512];
            sprintf_s(line, "Proving Grounds: aftercare assignment care=%u slot=%u patient=%s bed=%s from=%s to=%s elapsed=%.1f",
                careId, static_cast<unsigned>(slot+1), patient.toString().c_str(), bed.toString().c_str(),
                IntentName(m.lastKind), IntentName(intent.kind), elapsed);
            PGLog::Debug(line);
            if (intent.patient >= 0) {
                sprintf_s(line, "Proving Grounds: aftercare assignment role care=%u slot=%u patient=%s role=%s",
                    careId, static_cast<unsigned>(slot+1), patient.toString().c_str(),
                    coordinator.IsHelping(static_cast<int>(slot)) ? "treatment-helper" : "primary-care-and-transfer");
                PGLog::Debug(line);
            }
            m.command.Reset();
        }
        m.patient = patient; m.bed = bed; m.lastKind = intent.kind;
        Direct(m);
        Character* p = patient.getCharacter();
        UseableStuff* targetBed = HospitalBed(bed.getBuilding());
        if (intent.kind != Wait && (!p || !p->isValid())) return;
        if ((intent.kind == Deliver || intent.kind == WalkBed) &&
            (!targetBed || !Compatible(targetBed, p) || (Occupied(targetBed) && targetBed->getOccupant() != p))) {
            m.command.Reset(); return; // observation will reassign this bed next tick
        }
        const bool walking = intent.kind == WalkBed;
        Ogre::Vector3 destination = p ? p->getPosition() : c->getPosition();
        if (intent.kind == Deliver || walking) destination = targetBed->getPosition();
        const float distance = ((walking ? p : c)->getPosition() - destination).length();
        TownAftercarePolicy::Action action = TownAftercarePolicy::Hold;
        switch (intent.kind) {
        case Bandage: case Repair: action = TownAftercarePolicy::Treat; break;
        case ApproachCarry: action = TownAftercarePolicy::ApproachPickup; break;
        case PickUp: action = TownAftercarePolicy::PickUp; break;
        case Deliver: action = TownAftercarePolicy::Deliver; break;
        case WalkBed: action = TownAftercarePolicy::WalkToBed; break;
        default: break;
        }
        const bool issued = m.command.issued;
        const float previousProgress = m.command.lastProgress;
        // Only the selected treatment mode contributes medical progress.
        const float aid = intent.kind == Bandage ? NativeAidNeed(p, false) :
            intent.kind == Repair ? NativeAidNeed(p, true) : 0;
        if (action == TownAftercarePolicy::Treat) {
            // An executing native treatment is progress even if its cached score
            // is updated less frequently. Completed/blocked callbacks do not count.
            AcquireSRWLockShared(&ownershipLock);
            for (int i = 0; i < 10; ++i) if (treatmentTraces[i].actor == c) {
                const TreatmentTrace& trace = treatmentTraces[i];
                if (trace.patient == p && trace.workTicks != m.lastWorkTicks && m.command.issued)
                    m.command.lastProgress = elapsed;
                m.lastWorkTicks = trace.workTicks;
                break;
            }
            ReleaseSRWLockShared(&ownershipLock);
        }
        if (!m.command.Update(action, elapsed, distance, aid)) return;
        if (m.command.stalledRetries >= 2 && intent.patient >= 0) {
            char line[512];
            sprintf_s(line, "Proving Grounds: aftercare action failed care=%u slot=%u patient=%s action=%s retries=%d progressAge=%.1f distance=%.1f aid=%.4f reason=no-observed-progress",
                careId, static_cast<unsigned>(slot+1), patient.toString().c_str(), IntentName(intent.kind),
                m.command.stalledRetries, elapsed-previousProgress, distance, aid);
            PGLog::Debug(line);
            coordinator.Fail(static_cast<int>(slot), intent.patient, intent.kind);
            // Another medic may still be treating this patient. A failed helper
            // must not restore/cancel the patient's shared temporary care state.
            if (!coordinator.HasPatientWork(intent.patient)) RestorePatient(patients[intent.patient]);
            ReleaseMedic(m); return;
        }
        if (issued && p) {
            MedicalSystem* medical = p->getMedical();
            char detail[768];
            sprintf_s(detail, "Proving Grounds: aftercare stall snapshot care=%u patient=%s action=%s nativeFlesh=%.5f nativeRepair=%.5f filteredFlesh=%.5f filteredRepair=%.5f bleed=%.5f wounds=%u patientMoving=%d patientKO=%d patientCrippled=%d bed=%s occupied=%d",
                careId, patient.toString().c_str(), IntentName(intent.kind), NativeAidNeed(p, false), NativeAidNeed(p, true),
                ArenaMedical::ActionableAidNeed(p, false), ArenaMedical::ActionableAidNeed(p, true),
                medical ? medical->currentBleedRate : 0, medical ? static_cast<unsigned>(medical->wounds.size()) : 0,
                p->getMovement() && p->getMovement()->isCurrentlyMoving() ? 1 : 0,
                p->isUnconcious() ? 1 : 0, p->isCrippled() ? 1 : 0, bed.toString().c_str(),
                targetBed && Occupied(targetBed) ? 1 : 0);
            PGLog::Debug(detail);
            if (medical) for (uint32_t partIndex = 0; partIndex < medical->anatomy.size(); ++partIndex) {
                MedicalSystem::HealthPartStatus* part = medical->anatomy[partIndex];
                if (!part) continue;
                sprintf_s(detail, "Proving Grounds: aftercare stall limb care=%u patient=%s part=%u robotic=%d dead=%d flesh=%.4f stun=%.4f bandaging=%.4f max=%.4f",
                    careId, patient.toString().c_str(), static_cast<unsigned>(partIndex), part->isRobotic() ? 1 : 0,
                    part->isDead() ? 1 : 0, part->flesh, part->fleshStun, part->bandaging, part->maxHealth());
                PGLog::Debug(detail);
            }
        }
        // Goals are prepared only at an actual transition or bounded retry.
        Reassert(m);
        TaskType task = MOVE_CUS_ORDERED;
        switch (intent.kind) {
        case Bandage: task = FIRST_AID_ORDER; break;
        case Repair: task = FIRST_AID_ROBOT; break;
        case PickUp: task = LIFT_PERSON_PLAYER_ORDER; break;
        case Deliver: task = PUT_SOMEONE_IN_BED; break;
        case WalkBed: task = USE_BED_ORDER; break;
        default: break;
        }
        char line[768];
        sprintf_s(line, "Proving Grounds: aftercare native order care=%u slot=%u medic=%s patient=%s bed=%s action=%s task=%d executor=%s trigger=%s elapsed=%.1f distance=%.1f aid=%.4f retries=%d previousProgressAge=%.1f",
            careId, static_cast<unsigned>(slot+1), m.actor.toString().c_str(), patient.toString().c_str(),
            bed.toString().c_str(), IntentName(intent.kind), static_cast<int>(task), walking ? "patient" : "medic",
            issued ? "stalled-retry" : "transition", elapsed, distance, aid, m.command.stalledRetries,
            issued ? elapsed-previousProgress : 0);
        PGLog::Debug(line);
        if (intent.kind == Wait) {
            // Hold only the reserved medic while a compatible bed is unavailable.
            c->addOrder(NULL, MOVE_CUS_ORDERED, NULL, false, true, c->getPosition());
            return;
        }
        if (walking || intent.kind == Deliver) {
            AcquireSRWLockExclusive(&ownershipLock);
            deliveries.Watch(p, targetBed);
            ReleaseSRWLockExclusive(&ownershipLock);
        }
        if (walking) {
            c->addOrder(NULL, MOVE_CUS_ORDERED, NULL, false, true, c->getPosition());
            p->addOrder(targetBed, task, targetBed, false, true, destination);
        } else {
            Run(c);
            c->addOrder(intent.kind == Deliver ? targetBed : NULL, task,
                intent.kind == Deliver ? static_cast<RootObject*>(targetBed) : static_cast<RootObject*>(p),
                false, true, destination);
            if (intent.kind == ApproachCarry) {
                CharMovement* movement = c->getMovement();
                if (movement) movement->setDestination(destination, HIGH_PRIORITY, true);
                c->setDestination(destination, false);
            }
        }
    }

}
namespace TownAftercare {
    void RecordTreatment(Character* medic, Character* patient, bool doctoring,
        bool blocked, bool complete, float frameTime) {
        if (!medic) return;
        AcquireSRWLockExclusive(&ownershipLock);
        for (int i = 0; i < 10; ++i) if (treatmentTraces[i].actor == medic) {
            TreatmentTrace& trace = treatmentTraces[i];
            trace.patient = patient;
            if (doctoring) { ++trace.doctoring; if (!blocked) trace.doctorTime += frameTime; }
            else { ++trace.firstAid; if (!blocked) trace.aidTime += frameTime; }
            if (blocked) ++trace.blocked;
            if (complete) ++trace.completed;
            if (!blocked && !complete && frameTime > 0) ++trace.workTicks;
            break;
        }
        ReleaseSRWLockExclusive(&ownershipLock);
    }
    bool ControlAvailable() { return controlAvailable; }
    bool ReserveBenchOrders(void* receiver) {
        if (!controlAvailable || !receiver) return false;
        AcquireSRWLockExclusive(&ownershipLock);
        const bool available = !reservations.Contains(receiver) && !patientReservations.Contains(receiver) &&
            !externalReservations.Contains(receiver) && benchReservations.Add(receiver);
        ReleaseSRWLockExclusive(&ownershipLock);
        return available;
    }
    void ReleaseBenchOrders(void* receiver) {
        AcquireSRWLockExclusive(&ownershipLock);
        benchReservations.Remove(receiver);
        ReleaseSRWLockExclusive(&ownershipLock);
    }
    bool ReserveExternalOrders(void* receiver) {
        if (!controlAvailable || !receiver) return false;
        AcquireSRWLockExclusive(&ownershipLock);
        const bool available = TownMedicAIPolicy::CanReserveExternal(
            reservations.Contains(receiver), patientReservations.Contains(receiver) || benchReservations.Contains(receiver),
            externalReservations.Contains(receiver));
        const bool reserved = available && externalReservations.Add(receiver);
        ReleaseSRWLockExclusive(&ownershipLock);
        return reserved;
    }
    void ReleaseExternalOrders(void* receiver) {
        AcquireSRWLockExclusive(&ownershipLock);
        externalReservations.Remove(receiver);
        ReleaseSRWLockExclusive(&ownershipLock);
    }
    bool InstallHooks() {
        if (controlAvailable) return true;
        // The installed KenshiLib exports this API although the older build SDK
        // only forward-declares its class. Resolve through the versioned library,
        // never through a hardcoded game address or an assumed object layout.
        HMODULE library = GetModuleHandleW(L"KenshiLib.dll");
        FARPROC entry = library ? GetProcAddress(library, "?checkOrders@AITaskSytem@@QEAA_NXZ") : NULL;
        FARPROC clearEntry = library ? GetProcAddress(library, "?clearOrders@OrdersReceiver@@QEAAXXZ") : NULL;
        FARPROC chooseEntry = library ? GetProcAddress(library, "?chooseGoal@AITaskSytem@@QEAAX_N@Z") : NULL;
        FARPROC updateEntry = library ? GetProcAddress(library, "?callForUpdate@AITaskSytem@@QEAAX_N@Z") : NULL;
        FARPROC goalEntry = library ? GetProcAddress(library, "?tryToGetCurrentGoal@OrdersReceiver@@QEAAPEAVTasker@@XZ") : NULL;
        if (!entry || !clearEntry || !chooseEntry || !updateEntry || !goalEntry) return false;
        clearOrders = reinterpret_cast<ClearOrdersFunction>(clearEntry);
        chooseGoal = reinterpret_cast<TaskSystemRequest>(chooseEntry);
        callForUpdate = reinterpret_cast<TaskSystemRequest>(updateEntry);
        currentGoal = reinterpret_cast<CurrentGoalFunction>(goalEntry);
        if (!setBedModeOriginal && KenshiLib::SUCCESS != KenshiLib::AddHook(
            KenshiLib::GetRealAddress(&Character::setBedMode), RecordBedEntry, &setBedModeOriginal)) return false;
        const intptr_t target = KenshiLib::GetRealAddress(reinterpret_cast<void*>(entry));
        if (!target || KenshiLib::SUCCESS != KenshiLib::AddHook(target, CheckMedicOrders, &checkOrdersOriginal)) return false;
        controlAvailable = true;
        PGLog::Debug("Proving Grounds: medic AI ownership hook installed");
        return true;
    }
    const std::string& GetStatus() { return status; }
    bool IsManaged(Character* character) {
        if (!character) return false;
        for (size_t i = 0; i < medics.size(); ++i) if (medics[i].actor == character) return true;
        for (size_t i = 0; i < patients.size(); ++i) if (patients[i].actor == character)
            return !coordinator.PatientDone(static_cast<int>(i));
        return false;
    }
    void MarkWinner(Character* fighter) {
        if (!fighter) return;
        for (size_t i = 0; i < patients.size(); ++i)
            if (patients[i].actor == fighter) {
                patients[i].winner = true;
                PGLog::Debug(("Proving Grounds: aftercare fighter marked winner name=" + fighter->getName() +
                    " actor=" + patients[i].actor.toString()).c_str());
            }
    }
    static void ResetState(bool worldTeardown) {
        AcquireSRWLockExclusive(&ownershipLock);
        reservations.Clear();
        patientReservations.Clear();
        if (TownAftercarePolicy::ClearExternalReservations(worldTeardown)) {
            externalReservations.Clear(); benchReservations.Clear();
        }
        deliveries.Clear();
        // The bed registry outlives a bout on purpose: a delivered patient stops
        // being managed while still in the bed, and only a real world teardown
        // invalidates the handles behind it.
        if (worldTeardown) {
            bedOccupantCount = 0; bedRecoveryElapsed = 0;
            lastBedSweep = 0; lastBedSweepTrace = -1000;
        }
        for (int i = 0; i < 10; ++i) treatmentTraces[i] = TreatmentTrace();
        ReleaseSRWLockExclusive(&ownershipLock);
        InterlockedExchange(&preventedFallbacks, 0);
        coordinator.Reset();
        postFightStart = -1;
        medics.clear(); patients.clear(); beds.clear(); lastStandby = 0; standbyElapsed = 0; lastSummaryTrace = -1000;
        lastBedTrace = -1000;
        for (int i = 0; i < TownMedicCoordinator::MaxBeds; ++i) { bedOccupant[i] = NULL; bedAvailable[i] = false; }
        arenaSite.setNull();
        returnHospital.setNull();
    }
    void Release() {
        for (size_t i = 0; i < medics.size(); ++i) ReleaseMedic(medics[i]);
        for (size_t i = 0; i < patients.size(); ++i) RestorePatient(patients[i]);
        ResetState(false);
    }
    void AbandonWorldState() {
        ResetState(true);
    }
    void Begin(const std::vector<Character*>& doctors, const std::vector<Character*>& fighters, Building* arena, const Ogre::Vector3& standby) {
        Release(); careId = PGLog::NextId(); standbyPoint = standby; arenaSite = arena;
        PGLog::Debug("Proving Grounds: aftercare controller=native-v2; native medical scores, independent patients, no direct pickup fallback");
        Ogre::Vector3 direction = standby - arena->getPosition();
        direction.y = 0;
        if (direction.squaredLength() > 1.0f) direction.normalise(); else direction = Ogre::Vector3(1,0,0);
        standbySide = Ogre::Vector3(-direction.z, 0, direction.x);
        const size_t count = static_cast<size_t>(TownAftercarePolicy::RequiredMedics(static_cast<int>(fighters.size())));
        for (size_t i = 0; i < doctors.size() && medics.size() < count; ++i) {
            Medic m; m.actor = doctors[i]; medics.push_back(m);
            char selected[192];
            sprintf_s(selected, "Proving Grounds: medic reserved slot=%u actor=%s",
                static_cast<unsigned>(i + 1), m.actor.toString().c_str());
            PGLog::Debug(selected);
        }
        for (size_t i = 0; i < fighters.size() && i < TownMedicCoordinator::MaxPatients; ++i) {
            Patient p; p.actor = fighters[i]; patients.push_back(p);
            Character* fighter = p.actor.getCharacter();
            char roster[384];
            sprintf_s(roster, "Proving Grounds: aftercare fighter registered slot=%u name=%s actor=%s player=%d valid=%d",
                static_cast<unsigned>(i + 1), fighter && fighter->isValid() ? fighter->getName().c_str() : "invalid",
                p.actor.toString().c_str(), fighter && fighter->isValid() && Player(fighter) ? 1 : 0,
                fighter && fighter->isValid() ? 1 : 0);
            PGLog::Debug(roster);
        }
        lektor<RootObject*> nearby;
        ou->getObjectsWithinSphere(nearby, arena->getPosition(), 2500, BUILDING, 1024, NULL);
        for (uint32_t i = 0; i < nearby.size(); ++i) {
            if (!nearby[i] || !nearby[i]->isValid()) continue;
            Building* b = static_cast<Building*>(nearby[i]);
            if (b->getTown() == arena->getTown() &&
                TownArenaRuntimePolicy::Equals(ArenaIdentity::GetStringId(b), "163-Proving Grounds.mod")) returnHospital = b;
            UseableStuff* bed = HospitalBed(b);
            if (!bed || b->getTown() != arena->getTown()) continue;
            bool duplicate = false; for (size_t j = 0; j < beds.size(); ++j) if (beds[j] == bed) duplicate = true;
            if (!duplicate) { hand h; h = bed; beds.push_back(h); }
        }
        char text[128]; sprintf_s(text, "Proving Grounds: medical team - %u medics, %u hospital beds", static_cast<unsigned>(medics.size()), static_cast<unsigned>(beds.size())); PGLog::Debug(text);
        // Adopt whoever is already lying in the hospital's beds: a patient from an
        // earlier bout (or an earlier build) whose care session is long gone still
        // owes the bed back once healed, and no session owns them any more.
        int occupiedBeds = 0, inBed = 0;
        for (size_t i = 0; i < beds.size(); ++i) {
            UseableStuff* bed = HospitalBed(beds[i].getBuilding());
            if (!bed) continue;
            Character* occupant = bed->getOccupant().getCharacter();
            if (!occupant || !occupant->isValid()) continue;
            ++occupiedBeds;
            if (occupant->inSomething != IN_BED || Player(occupant)) continue;
            ++inBed;
            RememberBedOccupant(occupant, bed);
        }
        char adopt[224];
        sprintf_s(adopt, "Proving Grounds: aftercare bed adopt care=%u occupied=%d owned=%d tracked=%d",
            careId, occupiedBeds, inBed, bedOccupantCount);
        PGLog::Debug(adopt);
        Standby();
    }
    void Standby() {
        const DWORD now = GetTickCount();
        float dt = lastStandby ? (now - lastStandby) / 1000.0f : 0;
        lastStandby = now;
        if (!ou || ou->isPaused()) return;
        // A suspended tick must not turn into an immediate stalled-order retry.
        if (dt > 1.0f) dt = 1.0f;
        standbyElapsed += dt;
        for (size_t i = 0; i < medics.size(); ++i) {
            Medic& m = medics[i];
            if (!Available(m.actor.getCharacter())) { ReleaseMedic(m); continue; }
            StageMedic(m, i, standbyElapsed);
        }
    }
    void TickBedRecovery() {
        if (!ou || ou->isPaused()) return;
        const DWORD now = GetTickCount();
        if (!TownAftercarePolicy::BedPollDue(lastBedSweep, now)) return;
        const float dt = lastBedSweep ? (now - lastBedSweep) / 1000.0f : 0.0f;
        lastBedSweep = now;
        // A suspended tick must not age a bed stay in one step.
        bedRecoveryElapsed += dt > 1.0f ? 1.0f : dt;
        if (bedOccupantCount <= 0) return;
        BedOccupant tracked[TownMedicCoordinator::MaxBeds];
        AcquireSRWLockShared(&ownershipLock);
        const int count = bedOccupantCount;
        for (int i = 0; i < count; ++i) tracked[i] = bedOccupants[i];
        ReleaseSRWLockShared(&ownershipLock);
        int released = 0;
        for (int i = 0; i < count; ++i) {
            Character* p = tracked[i].actor.getCharacter();
            UseableStuff* bed = HospitalBed(tracked[i].bed.getBuilding());
            if (!p || !p->isValid() || Player(p) || !bed || p->inSomething != IN_BED) {
                // Unresolvable, recruited by the player, or already out of the bed
                // on their own: nothing left to give back.
                ForgetBedOccupant(tracked[i].actor);
                continue;
            }
            RosterStatus::Snapshot recovery = {};
            std::string recoveryError;
            const bool recoveryRead = RosterStatus::Read(p, recovery, &recoveryError);
            const bool dead = p->isDead();
            const bool healed = !dead && recoveryRead && recovery.band == RosterStatusPolicy::Green;
            const float age = bedRecoveryElapsed - tracked[i].entered;
            if (!dead && !healed && !TownAftercarePolicy::BedStayExpired(age)) continue;
            // A patient the care session still owns, a body being carried, or a
            // fighter who went down again must not be pulled out of the bed.
            if (!dead) {
                if (IsManaged(p) || p->isBeingCarried() || p->isUnconcious() ||
                    p->isInCombatMode(true, true)) continue;
                if (bedRecoveryElapsed - tracked[i].lastAttempt < TownAftercarePolicy::BedReleaseRetry()) continue;
            }
            const BedReleaseOutcome outcome = ReleaseBedOccupant(p, bed);
            char line[768];
            sprintf_s(line, "Proving Grounds: aftercare bed release actor=%s name=%s bed=%s reason=%s path=%s stayAge=%.1f recovery=%.3f blood=%.3f lowestLimb=%.3f band=%d leftBed=%d bedFree=%d",
                tracked[i].actor.toString().c_str(), p->getName().c_str(), tracked[i].bed.toString().c_str(),
                dead ? "dead" : healed ? "healed" : "stay-limit", outcome.path, age,
                recoveryRead ? recovery.recovery : -1.0f, recoveryRead ? recovery.blood : -1.0f,
                recoveryRead ? recovery.lowestLimb : -1.0f,
                recoveryRead ? static_cast<int>(recovery.band) : -1,
                outcome.leftBed ? 1 : 0, outcome.bedFree ? 1 : 0);
            PGLog::Debug(line);
            // The bed is the resource: stop tracking once either the character or
            // the bed itself reports the seat is free, so a refused call is retried
            // on the next poll instead of being forgotten.
            if (outcome.leftBed || outcome.bedFree) {
                ++released;
                ForgetBedOccupant(tracked[i].actor);
            } else {
                DeferBedOccupant(tracked[i].actor, bedRecoveryElapsed);
            }
        }
        if (released > 0 || bedRecoveryElapsed - lastBedSweepTrace >= 30.0f) {
            lastBedSweepTrace = bedRecoveryElapsed;
            char line[256];
            sprintf_s(line, "Proving Grounds: aftercare bed sweep tracked=%d released=%d elapsed=%.0f",
                count, released, bedRecoveryElapsed);
            PGLog::Debug(line);
        }
    }
    bool Treat(float elapsed, bool eliminatedOnly) {
        // Only the sequential town mode calls this during combat. Admission is
        // permanent for the bout: waking up never returns a KO'd fighter to play.
        eliminatedOnly = eliminatedOnly && SparSession::IsActive() &&
            SparSession::GetMode() == MatchRules::ModeTeams1v1;
        if (eliminatedOnly) {
            for (size_t i = 0; i < patients.size(); ++i) {
                Character* c = patients[i].actor.getCharacter();
                if (patients[i].earlyAdmitted || !SparSession::IsEliminated(c)) continue;
                patients[i].earlyAdmitted = true;
                PGLog::Debug("Proving Grounds: Teams 1v1 early care admitted patient=" + patients[i].actor.toString());
            }
        } else if (postFightStart < 0) postFightStart = elapsed;
        if (!eliminatedOnly && TownArenaRuntimePolicy::AftercareExpired(elapsed - postFightStart)) {
            const TownMedicCoordinator::Observation timedOut = Observe();
            for (size_t i = 0; i < patients.size(); ++i)
                TracePatient(patients[i], static_cast<int>(i), timedOut, elapsed, true);
            for (size_t i = 0; i < medics.size(); ++i)
                TraceMedic(medics[i], i, elapsed, PhaseName(coordinator.GetPhase()),
                    &coordinator.Command(static_cast<int>(i)));
            coordinator.HandoffPending(static_cast<int>(patients.size()));
            for (size_t i = 0; i < patients.size(); ++i)
                LogOutcome(static_cast<int>(i), elapsed, "bout-time-limit");
            TraceSummary(timedOut, elapsed, "timeout-unresolved-handoff", true);
            status = std::string("Medical ") + PhaseName(coordinator.GetPhase()) + " timed out; remaining care returned to town AI";
            return false;
        }
        const TownMedicCoordinator::Observation observation = Observe(eliminatedOnly);
        TownMedicCoordinator::Outcome previous[TownMedicCoordinator::MaxPatients];
        for (size_t i = 0; i < patients.size(); ++i) previous[i] = coordinator.PatientOutcome(static_cast<int>(i));
        coordinator.Tick(observation, true, elapsed, eliminatedOnly);
        for (size_t i = 0; i < patients.size(); ++i) {
            const int index = static_cast<int>(i);
            Character* p = patients[i].actor.getCharacter();
            if (!observation.patients[i].eligible) {
                TracePatient(patients[i], index, observation, elapsed, false);
                continue;
            }
            bool assigned = false, walking = false;
            for (size_t m = 0; m < medics.size(); ++m) {
                const TownMedicCoordinator::Intent& intent = coordinator.Command(static_cast<int>(m));
                if (intent.patient == index && intent.kind != TownMedicCoordinator::Wait &&
                    intent.kind != TownMedicCoordinator::Release && intent.kind != TownMedicCoordinator::PutDown) {
                    assigned = true; walking = intent.kind == TownMedicCoordinator::WalkBed;
                }
            }
            if (!assigned || coordinator.PatientDone(index) || observation.patients[i].combat) RestorePatient(patients[i]);
            else DirectPatient(p, !walking);
            const bool changed = previous[i] != coordinator.PatientOutcome(index);
            if (changed) LogOutcome(index, elapsed, coordinator.WaitExpired(index) ? "bed-wait-expired" :
                coordinator.PatientOutcome(index) == TownMedicCoordinator::Unresolved ?
                "no-capable-unfailed-medic" : "observed-patient-state");
            TracePatient(patients[i], index, observation, elapsed, changed);
        }
        const bool complete = coordinator.GetPhase() == TownMedicCoordinator::Complete;
        if (complete && !eliminatedOnly) {
            const int admitted = AdmitArenaStragglers();
            if (admitted > 0) {
                coordinator.Reopen();
                postFightStart = elapsed;
                for (size_t i = 0; i < medics.size(); ++i) {
                    if (!Available(medics[i].actor.getCharacter())) continue;
                    medics[i].patient.setNull(); medics[i].bed.setNull();
                    medics[i].lastKind = TownMedicCoordinator::Gather;
                    medics[i].command.Reset();
                    StageMedic(medics[i], i, elapsed);
                }
                char sweepStatus[160];
                sprintf_s(sweepStatus, "Aftercare final sweep: %d unconscious fighter%s awaiting hospital transfer",
                    admitted, admitted == 1 ? "" : "s");
                status = sweepStatus;
                return true;
            }
        }
        TraceSummary(observation, elapsed, complete ? (coordinator.HasUnresolved() ? "unresolved-handoff" : "complete") : "heartbeat", complete);
        for (size_t i = 0; i < medics.size(); ++i) {
            const TownMedicCoordinator::Intent intent = coordinator.Command(static_cast<int>(i));
            // Spare medics remain ringside without interrupting an active
            // treatment/carry/bed order to re-run team-wide Standby.
            if (eliminatedOnly && (intent.kind == TownMedicCoordinator::Release ||
                (intent.kind == TownMedicCoordinator::Wait && intent.patient < 0)) && observation.medics[i].ready &&
                observation.medics[i].carrying < 0) {
                if (medics[i].lastKind != TownMedicCoordinator::Gather) {
                    medics[i].patient.setNull(); medics[i].bed.setNull();
                    medics[i].lastKind = TownMedicCoordinator::Gather; medics[i].command.Reset();
                }
                StageMedic(medics[i], i, elapsed);
            } else Execute(medics[i], i, intent, observation, elapsed);
            TraceMedic(medics[i], i, elapsed, PhaseName(coordinator.GetPhase()), &intent);
        }
        if (complete && !eliminatedOnly) {
            status = coordinator.HasUnresolved() ? "Arena care ended with unresolved patients; remaining care returned to town AI" :
                "Arena care complete; medics returning to hospital";
            return false;
        }
        int treatmentPending = 0, transferPending = 0;
        for (int i = 0; i < observation.patientCount; ++i) {
            if (!observation.patients[i].eligible || coordinator.PatientDone(i)) continue;
            const TownMedicCoordinator::PatientObservation& patient = observation.patients[i];
            if (patient.flesh > .01f || patient.repair > .01f) ++treatmentPending;
            else ++transferPending;
        }
        char progress[192];
        sprintf_s(progress, "Aftercare: %d awaiting treatment; %d awaiting hospital transfer%s",
            treatmentPending, transferPending,
            coordinator.MissingBed() ? "; waiting for a free compatible bed" : "");
        status = progress;
        return true;
    }
}
