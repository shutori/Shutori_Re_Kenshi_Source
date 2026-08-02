#include "GameAdapters.h"

#include <Debug.h>

#include <kenshi/Globals.h>
#include <kenshi/GameWorld.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Character.h>
#include <kenshi/CharMovement.h>
#include <kenshi/Building/Building.h>
#include <kenshi/Building/UseableStuff.h>
#include <kenshi/util/hand.h>
#include <kenshi/util/OgreUnordered.h>
#include <kenshi/Enums.h>

#include <ogre/OgreVector3.h>

#include <cmath>
#include <sstream>

namespace
{
    // Packs a `hand`'s (index, serial) pair into a single 64-bit id. `serial`
    // is what guards against a reused `index` referring to a different
    // object after that slot is recycled, so both halves are required for a
    // save/load-stable identity. Used for characters (standalone objects).
    uint64_t packHand(const hand& h)
    {
        return (static_cast<uint64_t>(h.serial) << 32) | static_cast<uint64_t>(h.index);
    }

    hand unpackHand(uint64_t id, itemType type)
    {
        const unsigned int index = static_cast<unsigned int>(id & 0xFFFFFFFFu);
        const unsigned int serial = static_cast<unsigned int>(id >> 32);
        return hand(index, serial, type, 0, 0);
    }

    // Full hand ↔ ObjectRef. Required for furniture turrets whose container
    // fields identify the wall/building they are attached to.
    ObjectRef toObjectRef(const hand& h)
    {
        ObjectRef ref = {};
        ref.index = h.index;
        ref.serial = h.serial;
        ref.container = h.container;
        ref.containerSerial = h.containerSerial;
        ref.type = static_cast<int32_t>(h.type);
        return ref;
    }

    hand fromObjectRef(const ObjectRef& ref)
    {
        return hand(
            ref.index,
            ref.serial,
            static_cast<itemType>(ref.type),
            ref.container,
            ref.containerSerial);
    }

    void appendObjectRef(std::ostringstream& out, const ObjectRef& ref)
    {
        out << "idx=" << ref.index
            << " serial=0x" << std::hex << ref.serial << std::dec
            << " type=" << ref.type
            << " container=" << ref.container
            << "/" << ref.containerSerial;
    }
}

namespace GameAdapters
{
    uint64_t stableCharacterId(Character* c)
    {
        if (!c)
            return 0;

        return packHand(c->getHandle());
    }

    Character* resolveCharacter(uint64_t characterId)
    {
        if (characterId == 0)
            return nullptr;

        const hand h = unpackHand(characterId, CHARACTER);
        if (!h.isValid())
            return nullptr;

        return h.getCharacter();
    }

    std::vector<Character*> getSelectedCharacters()
    {
        std::vector<Character*> result;

        if (!ou || !ou->player)
        {
            DebugLog("DefensivePositions: getSelectedCharacters - no player interface");
            return result;
        }

        PlayerInterface* player = ou->player;

        for (ogre_unordered_set<hand>::type::const_iterator it = player->selectedCharacters.begin();
             it != player->selectedCharacters.end(); ++it)
        {
            Character* c = it->getCharacter();
            if (c)
                result.push_back(c);
        }

        // Single-click selection (no drag box) may only populate the
        // singular `selectedCharacter` handle rather than the multi-select
        // set above; fall back to it so a one-unit selection still works.
        if (result.empty() && player->selectedCharacter.isValid())
        {
            Character* c = player->selectedCharacter.getCharacter();
            if (c)
                result.push_back(c);
        }

        std::ostringstream out;
        out << "DefensivePositions: getSelectedCharacters -> " << result.size() << " character(s)";
        DebugLog(out.str());

        return result;
    }

    bool isPlayerControllable(Character* c)
    {
        return c && c->isPlayerCharacter();
    }

    std::string getDisplayName(Character* c)
    {
        if (!c)
            return "";

        return c->getName();
    }

    Vec3 getPosition(Character* c)
    {
        Vec3 result = { 0.f, 0.f, 0.f };
        if (!c)
            return result;

        const Ogre::Vector3 pos = c->getPosition();
        result.x = pos.x;
        result.y = pos.y;
        result.z = pos.z;
        return result;
    }

    float getFacingYaw(Character* c)
    {
        if (!c)
            return 0.f;

        CharMovement* movement = c->getMovement();
        if (!movement)
            return 0.f;

        const Ogre::Vector3& dir = movement->getFacingDirection();
        return std::atan2(dir.x, dir.z);
    }

    void setFacingYaw(Character* c, float yaw)
    {
        if (!c)
            return;

        CharMovement* movement = c->getMovement();
        if (!movement)
            return;

        const Ogre::Vector3 dir(std::sin(yaw), 0.f, std::cos(yaw));
        movement->faceDirection(dir);
    }

    bool isMountedOnTurret(Character* c, ObjectRef& turretOut)
    {
        turretOut = ObjectRef();

        if (!c || !c->isUsingTurret.isValid())
            return false;

        // Prefer the live isUsingTurret hand (includes furniture container
        // fields). Rebuilding from Building::getHandle() alone and forcing
        // type=BUILDING was the resolve=missing remount bug.
        if (!c->isUsingTurret.getBuilding())
            return false;

        turretOut = toObjectRef(c->isUsingTurret);
        return true;
    }

