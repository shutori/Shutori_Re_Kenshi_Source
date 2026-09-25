#pragma once

class ForgottenGUI;

namespace ContextMenuHooks
{
    bool Install();
    void Tick(ForgottenGUI* gui);
    void AbandonWorldState();
    void RefreshArenaCursor(ForgottenGUI* gui);
}
