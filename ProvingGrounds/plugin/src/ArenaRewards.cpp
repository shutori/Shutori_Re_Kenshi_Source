#include "ArenaRewards.h"
#include "PGConfig.h"

namespace ArenaRewards
{
    namespace
    {
        // Kenshi serializes a weapon's model through the manufacturer table
        // using its level_0_100 value. These values must therefore match the
        // FCS "weapon models" records exactly as well as setting its balance:
        // Arena Forge models are 55, Pit Forge models are 75, and Warlord
        // Forge models are 95.
        // Armour Standard/High/Specialist/Masterwork is 40/60/80/95.
        const int kTierForPiece[PieceCount] =
        {
            3, // Greatsword (Crucible Forge models)
            4, // Cloak
            4, // Plateskirt
            4, // Plate Boots
            4, // Plate Armour
            4, // Helmet
            3, // Nodachi (Crucible Forge models)
            3, // Polearm (Crucible Forge models)
            4, // Pitfighter Footwraps
            4, // Pitfighter Harness
            4, // Pitfighter Jaw Guard
            4, // Pitfighter Pants
            4, // Retainer Leather Pants
            4, // Retainer Leather Jacket
            4, // Retainer Boots
            4, // Retainer Leather Helmet
            4, // Enforcer Legplates
            4, // Enforcer Plated Boots
            4, // Plate Enforcer Chest
            4, // Enforcer Helmet
            3, // Warlord Longsword
            3, // Warlord Katana
            3, // Mace
            3, // Sabre
            3  // Hacker
        };

        const Tier kFirstTierForPiece[PieceCount] =
        {
            TierHighGrade, // Greatsword
            TierStandard,  // Cloak
            TierStandard,  // Plateskirt
            TierStandard,  // Plate Boots
            TierStandard,  // Plate Armour
            TierStandard,  // Helmet
            TierHighGrade, // Nodachi
            TierHighGrade, // Polearm
            TierStandard,  // Pitfighter Footwraps
            TierStandard,  // Pitfighter Harness
            TierStandard,  // Pitfighter Jaw Guard
            TierStandard,  // Pitfighter Pants
            TierStandard,  // Retainer Leather Pants
            TierStandard,  // Retainer Leather Jacket
            TierStandard,  // Retainer Boots
            TierStandard,  // Retainer Leather Helmet
            TierStandard,  // Enforcer Legplates
            TierStandard,  // Enforcer Plated Boots
            TierStandard,  // Plate Enforcer Chest
            TierStandard,  // Enforcer Helmet
            TierHighGrade, // Warlord Longsword
            TierHighGrade, // Warlord Katana
            TierHighGrade, // Mace
            TierHighGrade, // Sabre
            TierHighGrade  // Hacker
        };

        const char* kStableIds[PieceCount] =
        {
            "weapon.greatsword",
            "armour.warlord.cloak",
            "armour.warlord.skirt",
            "armour.warlord.boots",
            "armour.warlord.chest",
            "armour.warlord.helmet",
            "weapon.katana",
            "weapon.polearm",
            "armour.pitfighter.footwraps",
            "armour.pitfighter.harness",
            "armour.pitfighter.jaw_guard",
            "armour.pitfighter.pants",
            "armour.retainer.pants",
            "armour.retainer.jacket",
            "armour.retainer.boots",
            "armour.retainer.helmet",
            "armour.enforcer.legplates",
            "armour.enforcer.boots",
            "armour.enforcer.chest",
            "armour.enforcer.helmet",
            "weapon.warlord_longsword",
            "weapon.warlord_katana",
            "weapon.mace",
            "weapon.sabre",
            "weapon.hacker"
        };

        // Prices are stamped from PGConfig at load, so these tables are the
        // catalogue's shipped defaults rather than its final word.
        Licence kLicences[3] =
        {
            { ArmourSetT1, "armour.retainer", "RETAINER LEATHER", 50, 0 },
            { ArmourSetT2, "armour.enforcer", "PLATE ENFORCER", 100, "armour.retainer" },
            { ArmourSetT3, "armour.warlord", "WARLORD", 150, "armour.enforcer" }
        };

