#include "ArenaMedical.h"

#include "MedicSupplyPolicy.h"
#include "SparSession.h"
#include "TownAftercare.h"
#include "TownArenaRuntimePolicy.h"

#include "PGLog.h"
#include <core/Functions.h>

#include <cstdio>

#pragma warning(push)
#pragma warning(disable: 4005 4091)
#include <kenshi/Character.h>
#include <kenshi/Enums.h>
#include <kenshi/GameData.h>
#include <kenshi/GameWorld.h>
#include <kenshi/Globals.h>
#include <kenshi/Inventory.h>
#include <kenshi/Item.h>
#include <kenshi/MedicalSystem.h>
#include <kenshi/RootObjectFactory.h>
#include <kenshi/util/hand.h>
#include <ogre/OgreVector3.h>
#pragma warning(pop)

#ifndef NULL
#define NULL 0
#endif

namespace
{
    ArenaMedical::Protocol g_protocol = ArenaMedical::RingsideAid;

    bool (*applyFirstAid_orig)(MedicalSystem*, float, Item*, float, Character*) = NULL;
    bool (*applyDoctoring_orig)(MedicalSystem*, float, Item*, float, Character*) = NULL;
    bool (*applyRigging_orig)(MedicalSystem*, float, Item*, float) = NULL;

    ItemFunction FunctionFor(MedicSupplyPolicy::Supply supply)
    {
        return supply == MedicSupplyPolicy::RobotRepair ? ITEM_ROBOTREPAIR : ITEM_FIRSTAID;
    }

    struct MedicSupplyAccess
    {
        Character* medic;
        Item* usedKit;

        MedicSupplyAccess(Character* who, Item* equipment) : medic(who), usedKit(equipment) {}

        bool IsScratchMedic() const
        {
            GameData* data = medic && medic->isValid() ? medic->getGameData() : NULL;
            return data && TownArenaRuntimePolicy::IsMedic(data->stringID.c_str());
        }

        bool InventoryHasUsableKit(Inventory* inventory,
            MedicSupplyPolicy::Supply supply) const
        {
            if (!inventory)
                return false;
            lektor<Item*> kits;
            inventory->getAllItemsWithFunction(kits, FunctionFor(supply));
            for (uint32_t i = 0; i < kits.size(); ++i)
                if (kits[i] && kits[i]->chargesLeft > 0.0f)
                    return true;
            return false;
        }

        bool MainInventoryHasUsableKit(MedicSupplyPolicy::Supply supply) const
        {
            Inventory* inventory = medic && medic->isValid() ? medic->getInventory() : NULL;
            return InventoryHasUsableKit(inventory, supply);
        }

        bool BackpackHasUsableKit(MedicSupplyPolicy::Supply supply) const
        {
            ContainerItem* backpack = medic && medic->isValid() ?
                medic->hasABackpackOn() : NULL;
            return InventoryHasUsableKit(backpack ? backpack->getInventory() : NULL, supply);
        }

        bool GiveReplacement(const char* id)
        {
            if (!medic || !medic->isValid() || !ou || !ou->theFactory)
                return false;
            GameData* itemData = ou->gamedata.getData(id);
            if (!itemData)
                return false;
            Item* replacement = ou->theFactory->createItem(
                itemData, hand(), NULL, NULL, -1, NULL);
            return replacement && medic->giveItem(replacement, false, true);
        }

        void RechargeUsedKit()
        {
            if (usedKit)
                usedKit->resetCharges(false);
        }
    };

    void RestockMedicIfDepleted(Character* medic, Item* equipment, float chargesBefore)
    {
        if (!equipment || (equipment->itemFunction != ITEM_FIRSTAID &&
                equipment->itemFunction != ITEM_ROBOTREPAIR))
            return;
        const MedicSupplyPolicy::Supply supply = equipment->itemFunction == ITEM_ROBOTREPAIR ?
            MedicSupplyPolicy::RobotRepair : MedicSupplyPolicy::FirstAid;
        MedicSupplyAccess access(medic, equipment);
        const MedicSupplyPolicy::RestockResult result = MedicSupplyPolicy::RestockAfterUse(
            chargesBefore, equipment->chargesLeft, supply, access);
        if (result == MedicSupplyPolicy::NoRestock)
            return;
        char line[256];
        sprintf_s(line, "Proving Grounds: Scratch medic supply restored actor=%s type=%s method=%s",
            medic && medic->isValid() ? medic->getName().c_str() : "invalid",
            supply == MedicSupplyPolicy::RobotRepair ? "skeleton repair" : "advanced first aid",
            result == MedicSupplyPolicy::ReplacementSpawned ? "replacement item" : "recharged spent kit fallback");
        PGLog::Debug(line);
        if (result == MedicSupplyPolicy::UsedKitRecharged)
            PGLog::Error(line);
    }

