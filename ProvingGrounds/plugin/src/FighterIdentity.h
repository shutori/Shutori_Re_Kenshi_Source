#pragma once
#include <string>

class Character;

namespace FighterIdentity
{
    // Fail closed until every required hook was installed. Partial installs
    // stay inert. PG_ENABLE_FIGHTER_IDENTITY=0 disables the candidate adapter.
    bool InstallHooks();
    // Synchronized, sticky adapter health. Reading never clears live bindings.
    bool CanPersist();
    bool GetOrCreate(Character* character, std::string& id);
    bool Find(Character* character, std::string& id);

    // Call at transition BEGIN, before old objects are discarded or new
    // characters load. Never call at transition completion: loaded bindings
    // must survive world activation. This function dereferences no characters.
    void AbandonWorldState();
}
