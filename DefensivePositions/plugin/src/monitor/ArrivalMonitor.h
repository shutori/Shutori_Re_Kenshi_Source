#pragma once

// Pure state machine tracking units moving into their assigned defense
// positions/turrets.
//
// IMPORTANT: This header (and its .cpp) must have ZERO KenshiLib includes so
// it can be unit-tested outside the game process. Adapters in the game layer
// are responsible for filling `GameSnapshot` from real Kenshi state each tick.

#include <stdint.h>
#include <vector>

#include "report/StatusReport.h"
#include "types/DefenseTypes.h"

const float kPositionTolerance = 1.5f;
const float kDefaultTimeoutSec = 45.f;

struct TrackedUnit
{
    uint64_t characterId;
    std::string displayName;
    DefenseAssignment assignment;
    UnitResult::Type result;
    bool done;
    float elapsedSec;
    std::string detail;
};

// Pure snapshot of one unit's live game state, filled by adapters each tick.
struct UnitSnap
{
    uint64_t characterId;
    Vec3 position;
    float facingYaw;
    bool alive;
    bool ko;
    bool imprisoned;
    bool controllable;
    bool mounted;
    ObjectRef mountedTurret;
    bool turretExists;
};

struct GameSnapshot
{
    std::vector<UnitSnap> units;
};

class ArrivalMonitor
{
public:
    // Starts tracking the given units, replacing any previous tracking set.
    void begin(const std::vector<TrackedUnit>& units);

    // True while at least one tracked unit is not yet done.
    bool isActive() const;

    // Advances every not-yet-done unit by dtSec, evaluating it against snap.
    void tick(float dtSec, const GameSnapshot& snap);

    // Externally forces an early terminal TurretRemountFailed classification
    // for one still-in-progress tracked unit, bypassing the timeout wait.
    // Intended for adapter-detected failures that are knowable before the
    // full timeout elapses (e.g. the assigned turret's operator slots are
    // occupied by someone else). No-op if characterId isn't tracked or is
    // already done (already-terminal units, e.g. TurretMissing, are not
    // overwritten).
    void markRemountFailed(uint64_t characterId, const std::string& detail = std::string());

    // Same, but for a unit the adapter layer can no longer resolve to a live
    // game object at all (its handle went stale mid-Return).
    void markUnavailable(uint64_t characterId);

    // Attaches/overwrites the optional detail string on a tracked unit
    // (whether or not it is already done). Used to annotate Timeout /
    // TurretMissing rows with adapter-side diagnostics after the fact.
    void setDetail(uint64_t characterId, const std::string& detail);

    // Drops the whole tracking set without producing a report. Used when the
    // world the units belong to goes away (save loaded, new game started).
    void reset();

    // Converts tracked units to report rows. Intended to be called once all
    // units are done (isActive() == false), but is safe to call any time.
    std::vector<UnitStatus> results() const;

private:
    void markTerminal(uint64_t characterId, UnitResult::Type result, const std::string& detail);

    std::vector<TrackedUnit> m_units;
};
