#pragma once
#include <cmath>
#include "TownAftercarePolicy.h"

// No engine objects, clocks or native calls: observations in, intentions out.
namespace TownMedicCoordinator {
    enum { MaxMedics = 10, MaxPatients = 16, MaxBeds = 64 };
    enum Phase { Gathering, Treatment, Transport, Returning, Complete, Care };
    enum Kind { Gather, Wait, Approach, Bandage, Repair, PickUp, Deliver, GoHospital, Release, PutDown, ApproachCarry, WalkBed };
    struct Point {
        float x, y, z;
        Point() : x(0), y(0), z(0) {}
    };
    inline float Distance(const Point& a, const Point& b) {
        const float x = a.x-b.x, y = a.y-b.y, z = a.z-b.z;
        return std::sqrt(x*x+y*y+z*z);
    }
    struct MedicObservation {
        bool ready, bandages, repairs;
        int carrying; // -1 empty, -2 foreign object, otherwise patient index
        Point position;
        MedicObservation() : ready(false), bandages(false), repairs(false), carrying(-1) {}
    };
    struct PatientObservation {
        bool valid, hospital, robot, inBed, delivered, externalCarry, combat, canWalk, eligible;
        float flesh, repair, bleed;
        Point position;
        PatientObservation() : valid(false), hospital(false), robot(false), inBed(false), delivered(false),
            externalCarry(false), combat(false), canWalk(false), eligible(true), flesh(0), repair(0), bleed(0) {}
    };
    struct BedObservation {
        bool available, robot;
        int occupant; // patient index, or -1
        Point position;
        BedObservation() : available(false), robot(false), occupant(-1) {}
    };
    struct Observation {
        int medicCount, patientCount, bedCount;
        MedicObservation medics[MaxMedics];
        PatientObservation patients[MaxPatients];
        BedObservation beds[MaxBeds];
        bool hospitalAvailable;
        Point hospital;
        Observation() : medicCount(0), patientCount(0), bedCount(0), hospitalAvailable(false) {}
    };
    struct Intent {
        Kind kind;
        int patient, bed;
        Intent(Kind k = Wait, int p = -1, int b = -1) : kind(k), patient(p), bed(b) {}
    };
    enum Outcome { Pending, Treated, Delivered, Recovered, Unavailable, Unresolved };
    class Coordinator {
        Phase phase;
        int assigned[MaxMedics], reservedBed[MaxMedics], activeMode[MaxMedics];
        int helperTarget[MaxMedics], helperMode[MaxMedics];
        unsigned failed[MaxMedics][MaxPatients];
        bool walkFailed[MaxPatients], missingBed;
        Outcome outcomes[MaxPatients];
        Intent commands[MaxMedics];
        // Time a patient has spent owing a hospital transfer with a medic
        // blocked on Wait for it. Cleared by any real work on that patient.
        float bedWait[MaxPatients];
        bool bedWaitExpired[MaxPatients];
        int expiredWaits;
        float lastTick;
        static bool NeedsAid(const PatientObservation& p) {
            return p.valid && (p.flesh > .01f || p.repair > .01f);
        }
        static int Mode(const MedicObservation& m, const PatientObservation& p) {
            const bool flesh = m.bandages && p.flesh > .01f, robot = m.repairs && p.repair > .01f;
            return robot && (!flesh || p.repair > p.flesh) ? 1 : flesh ? 0 : -1;
        }
        static bool Carried(int p, const Observation& o) {
            for (int m = 0; m < o.medicCount; ++m) if (o.medics[m].carrying == p) return true;
            return false;
        }
        bool Assigned(int p, const Observation& o) const {
            for (int m = 0; m < o.medicCount; ++m) if (assigned[m] == p) return true;
            return false;
        }
        bool Failed(int m, int p, Kind k) const { return (failed[m][p] & (1u << k)) != 0; }
        int AidMode(int m, int p, const Observation& o) const {
            // Keep the native treatment running when its relative score crosses
            // the other injury type. Switch only when the current work clears.
            if (assigned[m] == p) {
                if (activeMode[m] == 0 && o.medics[m].bandages && o.patients[p].flesh > .01f && !Failed(m,p,Bandage)) return 0;
                if (activeMode[m] == 1 && o.medics[m].repairs && o.patients[p].repair > .01f && !Failed(m,p,Repair)) return 1;
            }
            if (helperTarget[m] == p) {
                if (helperMode[m] == 0 && o.medics[m].bandages && o.patients[p].flesh > .01f && !Failed(m,p,Bandage)) return 0;
                if (helperMode[m] == 1 && o.medics[m].repairs && o.patients[p].repair > .01f && !Failed(m,p,Repair)) return 1;
            }
            return Mode(o.medics[m], o.patients[p]);
        }
        bool Eligible(int m, int p, const Observation& o) const {
            if (!o.patients[p].eligible || outcomes[p] != Pending || !o.patients[p].valid || o.patients[p].combat ||
                o.patients[p].externalCarry || Carried(p, o)) return false;
            if (NeedsAid(o.patients[p])) {
                const int mode = AidMode(m, p, o);
                return mode >= 0 && !Failed(m, p, mode ? Repair : Bandage);
            }
            if (Failed(m, p, Deliver)) return false;
            return (o.patients[p].canWalk && !walkFailed[p]) || !Failed(m, p, PickUp);
        }
        bool BedUsable(int b, int p, const Observation& o) const {
            return b >= 0 && b < o.bedCount && o.beds[b].robot == o.patients[p].robot &&
                (o.beds[b].available || o.beds[b].occupant == p);
        }
        int FindBed(int m, int p, const Observation& o) const {
            int best = -1; float nearest = 0;
            for (int b = 0; b < o.bedCount; ++b) {
                if (!BedUsable(b, p, o)) continue;
                bool taken = false;
                for (int other = 0; other < o.medicCount; ++other)
                    if (other != m && reservedBed[other] == b) taken = true;
                const float distance = Distance(o.patients[p].position, o.beds[b].position);
                if (!taken && (best < 0 || distance < nearest)) { best = b; nearest = distance; }
            }
            return best;
        }
    public:
        Coordinator() { Reset(); }
        void Reset() {
            phase = Gathering; missingBed = false;
            for (int p = 0; p < MaxPatients; ++p) {
                outcomes[p] = Pending; walkFailed[p] = false;
                bedWait[p] = 0.0f; bedWaitExpired[p] = false;
            }
            expiredWaits = 0; lastTick = 0.0f;
            for (int m = 0; m < MaxMedics; ++m) {
                assigned[m] = reservedBed[m] = activeMode[m] = -1;
                helperTarget[m] = helperMode[m] = -1; commands[m] = Intent(Release);
                for (int p = 0; p < MaxPatients; ++p) failed[m][p] = 0;
            }
        }
        Phase GetPhase() const { return phase; }
        const Intent& Command(int m) const { return commands[m]; }
        void Reopen() {
            if (phase != Complete) return;
            phase = Care; missingBed = false;
            for (int m = 0; m < MaxMedics; ++m) {
                assigned[m] = reservedBed[m] = activeMode[m] = -1;
                helperTarget[m] = helperMode[m] = -1;
                commands[m] = Intent(Wait);
            }
            for (int p = 0; p < MaxPatients; ++p) { bedWait[p] = 0.0f; bedWaitExpired[p] = false; }
        }
        bool IsHelping(int m) const { return assigned[m] < 0 && helperTarget[m] >= 0; }
        bool HasPatientWork(int p) const {
            for (int m = 0; m < MaxMedics; ++m)
                if (commands[m].patient == p && commands[m].kind != Wait &&
                    commands[m].kind != Release && commands[m].kind != PutDown) return true;
            return false;
        }
        bool PatientDone(int p) const { return outcomes[p] != Pending; }
        Outcome PatientOutcome(int p) const { return outcomes[p]; }
        // A bounded bed wait reports its own outcome so the log can tell a
        // handoff from a patient that walked off, instead of one shared reason.
        float BedWait(int p) const { return p >= 0 && p < MaxPatients ? bedWait[p] : 0.0f; }
        bool WaitExpired(int p) const { return p >= 0 && p < MaxPatients ? bedWaitExpired[p] : false; }
        int ExpiredWaits() const { return expiredWaits; }
        int ReservedBed(int m) const { return m >= 0 && m < MaxMedics ? reservedBed[m] : -1; }
        bool MedicDone(int) const { return phase == Complete; }
        bool MissingBed() const { return missingBed; }
        bool HasUnresolved() const {
            for (int p = 0; p < MaxPatients; ++p) if (outcomes[p] == Unresolved) return true;
            return false;
        }
        void Fail(int m, int p, Kind k) {
            if (p < 0 || p >= MaxPatients) return;
            if (k == WalkBed) walkFailed[p] = true;
            else failed[m][p] |= 1u << (k == ApproachCarry ? PickUp : k);
            assigned[m] = reservedBed[m] = activeMode[m] = -1;
            helperTarget[m] = helperMode[m] = -1;
            commands[m] = Intent(Release);
        }
        void HandoffPending(int count) {
            for (int p = 0; p < count; ++p) if (outcomes[p] == Pending) outcomes[p] = Unresolved;
        }
        void Tick(const Observation& o, bool fightOver, float elapsed, bool holdSpares) {
            for (int m = 0; m < MaxMedics; ++m) commands[m] = Intent(Release);
            // A suspended or restarted clock must not age a transfer wait.
            float dt = elapsed - lastTick;
            lastTick = elapsed;
            if (dt < 0.0f || dt > 1.0f) dt = dt > 1.0f ? 1.0f : 0.0f;
            if (phase == Complete) return;
            if (!fightOver) {
                for (int m = 0; m < o.medicCount; ++m) if (o.medics[m].ready) commands[m] = Intent(Gather);
                return;
            }
            phase = Care; missingBed = false;
            for (int p = 0; p < o.patientCount; ++p) {
                if (outcomes[p] != Pending || !o.patients[p].eligible) continue;
                const PatientObservation& patient = o.patients[p];
                if (!patient.valid) outcomes[p] = Unavailable;
                else if (!NeedsAid(patient) && !Carried(p, o) && !patient.externalCarry) {
                    if (patient.delivered || patient.inBed) outcomes[p] = Delivered;
                    else if (!patient.hospital) outcomes[p] = Treated;
                }
            }
            // Release obsolete assignments before assigning any new work.
            for (int m = 0; m < o.medicCount; ++m) {
                const int p = assigned[m];
                if (!o.medics[m].ready || (p >= 0 && o.medics[m].carrying != p && !Eligible(m, p, o))) assigned[m] = reservedBed[m] = activeMode[m] = -1;
            }
            // An observed carry takes precedence over all planned assignments.
            for (int m = 0; m < o.medicCount; ++m) {
                const int p = o.medics[m].carrying;
                if (p < 0) continue;
                if (!o.patients[p].eligible) continue;
                if (!o.patients[p].valid) { commands[m] = Intent(PutDown, p); continue; }
                if (!o.medics[m].ready) continue;
                for (int other = 0; other < o.medicCount; ++other)
                    if (other != m && assigned[other] == p) assigned[other] = reservedBed[other] = activeMode[other] = -1;
                if (assigned[m] != p) reservedBed[m] = -1;
                assigned[m] = p;
            }
            for (int m = 0; m < o.medicCount; ++m) {
                if (!o.medics[m].ready || commands[m].kind == PutDown) continue;
                if (assigned[m] < 0) {
                    int best = -1; float priority = 0;
                    for (int p = 0; p < o.patientCount; ++p) {
                        if (Assigned(p, o) || !Eligible(m, p, o)) continue;
                        const PatientObservation& patient = o.patients[p];
                        const float score = (patient.bleed > .0001f ? 100000.0f : 0) +
                            (NeedsAid(patient) ? 10000.0f : 0) + patient.flesh + patient.repair -
                            Distance(o.medics[m].position, patient.position);
                        if (best < 0 || score > priority) { best = p; priority = score; }
                    }
                    assigned[m] = best;
                }
                const int p = assigned[m];
                if (p < 0) continue;
                const PatientObservation& patient = o.patients[p];
                if (NeedsAid(patient) && o.medics[m].carrying != p) {
                    activeMode[m] = AidMode(m, p, o);
                    commands[m] = Intent(activeMode[m] == 1 ? Repair : Bandage, p);
                    continue;
                }
                if (!BedUsable(reservedBed[m], p, o)) reservedBed[m] = -1;
                if (reservedBed[m] < 0) reservedBed[m] = FindBed(m, p, o);
                if (reservedBed[m] < 0) { commands[m] = Intent(Wait, p); missingBed = true; continue; }
                Kind kind = o.medics[m].carrying == p ? Deliver :
                    patient.canWalk && !walkFailed[p] ? WalkBed :
                    Distance(o.medics[m].position, patient.position) > 12.0f ? ApproachCarry : PickUp;
                commands[m] = Intent(kind, p, reservedBed[m]);
            }
            // Extra hands treat only. The primary owner above retains the bed
            // claim and is the only medic allowed to start this patient's transfer.
            for (int m = 0; m < o.medicCount; ++m) {
                int p = helperTarget[m];
                if (!o.medics[m].ready || o.medics[m].carrying != -1 || assigned[m] >= 0 ||
                    p < 0 || !NeedsAid(o.patients[p]) || !Eligible(m, p, o))
                    helperTarget[m] = helperMode[m] = -1;
            }
            for (int m = 0; m < o.medicCount; ++m) {
                if (!o.medics[m].ready || o.medics[m].carrying != -1 || assigned[m] >= 0 ||
                    commands[m].kind == PutDown) continue;
                if (helperTarget[m] < 0) {
                    int best = -1, fewest = MaxMedics + 1; float nearest = 0;
                    for (int p = 0; p < o.patientCount; ++p) {
                        if (!NeedsAid(o.patients[p]) || !Eligible(m, p, o)) continue;
                        int helpers = 0;
                        for (int other = 0; other < o.medicCount; ++other)
                            if (helperTarget[other] == p) ++helpers;
                        const float distance = Distance(o.medics[m].position, o.patients[p].position);
                        if (helpers < fewest || (helpers == fewest && (best < 0 || distance < nearest))) {
                            best = p; fewest = helpers; nearest = distance;
                        }
                    }
                    helperTarget[m] = best;
                }
                const int p = helperTarget[m];
                if (p >= 0) {
                    helperMode[m] = AidMode(m, p, o);
                    commands[m] = Intent(helperMode[m] == 1 ? Repair : Bandage, p);
                }
            }
            // Bound the wait for a hospital bed. A patient that still owes a
            // transfer but has no bed resolved holds its medic on Wait; nothing
            // else ages that hold out, so a full or unavailable hospital used to
            // pin the patient, the medics and the bout open until the 300s cap.
            // Real work on the patient (treatment, a carry, a resolved
            // pickup/approach/delivery or a bed walk) clears the clock, so a
            // normal bed wait is never charged the budget.
            for (int p = 0; p < o.patientCount; ++p) {
                const PatientObservation& patient = o.patients[p];
                if (outcomes[p] != Pending || !patient.eligible || !patient.valid ||
                    !patient.hospital || NeedsAid(patient) || patient.delivered ||
                    patient.inBed || patient.combat || patient.externalCarry || Carried(p, o)) {
                    bedWait[p] = 0.0f; continue;
                }
                bool blocked = false;
                for (int m = 0; m < o.medicCount; ++m)
                    if (commands[m].patient == p && commands[m].kind == Wait) { blocked = true; break; }
                if (!blocked) { bedWait[p] = 0.0f; continue; }
                bedWait[p] += dt;
                if (!TownAftercarePolicy::TransferWaitExpired(bedWait[p])) continue;
                outcomes[p] = Unresolved;
                bedWaitExpired[p] = true;
                ++expiredWaits;
                for (int m = 0; m < MaxMedics; ++m) if (assigned[m] == p) Fail(m, p, Wait);
            }
            bool pending = false;
            for (int p = 0; p < o.patientCount; ++p) if (outcomes[p] == Pending) {
                if (!o.patients[p].eligible) { pending = true; continue; }
                bool candidate = Assigned(p, o) || o.patients[p].combat ||
                    o.patients[p].externalCarry || Carried(p, o);
                for (int m = 0; m < o.medicCount; ++m)
                    if (o.medics[m].ready && Eligible(m, p, o)) candidate = true;
                if (!candidate) outcomes[p] = Unresolved;
                else pending = true;
            }
            if (!pending) {
                phase = Complete;
                for (int m = 0; m < o.medicCount; ++m) {
                    if (commands[m].kind == PutDown || !o.medics[m].ready || o.medics[m].carrying != -1) continue;
                    commands[m] = Intent(o.hospitalAvailable && Distance(o.medics[m].position, o.hospital) > 12.0f ? GoHospital : Release);
                }
            } else {
                // Spare medics head back to the hospital instead of holding
                // formation ringside: a single patient waiting on a bed must not
                // park the whole team while the bout stays open. The waiter keeps
                // the Wait command the assignment loop set for it, and any other
                // ready medic is still re-assigned the moment work appears.
                // Mid-bout care (sequential Teams 1v1) keeps the old ringside
                // hold, so the next eliminated fighter still has its team at hand.
                for (int m = 0; m < o.medicCount; ++m) {
                    if (!o.medics[m].ready || o.medics[m].carrying != -1) continue;
                    if (commands[m].kind != Release) continue;
                    commands[m] = holdSpares ? Intent(Wait) : Intent(o.hospitalAvailable &&
                        Distance(o.medics[m].position, o.hospital) > 12.0f ? GoHospital : Release);
                }
            }
        }
    };
}
