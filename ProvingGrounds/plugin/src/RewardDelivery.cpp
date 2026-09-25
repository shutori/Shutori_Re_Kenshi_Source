#include "RewardDelivery.h"

#include "LeaderboardStore.h"
#include "FighterIdentity.h"
#include "RewardTransaction.h"
#include "SquadUtil.h"

#include "PGLog.h"

#include <cstdio>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable: 4091)
#include <kenshi/Character.h>
#include <kenshi/Enums.h>
#include <kenshi/GameData.h>
#include <kenshi/GameWorld.h>
#include <kenshi/Gear.h>
#include <kenshi/Globals.h>
#include <kenshi/Inventory.h>
#include <kenshi/Item.h>
#include <kenshi/ModInfo.h>
#include <kenshi/RootObjectFactory.h>
#include <kenshi/util/hand.h>
#pragma warning(pop)

#ifndef NULL
#define NULL 0
#endif

namespace RewardDelivery
{
    namespace
    {
        // Warlord Forge contains the persisted masterwork weapon models.
        const char* kWarlordManufacturerId = "71-Proving Grounds.mod";
        const char* kNeckCloakId = "72-Proving Grounds.mod";
        const char* kBeltCloakId = "89-Proving Grounds.mod";

        void RewardDiag(const char* message)
        {
            PGLog::Error(message);
            PGLog::Debug(message);
        }

        std::string NormaliseModName(const std::string& value)
        {
            std::string result;
            result.reserve(value.size());
            for (size_t i = 0; i < value.size(); ++i)
            {
                const unsigned char ch =
                    static_cast<unsigned char>(value[i]);
                if (ch >= 'A' && ch <= 'Z')
                    result.push_back(static_cast<char>(ch - 'A' + 'a'));
                else if ((ch >= 'a' && ch <= 'z') ||
                    (ch >= '0' && ch <= '9'))
                    result.push_back(static_cast<char>(ch));
            }
            return result;
        }

        bool HasExtraInventorySections()
        {
            if (!ou)
                return false;

            const lektor<ModInfo*>& mods = ou->activeMods;
            for (unsigned int i = 0; i < mods.size(); ++i)
            {
                const ModInfo* mod = mods[i];
                if (!mod)
                    continue;
                const std::string identity = NormaliseModName(
                    mod->name + " " + mod->file);
                if (identity.find("extrainventorysectionsplugin") !=
                    std::string::npos)
                {
                    return true;
                }
            }
            return false;
        }

        GameData* ResolveAlternateCloakData(GameData* primary)
        {
            if (!ou)
                return NULL;
            GameData* neck = ou->gamedata.getData(kNeckCloakId);
            GameData* belt = ou->gamedata.getData(kBeltCloakId);
            return primary == neck ? belt : neck;
        }

        bool IsPlayerSquadFighter(Character* character)
        {
            std::vector<Character*> squad;
            SquadUtil::CollectPlayerSquad(squad);
            for (size_t i = 0; i < squad.size(); ++i)
            {
                if (squad[i] == character)
                    return true;
            }
            return false;
        }

        GameData* ResolveWeaponManufacturer(const char* manufacturerStringId)
        {
            if (!ou)
                return NULL;
            if (manufacturerStringId)
                return ou->gamedata.getData(manufacturerStringId);
            GameData* manufacturer = ou->gamedata.getData(kWarlordManufacturerId);
            if (manufacturer)
                return manufacturer;
            return ou->gamedata.getDataByName("Warlord Forge", WEAPON_MANUFACTURER);
        }

        void EnsureWeaponListed(GameData* manufacturer, GameData* weaponData)
        {
            if (!manufacturer || !weaponData)
                return;
            if (manufacturer->findInList("weapon types", weaponData->stringID))
                return;
            manufacturer->addToList(
                "weapon types", weaponData->stringID, 100, 0, 0);
            RewardDiag((std::string(
                "Proving Grounds: registered reward weapon under manufacturer: ") +
                weaponData->stringID).c_str());
        }

        GameData* FindWeaponModel(
            GameData* manufacturer,
            int modelValue)
        {
            if (!manufacturer)
                return NULL;
            const Ogre::vector<GameDataReference>::type* models =
                manufacturer->getReferenceListIfExists("weapon models");
            if (!models)
                return NULL;
            for (size_t i = 0; i < models->size(); ++i)
            {
                const GameDataReference& ref = (*models)[i];
                if (ref.values[0] != modelValue)
                    continue;
                if (ref.ptr)
                    return ref.ptr;
                return ou->gamedata.getData(ref.sid);
            }
            return NULL;
        }

