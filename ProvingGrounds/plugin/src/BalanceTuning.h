// BalanceTuning.h - one home for the experimentally tunable balance constants.
//
// Every value defaults to the shipped behaviour EXCEPT four groups:
// zeroSumRating, which fixes a proven non-conservation bug
// (balancing/matchmaking-balance-insights.md, finding 6); the pricingExponent /
// pricingKnee pair; the ambientCloseCap group; and the ringNearRadius group,
// which keeps town-arena fighters out of the Crucible's walls. The remaining
// levers exist so a controlled diagnostic run can move ONE lever at a time; each
// value is written into the combat log on every market and match event, so any
// captured log is self-describing.
//
// Rationale for not shipping the other levers enabled by default: they change
// fight outcomes (downedPercent, showcasePercent) or the betting window
// (npcLeadHours*), and the analysis that motivated them is directional rather
// than confirmed on the current sample. A controlled comparison needs a
// baseline that is byte-identical to the shipped build.
//
// The three enabled-on-purpose groups each state their own case where they are
// declared, and they are not equivalent. The pricing pair is read only by
// TownBettingPolicy, so it cannot move a pairing -- strictly outside the rule
// above. The ambient band is INSIDE it: it picks different pairings, so it is a
// deliberate exception, taken because the narrower band is wanted in live play
// rather than only in a controlled run. Both keep the rule's real guarantee --
// the shipped behaviour is one edit away and byte-identical -- so a capture can
// always be compared against a baseline built from the shipped values.
#pragma once

namespace BalanceTuning
{
    struct Values
    {
        // Ability -> power exponent in TownMatchmakingPolicy::FighterPower.
        // Shipped 2.5. The diagnostic sweep leans 3.5-4.0 (one fighter carries
        // the bout: top damage dealer won 27/36) but the sample cannot confirm it.
        double abilityExponent;

        // MMR correction strength (the "full" model). Shipped 1.0. The sweep
        // leans 0.0: at the observed p10-p90 rating spread the term is a 21%
        // score swing that currently costs log-loss rather than adding signal.
        double ratingInfluence;

        // R1: conserve total rating per bout. Shipped behaviour moved every
        // fighter by an independent delta, so uneven-team bouts leaked rating
        // (net -115 across 36 bouts; 4v2 leaked -45.6 while 1v2 created +49.8).
        bool zeroSumRating;

        // R4: percent of a team that must be down before a team bout ends.
        // 0 = shipped behaviour, which requires a FULL team wipe. Every one of
        // the 36 recorded bouts was a wipe, leaving 59% of all fighters with a
        // destroyed limb. 50 ends a 4v4 at two down.
        int downedPercent;

        // R3: NPC bout announcement lead, in game hours. Shipped 1.0 minimum
        // plus up to 1.0 of hashed spread. The announced bout is destroyed by
        // any save inside the window (18 of 73 authored bouts died that way), so
        // a shorter window loses less content - at the cost of a shorter betting
        // window (betting already closes when combat begins).
        double npcLeadHoursMin;
        double npcLeadHoursSpread;

        // R2a: percent of ambient bouts authored as a deliberate mismatch
        // (ratio up to kShowcaseRatioHigh) so the book can price something other
        // than a coin flip. 0 = shipped behaviour (parity only, ratio <= 1.30,
        // which confines the price to p in [0.372, 0.628]).
        int showcasePercent;

        // Lever C, the ambient band. The matchmaker draws a pairing target and
        // keeps the candidate whose score ratio is closest to it, so these four
        // numbers are the whole shape of an ambient bout:
        //     close band     target uniform in [1.00, ambientCloseCap]
        //     underdog band  target uniform in [ambientCloseCap, +ambientUnderdogSpan]
        //     hard cap       any candidate above ambientRatioCap is discarded
        // Shipped: 1.15 / 0.15 / 1.30 / 0.15 -- i.e. 80% of bouts target 1.00-1.15
        // and 20% target 1.15-1.30.
        //
        // Why narrow it. Measured on the 191-market capture, the score ratio
        // decides a bout far harder than the shipped model believes: a logistic
        // fit gives the logit slope on ln(ratio) as 7.60 (se 2.64) against the
        // model's 2.00, and the realised underdog win rate falls 49.5% below
        // r=1.10, 31.9% at 1.10-1.20, 16.0% above 1.20. Pricing cannot rescue the
        // wide end -- at r=1.205 the honest price is 4.02x on a 16% shot (-36%),
        // and the book's 5.00 ceiling cannot pay honestly past r~1.29. So the
        // fix is to stop offering that end: the values shipped here put 80% of
        // bouts at 1.00-1.10 (a ~50/50 contest) and 20% at 1.10-1.15
        // (favourite ~0.737), giving a ~55/45 bout paying about 1.7 / 1.95.
        //
        // The spans are EXPLICIT literals, deliberately not derived as
        // (cap - base). Deriving them shifts the drawn target by one ULP on 1880
        // and 3115 of the 10000 possible draws, which would break the
        // bit-identical baseline claim below for no readability gain.
        //
        // This lever DOES change fight outcomes, so unlike the pricing pair it
        // deviates from the "ship outcome-changing levers disabled" rule at the
        // top of this file. It ships enabled because the narrower band is the
        // requested behaviour, and the baseline stays available: setting all four
        // back to the shipped values reproduces the shipped draw bit-for-bit,
        // and all four are stamped into every market and match event so a capture
        // always records which band produced it.
        double ambientCloseCap;
        double ambientCloseSpan;
        double ambientRatioCap;
        double ambientUnderdogSpan;

