#pragma once

class OptionsWindow;

namespace PGSettingsUI
{
    void InjectModsTabUI(OptionsWindow* options);
    void Toggle();
    bool IsVisible();
    void Close();
    void AbandonWorldState();
}