        GameData* ResolveItemMaterial(GameData* itemData)
        {
            if (!itemData || !ou)
                return NULL;
            const Ogre::vector<GameDataReference>::type* materials =
                itemData->getReferenceListIfExists("material");
            if (!materials || materials->empty())
                return NULL;
            const GameDataReference& ref = (*materials)[0];
            if (ref.ptr)
                return ref.ptr;
            return ou->gamedata.getData(ref.sid);
        }

        bool LevelsMatch(int have, int want)
        {
            if (have == want)
                return true;
            // Weapon colour-band migration: rewards created before the
            // Grade2/3/4 mapping used 70/75/80. Accept those older Warlord and
            // Frostcursed items as the previous grade during an upgrade.
            if ((have == 70 && want == 50) ||
                (have == 75 && want == 70) ||
                // Arena/Pit Forge now uses its FCS model values: 55/75.
                // Accept the earlier uniform 50/70 progression so an
                // existing reward can still be upgraded or replaced.
                (have == 50 && want == 55) ||
                (have == 50 && want == 75) ||
                (have == 70 && want == 55) ||
                (have == 70 && want == 75))
            {
                return true;
            }
            // Accept pre-fix manufacturer indices (3/4/5) as High/Spec/MW.
            if ((have == 3 && want == 60) || (have == 60 && want == 3))
                return true;
            if ((have == 4 && want == 80) || (have == 80 && want == 4))
                return true;
            if ((have == 5 && want == 95) || (have == 95 && want == 5))
                return true;
            return false;
        }

        Item* FindInInventorySections(
            Inventory* inventory,
            GameData* itemData,
            int qualityLevel,
            bool requireQuality)
        {
            if (!inventory || !itemData)
                return NULL;

            lektor<InventorySection*>& sections = inventory->getAllSections();
            for (unsigned int i = 0; i < sections.size(); ++i)
            {
                InventorySection* section = sections[i];
                if (!section)
                    continue;
                const Ogre::vector<InventorySection::SectionItem>::type& items =
                    section->getItems();
                for (size_t j = 0; j < items.size(); ++j)
                {
                    Item* item = items[j].item;
                    if (!item || item->getGameData() != itemData)
                        continue;
                    if (!requireQuality ||
                        LevelsMatch(item->getLevel(), qualityLevel))
                    {
                        return item;
                    }
                }
            }
            return NULL;
        }

        Item* FindExactGrade(
            Inventory* inventory,
            GameData* itemData,
            int qualityLevel)
        {
            if (!inventory || !itemData)
                return NULL;

            Item* direct = inventory->getItem(itemData);
            if (direct && LevelsMatch(direct->getLevel(), qualityLevel))
                return direct;

            const lektor<Item*>& items = inventory->getAllItems();
            for (unsigned int i = 0; i < items.size(); ++i)
            {
                Item* item = items[i];
                if (!item || item->getGameData() != itemData)
                    continue;
                if (LevelsMatch(item->getLevel(), qualityLevel))
                    return item;
            }

            lektor<Item*> equipped;
            if (itemData->type == ARMOUR)
                inventory->getEquippedArmour(equipped);
            else if (itemData->type == WEAPON)
                inventory->getEquippedWeapons(equipped);

            for (unsigned int i = 0; i < equipped.size(); ++i)
            {
                Item* item = equipped[i];
                if (!item || item->getGameData() != itemData)
                    continue;
                if (LevelsMatch(item->getLevel(), qualityLevel))
                    return item;
            }

            Item* sectionItem = FindInInventorySections(
                inventory, itemData, qualityLevel, true);
            if (sectionItem)
                return sectionItem;

            return NULL;
        }

        bool HasAnyGrade(Inventory* inventory, GameData* itemData)
        {
            if (!inventory || !itemData)
                return false;
            if (inventory->getItem(itemData))
                return true;

            const lektor<Item*>& items = inventory->getAllItems();
            for (unsigned int i = 0; i < items.size(); ++i)
            {
                Item* item = items[i];
                if (item && item->getGameData() == itemData)
                    return true;
            }

            lektor<Item*> equipped;
            if (itemData->type == ARMOUR)
                inventory->getEquippedArmour(equipped);
            else if (itemData->type == WEAPON)
                inventory->getEquippedWeapons(equipped);

            for (unsigned int i = 0; i < equipped.size(); ++i)
            {
                Item* item = equipped[i];
                if (item && item->getGameData() == itemData)
                    return true;
            }
            if (FindInInventorySections(inventory, itemData, 0, false))
                return true;
            return false;
        }