        // Lever B, the favourite-pricing transform. The book prices
        // p = r^k / (1 + r^k) where r = max(score) / min(score); shipped is k = 2,
        // which is exactly what WinProbability squares to. Measured on a
        // 191-market live capture the rule is fair below r = 1.10 (49/97 = 0.505
        // realised against a priced 0.518) and badly under-confident above it
        // (68/94 = 0.723 realised against a priced 0.579), so a single exponent
        // cannot fix it: raising k everywhere overtaxes the narrow band that is
        // already right.
        //
        // These ship ENABLED, unlike the other levers here. The rationale for
        // shipping the others disabled is that they change fight outcomes
        // (downedPercent, showcasePercent) or the betting window (npcLeadHours*).
        // This one does neither: it is read only by TownBettingPolicy, so it cannot
        // move a pairing. Set pricingKnee to 0 to restore the shipped square at
        // every ratio -- PricedProbability then returns WinProbability directly, so
        // the baseline is bit-for-bit, not merely close.
        //
        // Fitted by matching the pooled realised favourite rate above the knee
        // against the pooled priced probability, with no band exclusions:
        // pricingKnee 1.10, pricingExponent 6.65. Both sides of a wide market then
        // return about the -10% overround, against the shipped favourite +13.5%
        // and underdog -42.0%. The underdog's odds at r=1.25 go from 2.31 to 4.65,
        // where the honest price is 5.63.
        //
        // Known residual: the 1.16-1.20 band (n=11) realised 0.455, contradicting
        // both 0.729 at 1.10-1.14 and 0.840 at 1.20-1.30, and that is the one place
        // the fit is unverified -- on this sample an underdog backer restricted to
        // that band would have profited. Judge it on the next capture before
        // treating the transform as settled.
        //
        // pricingExponent is a PRICING value only and must not be confused with
        // abilityExponent above, which drives pairing selection.
        double pricingExponent;
        double pricingKnee;

        // Ring guard (town bouts only). A town bout keeps its fighters inside the
        // Crucible. The engine's crowding repulsion (FlockingTools, reached through
        // CharMovement) presses a fighter against the wall until the collision step
        // loses them and they pass through it -- seen live as a weaker fighter
        // ground into the wall by a stronger one, which is the whole reason this
        // exists. Two corrections, both scaled off the arena building's origin:
        //
        //   ringNearRadius    past this, the fighter is steered back in while still
        //                     fighting (combatMover.setForcedWP toward ringReturnRadius)
        //   ringOutRadius     past this, they are returned to the ring outright
        //   ringReturnRadius  the radius both corrections aim at
        //
        // The centre is the same point the medic line is measured from:
        // TownArenaRuntimePolicy::MedicStandbyOffset puts ringside aid at exactly
        // 190, so ringOutRadius must stay below 190 or fighters would be corrected
        // while standing at their own aid.
        //
        // Calibrating these against the real wall. The Crucible's geometry is FCS
        // data, so the code cannot read where its wall is; what bounds it from
        // below is the staging layout, which for a town bout is only ever ModeTeams
        // or ModeTeams1v1 (ModeLastStanding, and with it the 155 outer radius, is
        // the old arena's). Those place fighters at 85 + 35 per column out on x and
        // 46 per row on z, i.e. roughly 110-130 for a realistic team size, with the
        // 1v1 benches at 126.5. So the wall is somewhere in (130, 190).
        //
        // To pin it exactly, no new code is needed: every correction logs the
        // distance it fired at. Set ringNearRadius low enough to fire during normal
        // fighting for one session and read the distances in pg_debug.log -- the
        // largest distance a fighter reaches while still visibly inside the ring
        // brackets the wall from below, and a logged return distance brackets it
        // from above.
        //
        // Shipped 150 / 160 / 140. ringOutRadius was 180, which by live inspection
        // sits well outside the wall, so the trigger fired late; 160 moves it in.
        // ringReturnRadius was 120, which made a correction a 60-unit jump -- the
        // "pushes too hard" complaint. It now sits 10 units inside the near line
        // and 20 inside the out line, so a correction is a short step: the fighter
        // is returned to just inside the ring rather than to the middle of it.
        // 140 also stays clear of the ~130 the staging layout uses, so a returned
        // fighter is not dropped on top of a staged one.
        //
        // Unlike the levers above this one does move fight outcomes, since a fighter
        // who would have been cornered is now walked inward. It is a fix for a
        // broken state rather than a balance change -- being inside the wall is not
        // a legitimate outcome -- so it ships enabled. Setting ringNearRadius equal
        // to ringOutRadius disables both corrections without a code change.
        double ringNearRadius;
        double ringOutRadius;
        double ringReturnRadius;

        Values();
    };

    Values& Get();

    // Compact JSON fragment WITHOUT enclosing braces, for the combat log.
    // out must hold at least 512 bytes.
    void WriteJson(char* out, int outSize);
}
