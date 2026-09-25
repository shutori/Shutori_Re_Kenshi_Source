#pragma once

namespace FightDisengagePolicy
{
    // Ordered cleanup steps for FightStarter::DisengageFighter.
    // Keep engage/disengage symmetric so mid-fight saves do not serialize
    // leftover attack orders / AI combat goals.
    enum Step
    {
        StepClearTempEnemies = 0,
        StepRemoveMeleeJobs,
        StepClearPlayerOrders,
        StepEndCombatMode,
        StepClearAiGoals,
        StepRethinkAi,
        StepRestoreOrders
    };

    struct Plan
    {
        bool clearTempEnemies;
        bool removeFocusedMeleeJobs;
        bool clearPlayerAttackOrders;
        bool endCombatMode;
        bool clearAiGoals;
        bool rethinkAi;
        bool restoreStandingOrders;
    };

    inline Plan BuildFullDisengagePlan()
    {
        Plan plan = {};
        plan.clearTempEnemies = true;
        plan.removeFocusedMeleeJobs = true;
        plan.clearPlayerAttackOrders = true;
        plan.endCombatMode = true;
        plan.clearAiGoals = true;
        plan.rethinkAi = true;
        plan.restoreStandingOrders = true;
        return plan;
    }

    // A KO in a continuing match only needs stale combat targets removed.
    // Restoring standing orders (including Jobs via the UI command) here would
    // tear down and immediately rebuild the match state for every survivor.
    inline Plan BuildInMatchRetargetPlan()
    {
        Plan plan = {};
        plan.clearTempEnemies = true;
        plan.removeFocusedMeleeJobs = true;
        plan.clearPlayerAttackOrders = true;
        return plan;
    }
}
