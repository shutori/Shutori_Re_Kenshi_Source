#pragma once
namespace ArenaNativePersistence {
    bool InstallHooks();
    void Disable();
    void AbandonWorld();
    bool IsLoading();
    bool IsReady();
    void Tick();
}
