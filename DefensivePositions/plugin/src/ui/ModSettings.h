#pragma once

// Options → MODS settings for Defensive Positions.
// Persists to DefensivePositions.cfg next to the plugin DLL.
namespace ModSettings
{
    // Loads config and hooks OptionsWindow::update so the MODS tab UI
    // is injected whenever Options is open.
    void install();

    // When false, the HUD DEF button is hidden/removed.
    // Default: true.
    bool showGuiButton();
}