        Item* FindPreviousGrade(
            Inventory* inventory,
            GameData* itemData,
            int qualityLevel)
        {
            // Upgrades require the exact previous grade when present; fallback
            // to any copy only for exchange once an upgrade offer is confirmed.
            Item* exact = FindExactGrade(inventory, itemData, qualityLevel);
            if (exact)
                return exact;
            if (!inventory || !itemData)
                return NULL;

            Item* fallback = NULL;
            Item* direct = inventory->getItem(itemData);
            if (direct)
                fallback = direct;

            const lektor<Item*>& items = inventory->getAllItems();
            for (unsigned int i = 0; i < items.size(); ++i)
            {
                Item* item = items[i];
                if (!item || item->getGameData() != itemData)
                    continue;
                if (!fallback)
                    fallback = item;
            }

            lektor<Item*> equipped;
            if (itemData->type == ARMOUR)
                inventory->getEquippedArmour(equipped);
            else if (itemData->type == WEAPON)
                inventory->getEquippedWeapons(equipped);

            for (unsigned int i = 0; i < equipped.size(); ++i)
            {
                Item* item = equipped[i];
                if (!item || item->getGameData() != itemData)
                    continue;
                if (!fallback)
                    fallback = item;
            }

            if (!fallback)
            {
                fallback = FindInInventorySections(
                    inventory, itemData, qualityLevel, false);
            }

            return fallback;
        }

        Item* FindExactRewardGrade(
            Inventory* inventory,
            ArenaRewards::Piece piece,
            GameData* primary,
            int qualityLevel)
        {
            Item* item = FindExactGrade(inventory, primary, qualityLevel);
            if (item || piece != ArenaRewards::PieceCloak)
                return item;
            return FindExactGrade(
                inventory, ResolveAlternateCloakData(primary), qualityLevel);
        }

        bool HasAnyRewardGrade(
            Inventory* inventory,
            ArenaRewards::Piece piece,
            GameData* primary)
        {
            if (HasAnyGrade(inventory, primary))
                return true;
            return piece == ArenaRewards::PieceCloak && HasAnyGrade(
                inventory, ResolveAlternateCloakData(primary));
        }

        Item* FindPreviousRewardGrade(
            Inventory* inventory,
            ArenaRewards::Piece piece,
            GameData* primary,
            int qualityLevel)
        {
            Item* item = FindPreviousGrade(inventory, primary, qualityLevel);
            if (item || piece != ArenaRewards::PieceCloak)
                return item;
            return FindPreviousGrade(
                inventory, ResolveAlternateCloakData(primary), qualityLevel);
        }

        void LogGearQuality(Item* item, const char* label)
        {
            if (!item)
                return;
            Gear* gear = item->isGear();
            char line[192];
            if (!gear)
            {
                sprintf_s(line,
                    "Proving Grounds: %s getLevel=%d (not gear)",
                    label, item->getLevel());
                RewardDiag(line);
                return;
            }
            sprintf_s(line,
                "Proving Grounds: %s getLevel=%d level_0_100=%d level01=%.3f",
                label,
                item->getLevel(),
                gear->level_0_100,
                gear->getLevel01());
            RewardDiag(line);
        }

        void NormalizeGearQuality(Item* item, int qualityLevel)
        {
            Gear* gear = item ? item->isGear() : NULL;
            if (!gear)
                return;
            // createItem sometimes stores the override in getLevel while leaving
            // level_0_100 near junk. Force the 0-100 quality the UI/stats use.
            if (qualityLevel >= 0)
            {
                gear->level_0_100 = qualityLevel;
                gear->level = static_cast<float>(qualityLevel) / 100.0f;
            }
        }

        void DestroyUndelivered(Item* item, const char* reason)
        {
            if (item && ou)
                ou->destroy(item, false, reason);
        }

