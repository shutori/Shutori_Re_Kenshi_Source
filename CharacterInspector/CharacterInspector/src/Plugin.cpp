#include <Debug.h>

#include "InventoryStatsButton.h"

__declspec(dllexport) void startPlugin()
{
    DebugLog("Character Inspector: startPlugin");
    if (!InventoryStatsButton::InstallHooks())
        ErrorLog("Character Inspector: failed to install hooks");
}