    bool turretExists(const ObjectRef& turret)
    {
        if (turret.isEmpty())
            return false;

        const hand h = fromObjectRef(turret);
        return h.isValid() && h.getBuilding() != nullptr;
    }

    Building* resolveTurret(const ObjectRef& turret)
    {
        if (turret.isEmpty())
            return nullptr;

        const hand h = fromObjectRef(turret);
        if (!h.isValid())
            return nullptr;

        return h.getBuilding();
    }

    bool isTurretRemountBlocked(Character* c, const ObjectRef& turret)
    {
        if (!c)
            return false;

        Building* building = resolveTurret(turret);
        if (!building)
            return false; // gone entirely -> ArrivalMonitor's TurretMissing path, not this one

        UseableStuff* useable = building->getUseableStuff();
        if (!useable)
            return false;

        // couldIOperate() accounts for the turret's operator-slot occupancy
        // (numOperatorsMax / currentOperators) independent of walking
        // distance, so it can flag "occupied by someone else" well before a
        // never-arriving character would otherwise time out.
        return !useable->couldIOperate(c->getHandle());
    }

    bool isNearTurret(Character* c, const ObjectRef& turret, float radius)
    {
        if (!c)
            return false;

        Building* building = resolveTurret(turret);
        if (!building)
            return false;

        return c->getPosition().squaredDistance(building->getPosition()) <= radius * radius;
    }

    std::string diagnoseTurretRemount(Character* c, const ObjectRef& turret)
    {
        std::ostringstream out;
        appendObjectRef(out, turret);

        if (!c)
        {
            out << "; character=null";
            return out.str();
        }

        Building* building = resolveTurret(turret);
        if (!building)
        {
            out << "; resolve=missing";
            const hand h = fromObjectRef(turret);
            out << "; isValid=" << (h.isValid() ? "yes" : "no");
            return out.str();
        }

        out << "; name=\"" << building->getName() << "\"";

        const float dist = std::sqrt(c->getPosition().squaredDistance(building->getPosition()));
        out << "; dist=" << dist << "m";

        ObjectRef mountedRef;
        const bool mounted = isMountedOnTurret(c, mountedRef);
        out << "; mounted=" << (mounted ? "yes" : "no");
        if (mounted)
        {
            out << " on=[";
            appendObjectRef(out, mountedRef);
            out << "]" << (mountedRef == turret ? " (match)" : " (OTHER)");
        }

        UseableStuff* useable = building->getUseableStuff();
        if (!useable)
        {
            out << "; useable=no (Building has no UseableStuff facet)";
            return out.str();
        }

        const int operators = static_cast<int>(useable->currentOperators.size());
        out << "; operators=" << operators << "/" << useable->numOperatorsMax;
        out << "; needsOperating=" << (useable->needsOperating ? "yes" : "no");
        out << "; broken=" << (useable->_isBroken ? "yes" : "no");
        out << "; powerOn=" << (useable->isPowerOn() ? "yes" : "no");
        out << "; hasPower=" << (useable->hasPower() ? "yes" : "no");

        const hand ch = c->getHandle();
        out << "; isFreeSlot=" << (useable->isFreeSlot(ch) ? "yes" : "no");
        out << "; couldIOperate=" << (useable->couldIOperate(ch) ? "yes" : "no");

        if (!useable->currentOperators.empty())
        {
            out << "; operatorHandles=";
            bool first = true;
            for (std::set<hand, std::less<hand>, Ogre::STLAllocator<hand, Ogre::GeneralAllocPolicy> >::const_iterator
                     it = useable->currentOperators.begin();
                 it != useable->currentOperators.end();
                 ++it)
            {
                if (!first)
                    out << ",";
                first = false;
                const uint64_t opId = packHand(*it);
                out << "0x" << std::hex << opId << std::dec;
                if (it->getCharacter() == c)
                    out << "(self)";
                else if (Character* op = it->getCharacter())
                    out << "(\"" << op->getName() << "\")";
                else
                    out << "(unresolved)";
            }
        }

        return out.str();
    }

    UnitResult::Type classifyAvailability(Character* c)
    {
        if (!isPlayerControllable(c))
            return UnitResult::NotControllable;

        if (c->isDead())
            return UnitResult::Dead;

        if (c->isUnconcious())
            return UnitResult::Ko;

        if (c->inSomething == IN_PRISON)
            return UnitResult::Imprisoned;

        // Kidnapped (being carried off) or shackled as someone else's slave:
        // neither Dead/Ko/Imprisoned nor a normal Ok unit can take orders.
        if (c->isKidnapped() || c->isChainedMode())
            return UnitResult::Unavailable;

        return UnitResult::Ok;
    }

    UnitSnap buildUnitSnapshot(Character* c, const ObjectRef& assignedTurret)
    {
        UnitSnap snap = {};
        if (!c)
            return snap;

        snap.characterId = stableCharacterId(c);
        snap.position = getPosition(c);
        snap.facingYaw = getFacingYaw(c);
        snap.controllable = isPlayerControllable(c);
        snap.alive = !c->isDead();
        snap.ko = c->isUnconcious();
        snap.imprisoned = (c->inSomething == IN_PRISON);

        ObjectRef mountedTurret;
        snap.mounted = isMountedOnTurret(c, mountedTurret);
        snap.mountedTurret = mountedTurret;

        snap.turretExists = assignedTurret.isEmpty() ? false : turretExists(assignedTurret);

        return snap;
    }
}