        class PurchaseAccounting : public RewardTransaction::Binding,
            public RewardTransaction::ReservedAccounting
        {
            Character* character;
            hand actor;
        public:
            PurchaseAccounting(Character* c)
                : RewardTransaction::ReservedAccounting(static_cast<RewardTransaction::Binding&>(*this)), character(c), actor(c) {}
            bool Matches(const std::string& identity) const
            {
                std::string current;
                return actor.getCharacter() == character &&
                    FighterIdentity::Find(character,current) && current == identity;
            }
            bool ReservePurchase(int cost, const char* reward = NULL, int previous = -1,
                int next = -1, const char* unlock = NULL, bool requireNewUnlock = false)
            {
                ArenaLedger::Purchase purchase(cost);
                if (reward) purchase.reward=reward;
                purchase.previousTier=previous; purchase.nextTier=next;
                if (unlock) purchase.unlock=unlock;
                purchase.requireNewUnlock=requireNewUnlock;
                return LeaderboardStore::ReserveProgression(character,purchase,*this);
            }
        };

        class ItemDelivery : public RewardTransaction::Inventory
        {
            Character* character;
            ::Inventory* inventory;
            Item* replacement;
            Item* previous;
            Item* retained;
        public:
            ItemDelivery(Character* c, ::Inventory* inv, Item* item, Item* old = NULL)
                : character(c), inventory(inv), replacement(item), previous(old), retained(NULL) {}
            bool RemovePrevious()
            {
                if (!previous) return true;
                retained = inventory->removeItemDontDestroy_returnsItem(previous, 1, false);
                return retained != NULL;
            }
            bool DeliverNew() { return character->giveItem(replacement, false, true); }
            bool RemoveNew()
            {
                Item* removed = inventory->removeItemDontDestroy_returnsItem(replacement, 1, false);
                if (!removed) return false;
                replacement = removed;
                return true;
            }
            bool RestorePrevious()
            {
                if (!retained) return true;
                if (!character->giveItem(retained, false, false))
                {
                    RewardDiag("Proving Grounds: inventory rollback could not restore retained previous reward item");
                    return false;
                }
                retained = NULL;
                return true;
            }
            void DestroyNew() { DestroyUndelivered(replacement, "Proving Grounds reward transaction rollback"); replacement = NULL; }
            void DestroyPrevious() { DestroyUndelivered(retained, "Proving Grounds reward upgraded"); retained = NULL; }
        };

        bool DeliverPurchase(ItemDelivery& delivery, PurchaseAccounting& accounting, std::string& status)
        {
            const RewardTransaction::Result result = RewardTransaction::Deliver(delivery, accounting);
            if (result == RewardTransaction::Success) return true;
            if (result == RewardTransaction::AccountingRollbackFailed || result == RewardTransaction::RollbackFailed)
                status = "The purchase failed and accounting could not be restored. Check Arena Marks and equipment before saving.";
            else if (result == RewardTransaction::InventoryRollbackFailed)
                status = "The purchase failed and inventory restoration failed. Check the fighter's equipment before saving.";
            else if (result == RewardTransaction::RemovalFailed)
                status = "The previous grade could not be removed; the purchase was cancelled.";
            else if (result == RewardTransaction::DeliveryFailed)
                status = "The reward would not fit; the purchase was rolled back.";
            else
                status = "Arena progression became unavailable; the purchase was cancelled.";
            RewardDiag(status.c_str());
            return false;
        }

        Item* TryCreate(
            GameData* itemData,
            GameData* companyOrMesh,
            GameData* material,
            int qualityLevel,
            const char* attemptLabel)
        {
            Item* item = ou->theFactory->createItem(
                itemData,
                hand(),
                companyOrMesh,
                material,
                qualityLevel,
                NULL);
            char line[192];
            sprintf_s(line,
                "Proving Grounds: %s -> %s (level now %d)",
                attemptLabel,
                item ? "ok" : "null",
                item ? item->getLevel() : -1);
            RewardDiag(line);
            if (item)
            {
                NormalizeGearQuality(item, qualityLevel);
                LogGearQuality(item, attemptLabel);
            }
            return item;
        }

