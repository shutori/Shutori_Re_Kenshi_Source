#include <boost/test/unit_test.hpp>
#include "store/LayoutStore.h"

namespace
{
    ObjectRef makeTurretRef(uint32_t index)
    {
        ObjectRef ref = {};
        ref.index = index;
        ref.serial = 1;
        ref.type = 0; // BUILDING
        return ref;
    }
}

BOOST_AUTO_TEST_CASE(upsert_merges_by_character_id)
{
    LayoutStore store;
    DefenseAssignment a = {};
    a.characterId = 1;
    a.position.x = 10.f;
    a.facingYaw = 1.5f;
    store.upsert(a);

    DefenseAssignment b = a;
    b.position.x = 20.f;
    b.hasTurret = true;
    b.turret = makeTurretRef(99);
    store.upsert(b);

    BOOST_CHECK_EQUAL(store.size(), 1u);
    DefenseAssignment out;
    BOOST_REQUIRE(store.tryGet(1, out));
    BOOST_CHECK_CLOSE(out.position.x, 20.f, 0.001);
    BOOST_CHECK(out.hasTurret);
    BOOST_CHECK_EQUAL(out.turret.index, 99u);
}

BOOST_AUTO_TEST_CASE(upsert_does_not_wipe_other_characters)
{
    LayoutStore store;
    DefenseAssignment a = {}; a.characterId = 1; store.upsert(a);
    DefenseAssignment b = {}; b.characterId = 2; store.upsert(b);
    DefenseAssignment a2 = {}; a2.characterId = 1; a2.position.x = 5.f; store.upsert(a2);
    BOOST_CHECK_EQUAL(store.size(), 2u);
    DefenseAssignment out;
    BOOST_REQUIRE(store.tryGet(2, out));
}

BOOST_AUTO_TEST_CASE(serialize_round_trip)
{
    LayoutStore store;
    DefenseAssignment a = {};
    a.characterId = 42;
    a.position.x = 1.f;
    a.position.y = 2.f;
    a.position.z = 3.f;
    a.facingYaw = 0.25f;
    a.hasTurret = true;
    a.turret = makeTurretRef(7);
    a.turret.container = 11;
    a.turret.containerSerial = 22;
    a.turret.type = 5;
    store.upsert(a);

    LayoutStore loaded;
    BOOST_REQUIRE(loaded.deserialize(store.serialize()));
    DefenseAssignment out;
    BOOST_REQUIRE(loaded.tryGet(42, out));
    BOOST_CHECK_EQUAL(out.turret.index, 7u);
    BOOST_CHECK_EQUAL(out.turret.container, 11u);
    BOOST_CHECK_EQUAL(out.turret.containerSerial, 22u);
    BOOST_CHECK_EQUAL(out.turret.type, 5);
    BOOST_CHECK_CLOSE(out.position.y, 2.f, 0.001);
}

BOOST_AUTO_TEST_CASE(erase_removes_existing_character)
{
    LayoutStore store;
    DefenseAssignment a = {};
    a.characterId = 1;
    store.upsert(a);
    DefenseAssignment b = {};
    b.characterId = 2;
    store.upsert(b);

    BOOST_CHECK(store.erase(1));
    BOOST_CHECK_EQUAL(store.size(), 1u);

    DefenseAssignment out;
    BOOST_CHECK(!store.tryGet(1, out));
    BOOST_REQUIRE(store.tryGet(2, out));
}

BOOST_AUTO_TEST_CASE(erase_missing_character_returns_false)
{
    LayoutStore store;
    DefenseAssignment a = {};
    a.characterId = 1;
    store.upsert(a);

    BOOST_CHECK(!store.erase(99));
    BOOST_CHECK_EQUAL(store.size(), 1u);
    DefenseAssignment out;
    BOOST_REQUIRE(store.tryGet(1, out));
}
