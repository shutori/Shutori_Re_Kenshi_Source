// PGConfig.h - one home for the tunable economy and plugin settings.
//
// The structs below carry the SHIPPED values; pg_config.json, beside
// ProvingGrounds.dll, may override them. A missing file, key, or unusable value
// leaves the shipped default in force. Gameplay settings load at startup and
// require restart; crowd performance and F8 enablement apply on save.
//
// Two resolution times, because the readers differ:
//
//   - The catalogue prices (gear tiers, supplies, licences) are STAMPED into
//     ArenaRewards' tables once at load. Every reader -- the rewards screen, the
//     affordability gates, the ledger reservation, the half-price replace --
//     reads that field, so stamping is what makes one edit cover all of them.
//   - The computed prices (prisoner recruitment, the bookie stake range, the
//     challenge buy-in) are read through this module at use time instead, so
//     there is no second place a value could be scaled twice.
//
// marksMultiplier scales MARKS only: the catalogue, and the prisoner recruitment
// cost. The bookie stake and the challenge buy-in are paid in Cats, which this
// mod never adjusts.
//
// ADDING A SECTION. A new setting needs four things, all in PGConfig: a shipped
// default (a struct here, or the caller's own default passed in), a place in
// kSections and kKnown in Parse, a reader that validates it, and where the value
// becomes visible -- either an accessor read at use time, or an apply hook in the
// owning module. Only the apply hooks are one-shot: a section whose value is
// stamped into another module's table must not be applied twice, because the
// second apply sees the already-stamped value. BalanceTuning is the other home
// for tunables and is not read from this file; if a balance lever belongs here
// later, its struct is the template for the reader and the shipped default.
#pragma once

#include "ArenaPerformance.h"
#include <string>

namespace PGConfig
{
    // Wagers move in whole tens of Cats and the bookie's buttons step by
    // 10/100/1000, so this stays compiled rather than configurable: the range
    // is the lever, the increment is the control.
    const int kStakeStep = 10;

    // Prisoner recruitment cost model (PrisonerRecruitmentLogic::RequiredMarks).
    // minimum/maximum bound the stat-driven price and step rounds it up; the
    // marks multiplier is applied to that bounded result, so a multiplier of 2
    // doubles every prisoner's price, not just the cheap ones. Shipped
    // 25 / 250 / 5 / 2.5 -- the "soft bump vs armoury earn inflation: cheaper
    // than a full 6x, not free" balance.
    struct Recruitment
    {
        int minimum;
        int maximum;
        int step;
        double combatScale;
    };

    // Bookie wager range, in Cats. Shipped 10 / 10000. Both are normalised to
    // whole kStakeStep multiples at load, so a configured ceiling the wager
    // validators would reject cannot be reached.
    struct Bookie
    {
        int minimum;
        int maximum;
    };

    enum ChallengeDifficulty { ChallengeEasy, ChallengeNormal, ChallengeHard };
    struct Challenges
    {
        ChallengeDifficulty difficulty;
        int refreshHours;
        int refreshBaseCostCats;
        double refreshCostMultiplier;
        int cooldownHours;
        double winMarksMultiplier;
        double uniqueBonusMultiplier;
        double skarnBonusMultiplier;
    };

    // Settings edited by the in-game Mods page. Shop catalogue prices and the
    // Marks price multiplier deliberately remain in the hand-edited JSON only.
    struct EditableSettings
    {
        Recruitment recruitment;
        Bookie bookie;
        Challenges challenges;
        int challengeBase[3];
        ArenaPerformance::Profile performanceProfile;
        // true lets F8 toggle diagnostics; false makes F8 do nothing.
        bool f8OpensDebugMenu;
    };

    // Reads pg_config.json beside ProvingGrounds.dll and stamps the results into
    // the catalogue. Writes the shipped values to that path when no file exists;
    // leaves a file it cannot parse untouched, because the user is probably
    // mid-edit, and says so in pg_debug.log. Only the first call reads a file,
    // and only the first call may apply, because an apply is one-shot.
    void Load();

    // Catalogue costs: the file's value for this key if it supplied a usable
    // one, otherwise defaultCost -- either way scaled by the marks multiplier.
    // tier is an ArenaRewards::Tier value. Called only by
    // ArenaRewards::ApplyPriceOverrides, which passes its own compiled default.
    int GearCost(const char* pieceStableId, int tier, int defaultCost);
    int SupplyCost(const char* stableId, int defaultCost);
    int LicenceCost(const char* stableId, int defaultCost);

    // A computed Marks price scaled by the multiplier, rounded half up. The one
    // place the multiplier meets a price that is not in the catalogue.
    int ScaleMarks(int marks);

    const Recruitment& RecruitmentValues();
    const Bookie& BookieValues();
    const Challenges& ChallengeValues();
    EditableSettings ShippedEditableSettings();
    EditableSettings GetEditableSettings();
    ArenaPerformance::Profile PerformanceProfile();
    // Saves a UI-owned overlay in pg_config.json, preserving the shop sections
    // and comments/formatting outside that overlay. Gameplay values require a
    // restart; crowd performance and F8 enablement update immediately.
    bool SaveEditableSettings(const EditableSettings& settings, std::string& error);
    bool F8OpensDebugMenu();
    // Challenge buy-in base for a division: 0 Easy, 1 Medium, 2 Hard. Cats.
    int ChallengeBase(int division);

    // A wager of `stake` Cats as the bookie would hold it: bounded, and on a
    // whole step.
    int ClampStake(int stake);

    // Ceiling the save sidecar accepts for a pending bookie credit. A raised
    // stake ceiling must not let the plugin write a credit the reader would then
    // reject -- that discards the save's whole arena progression. Never below the
    // shipped bound, so an existing save keeps loading.
    int BookieCreditCeiling();
}