        GeneralReward kGeneralRewards[GeneralItemCount] =
        {
            { GeneralArenaBuildingsI, "blueprint.arena1", "Arena Buildings I", "280-Proving Grounds.mod", "50-Proving Grounds.mod", 25, 0, true, 0, "blueprint.arena1" },
            { GeneralArenaBuildingsII, "blueprint.arena2", "Arena Buildings II", "281-Proving Grounds.mod", "52-Proving Grounds.mod", 75, 0, true, "blueprint.arena1", 0 },
            { GeneralAdvancedFirstAid, "supply.advanced_first_aid", "Advanced First Aid Kit", "1359-gamedata.base", 0, 5, 0, true, 0, 0 },
            { GeneralSplintKit, "supply.splint", "Splint Kit", "1435-gamedata.base", 0, 4, 0, true, 0, 0 },
            { GeneralSkeletonRepair, "supply.skeleton_repair", "Skeleton Repair Kit", "18020-gamedata.base", 0, 10, 0, true, 0, 0 },
            { GeneralPitSurgeonRoll, "supply.pit_surgeon_roll", "Pit Surgeon Roll", "291-Proving Grounds.mod", 0, 7, 1200, true, 0, 0 },
            { GeneralBoneSetterBraces, "supply.bone_setter_braces", "Bone-Setter Braces", "293-Proving Grounds.mod", 0, 6, 125, true, 0, 0 },
            { GeneralIronmenderCase, "supply.ironmender_case", "Ironmender Case", "295-Proving Grounds.mod", 0, 15, 600, true, 0, 0 }
        };

        Licence kWeaponLicence =
            { ArmourSetNone, "weapon.all", "WEAPONS", 50, 0 };

