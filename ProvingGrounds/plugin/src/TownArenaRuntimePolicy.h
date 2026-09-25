#pragma once
#include <cstring>
#include <cmath>
#include <algorithm>
#include <map>
#include <string>
#include <sstream>
#include <vector>
#include "TownFighterCatalog.h"

namespace TownArenaRuntimePolicy
{
    // Player reservations hold live handles and are cancelled on load. Their
    // preview keys must distinguish namesakes without requiring saved NPC IDs.
    inline std::string PlayerSelectionKey(unsigned type, unsigned container, unsigned containerSerial,
        unsigned index, unsigned serial) {
        std::ostringstream key;
        key << "player:" << type << ':' << container << ':' << containerSerial << ':' << index << ':' << serial;
        return key.str();
    }
    // Medical staff are distinct live actors; only booked opponent lookup needs
    // an unambiguous persistent key. Namesake staff must not disappear with it.
    template<class Actor> struct DiscoveryIndex {
        std::vector<Actor*> actors;
        std::map<std::string, Actor*> byIdentity;
        bool Add(Actor* actor, const std::string& key) {
            if (!actor || std::find(actors.begin(), actors.end(), actor) != actors.end()) return false;
            actors.push_back(actor);
            if (!key.empty()) {
                typename std::map<std::string, Actor*>::iterator found = byIdentity.find(key);
                if (found == byIdentity.end()) byIdentity[key] = actor;
                else found->second = NULL;
            }
            return true;
        }
    };
    inline void MedicStandbyOffset(float dx, float dz, float& x, float& z)
    {
        const float length = std::sqrt(dx * dx + dz * dz);
        x = length > 1.0f ? dx * 190.0f / length : 190.0f;
        z = length > 1.0f ? dz * 190.0f / length : 0.0f;
    }
    inline bool RetainedIncidentPair(bool retained, int a, int b)
    {
        return retained && ((a == 0 && b == 1) || (a == 1 && b == 0));
    }
    inline bool ExemptArenaIncident(bool townMatch, bool opponents, bool combatIncident)
    {
        return townMatch && opponents && combatIncident;
    }
    inline float AdvanceAftercare(float elapsed, float dt, bool paused)
    {
        return !paused && dt > 0.0f ? elapsed + dt : elapsed;
    }
    inline bool AftercareExpired(float elapsed) { return elapsed >= 300.0f; }
    inline bool Equals(const char* id, const char* expected)
    {
        return id && std::strcmp(id, expected) == 0;
    }
    inline bool IsNewArena(const char* id)
    {
        return Equals(id, "183-Proving Grounds.mod");
    }
    inline bool IsTownRegistry(const char* id)
    {
        return Equals(id, "195-Proving Grounds.mod");
    }
    inline bool IsRegularFighter(const char* id)
    {
        return TownFighterCatalog::Find(id) != NULL;
    }
    inline bool IsMedic(const char* id)
    {
        return Equals(id, "100-Proving Grounds.mod");
    }
    struct Readiness
    {
        bool valid, player, dead, unconscious, inCombat, needsAid;
        float minimumHealth, blood;
    };
    inline bool CanParticipate(const Readiness& r)
    {
        return r.valid && !r.player && !r.dead && !r.unconscious &&
            !r.inCombat && !r.needsAid && r.minimumHealth >= 0.9f && r.blood >= 0.9f;
    }
    // Used both while gathering and at ingress's final combat handoff.
    // Cancellation owns ingress cleanup and the existing ticket refund path.
    template<class Preparation>
    bool GuardPreparation(bool npcPreparation, Preparation& preparation) {
        if (!npcPreparation || preparation.Ready()) return true;
        preparation.Cancel();
        return false;
    }
    // Medics work outside combat; use native medical capacity instead of
    // requiring the fighter's 90% blood reserve.
    inline bool CanProvideAid(const Readiness& r, bool medicallyAble)
    {
        return r.valid && !r.player && !r.dead && !r.unconscious &&
            !r.inCombat && !r.needsAid && medicallyAble;
    }
    inline bool IsolatePair(bool managedA, bool managedB, bool opponents)
    {
        return !opponents && (managedA || managedB);
    }
}
