#include "../src/ArenaIngressPolicy.h"

#include <cassert>

int main()
{
    assert(ArenaIngressPolicy::ShouldAdvancePrisonerRelease(true, true, true));
    assert(!ArenaIngressPolicy::ShouldAdvancePrisonerRelease(false, true, true));
    assert(!ArenaIngressPolicy::ShouldAdvancePrisonerRelease(true, false, true));
    assert(!ArenaIngressPolicy::ShouldAdvancePrisonerRelease(true, true, false));
    return 0;
}
