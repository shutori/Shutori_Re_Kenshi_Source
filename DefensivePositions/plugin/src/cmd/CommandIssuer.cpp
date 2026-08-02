#include "cmd/CommandIssuer.h"

#include "game/GameAdapters.h"

#include <Debug.h>

#include <kenshi/Character.h>
#include <kenshi/Building/Building.h>
#include <kenshi/Building/UseableStuff.h>
#include <kenshi/Enums.h>

#include <ogre/OgreVector3.h>

#include <sstream>

namespace
{
    Ogre::Vector3 toOgre(const Vec3& v)
    {
        return Ogre::Vector3(v.x, v.y, v.z);
    }

    // Wall-mounted turrets are furniture: pathing dest is the wall/building
    // they hang on, while the turret itself stays the order subject.
    Building* orderDestinationForTurret(Building* turret)
    {
        if (!turret)
            return nullptr;

        if (turret->isFurniture())
        {
            Building* wall = turret->getMountedBuilding().getBuilding();
            if (wall)
                return wall;
        }

        return turret;
    }

    // The Orders panel "JOBS" tick is wired to OrdersChaseButton / standing
    // order M_SET_ORDER_CHASE. When it stays on, characters resume their
    // permajob list as soon as a player order finishes — so Defense Return
    // must turn it off or they walk away from formation again.
    void disableJobsStandingOrder(Character* c)
    {
        if (!c)
            return;

        if (!c->getStandingOrder(MessageForB::M_SET_ORDER_CHASE))
            return;

        c->setStandingOrder(MessageForB::M_SET_ORDER_CHASE, false);
        DebugLog(std::string("DefensivePositions: Jobs disabled for \"")
                 + GameAdapters::getDisplayName(c) + "\"");
    }
}

namespace CommandIssuer
{
    bool issueMoveTo(Character* c, const Vec3& pos)
    {
        if (!c)
        {
            DebugLog("DefensivePositions: issueMoveTo - null character");
            return false;
        }

        if (!c->canTakePlayerOrdersAtThisTime())
        {
            DebugLog("DefensivePositions: issueMoveTo - character cannot take orders right now");
            return false;
        }

        disableJobsStandingOrder(c);

        // No target building/object: this is a plain "walk here" order.
        c->playerMoveOrderDefault(nullptr, nullptr, toOgre(pos));

        std::ostringstream out;
        out << "DefensivePositions: issueMoveTo -> (" << pos.x << ", " << pos.y << ", " << pos.z << ")";
        DebugLog(out.str());
        return true;
    }

    bool issueMountTurret(Character* c, const ObjectRef& turret)
    {
        if (!c)
        {
            DebugLog("DefensivePositions: issueMountTurret - null character");
            return false;
        }

        if (!c->canTakePlayerOrdersAtThisTime())
        {
            DebugLog("DefensivePositions: issueMountTurret - character cannot take orders right now");
            return false;
        }

        Building* building = GameAdapters::resolveTurret(turret);
        if (!building)
        {
            DebugLog(std::string("DefensivePositions: issueMountTurret - turret no longer exists; ")
                     + GameAdapters::diagnoseTurretRemount(c, turret));
            return false;
        }

        disableJobsStandingOrder(c);

        Building* dest = orderDestinationForTurret(building);
        const TaskType task = building->getDefaultTask();

        // Explicit man-turret order (not playerMoveOrderDefault): the default
        // click path was walking characters to the turret position without
        // applying MAN_A_TURRET / MAN_A_TURRET_ON_BUILDING for furniture.
        // clear=true replaces the current order queue (immediate Return).
        c->addOrder(dest, task, building, false, true, building->getPosition());

        std::ostringstream out;
        out << "DefensivePositions: issueMountTurret task=" << static_cast<int>(task)
            << " furniture=" << (building->isFurniture() ? "yes" : "no")
            << " dest=\"" << (dest ? dest->getName() : std::string("?")) << "\""
            << " subject=\"" << building->getName() << "\"; "
            << GameAdapters::diagnoseTurretRemount(c, turret);
        DebugLog(out.str());
        return true;
    }

    bool tryOperateTurret(Character* c, const ObjectRef& turret)
    {
        if (!c)
            return false;

        Building* building = GameAdapters::resolveTurret(turret);
        if (!building)
            return false;

        UseableStuff* useable = building->getUseableStuff();
        if (!useable)
            return false;

        const bool ok = useable->tryOperate(c->getHandle());
        if (ok)
        {
            DebugLog(std::string("DefensivePositions: tryOperateTurret ok for \"")
                     + GameAdapters::getDisplayName(c) + "\"");
        }
        return ok;
    }

    bool applyFacing(Character* c, float yaw)
    {
        if (!c)
        {
            DebugLog("DefensivePositions: applyFacing - null character");
            return false;
        }

        GameAdapters::setFacingYaw(c, yaw);

        std::ostringstream out;
        out << "DefensivePositions: applyFacing -> yaw " << yaw;
        DebugLog(out.str());
        return true;
    }
}
