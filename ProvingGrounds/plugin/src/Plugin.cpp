#include "PGLog.h"

#include "CombatHooks.h"
#include "ArenaMedical.h"
#include "TownAftercare.h"
#include "ArenaUI.h"
#include "ContextMenuHooks.h"
#include "WorldLifecycle.h"
#include "FighterIdentity.h"
#include "ArenaNativePersistence.h"
#include "PGConfig.h"

__declspec(dllexport) void startPlugin()
{
    PGLog::Debug("Proving Grounds: startPlugin; logs beside ProvingGrounds.dll in the mod folder; build " __DATE__ " " __TIME__);
    PGLog::Combat("\"event\":\"session_start\",\"schema\":1,\"build\":\"" __DATE__ " " __TIME__ "\"");

    // Prices first: it stamps the reward catalogue before any screen or
    // purchase can read a cost, and it is the only thing that writes
    // pg_config.json when the mod folder has none.
    PGConfig::Load();

    if (!CombatHooks::Install())
        PGLog::Error("Proving Grounds: CombatHooks install failed");

    if (!ArenaMedical::InstallHooks())
        PGLog::Error("Proving Grounds: ArenaMedical hooks install failed");

    if (!TownAftercare::InstallHooks())
        PGLog::Error("Proving Grounds: medic AI ownership hook unavailable; town booking disabled");

    if (!ArenaUI::InstallHooks())
        PGLog::Error("Proving Grounds: ArenaUI install failed");

    if (!ContextMenuHooks::Install())
        PGLog::Error("Proving Grounds: ContextMenuHooks install failed");

    const bool identityReady = FighterIdentity::InstallHooks();
    if (!identityReady)
        PGLog::Error("Proving Grounds: fighter identity hooks unavailable");

    if (!WorldLifecycle::InstallHooks())
        PGLog::Error("Proving Grounds: WorldLifecycle install failed");
    if (!identityReady) ArenaNativePersistence::Disable();
}