        Reward kRewards[PieceCount][TierMaxCount] =
        {
            {
                { PieceGreatsword, TierStandard, "", "", "", 0, 0, 0, 0 },
                { PieceGreatsword, TierHighGrade, "Warlord Greatsword", "Pit Reaver", "70-Proving Grounds.mod", "76-Proving Grounds.mod", "149-Proving Grounds.mod", 55, 100 },
                { PieceGreatsword, TierSpecialist, "Warlord Greatsword", "Arena Reaver", "70-Proving Grounds.mod", "77-Proving Grounds.mod", "149-Proving Grounds.mod", 75, 150 },
                { PieceGreatsword, TierMasterwork, "Warlord Greatsword", "Warlord's Judgement", "70-Proving Grounds.mod", "78-Proving Grounds.mod", "71-Proving Grounds.mod", 95, 200 }
            },
            {
                { PieceCloak, TierStandard, "Warlord Cloak", "Standard", "72-Proving Grounds.mod", 0, 0, 40, 50 },
                { PieceCloak, TierHighGrade, "Warlord Cloak", "High Grade", "72-Proving Grounds.mod", 0, 0, 60, 75 },
                { PieceCloak, TierSpecialist, "Warlord Cloak", "Specialist", "72-Proving Grounds.mod", 0, 0, 80, 100 },
                { PieceCloak, TierMasterwork, "Warlord Cloak", "Masterwork", "72-Proving Grounds.mod", 0, 0, 95, 150 }
            },
            {
                { PiecePlateSkirt, TierStandard, "Warlord Plateskirt", "Standard", "67-Proving Grounds.mod", 0, 0, 40, 50 },
                { PiecePlateSkirt, TierHighGrade, "Warlord Plateskirt", "High Grade", "67-Proving Grounds.mod", 0, 0, 60, 75 },
                { PiecePlateSkirt, TierSpecialist, "Warlord Plateskirt", "Specialist", "67-Proving Grounds.mod", 0, 0, 80, 100 },
                { PiecePlateSkirt, TierMasterwork, "Warlord Plateskirt", "Masterwork", "67-Proving Grounds.mod", 0, 0, 95, 150 }
            },
            {
                { PiecePlateBoots, TierStandard, "Warlord Plate Boots", "Standard", "68-Proving Grounds.mod", 0, 0, 40, 50 },
                { PiecePlateBoots, TierHighGrade, "Warlord Plate Boots", "High Grade", "68-Proving Grounds.mod", 0, 0, 60, 75 },
                { PiecePlateBoots, TierSpecialist, "Warlord Plate Boots", "Specialist", "68-Proving Grounds.mod", 0, 0, 80, 100 },
                { PiecePlateBoots, TierMasterwork, "Warlord Plate Boots", "Masterwork", "68-Proving Grounds.mod", 0, 0, 95, 150 }
            },
            {
                { PiecePlateArmour, TierStandard, "Warlord Plate Armour", "Standard", "61-Proving Grounds.mod", 0, 0, 40, 60 },
                { PiecePlateArmour, TierHighGrade, "Warlord Plate Armour", "High Grade", "61-Proving Grounds.mod", 0, 0, 60, 80 },
                { PiecePlateArmour, TierSpecialist, "Warlord Plate Armour", "Specialist", "61-Proving Grounds.mod", 0, 0, 80, 120 },
                { PiecePlateArmour, TierMasterwork, "Warlord Plate Armour", "Masterwork", "61-Proving Grounds.mod", 0, 0, 95, 180 }
            },
            {
                { PieceHelmet, TierStandard, "Warlord Helmet", "Standard", "58-Proving Grounds.mod", 0, 0, 40, 80 },
                { PieceHelmet, TierHighGrade, "Warlord Helmet", "High Grade", "58-Proving Grounds.mod", 0, 0, 60, 100 },
                { PieceHelmet, TierSpecialist, "Warlord Helmet", "Specialist", "58-Proving Grounds.mod", 0, 0, 80, 150 },
                { PieceHelmet, TierMasterwork, "Warlord Helmet", "Masterwork", "58-Proving Grounds.mod", 0, 0, 95, 200 }
            },
            {
                { PieceKatana, TierStandard, "", "", "", 0, 0, 0, 0 },
                { PieceKatana, TierHighGrade, "Warlord Nodachi", "Pit Fang", "81-Proving Grounds.mod", "82-Proving Grounds.mod", "149-Proving Grounds.mod", 55, 60 },
                { PieceKatana, TierSpecialist, "Warlord Nodachi", "Arena Fang", "81-Proving Grounds.mod", "83-Proving Grounds.mod", "149-Proving Grounds.mod", 75, 120 },
                { PieceKatana, TierMasterwork, "Warlord Nodachi", "Warlord's Fang", "81-Proving Grounds.mod", "84-Proving Grounds.mod", "71-Proving Grounds.mod", 95, 180 }
            },
            {
                { PiecePolearm, TierStandard, "", "", "", 0, 0, 0, 0 },
                { PiecePolearm, TierHighGrade, "Warlord Polearm", "Pit Cleaver", "85-Proving Grounds.mod", "86-Proving Grounds.mod", "149-Proving Grounds.mod", 55, 85 },
                { PiecePolearm, TierSpecialist, "Warlord Polearm", "Arena Cleaver", "85-Proving Grounds.mod", "87-Proving Grounds.mod", "149-Proving Grounds.mod", 75, 130 },
                { PiecePolearm, TierMasterwork, "Warlord Polearm", "Warlord's Cleaver", "85-Proving Grounds.mod", "88-Proving Grounds.mod", "71-Proving Grounds.mod", 95, 170 }
            },
            {
                { PiecePitfighterFootwraps, TierStandard, "Pitfighter Footwraps", "Standard", "283-Proving Grounds.mod", 0, 0, 40, 20 },
                { PiecePitfighterFootwraps, TierHighGrade, "Pitfighter Footwraps", "High Grade", "283-Proving Grounds.mod", 0, 0, 60, 40 },
                { PiecePitfighterFootwraps, TierSpecialist, "Pitfighter Footwraps", "Specialist", "283-Proving Grounds.mod", 0, 0, 80, 60 },
                { PiecePitfighterFootwraps, TierMasterwork, "Pitfighter Footwraps", "Masterwork", "283-Proving Grounds.mod", 0, 0, 95, 80 }
            },
            {
                { PiecePitfighterHarness, TierStandard, "Pitfighter Harness", "Standard", "282-Proving Grounds.mod", 0, 0, 40, 25 },
                { PiecePitfighterHarness, TierHighGrade, "Pitfighter Harness", "High Grade", "282-Proving Grounds.mod", 0, 0, 60, 50 },
                { PiecePitfighterHarness, TierSpecialist, "Pitfighter Harness", "Specialist", "282-Proving Grounds.mod", 0, 0, 80, 80 },
                { PiecePitfighterHarness, TierMasterwork, "Pitfighter Harness", "Masterwork", "282-Proving Grounds.mod", 0, 0, 95, 120 }
            },
            {
                { PiecePitfighterJawGuard, TierStandard, "Pitfighter Jaw Guard", "Standard", "284-Proving Grounds.mod", 0, 0, 40, 20 },
                { PiecePitfighterJawGuard, TierHighGrade, "Pitfighter Jaw Guard", "High Grade", "284-Proving Grounds.mod", 0, 0, 60, 40 },
                { PiecePitfighterJawGuard, TierSpecialist, "Pitfighter Jaw Guard", "Specialist", "284-Proving Grounds.mod", 0, 0, 80, 60 },
                { PiecePitfighterJawGuard, TierMasterwork, "Pitfighter Jaw Guard", "Masterwork", "284-Proving Grounds.mod", 0, 0, 95, 80 }
            },
            {
                { PiecePitfighterPants, TierStandard, "Pitfighter Pants", "Standard", "285-Proving Grounds.mod", 0, 0, 40, 22 },
                { PiecePitfighterPants, TierHighGrade, "Pitfighter Pants", "High Grade", "285-Proving Grounds.mod", 0, 0, 60, 45 },
                { PiecePitfighterPants, TierSpecialist, "Pitfighter Pants", "Specialist", "285-Proving Grounds.mod", 0, 0, 80, 70 },
                { PiecePitfighterPants, TierMasterwork, "Pitfighter Pants", "Masterwork", "285-Proving Grounds.mod", 0, 0, 95, 100 }
            },
            {
                { PieceRetainerPants, TierStandard, "Retainer Leather Pants", "Standard", "127-Proving Grounds.mod", 0, 0, 40, 22 },
                { PieceRetainerPants, TierHighGrade, "Retainer Leather Pants", "High Grade", "127-Proving Grounds.mod", 0, 0, 60, 45 },
                { PieceRetainerPants, TierSpecialist, "Retainer Leather Pants", "Specialist", "127-Proving Grounds.mod", 0, 0, 80, 70 },
                { PieceRetainerPants, TierMasterwork, "Retainer Leather Pants", "Masterwork", "127-Proving Grounds.mod", 0, 0, 95, 100 }
            },
            {
                { PieceRetainerJacket, TierStandard, "Retainer Leather Jacket", "Standard", "129-Proving Grounds.mod", 0, 0, 40, 25 },
                { PieceRetainerJacket, TierHighGrade, "Retainer Leather Jacket", "High Grade", "129-Proving Grounds.mod", 0, 0, 60, 50 },
                { PieceRetainerJacket, TierSpecialist, "Retainer Leather Jacket", "Specialist", "129-Proving Grounds.mod", 0, 0, 80, 80 },
                { PieceRetainerJacket, TierMasterwork, "Retainer Leather Jacket", "Masterwork", "129-Proving Grounds.mod", 0, 0, 95, 120 }
            },
            {
                { PieceRetainerBoots, TierStandard, "Retainer Boots", "Standard", "131-Proving Grounds.mod", 0, 0, 40, 20 },
                { PieceRetainerBoots, TierHighGrade, "Retainer Boots", "High Grade", "131-Proving Grounds.mod", 0, 0, 60, 40 },
                { PieceRetainerBoots, TierSpecialist, "Retainer Boots", "Specialist", "131-Proving Grounds.mod", 0, 0, 80, 60 },
                { PieceRetainerBoots, TierMasterwork, "Retainer Boots", "Masterwork", "131-Proving Grounds.mod", 0, 0, 95, 80 }
            },
            {
                { PieceRetainerHelmet, TierStandard, "Retainer Leather Helmet", "Standard", "134-Proving Grounds.mod", 0, 0, 40, 20 },
                { PieceRetainerHelmet, TierHighGrade, "Retainer Leather Helmet", "High Grade", "134-Proving Grounds.mod", 0, 0, 60, 40 },
                { PieceRetainerHelmet, TierSpecialist, "Retainer Leather Helmet", "Specialist", "134-Proving Grounds.mod", 0, 0, 80, 60 },
                { PieceRetainerHelmet, TierMasterwork, "Retainer Leather Helmet", "Masterwork", "134-Proving Grounds.mod", 0, 0, 95, 80 }
            },
            {
                { PieceEnforcerLegplates, TierStandard, "Enforcer Legplates", "Standard", "121-Proving Grounds.mod", 0, 0, 40, 30 },
                { PieceEnforcerLegplates, TierHighGrade, "Enforcer Legplates", "High Grade", "121-Proving Grounds.mod", 0, 0, 60, 60 },
                { PieceEnforcerLegplates, TierSpecialist, "Enforcer Legplates", "Specialist", "121-Proving Grounds.mod", 0, 0, 80, 90 },
                { PieceEnforcerLegplates, TierMasterwork, "Enforcer Legplates", "Masterwork", "121-Proving Grounds.mod", 0, 0, 95, 120 }
            },
            {
                { PieceEnforcerBoots, TierStandard, "Enforcer Plated Boots", "Standard", "123-Proving Grounds.mod", 0, 0, 40, 25 },
                { PieceEnforcerBoots, TierHighGrade, "Enforcer Plated Boots", "High Grade", "123-Proving Grounds.mod", 0, 0, 60, 50 },
                { PieceEnforcerBoots, TierSpecialist, "Enforcer Plated Boots", "Specialist", "123-Proving Grounds.mod", 0, 0, 80, 75 },
                { PieceEnforcerBoots, TierMasterwork, "Enforcer Plated Boots", "Masterwork", "123-Proving Grounds.mod", 0, 0, 95, 100 }
            },
            {
                { PieceEnforcerChest, TierStandard, "Plate Enforcer Chest", "Standard", "119-Proving Grounds.mod", 0, 0, 40, 40 },
                { PieceEnforcerChest, TierHighGrade, "Plate Enforcer Chest", "High Grade", "119-Proving Grounds.mod", 0, 0, 60, 80 },
                { PieceEnforcerChest, TierSpecialist, "Plate Enforcer Chest", "Specialist", "119-Proving Grounds.mod", 0, 0, 80, 120 },
                { PieceEnforcerChest, TierMasterwork, "Plate Enforcer Chest", "Masterwork", "119-Proving Grounds.mod", 0, 0, 95, 160 }
            },
            {
                { PieceEnforcerHelmet, TierStandard, "Enforcer Helmet", "Standard", "125-Proving Grounds.mod", 0, 0, 40, 30 },
                { PieceEnforcerHelmet, TierHighGrade, "Enforcer Helmet", "High Grade", "125-Proving Grounds.mod", 0, 0, 60, 60 },
                { PieceEnforcerHelmet, TierSpecialist, "Enforcer Helmet", "Specialist", "125-Proving Grounds.mod", 0, 0, 80, 90 },
                { PieceEnforcerHelmet, TierMasterwork, "Enforcer Helmet", "Masterwork", "125-Proving Grounds.mod", 0, 0, 95, 120 }
            },
            {
                { PieceWarlordLongsword, TierStandard, "", "", "", 0, 0, 0, 0 },
                { PieceWarlordLongsword, TierHighGrade, "Warlord Longsword", "Pit Cutter", "182-Proving Grounds.mod", "143-Proving Grounds.mod", "149-Proving Grounds.mod", 55, 40 },
                { PieceWarlordLongsword, TierSpecialist, "Warlord Longsword", "Arena Talon", "182-Proving Grounds.mod", "144-Proving Grounds.mod", "149-Proving Grounds.mod", 75, 80 },
                { PieceWarlordLongsword, TierMasterwork, "Warlord Longsword", "Warlord's Talon", "182-Proving Grounds.mod", "145-Proving Grounds.mod", "71-Proving Grounds.mod", 95, 120 }
            },
            {
                { PieceWarlordKatana, TierStandard, "", "", "", 0, 0, 0, 0 },
                { PieceWarlordKatana, TierHighGrade, "Warlord Katana", "Pit Fang", "181-Proving Grounds.mod", "82-Proving Grounds.mod", "149-Proving Grounds.mod", 55, 40 },
                { PieceWarlordKatana, TierSpecialist, "Warlord Katana", "Arena Fang", "181-Proving Grounds.mod", "83-Proving Grounds.mod", "149-Proving Grounds.mod", 75, 80 },
                { PieceWarlordKatana, TierMasterwork, "Warlord Katana", "Warlord Fang", "181-Proving Grounds.mod", "84-Proving Grounds.mod", "71-Proving Grounds.mod", 95, 120 }
            },
            {
                { PieceMace, TierStandard, "", "", "", 0, 0, 0, 0 },
                { PieceMace, TierHighGrade, "Warlord Mace", "Pit Breaker", "137-Proving Grounds.mod", "140-Proving Grounds.mod", "149-Proving Grounds.mod", 55, 50 },
                { PieceMace, TierSpecialist, "Warlord Mace", "Arena Crusher", "137-Proving Grounds.mod", "141-Proving Grounds.mod", "149-Proving Grounds.mod", 75, 100 },
                { PieceMace, TierMasterwork, "Warlord Mace", "Warlord's Verdict", "137-Proving Grounds.mod", "142-Proving Grounds.mod", "71-Proving Grounds.mod", 95, 150 }
            },
            {
                { PieceSabre, TierStandard, "", "", "", 0, 0, 0, 0 },
                { PieceSabre, TierHighGrade, "Warlord Sabre", "Pit Cutter", "138-Proving Grounds.mod", "143-Proving Grounds.mod", "149-Proving Grounds.mod", 55, 50 },
                { PieceSabre, TierSpecialist, "Warlord Sabre", "Arena Talon", "138-Proving Grounds.mod", "144-Proving Grounds.mod", "149-Proving Grounds.mod", 75, 100 },
                { PieceSabre, TierMasterwork, "Warlord Sabre", "Warlord's Talon", "138-Proving Grounds.mod", "145-Proving Grounds.mod", "71-Proving Grounds.mod", 95, 150 }
            },
            {
                { PieceHacker, TierStandard, "", "", "", 0, 0, 0, 0 },
                { PieceHacker, TierHighGrade, "Warlord Hacker", "Pit Cleaver", "139-Proving Grounds.mod", "146-Proving Grounds.mod", "149-Proving Grounds.mod", 55, 50 },
                { PieceHacker, TierSpecialist, "Warlord Hacker", "Arena Reaper", "139-Proving Grounds.mod", "147-Proving Grounds.mod", "149-Proving Grounds.mod", 75, 100 },
                { PieceHacker, TierMasterwork, "Warlord Hacker", "Warlord's Ruin", "139-Proving Grounds.mod", "148-Proving Grounds.mod", "71-Proving Grounds.mod", 95, 150 }
            }
        };
    }

