#pragma once

namespace KOWatcher
{
    // Call from UI/update thread while a spar may be active.
    void Tick();
}
