#include "SparHitLogic.h"
#include <cstdio>

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
    Expect(
        SparHitLogic::ShouldForceApplySparMiss(true, 10.0f, false),
        "force soft-miss when not dodging");
    Expect(
        !SparHitLogic::ShouldForceApplySparMiss(true, 10.0f, true),
        "never force successful dodge");
    Expect(
        !SparHitLogic::ShouldForceApplySparMiss(false, 10.0f, false),
        "no force when not a miss");
    Expect(
        !SparHitLogic::ShouldForceApplySparMiss(true, 0.0f, false),
        "no force when damage empty");
    Expect(
        !SparHitLogic::ShouldForceApplySparMiss(true, 0.01f, false),
        "no force at damage threshold boundary");
    Expect(
        SparHitLogic::ShouldForceApplySparMiss(true, 0.011f, false),
        "force just above damage threshold");

    if (g_fails)
    {
        std::printf("%d test(s) failed\n", g_fails);
        return 1;
    }
    std::printf("All SparHitLogic tests passed\n");
    return 0;
}