        Item* CreateBlueprintReward(GameData* researchData)
        {
            if (!researchData || !ou || !ou->theFactory)
                return NULL;

            GameData* blueprintData = ou->gamedata.getData("BLUEPRINT_ITEM");
            if (!blueprintData)
            {
                RewardDiag("Proving Grounds: generic blueprint item record missing");
                return NULL;
            }

            // Kenshi's native tech blueprints serialize with BLUEPRINT_ITEM as
            // their base and the target RESEARCH record as both company and
            // material. Creating that same shape gives the item its blueprint
            // tooltip and engine-owned right-click learning behavior.
            Item* blueprint = ou->theFactory->createItem(
                blueprintData,
                hand(),
                researchData,
                researchData,
                -1,
                NULL);

            char line[256];
            sprintf_s(line,
                "Proving Grounds: blueprint create research=%s -> %s function=%d",
                researchData->stringID.c_str(),
                blueprint ? "ok" : "null",
                blueprint ? static_cast<int>(blueprint->itemFunction) : -1);
            RewardDiag(line);
            return blueprint;
        }

        // createItem returns null for every WEAPON we have tried (including
        // vanilla Plank). Sword's constructor is public and matches the
        // (base, company, material/model, handle, level) layout Item uses.
        Item* TryCreateSword(
            GameData* itemData,
            GameData* manufacturer,
            GameData* model,
            int qualityLevel,
            const char* attemptLabel)
        {
            // Factory createItem returns null for WEAPON; construct directly.
            Sword* sword = new Sword(
                itemData, manufacturer, model, hand(), qualityLevel);
            char line[192];
            sprintf_s(line,
                "Proving Grounds: %s -> %s (level now %d)",
                attemptLabel,
                sword ? "ok" : "null",
                sword ? sword->getLevel() : -1);
            RewardDiag(line);
            if (!sword)
                return NULL;
            NormalizeGearQuality(sword, qualityLevel);
            LogGearQuality(sword, attemptLabel);
            return sword;
        }
    }

    Item* CreateRewardItem(
        GameData* itemData,
        int qualityLevel,
        const char* weaponModelStringId,
        const char* weaponManufacturerStringId)
    {
        if (!itemData || !ou || !ou->theFactory)
            return NULL;

        char line[256];
        sprintf_s(line,
            "Proving Grounds: CreateRewardItem sid=%s type=%d level=%d",
            itemData->stringID.c_str(),
            static_cast<int>(itemData->type),
            qualityLevel);
        RewardDiag(line);

        GameData* material = ResolveItemMaterial(itemData);
        sprintf_s(line,
            "Proving Grounds: material=%s",
            material ? material->stringID.c_str() : "(none)");
        RewardDiag(line);

        if (itemData->type != WEAPON)
        {
            Item* armour = TryCreate(
                itemData, NULL, material, qualityLevel, "armour createItem");
            if (!armour && material)
            {
                armour = TryCreate(
                    itemData, NULL, NULL, qualityLevel, "armour createItem no-mat");
            }
            return armour;
        }

        GameData* manufacturer = ResolveWeaponManufacturer(weaponManufacturerStringId);
        if (!manufacturer)
        {
            RewardDiag("Proving Grounds: reward weapon manufacturer missing");
            return NULL;
        }
        EnsureWeaponListed(manufacturer, itemData);

        GameData* model = weaponModelStringId
            ? ou->gamedata.getData(weaponModelStringId)
            : FindWeaponModel(manufacturer, qualityLevel);
        if (weaponModelStringId && !model)
        {
            sprintf_s(line,
                "Proving Grounds: weapon model record missing: %s",
                weaponModelStringId);
            RewardDiag(line);
        }
        sprintf_s(line,
            "Proving Grounds: weapon manufacturer=%s model=%s",
            manufacturer->stringID.c_str(),
            model ? model->stringID.c_str() : "(none)");
        RewardDiag(line);

        // Prefer Sword ctor — factory createItem is unreliable for WEAPON.
        Item* weapon = TryCreateSword(
            itemData, manufacturer, model, qualityLevel, "Sword(mfr,model)");
        if (weapon)
            return weapon;

        weapon = TryCreateSword(
            itemData, manufacturer, NULL, qualityLevel, "Sword(mfr,null)");
        if (weapon)
            return weapon;

        if (model)
        {
            weapon = TryCreateSword(
                itemData, NULL, model, qualityLevel, "Sword(null,model)");
            if (weapon)
                return weapon;
        }

        // Last-resort factory permutations (kept for diagnostics).
        weapon = TryCreate(
            itemData, manufacturer, model, qualityLevel, "createItem(mfr,model)");
        if (weapon)
            return weapon;
        weapon = TryCreate(
            itemData, model, manufacturer, qualityLevel, "createItem(model,mfr)");
        if (weapon)
            return weapon;
        weapon = TryCreate(
            itemData, manufacturer, NULL, qualityLevel, "createItem(mfr)");
        if (weapon)
            return weapon;
        weapon = TryCreate(
            itemData, NULL, model, qualityLevel, "createItem(null,model)");
        if (!weapon)
            RewardDiag("Proving Grounds: all weapon spawn attempts failed");
        return weapon;
    }

