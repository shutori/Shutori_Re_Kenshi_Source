#pragma once
#include <cstring>

namespace TownFighterCatalog {
struct Entry {
    const char* characterId;
    const char* squadId;
    const char* type;
    int tier; // Easy=0, Medium=1, Hard=2; template metadata, never ability.
    int count; // Expected spawns only; not an availability requirement.
};
inline const Entry* Find(const char* characterId) {
    static const Entry entries[] = {
        { "219-Proving Grounds.mod", "221-Proving Grounds.mod", "Polearm", 0, 2 },
        { "223-Proving Grounds.mod", "222-Proving Grounds.mod", "Polearm", 1, 2 },
        { "225-Proving Grounds.mod", "224-Proving Grounds.mod", "Polearm", 2, 2 },
        { "206-Proving Grounds.mod", "208-Proving Grounds.mod", "Sabre", 0, 3 },
        { "96-Proving Grounds.mod", "209-Proving Grounds.mod", "Sabre", 1, 3 },
        { "207-Proving Grounds.mod", "105-Proving Grounds.mod", "Sabre", 2, 3 },
        { "213-Proving Grounds.mod", "214-Proving Grounds.mod", "Katana", 0, 3 },
        { "97-Proving Grounds.mod", "106-Proving Grounds.mod", "Katana", 1, 3 },
        { "210-Proving Grounds.mod", "211-Proving Grounds.mod", "Katana", 2, 3 },
        { "218-Proving Grounds.mod", "217-Proving Grounds.mod", "Champion", 0, 2 },
        { "98-Proving Grounds.mod", "107-Proving Grounds.mod", "Champion", 1, 2 },
        { "216-Proving Grounds.mod", "215-Proving Grounds.mod", "Champion", 2, 2 },
        { "232-Proving Grounds.mod", "231-Proving Grounds.mod", "Mace", 0, 2 },
        { "230-Proving Grounds.mod", "229-Proving Grounds.mod", "Mace", 1, 2 },
        { "227-Proving Grounds.mod", "226-Proving Grounds.mod", "Mace", 2, 2 },
        { "238-Proving Grounds.mod", "237-Proving Grounds.mod", "Greatsword", 0, 2 },
        { "240-Proving Grounds.mod", "239-Proving Grounds.mod", "Greatsword", 1, 2 },
        { "236-Proving Grounds.mod", "235-Proving Grounds.mod", "Greatsword", 2, 2 },
        { "298-Proving Grounds.mod", "297-Proving Grounds.mod", "Martial", 0, 3 },
        { "302-Proving Grounds.mod", "299-Proving Grounds.mod", "Martial", 1, 3 },
        { "300-Proving Grounds.mod", "301-Proving Grounds.mod", "Martial", 2, 3 },
        // Second-generation Scratch Pit Fighters (FCS character ids in the 6xx
        // range). Each logical fighter exists as a name-identical PAIR in the
        // .mod: a character record and the squad template that spawns it, the
        // same way 232 (character) pairs with 231 (squad) above.
        //
        // Only a CHARACTER template can ever appear as a live fighter's role, so
        // an entry naming a squad id is inert rather than harmful -- but an entry
        // naming the wrong id would leave the real fighter unclassified, which
        // also drops it out of the roster (TownMatchmakingRuntime). The pairs
        // below are therefore listed on evidence, not on parity:
        //   666, 670, 673, 675, 677, 679, 681 -- confirmed CHARACTERS: each appears
        //     as a member of a squad record's member list, and across 14 such lists
        //     no catalog squad id ever appeared as a member.
        //   665, 678 -- supplied by the author but not seen in any member list.
        //   669, 672, 674, 676, 680 -- their pair partners, not yet confirmed
        //     either way (a character can legitimately sit in no squad).
        // All four of the 665/666 and 678/679 pair members are listed so whichever
        // is the character is covered; the dead ones are inert. The next capture's
        // `template` field says which actually spawned, and the dead entries can
        // then be pruned.
        //
        // tier is the FCS difficulty (Easy=0, Medium=1, Hard=2). Every "Champion
        // hard <style>" entry takes type "Champion" -- the style in the name is the
        // weapon the character carries, and a live fighter's `style` is read from
        // the equipped weapon at runtime (see ArenaCombatProfile), not from here.
        { "666-Proving Grounds.mod", "665-Proving Grounds.mod", "Champion", 2, 2 },
        { "665-Proving Grounds.mod", "666-Proving Grounds.mod", "Champion", 2, 2 },
        { "670-Proving Grounds.mod", "669-Proving Grounds.mod", "Champion", 2, 2 },
        { "673-Proving Grounds.mod", "672-Proving Grounds.mod", "Champion", 2, 2 },
        { "681-Proving Grounds.mod", "680-Proving Grounds.mod", "Champion", 2, 2 },
        // Hackers. "Hacker" is a NEW catalog type, so Detail::TypeIndex lists it
        // and the diagnostic's type round-robin counts eight, not seven -- see the
        // note there. Without that the diagnostic can never target a Hacker bout.
        { "675-Proving Grounds.mod", "674-Proving Grounds.mod", "Hacker", 0, 2 },
        { "677-Proving Grounds.mod", "676-Proving Grounds.mod", "Hacker", 1, 2 },
        { "679-Proving Grounds.mod", "678-Proving Grounds.mod", "Hacker", 2, 2 },
        { "678-Proving Grounds.mod", "679-Proving Grounds.mod", "Hacker", 2, 2 }
    };
    if (!characterId) return NULL;
    // Derived, never a literal: this used to read `i < 21` and silently ignored
    // every entry added after the original set.
    const int count = static_cast<int>(sizeof(entries) / sizeof(entries[0]));
    for (int i = 0; i < count; ++i)
        if (std::strcmp(characterId, entries[i].characterId) == 0) return &entries[i];
    return NULL;
}
}
