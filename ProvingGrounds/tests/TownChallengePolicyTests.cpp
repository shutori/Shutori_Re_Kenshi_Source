#include "../src/TownChallengePolicy.h"
#include "../src/TownMatchmakingPolicy.h"
#include <cassert>

int main()
{
    // A refresh must never leave the whole card row in one match mode.
    TownChallengePolicy::Card mixed;
    mixed.seed = 1;
    TownChallengePolicy::RefreshGenerated(mixed, 12, -1);
    bool hasTeams = false, hasTeams1v1 = false;
    for (int i = 0; i < 5; ++i) {
        hasTeams = hasTeams || mixed.offers[i].mode == TownChallengePolicy::ModeTeams;
        hasTeams1v1 = hasTeams1v1 || mixed.offers[i].mode == TownChallengePolicy::ModeTeams1v1;
    }
    assert(hasTeams);
    assert(hasTeams1v1);

    TownChallengePolicy::Card pinned;
    pinned.seed = 7;
    pinned.offers[2].mode = TownChallengePolicy::ModeTeams1v1;
    TownChallengePolicy::RefreshGenerated(pinned, 13, 2);
    hasTeams = hasTeams1v1 = false;
    for (int i = 0; i < 5; ++i) {
        hasTeams = hasTeams || pinned.offers[i].mode == TownChallengePolicy::ModeTeams;
        hasTeams1v1 = hasTeams1v1 || pinned.offers[i].mode == TownChallengePolicy::ModeTeams1v1;
    }
    assert(hasTeams);
    assert(hasTeams1v1);

    // Exactly half of Legendary outcomes select the division's unique pool.
    for (int division = 0; division < 3; ++division) {
        int unique = 0;
        for (unsigned roll = 0; roll < 8; ++roll) {
            const int encounter = TownChallengePolicy::LegendaryEncounter(division, roll, false);
            unique += TownChallengePolicy::NamedForDivision(encounter, division) ? 1 : 0;
        }
        assert(unique == 4);
    }

    // The gate requires all five non-Skarn unique victories; legacy milestones do not unlock it.
    TownChallengePolicy::Card progress;
    progress.milestones = 7;
    assert(!TownChallengePolicy::SkarnUnlocked(progress));
    TownChallengePolicy::RecordVictory(progress, true, true,
        TownChallengePolicy::UniqueRessaVane | TownChallengePolicy::UniqueBumPer |
        TownChallengePolicy::UniqueSenn | TownChallengePolicy::UniqueTorka);
    assert(!TownChallengePolicy::SkarnUnlocked(progress));
    TownChallengePolicy::RecordVictory(progress, true, true, TownChallengePolicy::UniqueVeyr);
    assert(TownChallengePolicy::SkarnUnlocked(progress));

    // Once unlocked, Skarn occupies one of the four Hard unique outcomes.
    int skarnBefore = 0, skarnAfter = 0;
    for (unsigned roll = 0; roll < 8; ++roll) {
        skarnBefore += TownChallengePolicy::LegendaryEncounter(2, roll, false) == TownChallengePolicy::EncounterSkarn;
        skarnAfter += TownChallengePolicy::LegendaryEncounter(2, roll, true) == TownChallengePolicy::EncounterSkarn;
    }
    assert(skarnBefore == 0);
    assert(skarnAfter == 1);

    // Runtime opponent roles map only the five prerequisite fighters into progress.
    assert(TownChallengePolicy::UniqueVictoryForEncounter(TownChallengePolicy::EncounterRessaVane) == TownChallengePolicy::UniqueRessaVane);
    assert(TownChallengePolicy::UniqueVictoryForEncounter(TownChallengePolicy::EncounterBumPer) == TownChallengePolicy::UniqueBumPer);
    assert(TownChallengePolicy::UniqueVictoryForEncounter(TownChallengePolicy::EncounterSenn) == TownChallengePolicy::UniqueSenn);
    assert(TownChallengePolicy::UniqueVictoryForEncounter(TownChallengePolicy::EncounterTorka) == TownChallengePolicy::UniqueTorka);
    assert(TownChallengePolicy::UniqueVictoryForEncounter(TownChallengePolicy::EncounterVeyr) == TownChallengePolicy::UniqueVeyr);
    assert(TownChallengePolicy::UniqueVictoryForEncounter(TownChallengePolicy::EncounterSkarn) == 0u);

    // Named encounters must anchor to the live character record IDs from the town registry.
    TownMatchmakingPolicy::Fighter player;
    player.id = "player"; player.ability = 50; player.mmr = 100;
    std::vector<TownMatchmakingPolicy::Fighter> players(1, player);
    TownMatchmakingPolicy::History noHistory;
    struct NamedOpponent { int encounter, division; const char* role; const char* id; double ability; };
    const NamedOpponent namedOpponents[] = {
        { TownChallengePolicy::EncounterRessaVane, TownMatchmakingPolicy::Easy,
            "616-Proving Grounds.mod", "ressa", 55 },
        { TownChallengePolicy::EncounterBumPer, TownMatchmakingPolicy::Medium,
            "619-Proving Grounds.mod", "bum-per", 60 },
        { TownChallengePolicy::EncounterVeyr, TownMatchmakingPolicy::Hard,
            "623-Proving Grounds.mod", "veyr", 68 }
    };
    for (int i = 0; i < 3; ++i) {
        assert(std::string(TownChallengePolicy::Role(namedOpponents[i].encounter)) == namedOpponents[i].role);
        TownMatchmakingPolicy::Fighter npc;
        npc.id = namedOpponents[i].id; npc.role = namedOpponents[i].role;
        npc.ability = namedOpponents[i].ability; npc.mmr = 100;
        std::vector<TownMatchmakingPolicy::Fighter> npcs(1, npc);
        const TownMatchmakingPolicy::Match match = TownMatchmakingPolicy::SelectPlayer(
            players, npcs, static_cast<TownMatchmakingPolicy::Division>(namedOpponents[i].division),
            1, noHistory, 3, 6, TownChallengePolicy::Role(namedOpponents[i].encounter), 3);
        assert(match.valid);
        assert(match.b.size() == 1 && match.b[0] == namedOpponents[i].id);
    }

    // The new progress survives the existing compact persistence format.
    const std::string encoded = TownChallengePolicy::Encode(progress);
    TownChallengePolicy::Card decoded;
    assert(TownChallengePolicy::Decode(encoded, decoded));
    assert(decoded.uniqueWins == 31u);
    assert(TownChallengePolicy::SkarnUnlocked(decoded));

    // Compact version 4 saves migrate with no invented unique victories.
    const std::string current = TownChallengePolicy::EncodeLegacy(TownChallengePolicy::Card());
    const size_t first = current.find(' '), second = current.find(' ', first + 1);
    const size_t third = current.find(' ', second + 1), fourth = current.find(' ', third + 1);
    const std::string legacy = "4" + current.substr(first, third - first) + current.substr(fourth);
    TownChallengePolicy::Card migrated;
    assert(TownChallengePolicy::DecodeLegacy(legacy, migrated));
    assert(migrated.uniqueWins == 0u);
    assert(!TownChallengePolicy::SkarnUnlocked(migrated));

    // Persisted or manually edited cards cannot bypass the five-victory gate.
    TownChallengePolicy::Card lockedSkarn;
    lockedSkarn.division = 2;
    lockedSkarn.offers[0].generated = true;
    lockedSkarn.offers[0].division = 2;
    lockedSkarn.offers[0].encounter = TownChallengePolicy::EncounterSkarn;
    lockedSkarn.offers[0].epoch = 1;
    lockedSkarn.offers[0].playerCount = 1;
    lockedSkarn.offers[0].enemyCount = 1;
    lockedSkarn.offers[0].context = "lineup";
    lockedSkarn.offers[0].opponent = "skarn";
    lockedSkarn.offers[0].roles[0] = TownChallengePolicy::Role(TownChallengePolicy::EncounterSkarn);
    assert(!TownChallengePolicy::CanBookDivision(lockedSkarn, 0));
    TownChallengePolicy::Card rejected;
    assert(!TownChallengePolicy::Decode(TownChallengePolicy::Encode(lockedSkarn), rejected));
    TownChallengePolicy::Card unlockedSkarn = lockedSkarn;
    unlockedSkarn.uniqueWins = TownChallengePolicy::AllUniqueVictories;
    assert(TownChallengePolicy::CanBookDivision(unlockedSkarn, 0));
    TownChallengePolicy::Card accepted;
    assert(TownChallengePolicy::Decode(TownChallengePolicy::Encode(unlockedSkarn), accepted));
    assert(accepted.offers[0].encounter == TownChallengePolicy::EncounterSkarn);
    return 0;
}
