#pragma once

namespace MedicSupplyPolicy {
    enum Supply { FirstAid, RobotRepair };
    enum RestockResult { NoRestock, ReplacementSpawned, UsedKitRecharged };

    inline bool ShouldRestock(bool scratchMedic, float chargesBefore,
        float chargesAfter, bool anotherUsableKit) {
        return scratchMedic && chargesBefore > 0.0f && chargesAfter <= 0.0f &&
            !anotherUsableKit;
    }

    inline const char* ReplacementId(Supply supply) {
        return supply == RobotRepair ? "18020-gamedata.base" : "1359-gamedata.base";
    }

    template<class Access>
    RestockResult RestockAfterUse(float chargesBefore, float chargesAfter,
        Supply supply, Access& access) {
        if (!ShouldRestock(access.IsScratchMedic(), chargesBefore, chargesAfter, false))
            return NoRestock;
        if (access.MainInventoryHasUsableKit(supply) ||
                access.BackpackHasUsableKit(supply))
            return NoRestock;
        if (access.GiveReplacement(ReplacementId(supply)))
            return ReplacementSpawned;
        access.RechargeUsedKit();
        return UsedKitRecharged;
    }
}
