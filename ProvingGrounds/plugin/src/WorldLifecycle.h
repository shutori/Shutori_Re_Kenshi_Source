#pragma once

namespace WorldLifecycle
{
    bool InstallHooks();
    void Tick();
    bool IsArenaOperationBusy();
    void AbandonWorldState();
}