    int TiersFor(Piece piece)
    {
        if (piece < PieceGreatsword || piece >= PieceCount)
            return 0;
        return kTierForPiece[piece];
    }

    Tier FirstTier(Piece piece)
    {
        if (piece < PieceGreatsword || piece >= PieceCount)
            return TierMaxCount;
        return kFirstTierForPiece[piece];
    }

    bool IsValidTier(Piece piece, Tier tier)
    {
        if (piece < PieceGreatsword || piece >= PieceCount)
            return false;
        const int first = static_cast<int>(FirstTier(piece));
        const int value = static_cast<int>(tier);
        return value >= first && value < first + TiersFor(piece);
    }

    bool IsWeapon(Piece piece)
    {
        return piece == PieceGreatsword ||
            piece == PieceKatana ||
            piece == PiecePolearm ||
            piece == PieceWarlordLongsword ||
            piece == PieceWarlordKatana ||
            piece == PieceMace ||
            piece == PieceSabre ||
            piece == PieceHacker;
    }

    ArmourSet SetFor(Piece piece)
    {
        if ((piece >= PiecePitfighterFootwraps && piece <= PiecePitfighterPants) ||
            (piece >= PieceRetainerPants && piece <= PieceRetainerHelmet))
            return ArmourSetT1;
        if (piece >= PieceEnforcerLegplates && piece <= PieceEnforcerHelmet)
            return ArmourSetT2;
        if (!IsWeapon(piece))
            return ArmourSetT3;
        return ArmourSetNone;
    }

