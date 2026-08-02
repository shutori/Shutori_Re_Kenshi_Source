#include <boost/test/unit_test.hpp>
#include "monitor/ArrivalMonitor.h"

namespace
{
    ObjectRef makeTurretRef(uint32_t index)
    {
        ObjectRef ref = {};
        ref.index = index;
        ref.serial = 1;
        ref.type = 0;
        return ref;
    }

    TrackedUnit makeUnit(uint64_t id, const DefenseAssignment& assignment)
    {
        TrackedUnit u = {};
        u.characterId = id;
        u.assignment = assignment;
        u.result = UnitResult::Unavailable;
        u.done = false;
        u.elapsedSec = 0.f;
        return u;
    }

    UnitSnap makeSnap(uint64_t id, Vec3 position)
    {
        UnitSnap s = {};
        s.characterId = id;
        s.position = position;
        s.facingYaw = 0.f;
        s.alive = true;
        s.ko = false;
        s.imprisoned = false;
        s.controllable = true;
        s.mounted = false;
        s.mountedTurret = ObjectRef();
        s.turretExists = false;
        return s;
    }
}

BOOST_AUTO_TEST_CASE(ground_arrival_succeeds_within_tolerance)
{
    DefenseAssignment assignment = {};
    assignment.characterId = 1;
    assignment.position.x = 10.f;
    assignment.position.y = 0.f;
    assignment.position.z = 0.f;
    assignment.hasTurret = false;

    ArrivalMonitor monitor;
    std::vector<TrackedUnit> units;
    units.push_back(makeUnit(1, assignment));
    monitor.begin(units);
    BOOST_CHECK(monitor.isActive());

    UnitSnap snap = makeSnap(1, assignment.position);
    snap.position.x = 10.5f; // within kPositionTolerance (1.5)
    GameSnapshot gs;
    gs.units.push_back(snap);
    monitor.tick(1.f, gs);

    BOOST_CHECK(!monitor.isActive());
    std::vector<UnitStatus> results = monitor.results();
    BOOST_REQUIRE_EQUAL(results.size(), 1u);
    BOOST_CHECK_EQUAL(results[0].characterId, 1u);
    BOOST_CHECK(results[0].result == UnitResult::Ok);
}

BOOST_AUTO_TEST_CASE(turret_arrival_succeeds_when_mounted_on_assigned_turret)
{
    DefenseAssignment assignment = {};
    assignment.characterId = 2;
    assignment.hasTurret = true;
    assignment.turret = makeTurretRef(42);

    ArrivalMonitor monitor;
    std::vector<TrackedUnit> units;
    units.push_back(makeUnit(2, assignment));
    monitor.begin(units);

    UnitSnap snap = makeSnap(2, assignment.position);
    snap.turretExists = true;
    snap.mounted = true;
    snap.mountedTurret = makeTurretRef(42);
    GameSnapshot gs;
    gs.units.push_back(snap);
    monitor.tick(1.f, gs);

    BOOST_CHECK(!monitor.isActive());
    std::vector<UnitStatus> results = monitor.results();
    BOOST_REQUIRE_EQUAL(results.size(), 1u);
    BOOST_CHECK(results[0].result == UnitResult::Ok);
}

BOOST_AUTO_TEST_CASE(turret_arrival_fails_immediately_when_turret_missing)
{
    DefenseAssignment assignment = {};
    assignment.characterId = 3;
    assignment.hasTurret = true;
    assignment.turret = makeTurretRef(42);

    ArrivalMonitor monitor;
    std::vector<TrackedUnit> units;
    units.push_back(makeUnit(3, assignment));
    monitor.begin(units);

    UnitSnap snap = makeSnap(3, assignment.position);
    snap.turretExists = false; // destroyed/removed
    GameSnapshot gs;
    gs.units.push_back(snap);
    monitor.tick(0.1f, gs); // should not need to wait for timeout

    BOOST_CHECK(!monitor.isActive());
    std::vector<UnitStatus> results = monitor.results();
    BOOST_REQUIRE_EQUAL(results.size(), 1u);
    BOOST_CHECK(results[0].result == UnitResult::TurretMissing);
}

BOOST_AUTO_TEST_CASE(ground_arrival_times_out_when_never_reaching_position)
{
    DefenseAssignment assignment = {};
    assignment.characterId = 4;
    assignment.position.x = 0.f;
    assignment.position.y = 0.f;
    assignment.position.z = 0.f;
    assignment.hasTurret = false;

    ArrivalMonitor monitor;
    std::vector<TrackedUnit> units;
    units.push_back(makeUnit(4, assignment));
    monitor.begin(units);

    UnitSnap snap = makeSnap(4, assignment.position);
    snap.position.x = 100.f; // far outside tolerance
    GameSnapshot gs;
    gs.units.push_back(snap);

    monitor.tick(44.f, gs);
    BOOST_CHECK(monitor.isActive()); // not yet timed out (kDefaultTimeoutSec = 45)

    monitor.tick(2.f, gs); // elapsed now 46s, past timeout
    BOOST_CHECK(!monitor.isActive());
    std::vector<UnitStatus> results = monitor.results();
    BOOST_REQUIRE_EQUAL(results.size(), 1u);
    BOOST_CHECK(results[0].result == UnitResult::Timeout);
}

