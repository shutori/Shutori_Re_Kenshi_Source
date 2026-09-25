// ArenaRewards.h - pure Warlord gear reward catalog and progression rules
#pragma once

namespace ArenaRewards
{
    enum Piece
    {
        PieceGreatsword = 0,
        PieceCloak,
        PiecePlateSkirt,
        PiecePlateBoots,
        PiecePlateArmour,
        PieceHelmet,
        PieceKatana,
        PiecePolearm,
        PiecePitfighterFootwraps,
        PiecePitfighterHarness,
        PiecePitfighterJawGuard,
        PiecePitfighterPants,
        PieceRetainerPants,
        PieceRetainerJacket,
        PieceRetainerBoots,
        PieceRetainerHelmet,
        PieceEnforcerLegplates,
        PieceEnforcerBoots,
        PieceEnforcerChest,
        PieceEnforcerHelmet,
        PieceWarlordLongsword,
        PieceWarlordKatana,
        PieceMace,
        PieceSabre,
        PieceHacker,
        PieceCount
    };

    enum ArmourSet
    {
        ArmourSetT1 = 0,
        ArmourSetT2,
        ArmourSetT3,
        ArmourSetNone
    };

    enum ArmourRow
    {
        ArmourRowPitfighter = 0,
        ArmourRowRetainer,
        ArmourRowEnforcer,
        ArmourRowWarlord,
        ArmourRowCount
    };

    // Armour uses all four tiers. Weapons start at High Grade and reuse the
    // final three enum slots with their Crucible forge-grade names.
    enum Tier
    {
        TierStandard = 0,
        TierHighGrade,
        TierSpecialist,
        TierMasterwork,
        TierMaxCount
    };

    struct Reward
    {
        Piece piece;
        Tier tier;
        const char* pieceName;
        const char* tierName;
        const char* stringId;
        // Weapon mesh/model for this grade; null for armour.
        const char* modelStringId;
        // Weapon manufacturer for this grade; null for armour.
        const char* manufacturerStringId;
        int qualityLevel;
        int cost;
    };

    struct Licence
    {
        ArmourSet set;
        const char* stableId;
        const char* name;
        int cost;
        const char* prerequisiteId;
    };

    enum GeneralItem
    {
        GeneralArenaBuildingsI = 0,
        GeneralArenaBuildingsII,
        GeneralAdvancedFirstAid,
        GeneralSplintKit,
        GeneralSkeletonRepair,
        GeneralPitSurgeonRoll,
        GeneralBoneSetterBraces,
        GeneralIronmenderCase,
        GeneralItemCount
    };

    struct GeneralReward
    {
        GeneralItem item;
        const char* stableId;
        const char* name;
        const char* stringId;
        // Research record wrapped by Kenshi's runtime blueprint item. Null
        // for rewards that are delivered as their catalog item directly.
        const char* blueprintResearchStringId;
        int cost;
        int charges;
        bool repeatable;
        const char* prerequisiteId;
        const char* grantsUnlockId;
    };

    int TiersFor(Piece piece);
    Tier FirstTier(Piece piece);
    bool IsValidTier(Piece piece, Tier tier);
    bool IsWeapon(Piece piece);
    ArmourSet SetFor(Piece piece);
    ArmourRow RowFor(Piece piece);
    ArmourSet SetForRow(ArmourRow row);
    const char* StableId(Piece piece);
    const Reward& Get(Piece piece, Tier tier);
    Tier NextTier(int highestTier, Piece piece);
    bool IsComplete(int highestTier, Piece piece);
    bool CanClaim(int highestTier, Piece piece, Tier tier, int marks);

    const Licence& GetLicence(ArmourSet set);
    const Licence& GetWeaponLicence();
    bool CanPurchaseLicence(
        ArmourSet set,
        bool alreadyUnlocked,
        bool prerequisiteUnlocked,
        int marks);
    bool CanPurchaseWeaponLicence(bool alreadyUnlocked, int marks);
    const GeneralReward& GetGeneral(GeneralItem item);
    bool IsGeneralVisible(GeneralItem item, bool arenaOnePurchased);

    // Stamps the configured prices into this catalogue: every reward tier, every
    // general item, and the licences. Called once by PGConfig::Load, because
    // the rewards screen, the affordability gates, the ledger reservation and the
    // half-price replace all read the stamped field rather than asking for a price.
    void ApplyPriceOverrides();

    // Half the catalog cost, rounded half-up (15 -> 8, 10 -> 5).
    int ReplaceCost(int fullCost);

    // Last claimed tier for a piece, or TierMaxCount if none claimed.
    Tier HighestClaimedTier(int highestTier, Piece piece);

    enum OfferKind
    {
        OfferClaim = 0,
        OfferReplace,
        OfferComplete
    };

    // Pure offer resolution given whether the fighter still holds the
    // previous / any grade of the piece in inventory or equipped.
    struct Offer
    {
        OfferKind kind;
        Tier tier;
        int cost;
    };

    Offer ResolveOffer(
        int highestTier,
        Piece piece,
        bool hasExactPreviousGrade,
        bool hasAnyGradeOfPiece);
}
