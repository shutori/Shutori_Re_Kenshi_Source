#pragma once

// Issues KenshiLib movement/interaction orders to restore a saved defense
// position: walk to a point, or walk-and-mount a turret.
//
// This header pulls in Character (a forward-declared KenshiLib type used by
// value in real game code), so like GameAdapters it may only be included
// from `game/`/`ui`/plugin entry-point sources, not from the pure,
// unit-tested modules (types/, store/, report/, monitor/).

#include "types/DefenseTypes.h"

#include <stdint.h>

class Character;

namespace CommandIssuer
{
    // Issues the default "move here" player order (Character::
    // playerMoveOrderDefault with no target building/object) so the
    // character walks to pos under its own AI/pathing. Returns false (no
    // order issued) if c is null or currently can't take player orders
    // (dead/KO/imprisoned/etc.) — see Character::canTakePlayerOrdersAtThisTime.
    bool issueMoveTo(Character* c, const Vec3& pos);

    // Resolves turret to a live Building via GameAdapters::resolveTurret,
    // then issues Character::addOrder with the turret's getDefaultTask()
    // (MAN_A_TURRET / MAN_A_TURRET_ON_BUILDING). For furniture turrets the
    // pathing dest is the wall from getMountedBuilding(); the turret stays
    // the order subject.
    // Returns false if c is null, can't take orders right now, or turret
    // no longer resolves to a live Building.
    bool issueMountTurret(Character* c, const ObjectRef& turret);

    // Near-range remount nudge: UseableStuff::tryOperate once the character
    // is already next to the turret (covers the case where the walk order
    // arrived but the man-turret task never started). Returns true if
    // tryOperate accepted the character.
    bool tryOperateTurret(Character* c, const ObjectRef& turret);

    // Directly sets the character's facing (GameAdapters::setFacingYaw) —
    // not an order, just an immediate pose restore for ground positions
    // that were saved without a turret mount. Returns false if c is null.
    bool applyFacing(Character* c, float yaw);
}