    GameData* ResolveRewardItemData(ArenaRewards::Piece piece)
    {
        if (!ou)
            return NULL;
        if (piece == ArenaRewards::PieceCloak)
        {
            return ou->gamedata.getData(HasExtraInventorySections()
                ? kNeckCloakId
                : kBeltCloakId);
        }
        return ou->gamedata.getData(ArenaRewards::Get(
            piece, ArenaRewards::FirstTier(piece)).stringId);
    }

    ArenaRewards::Offer ResolveOffer(
        Character* character,
        ArenaRewards::Piece piece)
    {
        ArenaRewards::Offer offer;
        offer.kind = ArenaRewards::OfferComplete;
        offer.tier = ArenaRewards::TierMaxCount;
        offer.cost = 0;
        if (!character || !character->isValid() || !ou)
            return offer;

        const int highestTier = LeaderboardStore::GetRewardTier(
            character, ArenaRewards::StableId(piece));
        GameData* itemData = ResolveRewardItemData(piece);
        Inventory* inventory = character->getInventory();

        bool hasExactPrevious = false;
        bool hasAny = false;
        const ArenaRewards::Tier next = ArenaRewards::NextTier(highestTier, piece);
        if (inventory && itemData)
        {
            hasAny = HasAnyRewardGrade(inventory, piece, itemData);
            if (next != ArenaRewards::TierMaxCount &&
                next > ArenaRewards::FirstTier(piece))
            {
                const ArenaRewards::Reward& previousReward = ArenaRewards::Get(
                    piece, static_cast<ArenaRewards::Tier>(next - 1));
                hasExactPrevious = FindExactRewardGrade(
                    inventory, piece, itemData,
                    previousReward.qualityLevel) != NULL;
            }
        }

        return ArenaRewards::ResolveOffer(
            highestTier, piece, hasExactPrevious, hasAny);
    }