    bool BlockFor(MedicalSystem* medical)
    {
        Character* target = medical ? medical->me : NULL;
        return ArenaMedical::ShouldBlockTreatment(
            g_protocol,
            SparSession::IsActive(),
            SparSession::IsParticipant(target),
            SparSession::IsEliminated(target));
    }

    bool applyFirstAid_hook(
        MedicalSystem* thisptr,
        float skill,
        Item* equipment,
        float frameTime,
        Character* who)
    {
        const bool blocked = BlockFor(thisptr);
        const float chargesBefore = equipment ? equipment->chargesLeft : 0.0f;
        const bool complete = !blocked && applyFirstAid_orig(thisptr, skill, equipment, frameTime, who);
        if (!blocked)
            RestockMedicIfDepleted(who, equipment, chargesBefore);
        TownAftercare::RecordTreatment(who, thisptr ? thisptr->me : NULL, false, blocked, complete, frameTime);
        return complete;
    }

    bool applyDoctoring_hook(
        MedicalSystem* thisptr,
        float skill,
        Item* equipment,
        float frameTime,
        Character* who)
    {
        const bool blocked = BlockFor(thisptr);
        const float chargesBefore = equipment ? equipment->chargesLeft : 0.0f;
        const bool complete = !blocked && applyDoctoring_orig(thisptr, skill, equipment, frameTime, who);
        if (!blocked)
            RestockMedicIfDepleted(who, equipment, chargesBefore);
        TownAftercare::RecordTreatment(who, thisptr ? thisptr->me : NULL, true, blocked, complete, frameTime);
        return complete;
    }

    bool applyRigging_hook(
        MedicalSystem* thisptr,
        float skill,
        Item* equipment,
        float frameTime)
    {
        if (BlockFor(thisptr))
            return false;
        return applyRigging_orig(thisptr, skill, equipment, frameTime);
    }
}

namespace ArenaMedical
{
    void SetProtocol(Protocol protocol)
    {
        if (protocol != RingsideAid && protocol != Bloodsport)
            protocol = RingsideAid;
        if (!SparSession::IsActive())
            g_protocol = protocol;
    }

    Protocol GetProtocol()
    {
        return g_protocol;
    }

    float ActionableAidNeed(Character* target, bool robotAid)
    {
        if (!target || !target->isValid() || target->isDead())
            return 0.0f;
        MedicalSystem* medical = target->getMedical();
        if (!medical)
            return 0.0f;
        // A non-zero cached score with no wound records and no bleeding cannot
        // drive applyFirstAid/applyDoctoring. Treating it would leave medics
        // retrying an impossible order until the five-minute arena timeout.
        if (medical->wounds.size() == 0 && medical->currentBleedRate <= 0.0001f)
            return 0.0f;
        const float need = medical->scoreFirstAidNeed(robotAid);
        return need > 0.0f ? need : 0.0f;
    }

    bool NeedsStabilization(Character* target)
    {
        return ActionableAidNeed(target, false) > 0.01f ||
            ActionableAidNeed(target, true) > 0.01f;
    }

    void IssueStabilizeOrder(Character* medic, Character* target)
    {
        if (!medic || !medic->isValid() || medic->isDead() ||
            !target || !target->isValid() || target->isDead())
        {
            return;
        }

        MedicalSystem* medical = target->getMedical();
        const bool robotAid = medical &&
            medical->scoreFirstAidNeed(true) > medical->scoreFirstAidNeed(false);
        medic->addOrder(
            NULL,
            robotAid ? FIRST_AID_ROBOT : FIRST_AID_ORDER,
            target,
            false,
            true,
            target->getPosition());
    }

    bool InstallHooks()
    {
        bool ok = true;
        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(&MedicalSystem::applyFirstAid),
                applyFirstAid_hook,
                &applyFirstAid_orig))
        {
            PGLog::Error("Proving Grounds: failed to hook MedicalSystem::applyFirstAid");
            ok = false;
        }
        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(&MedicalSystem::applyDoctoring),
                applyDoctoring_hook,
                &applyDoctoring_orig))
        {
            PGLog::Error("Proving Grounds: failed to hook MedicalSystem::applyDoctoring");
            ok = false;
        }
        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(&MedicalSystem::applyRigging),
                applyRigging_hook,
                &applyRigging_orig))
        {
            PGLog::Error("Proving Grounds: failed to hook MedicalSystem::applyRigging");
            ok = false;
        }
        return ok;
    }
}
