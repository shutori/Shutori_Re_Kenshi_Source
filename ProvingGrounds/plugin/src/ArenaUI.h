#pragma once

class RootObject;
class Character;

namespace ArenaUI
{
    bool InstallHooks();
    void ShowFromRegistry(RootObject* registry, bool bindRegistry = true);
    // Fighter currently highlighted in the Arena panel (opener / last clicked).
    Character* GetPreviewCharacter();
    // Clear world pointers retained by setup, rows, presets, and drag state.
    void AbandonWorldState();
}