    bool ClaimNext(
        Character* character,
        ArenaRewards::Piece piece,
        std::string& status)
    {
        status.clear();
        if (!character || !character->isValid())
        {
            status = "Select a valid squad fighter.";
            return false;
        }
        if (!IsPlayerSquadFighter(character))
        {
            status = "Prisoners retain Marks but cannot claim rewards.";
            return false;
        }
        if (!ou || !ou->theFactory)
        {
            status = "The world item factory is not ready.";
            return false;
        }

        Inventory* inventory = character->getInventory();
        if (!inventory)
        {
            status = "The selected fighter has no accessible inventory.";
            return false;
        }

        const ArenaRewards::ArmourSet armourSet =
            ArenaRewards::SetFor(piece);
        if (ArenaRewards::IsWeapon(piece) &&
            !LeaderboardStore::HasFactionUnlock(
                ArenaRewards::GetWeaponLicence().stableId))
        {
            status = "Unlock the weapons licence before purchasing weapons.";
            return false;
        }
        if (armourSet != ArenaRewards::ArmourSetNone &&
            !LeaderboardStore::HasFactionUnlock(
                ArenaRewards::GetLicence(armourSet).stableId))
        {
            status = "Unlock this armour licence before purchasing pieces.";
            return false;
        }

        const ArenaRewards::Offer offer = ResolveOffer(character, piece);
        if (offer.kind == ArenaRewards::OfferComplete)
        {
            status = "All grades of this reward are already claimed.";
            return false;
        }

        const ArenaRewards::Reward& reward = ArenaRewards::Get(piece, offer.tier);
        const int marks = LeaderboardStore::GetMarks(character);
        if (marks < offer.cost)
        {
            status = "This fighter does not have enough Arena Marks.";
            return false;
        }

        GameData* itemData = ResolveRewardItemData(piece);
        if (!itemData)
        {
            status = "The Warlord reward record could not be found.";
            RewardDiag((std::string("Proving Grounds: missing reward FCS record ") +
                reward.stringId).c_str());
            return false;
        }

        Item* previous = NULL;
        int previousLevel = -1;
        if (offer.kind == ArenaRewards::OfferClaim &&
            offer.tier > ArenaRewards::FirstTier(piece))
        {
            const ArenaRewards::Reward& previousReward = ArenaRewards::Get(
                piece, static_cast<ArenaRewards::Tier>(offer.tier - 1));
            previous = FindExactRewardGrade(
                inventory, piece, itemData, previousReward.qualityLevel);
            if (!previous)
                previous = FindPreviousRewardGrade(
                    inventory, piece, itemData, previousReward.qualityLevel);
            if (!previous)
            {
                status = "Place the previous grade in this fighter's inventory to upgrade it.";
                return false;
            }
            previousLevel = previous->getLevel();
        }
        else if (!inventory->hasRoomForItem(itemData))
        {
            status = "This fighter needs more inventory space for the reward.";
            return false;
        }

        Item* newItem = CreateRewardItem(
            itemData, reward.qualityLevel, reward.modelStringId,
            reward.manufacturerStringId);
        if (!newItem)
        {
            status = "The reward item could not be created.";
            char failDetail[160];
            sprintf_s(failDetail,
                "Proving Grounds: createItem failed for %s level %d",
                itemData->stringID.c_str(), reward.qualityLevel);
            RewardDiag(failDetail);
            return false;
        }

        char gradeLine[192];
        sprintf_s(gradeLine,
            "Proving Grounds: %s prev=%d -> created=%d (want %d) for %s cost=%d",
            offer.kind == ArenaRewards::OfferReplace ? "replace" : "claim",
            previousLevel,
            newItem->getLevel(),
            reward.qualityLevel,
            itemData->stringID.c_str(),
            offer.cost);
        RewardDiag(gradeLine);

        if (offer.kind == ArenaRewards::OfferReplace)
        {
            PurchaseAccounting accounting(character);
            if (!accounting.ReservePurchase(offer.cost))
            {
                DestroyUndelivered(newItem, "Proving Grounds replace payment failed");
                status = "The replacement purchase could not be reserved.";
                return false;
            }
            ItemDelivery delivery(character, inventory, newItem);
            if (!DeliverPurchase(delivery, accounting, status)) return false;
            status = std::string(reward.pieceName) + " - " + reward.tierName +
                " replaced for " + character->getName() + ".";
            RewardDiag((std::string("Proving Grounds: reward replaced: ") +
                itemData->stringID + " / " + reward.tierName).c_str());
            return true;
        }

        const int previousTier = LeaderboardStore::GetRewardTier(
            character, ArenaRewards::StableId(piece));
        PurchaseAccounting accounting(character);
        if (!accounting.ReservePurchase(offer.cost, ArenaRewards::StableId(piece),
                previousTier, static_cast<int>(offer.tier)))
        {
            DestroyUndelivered(newItem, "Proving Grounds reward reservation failed");
            status = "The reward purchase could not be reserved.";
            return false;
        }

        ItemDelivery delivery(character, inventory, newItem, previous);
        if (!DeliverPurchase(delivery, accounting, status)) return false;

        status = std::string(reward.pieceName) + " - " + reward.tierName +
            " claimed for " + character->getName() + ".";
        RewardDiag((std::string("Proving Grounds: reward delivered: ") +
            itemData->stringID + " / " + reward.tierName).c_str());
        return true;
    }

    bool PurchaseLicence(
        Character* character,
        ArenaRewards::ArmourSet set,
        std::string& status)
    {
        status.clear();
        if (!character || !character->isValid() ||
            !IsPlayerSquadFighter(character))
        {
            status = "Select an eligible squad fighter.";
            return false;
        }

        const ArenaRewards::Licence& licence = ArenaRewards::GetLicence(set);
        const bool owned = LeaderboardStore::HasFactionUnlock(
            licence.stableId);
        const bool prerequisite = !licence.prerequisiteId ||
            LeaderboardStore::HasFactionUnlock(licence.prerequisiteId);
        const int marks = LeaderboardStore::GetMarks(character);
        if (owned)
        {
            status = "This armour licence is already unlocked.";
            return false;
        }
        if (!prerequisite)
        {
            status = "The previous armour licence is required.";
            return false;
        }
        if (!ArenaRewards::CanPurchaseLicence(
                set, owned, prerequisite, marks))
        {
            status = "This fighter does not have enough Arena Marks.";
            return false;
        }
        PurchaseAccounting accounting(character);
        if (!accounting.ReservePurchase(licence.cost,NULL,-1,-1,licence.stableId,true))
        {
            status = "The armour licence purchase could not be reserved.";
            return false;
        }
        if (!accounting.Complete() || !accounting.Accept())
        {
            status = accounting.Rollback()
                ? "Arena progression became unavailable; the armour licence purchase was cancelled."
                : "The armour licence purchase failed and accounting could not be restored.";
            return false;
        }

        status = std::string(licence.name) +
            " licence unlocked for the faction.";
        return true;
    }

