#include <Debug.h>
#include <Windows.h>

#include <core/Functions.h>
#include <kenshi/GameWorld.h>

#include "ui/DefenseButton.h"
#include "ui/ModSettings.h"
#include "cmd/DefenseController.h"
#include "game/Persistence.h"
#include "store/LayoutStore.h"

namespace
{
    // Owns the in-memory defense layout for the lifetime of the plugin.
    // Persistence::install ties this to Kenshi's own save/load/new-game
    // hooks so it's scoped to the active playthrough save.
    LayoutStore g_layout;

    // Per-frame game-update hook used to drive DefenseController::onTick
    // (ArrivalMonitor polling while a Return is in progress). GameWorld::
    // mainLoop_GPUSensitiveStuff is virtual (vtable offset 0x0 on GameWorld
    // itself), so GetRealAddress/AddHook target its non-virtual `_NV_`
    // twin — see core/Functions.h: "GetRealAddress doesn't work with
    // virtual functions". It runs once per frame with the frame's delta
    // time, for as long as the game world exists (menus included), which is
    // exactly the "safe per-frame hook" Task 9 needs.
    //
    // It is also the GPU/render-side half of the main loop, i.e. the same
    // thread that drives MyGUI, so DefenseController::onTick may update HUD
    // widgets directly from here without marshalling to another callback.
    void (*GameWorld_mainLoop_orig)(GameWorld*, float) = nullptr;
    void GameWorld_mainLoop_hook(GameWorld* thisptr, float time)
    {
        GameWorld_mainLoop_orig(thisptr, time);
        // DEF button is parented to the MainBar; retry until SpeedButtonsPanel
        // exists (constructor hook alone can be too early after load/new game).
        DefenseButton::ensureCreated();
        DefenseController::onTick(time);
    }
}

__declspec(dllexport) void startPlugin()
{
    DebugLog("DefensivePositions: startPlugin");

    DefenseController::configure(&g_layout);
    ModSettings::install();
    DefenseButton::install(
        DefenseController::onSave,
        DefenseController::onReturn,
        DefenseController::onDiscard);
    Persistence::install(g_layout, DefenseController::abortReturn);

    if (KenshiLib::SUCCESS != KenshiLib::AddHook(
            KenshiLib::GetRealAddress(&GameWorld::_NV_mainLoop_GPUSensitiveStuff),
            GameWorld_mainLoop_hook,
            &GameWorld_mainLoop_orig))
        ErrorLog("DefensivePositions: Could not hook GameWorld per-frame update for ArrivalMonitor ticking");
}
