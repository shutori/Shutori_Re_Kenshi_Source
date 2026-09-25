#include "BalanceTuning.h"
#include <cstdio>

namespace BalanceTuning
{
    Values::Values()
        : abilityExponent(2.5),
          ratingInfluence(1.0),
          zeroSumRating(true),
          downedPercent(0),
          npcLeadHoursMin(1.0),
          npcLeadHoursSpread(1.0),
          showcasePercent(0),
          ambientCloseCap(1.10),
          ambientCloseSpan(0.10),
          ambientRatioCap(1.15),
          ambientUnderdogSpan(0.05),
          pricingExponent(6.65),
          pricingKnee(1.10),
          ringNearRadius(145.0),
          ringOutRadius(147.5),
          ringReturnRadius(142.5)
    {
    }

    namespace
    {
        // Namespace-scope construction runs during static init, before any
        // worker thread exists. A function-local static would need C++11
        // thread-safe initialisation that the v100 toolset does not provide,
        // and TownMatchmakingSearch calls into this from a worker thread.
        Values g_values;
    }

    Values& Get()
    {
        return g_values;
    }

    void WriteJson(char* out, int outSize)
    {
        if (!out || outSize <= 0)
            return;
        const Values& v = Get();
        sprintf_s(out, static_cast<size_t>(outSize),
            "\"ability_exponent\":%.4f,\"rating_influence\":%.4f,\"zero_sum_rating\":%d,"
            "\"downed_percent\":%d,\"npc_lead_min\":%.3f,\"npc_lead_spread\":%.3f,"
            "\"showcase_percent\":%d,\"pricing_exponent\":%.4f,\"pricing_knee\":%.4f,"
            "\"ambient_close_cap\":%.4f,\"ambient_close_span\":%.4f,"
            "\"ambient_ratio_cap\":%.4f,\"ambient_underdog_span\":%.4f,"
            "\"ring_near_radius\":%.1f,\"ring_out_radius\":%.1f,"
            "\"ring_return_radius\":%.1f",
            v.abilityExponent, v.ratingInfluence, v.zeroSumRating ? 1 : 0,
            v.downedPercent, v.npcLeadHoursMin, v.npcLeadHoursSpread, v.showcasePercent,
            v.pricingExponent, v.pricingKnee,
            v.ambientCloseCap, v.ambientCloseSpan, v.ambientRatioCap, v.ambientUnderdogSpan,
            v.ringNearRadius, v.ringOutRadius, v.ringReturnRadius);
    }
}