    ArmourRow RowFor(Piece piece)
    {
        if (piece >= PiecePitfighterFootwraps && piece <= PiecePitfighterPants)
            return ArmourRowPitfighter;
        if (piece >= PieceRetainerPants && piece <= PieceRetainerHelmet)
            return ArmourRowRetainer;
        if (piece >= PieceEnforcerLegplates && piece <= PieceEnforcerHelmet)
            return ArmourRowEnforcer;
        if (!IsWeapon(piece))
            return ArmourRowWarlord;
        return ArmourRowCount;
    }

    ArmourSet SetForRow(ArmourRow row)
    {
        switch (row)
        {
        case ArmourRowPitfighter:
        case ArmourRowRetainer:
            return ArmourSetT1;
        case ArmourRowEnforcer:
            return ArmourSetT2;
        case ArmourRowWarlord:
            return ArmourSetT3;
        default:
            return ArmourSetNone;
        }
    }

    const char* StableId(Piece piece)
    {
        if (piece < PieceGreatsword || piece >= PieceCount)
            return "";
        return kStableIds[piece];
    }

    const Reward& Get(Piece piece, Tier tier)
    {
        if (piece < PieceGreatsword || piece >= PieceCount)
            piece = PieceGreatsword;
        if (!IsValidTier(piece, tier))
            tier = FirstTier(piece);
        return kRewards[piece][tier];
    }

