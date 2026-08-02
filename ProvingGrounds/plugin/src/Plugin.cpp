#include <Debug.h>

#include "CombatHooks.h"
#include "ArenaUI.h"
#include "ContextMenuHooks.h"

__declspec(dllexport) void startPlugin()
{
    DebugLog("Proving Grounds: startPlugin");

    if (!CombatHooks::Install())
        ErrorLog("Proving Grounds: CombatHooks install failed");

    if (!ArenaUI::InstallHooks())
        ErrorLog("Proving Grounds: ArenaUI install failed");

    if (!ContextMenuHooks::Install())
        ErrorLog("Proving Grounds: ContextMenuHooks install failed");
}
