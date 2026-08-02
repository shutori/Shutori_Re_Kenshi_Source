#include "PrisonerUtilInternal.h"

#pragma warning(push)
#pragma warning(disable: 4091)
#include <kenshi/Enums.h>
#include <kenshi/Faction.h>
#include <kenshi/GameWorld.h>
#include <kenshi/Globals.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/RootObjectBase.h>
#include <kenshi/Platoon.h>
#include <kenshi/util/hand.h>
#include <kenshi/util/lektor.h>
#pragma warning(pop)

#ifndef NULL
#define NULL 0
#endif

namespace PrisonerUtilInternal
{
    namespace
    {
        void CollectPlayerOwnerships(std::vector<Ownerships*>& out)
        {
            out.clear();
            if (!ou || !ou->player)
                return;

            Faction* faction = ou->player->getFaction();
            if (!faction)
                return;

            const lektor<Platoon*>* squads = faction->getAllActiveSquads();
            if (!squads || !squads->valid())
                return;

            for (uint32_t i = 0; i < squads->size(); ++i)
            {
                Platoon* platoon = (*squads)[i];
                if (!platoon)
                    continue;

                Ownerships* ownerships = platoon->getOwnerships();
                if (!ownerships)
                    continue;

                bool already = false;
                for (size_t o = 0; o < out.size(); ++o)
                {
                    if (out[o] == ownerships)
                    {
                        already = true;
                        break;
                    }
                }
                if (!already)
                    out.push_back(ownerships);
            }
        }

        bool ContainsBuilding(const std::vector<Building*>& list, Building* building)
        {
            for (size_t i = 0; i < list.size(); ++i)
            {
                if (list[i] == building)
                    return true;
            }
            return false;
        }
    }

    void CollectPlayerOwnedCageBuildings(std::vector<Building*>& out)
    {
        out.clear();

        std::vector<Ownerships*> ownershipsList;
        CollectPlayerOwnerships(ownershipsList);

        lektor<Building*> found;
        for (size_t o = 0; o < ownershipsList.size(); ++o)
        {
            Ownerships* ownerships = ownershipsList[o];
            if (!ownerships)
                continue;

            found.clear();
            ownerships->getHomeFurnitureOfType(found, BF_CAGE);
            if (found.size() == 0)
                ownerships->getBuildingsWithFunction(found, BF_CAGE);

            for (uint32_t i = 0; i < found.size(); ++i)
            {
                Building* building = found[i];
                if (!building || ContainsBuilding(out, building))
                    continue;
                out.push_back(building);
            }
        }
    }

    bool IsBuildingOwnedByPlayer(Building* building)
    {
        if (!building)
            return false;

        std::vector<Ownerships*> ownershipsList;
        CollectPlayerOwnerships(ownershipsList);

        RootObjectBase* base = reinterpret_cast<RootObjectBase*>(building);
        const hand& buildingHand = base->getHandle();
        for (size_t o = 0; o < ownershipsList.size(); ++o)
        {
            if (ownershipsList[o] && ownershipsList[o]->isOwned(buildingHand))
                return true;
        }
        return false;
    }
}