    Tier NextTier(int highestTier, Piece piece)
    {
        const Tier first = FirstTier(piece);
        if (first == TierMaxCount)
            return TierMaxCount;
        if (highestTier < static_cast<int>(first))
            return first;
        const Tier next = static_cast<Tier>(highestTier + 1);
        return IsValidTier(piece, next) ? next : TierMaxCount;
    }

    bool IsComplete(int highestTier, Piece piece)
    {
        return NextTier(highestTier, piece) == TierMaxCount;
    }

    bool CanClaim(int highestTier, Piece piece, Tier tier, int marks)
    {
        if (!IsValidTier(piece, tier))
            return false;
        if (NextTier(highestTier, piece) != tier)
            return false;
        return marks >= Get(piece, tier).cost;
    }

    const Licence& GetLicence(ArmourSet set)
    {
        if (set < ArmourSetT1 || set > ArmourSetT3)
            set = ArmourSetT1;
        return kLicences[set];
    }

    const Licence& GetWeaponLicence()
    {
        return kWeaponLicence;
    }

    bool CanPurchaseLicence(
        ArmourSet set,
        bool alreadyUnlocked,
        bool prerequisiteUnlocked,
        int marks)
    {
        if (set < ArmourSetT1 || set > ArmourSetT3 || alreadyUnlocked ||
            !prerequisiteUnlocked)
            return false;
        return marks >= GetLicence(set).cost;
    }

