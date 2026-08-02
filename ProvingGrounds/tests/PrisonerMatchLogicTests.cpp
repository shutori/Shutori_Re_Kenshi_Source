#include "PrisonerMatchLogic.h"
#include <cstdio>
#include <cstring>
#include <string>

static int g_fails = 0;

static void Expect(bool cond, const char* msg)
{
    if (!cond)
    {
        std::printf("FAIL: %s\n", msg);
        ++g_fails;
    }
}

int main()
{
    Expect(PrisonerMatchLogic::HasEnoughHandlers(0, 0), "0 prisoners ok");
    Expect(PrisonerMatchLogic::HasEnoughHandlers(2, 2), "exact handlers ok");
    Expect(!PrisonerMatchLogic::HasEnoughHandlers(2, 1), "shortage");

    Expect(
        PrisonerMatchLogic::FormatHandlerShortage(2, 1) ==
            "Need 2 handlers for 2 prisoners (1 available)",
        "shortage message");

    // original free
    {
        const bool free[] = { true, true };
        const float dist[] = { 100.0f, 10.0f };
        const bool inRange[] = { true, true };
        const bool reserved[] = { false, false };
        PrisonerMatchLogic::CagePick p = PrisonerMatchLogic::ChooseReturnCage(
            0, free, dist, inRange, reserved, 2);
        Expect(p.index == 0, "prefer original");
    }

    // original busy -> nearest
    {
        const bool free[] = { false, true, true };
        const float dist[] = { 1.0f, 50.0f, 20.0f };
        const bool inRange[] = { true, true, true };
        const bool reserved[] = { false, false, false };
        PrisonerMatchLogic::CagePick p = PrisonerMatchLogic::ChooseReturnCage(
            0, free, dist, inRange, reserved, 3);
        Expect(p.index == 2, "nearest free when original busy");
    }

    // none in range
    {
        const bool free[] = { true };
        const float dist[] = { 5.0f };
        const bool inRange[] = { false };
        const bool reserved[] = { false };
        PrisonerMatchLogic::CagePick p = PrisonerMatchLogic::ChooseReturnCage(
            0, free, dist, inRange, reserved, 1);
        Expect(p.index == -1, "none when out of registry range");
    }

    // reserved skipped
    {
        const bool free[] = { true, true };
        const float dist[] = { 10.0f, 20.0f };
        const bool inRange[] = { true, true };
        const bool reserved[] = { true, false };
        PrisonerMatchLogic::CagePick p = PrisonerMatchLogic::ChooseReturnCage(
            -1, free, dist, inRange, reserved, 2);
        Expect(p.index == 1, "skip reserved");
    }

    Expect(
        !PrisonerMatchLogic::ShouldSuppressNaturalEnemy(true, true, false),
        "spar opponents never suppressed");
    Expect(
        PrisonerMatchLogic::ShouldSuppressNaturalEnemy(false, true, false),
        "match prisoner vs outsider suppressed");
    Expect(
        PrisonerMatchLogic::ShouldSuppressNaturalEnemy(false, false, true),
        "outsider vs match prisoner suppressed");
    Expect(
        !PrisonerMatchLogic::ShouldSuppressNaturalEnemy(false, false, false),
        "non-prisoners not suppressed");

    Expect(
        PrisonerMatchLogic::ChooseReturnAction(true, false) ==
            PrisonerMatchLogic::ReturnEscortWalk,
        "conscious escort");
    Expect(
        PrisonerMatchLogic::ChooseReturnAction(false, false) ==
            PrisonerMatchLogic::ReturnCarry,
        "ko carry");
    Expect(
        PrisonerMatchLogic::ChooseReturnAction(false, true) ==
            PrisonerMatchLogic::ReturnSkip,
        "dead skip");

    if (g_fails)
    {
        std::printf("%d failed\n", g_fails);
        return 1;
    }
    std::printf("All PrisonerMatchLogicTests passed\n");
    return 0;
}
