#pragma once

// Thin KenshiLib wrappers for selection, identity, pose, and turret queries.
//
// This header may only be included from `game/` and `ui/` sources — it pulls
// in KenshiLib types (Character, Building) and is not safe for the pure,
// unit-tested modules (types/, store/, report/, monitor/ core logic).

#include "types/DefenseTypes.h"
#include "report/StatusReport.h"
#include "monitor/ArrivalMonitor.h"

#include <stdint.h>
#include <string>
#include <vector>

class Character;
class Building;

namespace GameAdapters
{
    // Stable across save/load: packed from RootObjectBase::getHandle() (the
    // `hand` index+serial pair), which is KenshiLib's own mechanism for
    // referencing objects across serialise()/loadFromSerialise() and
    // survives quicksave/load (see kenshi/util/hand.h toString/fromString).
    uint64_t stableCharacterId(Character* c);

    // Reconstructs a `hand` from a previously-saved stableCharacterId() and
    // resolves it to the live Character, or nullptr if that handle no longer
    // refers to one (unloaded, destroyed, or the slot was recycled — the
    // packed serial is what catches recycling).
    //
    // Callers that need a Character across more than one frame must hold the
    // id and re-resolve through this each tick rather than caching the
    // pointer: Kenshi is free to destroy the object in between.
    Character* resolveCharacter(uint64_t characterId);

    // Live multi-selection (PlayerInterface::selectedCharacters), falling
    // back to the single-select handle if the set is empty.
    std::vector<Character*> getSelectedCharacters();

    bool isPlayerControllable(Character* c);

    // Player-visible name from RootObjectBase::getName() (e.g. "Rex").
    std::string getDisplayName(Character* c);

    Vec3 getPosition(Character* c);
    float getFacingYaw(Character* c);
    void setFacingYaw(Character* c, float yaw);

    // Resolves Character::isUsingTurret; on success turretOut is the full
    // hand of that mount (not a rebuilt BUILDING-only handle).
    bool isMountedOnTurret(Character* c, ObjectRef& turretOut);

    bool turretExists(const ObjectRef& turret);
    Building* resolveTurret(const ObjectRef& turret);

    // Early (pre-timeout) remount-failure detection: true if turret still
    // resolves to a live Building but its UseableStuff::couldIOperate(c's
    // handle) says c cannot use it right now (e.g. all operator slots are
    // occupied by other characters).
    bool isTurretRemountBlocked(Character* c, const ObjectRef& turret);

    bool isNearTurret(Character* c, const ObjectRef& turret, float radius);

    // Human-readable remount diagnosis for HUD/debug logs.
    std::string diagnoseTurretRemount(Character* c, const ObjectRef& turret);

    UnitResult::Type classifyAvailability(Character* c);

    // Fills a pure UnitSnap from live KenshiLib state. assignedTurret is the
    // unit's DefenseAssignment::turret (empty for ground) — used for
    // UnitSnap::turretExists.
    UnitSnap buildUnitSnapshot(Character* c, const ObjectRef& assignedTurret);
}