    bool CanPurchaseWeaponLicence(bool alreadyUnlocked, int marks)
    {
        return !alreadyUnlocked && marks >= kWeaponLicence.cost;
    }

    const GeneralReward& GetGeneral(GeneralItem item)
    {
        if (item < GeneralArenaBuildingsI || item >= GeneralItemCount)
            item = GeneralArenaBuildingsI;
        return kGeneralRewards[item];
    }

    void ApplyPriceOverrides()
    {
        for (int index = 0; index < static_cast<int>(PieceCount); ++index)
        {
            const Piece piece = static_cast<Piece>(index);
            const int first = static_cast<int>(FirstTier(piece));
            const int tiers = TiersFor(piece);
            for (int offset = 0; offset < tiers; ++offset)
            {
                const int tier = first + offset;
                Reward& reward = kRewards[index][tier];
                reward.cost = PGConfig::GearCost(kStableIds[index], tier, reward.cost);
            }
        }
        for (int item = 0; item < static_cast<int>(GeneralItemCount); ++item)
            kGeneralRewards[item].cost = PGConfig::SupplyCost(
                kGeneralRewards[item].stableId, kGeneralRewards[item].cost);
        for (int set = 0; set < 3; ++set)
            kLicences[set].cost = PGConfig::LicenceCost(
                kLicences[set].stableId, kLicences[set].cost);
        kWeaponLicence.cost = PGConfig::LicenceCost(
            kWeaponLicence.stableId, kWeaponLicence.cost);
    }

