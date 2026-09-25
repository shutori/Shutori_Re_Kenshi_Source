#pragma once

#include "ArenaRewards.h"

#include <string>

class Character;
class GameData;
class Item;

namespace RewardDelivery
{
    // Resolves compatibility variants of a catalog item. The cloak uses its
    // neck-slot record when Extra Inventory Sections Plugin is active,
    // otherwise its vanilla-compatible belt-slot record.
    GameData* ResolveRewardItemData(ArenaRewards::Piece piece);

    // Creates a reward item at the catalog grade. Weapons use their catalog
    // manufacturer/model pairing; armour uses createItem.
    Item* CreateRewardItem(
        GameData* itemData,
        int qualityLevel,
        const char* weaponModelStringId = 0,
        const char* weaponManufacturerStringId = 0);

    // Resolves claim / half-cost replace / complete for the Rewards UI.
    ArenaRewards::Offer ResolveOffer(
        Character* character,
        ArenaRewards::Piece piece);

    // Claims the next tier, or replaces a lost claimed grade at half cost.
    bool ClaimNext(
        Character* character,
        ArenaRewards::Piece piece,
        std::string& status);

    // Purchases faction-wide access to one armour set. No item is delivered.
    bool PurchaseLicence(
        Character* character,
        ArenaRewards::ArmourSet set,
        std::string& status);

    // Purchases faction-wide access to every weapon reward.
    bool PurchaseWeaponLicence(Character* character, std::string& status);

    // Purchases and delivers one repeatable blueprint book or supply item.
    bool PurchaseGeneral(
        Character* character,
        ArenaRewards::GeneralItem item,
        std::string& status);
}
