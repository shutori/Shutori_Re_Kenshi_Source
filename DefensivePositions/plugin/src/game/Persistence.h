#pragma once

// Ties LayoutStore's save/load blob into Kenshi's own save-game lifecycle so
// the defense layout is scoped to the active playthrough save, not a global
// cross-save file. See Persistence.cpp for the hook points chosen and why.
//
// This header pulls in no KenshiLib types itself, but Persistence.cpp does,
// so (like GameAdapters/DefenseController) it may only be used from
// `game/`/plugin entry-point sources, not the pure, unit-tested modules.

class LayoutStore;

namespace Persistence
{
    // Invoked after the game has swapped worlds (a save finished loading, or
    // a new game started). Anything holding per-world state must drop it.
    typedef void (*WorldResetCallback)();

    // Registers the save/load/new-game hooks. Must be called once (e.g. from
    // startPlugin) with a LayoutStore that outlives the plugin (Plugin.cpp
    // owns it as a static/global, matching DefenseController::configure).
    //
    // onWorldReset may be null; when set it fires on both load and new game.
    void install(LayoutStore& store, WorldResetCallback onWorldReset);
}
