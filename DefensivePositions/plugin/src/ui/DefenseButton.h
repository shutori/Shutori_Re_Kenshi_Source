#pragma once

#include <string>

// HUD Defense button — single compact DEF control above the speed controls.
//
// Widgets are created lazily once MainBarGUI exists on Kenshi's UI thread
// (see DefenseButton.cpp). install() only records the callbacks and registers
// the hook; it is safe to call from plugin startup.
namespace DefenseButton
{
    typedef void (*Callback)();

    // Registers callbacks and hooks UI construction so the button gets built
    // on the UI thread. onReturn fires on LMB click, onSave on RMB, onDiscard
    // on MMB.
    void install(Callback onSave, Callback onReturn, Callback onDiscard);

    // Idempotent: creates the DEF button once MainBarGUI is present,
    // unless ModSettings has disabled the GUI. Safe to call every frame
    // (from the GameWorld tick hook).
    void ensureCreated();

    // Shows or destroys the DEF button to match ModSettings::showGuiButton().
    // Call after the Options → MODS toggle changes.
    void applyGuiVisibility();

    // Updates the hover tooltip (and optional debug status panel). Empty text
    // restores the default "LMB Return | MMB Discard | RMB Save" help. Always
    // mirrors the text to the RE_Kenshi debug log. Must be called on the UI
    // thread.
    void setStatusText(const std::string& text);
}