    bool PurchaseWeaponLicence(Character* character, std::string& status)
    {
        status.clear();
        if (!character || !character->isValid() ||
            !IsPlayerSquadFighter(character))
        {
            status = "Select an eligible squad fighter.";
            return false;
        }

        const ArenaRewards::Licence& licence =
            ArenaRewards::GetWeaponLicence();
        const bool owned = LeaderboardStore::HasFactionUnlock(
            licence.stableId);
        const int marks = LeaderboardStore::GetMarks(character);
        if (owned)
        {
            status = "The weapons licence is already unlocked.";
            return false;
        }
        if (!ArenaRewards::CanPurchaseWeaponLicence(owned, marks))
        {
            status = "This fighter does not have enough Arena Marks.";
            return false;
        }
        PurchaseAccounting accounting(character);
        if (!accounting.ReservePurchase(
                licence.cost, NULL, -1, -1, licence.stableId, true))
        {
            status = "The weapons licence purchase could not be reserved.";
            return false;
        }
        if (!accounting.Complete() || !accounting.Accept())
        {
            status = accounting.Rollback()
                ? "Arena progression became unavailable; the weapons licence purchase was cancelled."
                : "The weapons licence purchase failed and accounting could not be restored.";
            return false;
        }

        status = "Weapons licence unlocked for the faction.";
        return true;
    }

    bool PurchaseGeneral(
        Character* character,
        ArenaRewards::GeneralItem item,
        std::string& status)
    {
        status.clear();
        if (!character || !character->isValid() ||
            !IsPlayerSquadFighter(character))
        {
            status = "Select an eligible squad fighter.";
            return false;
        }
        if (!ou || !ou->theFactory)
        {
            status = "The world item factory is not ready.";
            return false;
        }

        const ArenaRewards::GeneralReward& reward =
            ArenaRewards::GetGeneral(item);
        if (reward.prerequisiteId &&
            !LeaderboardStore::HasFactionUnlock(reward.prerequisiteId))
        {
            status = "Purchase Arena Buildings I first.";
            return false;
        }
        if (LeaderboardStore::GetMarks(character) < reward.cost)
        {
            status = "This fighter does not have enough Arena Marks.";
            return false;
        }

        Inventory* inventory = character->getInventory();
        GameData* itemData = ou->gamedata.getData(reward.stringId);
        GameData* researchData = reward.blueprintResearchStringId
            ? ou->gamedata.getData(reward.blueprintResearchStringId)
            : NULL;
        GameData* deliveryData = researchData
            ? ou->gamedata.getData("BLUEPRINT_ITEM")
            : itemData;
        if (!inventory || !itemData ||
            (reward.blueprintResearchStringId && (!researchData || !deliveryData)))
        {
            if (!inventory)
                status = "The selected fighter has no accessible inventory.";
            else if (!itemData)
                status = "The reward item record could not be found.";
            else
                status = "The blueprint research record could not be found.";
            return false;
        }
        if (!inventory->hasRoomForItem(deliveryData))
        {
            status = "This fighter needs more inventory space for the reward.";
            return false;
        }

        Item* newItem = researchData
            ? CreateBlueprintReward(researchData)
            : CreateRewardItem(itemData, -1);
        if (!newItem)
        {
            status = "The reward item could not be created.";
            return false;
        }
        if (reward.charges > 0)
            newItem->chargesLeft = static_cast<float>(reward.charges);
        PurchaseAccounting accounting(character);
        if (!accounting.ReservePurchase(reward.cost,NULL,-1,-1,reward.grantsUnlockId))
        {
            DestroyUndelivered(newItem,
                "Proving Grounds general reward payment failed");
            status = "The reward purchase could not be reserved.";
            return false;
        }

        ItemDelivery delivery(character, inventory, newItem);
        if (!DeliverPurchase(delivery, accounting, status)) return false;

        status = std::string(reward.name) + " purchased for " +
            character->getName() + ".";
        return true;
    }
}
