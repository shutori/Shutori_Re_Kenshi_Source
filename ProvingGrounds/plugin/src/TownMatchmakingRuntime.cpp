#include "TownMatchmakingRuntime.h"
#include "ArenaCombatProfile.h"
#include "FighterIdentity.h"
#include "LeaderboardStore.h"
#include "TownFighterCatalog.h"
#include "TownChallengePolicy.h"
#include "TownArena.h"
#include "TownAftercare.h"
#include "TownLimbShop.h"
#include "TownArenaRuntimePolicy.h"
#include <kenshi/Building/Building.h>
#include <kenshi/Character.h>
#include <kenshi/GameData.h>
#include <kenshi/util/hand.h>
#include <kenshi/GameWorld.h>
#include <kenshi/Globals.h>
#include <algorithm>
#include <sstream>
#include <iomanip>
namespace TownMatchmakingRuntime {
    std::string Identity(Character* c) {
        if (!c || !c->isValid()) return "";
        std::string id;
        return FighterIdentity::Find(c, id) ? id : std::string();
    }
    std::string Role(Character* c) {
        GameData* data = c && c->isValid() ? c->getGameData() : NULL;
        return data ? data->stringID : "";
    }
    double Ability(Character* c) {
        return ArenaCombatProfile::Evaluate(ArenaCombatProfile::Read(c));
    }
    std::string Label(Character* c) {
        if (!c || !c->isValid()) return "Unavailable";
        const TownFighterCatalog::Entry* entry = TownFighterCatalog::Find(Role(c).c_str());
        return c->getName() + (entry ? std::string(" (") + TownChallengePolicy::DivisionName(entry->tier) +
            " " + entry->type + ")" : std::string());
    }
    Character* Resolve(const Snapshot& s, const std::string& id) {
        std::map<std::string, Character*>::const_iterator found = s.characters.find(id);
        if (found == s.characters.end() || !found->second || Identity(found->second) != id) return NULL;
        return found->second;
    }
    static bool ById(const TownMatchmakingPolicy::Fighter& a, const TownMatchmakingPolicy::Fighter& b) {
        return a.id < b.id;
    }
    static TownMatchmakingPolicy::Fighter Fighter(Character* c) {
        TownMatchmakingPolicy::Fighter f;
        f.id = Identity(c); f.role = Role(c); f.ability = Ability(c);
        f.mmr = LeaderboardStore::GetRating(c, LeaderboardData::Town);
        return f;
    }
    std::string SelectionIdentity(Character* c) {
        if (!c || !c->isValid()) return "";
        const hand actor(c);
        if (actor.getCharacter() != c) return "";
        return TownArenaRuntimePolicy::PlayerSelectionKey(static_cast<unsigned>(actor.type),
            actor.container, actor.containerSerial, actor.index, actor.serial);
    }
    Snapshot Collect(Building* registry, const std::vector<Character*>& players, ReadyCheck ready, bool includeNamed) {
        Snapshot s;
        s.availability = "Town registry unavailable";
        s.diagnostics = "registry/world/town/readiness callback unavailable";
        if (!ou || !registry || !registry->isValid() || !registry->getTown() || !ready) return s;
        lektor<RootObject*> objects;
        ou->getObjectsWithinSphere(objects, registry->getPosition(), 2500.0f, CHARACTER, 512, NULL);
        int inTown = 0, missingIdentity = 0, ambiguous = 0;
        std::map<std::string, int> roles, blockers;
        TownArenaRuntimePolicy::DiscoveryIndex<Character> discovery;
        // Resolve ambiguity before readiness filtering: an injured namesake still collides.
        for (uint32_t i = 0; i < objects.size(); ++i) {
            Character* c = static_cast<Character*>(objects[i]);
            if (!c || !c->isValid() || c->getCurrentTownLocation() != registry->getTown()) continue;
            const std::string role = Role(c);
            const bool named = TownChallengePolicy::NamedRoleDivision(role) >= 0;
            const bool opponent = TownFighterCatalog::Find(role.c_str()) ||
                (includeNamed && named);
            std::string id;
            if (opponent) FighterIdentity::GetOrCreate(c, id);
            // Remove old unused Marks rows as known town fighters are discovered.
            // Never infer NPC ownership from an absent/unloaded player character.
            if (opponent || named)
                LeaderboardStore::RemoveTownNpcProgression(c);
            if (!discovery.Add(c, id)) continue;
            ++inTown;
            ++roles[Role(c)];
            if (id.empty()) ++missingIdentity;
        }
        s.characters.swap(discovery.byIdentity);
        for (std::map<std::string, Character*>::const_iterator it = s.characters.begin(); it != s.characters.end(); ++it)
            if (!it->second) ++ambiguous;
        s.playersReady = !players.empty() && players.size() <= 3;
        for (size_t i = 0; i < players.size(); ++i) {
            Character* c = players[i];
            std::string playerReason;
            const std::string selectionId = SelectionIdentity(c);
            if (!c || !c->isValid()) playerReason = "selected character is unavailable";
            else if (c->getCurrentTownLocation() != registry->getTown()) playerReason = "return to the registry town";
            else if (!ready(c, false, true, &playerReason)) {
                if (playerReason.empty()) playerReason = "not ready to fight";
            }
            else if (TownAftercare::IsManaged(c)) playerReason = "still in aftercare";
            else if (selectionId.empty()) playerReason = "live character handle unavailable";
            else if (!(Ability(c) > 0)) playerReason = "combat stats unavailable";
            for (size_t j = 0; j < i; ++j) if (players[j] == c) playerReason = "same fighter selected more than once";
            if (!playerReason.empty()) {
                ++blockers["selected player: " + playerReason];
                if (s.playerBlocker.empty()) s.playerBlocker = c && c->isValid() ?
                    c->getName() + ": " + playerReason : playerReason;
                s.playersReady = false; continue;
            }
            TownMatchmakingPolicy::Fighter fighter = Fighter(c);
            fighter.id = selectionId;
            s.players.push_back(fighter);
        }
        for (size_t i = 0; i < discovery.actors.size(); ++i) {
            Character* c = discovery.actors[i];
            const std::string role = Role(c);
            const bool medic = TownArenaRuntimePolicy::IsMedic(role.c_str());
            const bool named = TownChallengePolicy::NamedRoleDivision(role) >= 0;
            if (!medic && !TownFighterCatalog::Find(role.c_str()) && !(includeNamed && named)) continue;
            if (TownAftercare::IsManaged(c) || TownLimbShop::IsManaged(c) || TownArena::IsFighter(c)) {
                ++blockers[role + ": active bout/recovery ownership"]; continue;
            }
            std::string reason;
            if (medic) {
                if (c->isCarryingSomething || c->isBeingCarried())
                    ++blockers[role + ": carrying/being carried"];
                else if (ready(c, true, false, &reason)) s.medics.push_back(c);
                else ++blockers[role + ": " + reason];
                continue;
            }
            if (Resolve(s, Identity(c)) != c) {
                ++blockers[role + ": missing/ambiguous opponent identity"]; continue;
            }
            if (!ready(c, false, false, &reason)) { ++blockers[role + ": " + reason]; continue; }
            if (!(Ability(c) > 0)) { ++blockers[role + ": invalid ability stats"]; continue; }
            s.npcs.push_back(Fighter(c));
        }
        // Preserve stable selector/context order even if spatial query order changes.
        std::sort(s.npcs.begin(), s.npcs.end(), ById);
        std::ostringstream availability, diagnostic;
        availability << s.npcs.size() << " eligible opponents, " << s.medics.size() << " available medics";
        s.availability = availability.str();
        diagnostic << s.availability << "; scanned=" << objects.size() << " inTown=" << inTown
            << " missingIdentity=" << missingIdentity << " ambiguousKeys=" << ambiguous
            << " selected=" << players.size() << " eligiblePlayers=" << s.players.size()
            << " playersReady=" << s.playersReady << "; roles=";
        int diagnosticRows = 0;
        for (std::map<std::string, int>::const_iterator it = roles.begin(); it != roles.end() && diagnosticRows < 64; ++it, ++diagnosticRows)
            diagnostic << '[' << it->first << '=' << it->second << ']';
        diagnostic << "; rejected=";
        diagnosticRows = 0;
        for (std::map<std::string, int>::const_iterator it = blockers.begin(); it != blockers.end() && diagnosticRows < 64; ++it, ++diagnosticRows)
            diagnostic << '[' << it->first << '=' << it->second << ']';
        s.diagnostics = diagnostic.str();
        std::sort(s.players.begin(), s.players.end(), ById);
        std::ostringstream context;
        context << std::setprecision(17) << s.playersReady << ':' << s.medics.size();
        for (size_t i = 0; i < s.players.size(); ++i)
            context << "|P" << s.players[i].id.size() << ':' << s.players[i].id << ':' <<
                s.players[i].ability << ':' << s.players[i].mmr;
        for (size_t i = 0; i < s.npcs.size(); ++i)
            context << "|N" << s.npcs[i].id.size() << ':' << s.npcs[i].id << ':' << s.npcs[i].role << ':' <<
                s.npcs[i].ability << ':' << s.npcs[i].mmr;
        // Two independent 32-bit hashes keep persisted preview context bounded.
        const std::string raw = context.str();
        unsigned first = 2166136261u, second = 5381u;
        for (size_t i = 0; i < raw.size(); ++i) {
            first = (first ^ static_cast<unsigned char>(raw[i])) * 16777619u;
            second = (second * 33u) ^ static_cast<unsigned char>(raw[i]);
        }
        std::ostringstream digest; digest << first << ':' << second;
        s.context = digest.str();
        return s;
    }
}