BOOST_AUTO_TEST_CASE(turret_arrival_fails_early_when_marked_remount_failed)
{
    DefenseAssignment assignment = {};
    assignment.characterId = 6;
    assignment.hasTurret = true;
    assignment.turret = makeTurretRef(42);

    ArrivalMonitor monitor;
    std::vector<TrackedUnit> units;
    units.push_back(makeUnit(6, assignment));
    monitor.begin(units);

    // Simulate an adapter detecting the turret is occupied/unreachable well
    // before the timeout would otherwise fire.
    monitor.markRemountFailed(6);
    BOOST_CHECK(!monitor.isActive());

    // A still-in-progress snapshot tick afterwards must not override the
    // early failure (unit is already done).
    UnitSnap snap = makeSnap(6, assignment.position);
    snap.turretExists = true;
    GameSnapshot gs;
    gs.units.push_back(snap);
    monitor.tick(0.1f, gs);

    std::vector<UnitStatus> results = monitor.results();
    BOOST_REQUIRE_EQUAL(results.size(), 1u);
    BOOST_CHECK(results[0].result == UnitResult::TurretRemountFailed);
}

BOOST_AUTO_TEST_CASE(mark_remount_failed_is_noop_for_unknown_or_done_unit)
{
    DefenseAssignment assignment = {};
    assignment.characterId = 7;
    assignment.hasTurret = true;
    assignment.turret = makeTurretRef(42);

    ArrivalMonitor monitor;
    std::vector<TrackedUnit> units;
    units.push_back(makeUnit(7, assignment));
    monitor.begin(units);

    monitor.markRemountFailed(999); // unknown id -> no-op
    BOOST_CHECK(monitor.isActive());

    UnitSnap snap = makeSnap(7, assignment.position);
    snap.turretExists = true;
    snap.mounted = true;
    snap.mountedTurret = makeTurretRef(42);
    GameSnapshot gs;
    gs.units.push_back(snap);
    monitor.tick(1.f, gs); // unit finishes as Ok

    monitor.markRemountFailed(7); // already done -> must not overwrite Ok
    std::vector<UnitStatus> results = monitor.results();
    BOOST_REQUIRE_EQUAL(results.size(), 1u);
    BOOST_CHECK(results[0].result == UnitResult::Ok);
}

BOOST_AUTO_TEST_CASE(mark_unavailable_ends_tracking_for_stale_character)
{
    DefenseAssignment assignment = {};
    assignment.characterId = 8;
    assignment.hasTurret = false;

    ArrivalMonitor monitor;
    std::vector<TrackedUnit> units;
    units.push_back(makeUnit(8, assignment));
    monitor.begin(units);

    // The adapter layer could no longer resolve this character's handle to a
    // live object, so it can't be tracked any further.
    monitor.markUnavailable(8);

    BOOST_CHECK(!monitor.isActive());
    std::vector<UnitStatus> results = monitor.results();
    BOOST_REQUIRE_EQUAL(results.size(), 1u);
    BOOST_CHECK(results[0].result == UnitResult::Unavailable);

    // Already done: a later arrival snapshot must not resurrect it.
    UnitSnap snap = makeSnap(8, assignment.position);
    GameSnapshot gs;
    gs.units.push_back(snap);
    monitor.tick(1.f, gs);

    results = monitor.results();
    BOOST_CHECK(results[0].result == UnitResult::Unavailable);
}

BOOST_AUTO_TEST_CASE(reset_abandons_tracking_without_reporting)
{
    DefenseAssignment assignment = {};
    assignment.characterId = 9;
    assignment.hasTurret = false;

    ArrivalMonitor monitor;
    std::vector<TrackedUnit> units;
    units.push_back(makeUnit(9, assignment));
    monitor.begin(units);
    BOOST_CHECK(monitor.isActive());

    monitor.reset();

    BOOST_CHECK(!monitor.isActive());
    BOOST_CHECK(monitor.results().empty());

    // A tick after reset must not revive the dropped unit.
    UnitSnap snap = makeSnap(9, assignment.position);
    snap.position.x = 100.f;
    GameSnapshot gs;
    gs.units.push_back(snap);
    monitor.tick(1.f, gs);
    BOOST_CHECK(monitor.results().empty());
}

BOOST_AUTO_TEST_CASE(ko_unit_is_skipped_immediately)
{
    DefenseAssignment assignment = {};
    assignment.characterId = 5;
    assignment.hasTurret = false;

    ArrivalMonitor monitor;
    std::vector<TrackedUnit> units;
    units.push_back(makeUnit(5, assignment));
    monitor.begin(units);

    UnitSnap snap = makeSnap(5, assignment.position);
    snap.ko = true;
    GameSnapshot gs;
    gs.units.push_back(snap);
    monitor.tick(1.f, gs);

    BOOST_CHECK(!monitor.isActive());
    std::vector<UnitStatus> results = monitor.results();
    BOOST_REQUIRE_EQUAL(results.size(), 1u);
    BOOST_CHECK(results[0].result == UnitResult::Ko);
}