    bool IsGeneralVisible(GeneralItem item, bool arenaOnePurchased)
    {
        return item != GeneralArenaBuildingsII || arenaOnePurchased;
    }

    int ReplaceCost(int fullCost)
    {
        if (fullCost <= 0)
            return 0;
        return (fullCost + 1) / 2;
    }

    Tier HighestClaimedTier(int highestTier, Piece piece)
    {
        return IsValidTier(piece, static_cast<Tier>(highestTier))
            ? static_cast<Tier>(highestTier)
            : TierMaxCount;
    }

    Offer ResolveOffer(
        int highestTier,
        Piece piece,
        bool hasExactPreviousGrade,
        bool hasAnyGradeOfPiece)
    {
        Offer offer;
        offer.kind = OfferComplete;
        offer.tier = TierMaxCount;
        offer.cost = 0;

        const Tier next = NextTier(highestTier, piece);
        if (next == TierMaxCount)
        {
            if (hasAnyGradeOfPiece)
                return offer;
            const Tier highest = HighestClaimedTier(highestTier, piece);
            if (highest == TierMaxCount)
                return offer;
            offer.kind = OfferReplace;
            offer.tier = highest;
            offer.cost = ReplaceCost(Get(piece, highest).cost);
            return offer;
        }

        if (highestTier < static_cast<int>(FirstTier(piece)))
        {
            offer.kind = OfferClaim;
            offer.tier = next;
            offer.cost = Get(piece, next).cost;
            return offer;
        }

        if (hasExactPreviousGrade)
        {
            offer.kind = OfferClaim;
            offer.tier = next;
            offer.cost = Get(piece, next).cost;
            return offer;
        }

        const Tier replaceTier = static_cast<Tier>(next - 1);
        offer.kind = OfferReplace;
        offer.tier = replaceTier;
        offer.cost = ReplaceCost(Get(piece, replaceTier).cost);
        return offer;
    }
}
