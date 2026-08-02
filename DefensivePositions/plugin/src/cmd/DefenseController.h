#pragma once

// Orchestrates the RMB "Save", LMB "Return", and MMB "Discard" HUD commands:
// reads/writes selected characters via GameAdapters, merges or erases
// DefenseAssignment records in a LayoutStore, and drives an ArrivalMonitor
// while units walk/mount back into position.
//
// This header pulls in GameAdapters.h (KenshiLib types), so like GameAdapters
// itself it may only be included from `game/`/`ui`/plugin entry-point sources,
// not from the pure, unit-tested modules (types/, store/, report/, monitor/).

#include <string>

class LayoutStore;

namespace DefenseController
{
    // Must be called once (e.g. from startPlugin) before onSave/onReturn/
    // onDiscard are used. DefenseController does not own the store's lifetime
    // — the caller (Plugin.cpp) must keep it alive for as long as the plugin
    // runs.
    void configure(LayoutStore* store);

    // Saves the current selection's positions (and turret mount state, if
    // any) into the configured store, merging by characterId
    // (LayoutStore::upsert). Shows the result on the Defense HUD status area.
    //
    // Characters that are not player-controllable are skipped (not written).
    std::string saveSelectedDefense(LayoutStore& store);

    // Removes saved assignments for the current selection only
    // (LayoutStore::erase per characterId). Characters with no assignment and
    // non-controllable characters are skipped. Does not abort an in-flight
    // Return.
    std::string discardSelectedDefense(LayoutStore& store);

    // LMB "Return": for every currently selected character, classifies
    // availability and looks up its saved assignment, issuing a move/mount
    // order for each one that is available and has an assignment. Begins
    // ArrivalMonitor tracking for those units; onTick() advances it. If a
    // Return is already in progress, this is a no-op (just re-shows a
    // "Return already in progress" message).
    void onReturn();

    // RMB "Save": wraps saveSelectedDefense() against the configured store
    // and shows the result on the Defense HUD status area.
    void onSave();

    // MMB "Discard": wraps discardSelectedDefense() against the configured
    // store and shows the result on the Defense HUD status area.
    void onDiscard();

    // Advances any in-progress Return by dt seconds. Intended to be called
    // from a per-frame game-update hook (see Plugin.cpp). No-op if no
    // Return is in progress. Applies facing (CommandIssuer::applyFacing)
    // for ground units the moment they first arrive, and shows the final
    // formatStatusReport() on the Defense HUD once every tracked unit is
    // done (Ok/timeout/etc.).
    //
    // Safe to call from Kenshi's per-frame update: that hook runs on the same
    // thread that pumps MyGUI, so the status-text update it may trigger is
    // UI-safe (see DefenseButton::setStatusText).
    void onTick(float dt);

    // Abandons any in-progress Return without reporting, discarding the
    // tracked characters and their elapsed timeouts. Must be called whenever
    // the world those characters belong to is replaced (save loaded, new game
    // started) — see Persistence::install's onWorldReset callback.
    void abortReturn();
}
