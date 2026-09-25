#include "TownSpectators.h"
#include "TownSpectatorPolicy.h"
#include "TownFurniturePolicy.h"
#include "TownArenaPolicy.h"
#include "TownArenaRuntimePolicy.h"
#include "TownArena.h"
#include "SparSession.h"
#include "SparPodium.h"
#include "ArenaIdentity.h"
#include "ArenaMedical.h"
#include "ArenaPerformance.h"
#include "PGConfig.h"
#include "PGLog.h"
#include <kenshi/Character.h>
#include <kenshi/CharMovement.h>
#include <kenshi/GameData.h>
#include <kenshi/GameWorld.h>
#include <kenshi/Globals.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Inventory.h>
#include <kenshi/Building/UseableStuff.h>
#include <kenshi/util/hand.h>
#include <Windows.h>
#include <vector>
#include <string>
#include <cstdio>

namespace {
    struct Guest {
        hand actor, seat;
        bool hold, passive, seated;
        MoveSpeed speed;
        TaskType task;
        float lastOrder;
        Guest() : hold(false), passive(false), seated(false), speed(WALK), task(MOVE_CUS_ORDERED), lastOrder(-3) {}
    };
    std::vector<Guest> guests;
    std::vector<Guest> pendingReleases;
    struct ChatterCue {
        hand speaker;
        float due;
        ChatterCue() : due(0.0f) {}
    };
    std::vector<ChatterCue> chatterCues;
    hand activeArena;
    hand lastChatterSpeaker;
    float elapsed = 0;
    float nextChatterCycle = 0;
    DWORD lastTick = 0;
    DWORD lastMaintenance = 0;
    unsigned nextAudience = 0;
    unsigned chatterSeed = 0;
    bool chatterActive = false;
    ArenaPerformance::Policy activePolicy;

    bool Player(Character* c) {
        if (!ou || !ou->player) return true;
        const lektor<Character*>& all = ou->player->getAllPlayerCharacters();
        for (uint32_t i = 0; i < all.size(); ++i) if (all[i] == c) return true;
        return false;
    }
    bool Healthy(Character* c) {
        return c && c->isValid() && !c->isDead() && !c->isUnconcious() &&
            !c->isInCombatMode(true, true) && !ArenaMedical::NeedsStabilization(c) &&
            !c->isBeingCarried() && !c->isCarryingSomething;
    }
    bool Occupied(UseableStuff* seat) {
        Character* occupant = seat ? seat->getOccupant().getCharacter() : NULL;
        return seat && TownFurniturePolicy::Occupied(seat->getOccupant().isValid(), occupant != NULL, occupant && occupant->isValid());
    }
    UseableStuff* Seat(Building* b) {
        if (!b || !b->isValid() || !TownArenaRuntimePolicy::Equals(ArenaIdentity::GetStringId(b), "192-Proving Grounds.mod") ||
            b->getSpecialFunction() != BF_CHAIR) return NULL;
        Building::ConstructionState* build = b->getBuildState();
        if (build && !build->isComplete) return NULL;
        UseableStuff* seat = b->getUseableStuff();
        // Chair furniture can itself be the native UseableStuff object, as
        // with the already established cage adapter.
        if (!seat) seat = reinterpret_cast<UseableStuff*>(b);
        return seat->numOperatorsMax == 1 && !seat->isDisabled() ? seat : NULL;
    }
    void ResumeNativeAI(Character* c) {
        c->addOrder(NULL, MOVE_CUS_ORDERED, NULL, false, true, c->getPosition());
        c->removeJob(MOVE_CUS_ORDERED);
        c->clearAllAIGoals();
        c->reThinkCurrentAIAction();
    }
    TownSpectatorPolicy::ReleaseDisposition ReleaseDisposition(Character* c) {
        if (!c || !c->isValid()) return TownSpectatorPolicy::ForgetRelease;
        return TownSpectatorPolicy::ReleaseFor(true, c->isDead(), Player(c), Healthy(c));
    }
    void ReleaseGuest(Guest& guest) {
        Character* c = guest.actor.getCharacter();
        if (!c || !c->isValid()) return;
        UseableStuff* seat = Seat(guest.seat.getBuilding());
        if (seat && seat->getOccupant() == c) seat->stopOperating(guest.actor);
        c->removeJob(guest.task);
        c->setStandingOrder(MessageForB::M_SET_ORDER_HOLD, guest.hold);
        c->setStandingOrder(MessageForB::M_SET_ORDER_PASSIVE, guest.passive);
        CharMovement* movement = c->getMovement();
        if (movement) { movement->setDesiredSpeedOrders(guest.speed); movement->restoreDesiredSpeed(); }
        const TownSpectatorPolicy::ReleaseDisposition disposition = ReleaseDisposition(c);
        if (disposition == TownSpectatorPolicy::ResumeNativeNow) ResumeNativeAI(c);
        else if (disposition == TownSpectatorPolicy::DeferNativeResume) pendingReleases.push_back(guest);
    }
    void TickPendingReleases() {
        for (size_t i = 0; i < pendingReleases.size();) {
            Character* c = pendingReleases[i].actor.getCharacter();
            const TownSpectatorPolicy::ReleaseDisposition disposition = ReleaseDisposition(c);
            if (disposition == TownSpectatorPolicy::DeferNativeResume) { ++i; continue; }
            if (disposition == TownSpectatorPolicy::ResumeNativeNow) ResumeNativeAI(c);
            pendingReleases.erase(pendingReleases.begin() + i);
        }
    }
    bool ReleasePending(Character* c) {
        for (size_t i = 0; i < pendingReleases.size(); ++i)
            if (pendingReleases[i].actor == c) return true;
        return false;
    }
    bool Invite(Character* c, const std::vector<UseableStuff*>& seats, int audienceTarget) {
        if (!c || static_cast<int>(guests.size()) >= audienceTarget) return false;
        hand actor; actor = c;
        for (size_t j = 0; j < seats.size(); ++j) {
            UseableStuff* seat = seats[j]; bool reserved = false;
            for (size_t k = 0; k < guests.size(); ++k) if (guests[k].seat == seat) reserved = true;
            if (!TownSpectatorPolicy::CanReserve(!Occupied(seat) && seat->isFreeSlot(actor) &&
                seat->couldIOperate(actor), reserved, static_cast<int>(guests.size()), audienceTarget)) continue;
            Guest guest; guest.actor = c; guest.seat = seat;
            guest.hold = c->getStandingOrder(MessageForB::M_SET_ORDER_HOLD);
            guest.passive = c->getStandingOrder(MessageForB::M_SET_ORDER_PASSIVE);
            guest.speed = c->getMovementSpeedOrders(); guest.task = seat->getDefaultTask();
            c->setStandingOrder(MessageForB::M_SET_ORDER_HOLD, false);
            c->setStandingOrder(MessageForB::M_SET_ORDER_PASSIVE, false);
            c->clearAllAIGoals(); c->reThinkCurrentAIAction();
            guests.push_back(guest);
            return true;
        }
        return false;
    }
    unsigned ChatterRandom() {
        if (!chatterSeed) chatterSeed = static_cast<unsigned>(GetTickCount()) ^ 0x9e3779b9u;
        chatterSeed ^= chatterSeed << 13;
        chatterSeed ^= chatterSeed >> 17;
        chatterSeed ^= chatterSeed << 5;
        return chatterSeed;
    }
    void ResetChatter() {
        chatterCues.clear();
        lastChatterSpeaker.setNull();
        nextChatterCycle = 0.0f;
        chatterActive = false;
    }
    bool IsSeatedGuest(Character* character) {
        for (size_t i = 0; character && i < guests.size(); ++i) {
            if (guests[i].actor != character || !guests[i].seated) continue;
            UseableStuff* seat = Seat(guests[i].seat.getBuilding());
            return seat && seat->getOccupant() == character;
        }
        return false;
    }
    std::vector<Character*> ChatterFighters() {
        std::vector<Character*> fighters;
        if (!SparSession::IsActive()) return fighters;
        if (SparSession::GetMode() == MatchRules::ModeTeams1v1) {
            Character* active[] = { SparSession::GetActiveA(), SparSession::GetActiveB() };
            for (int i = 0; i < 2; ++i)
                if (active[i] && active[i]->isValid() && !active[i]->isDead() && !active[i]->isUnconcious())
                    fighters.push_back(active[i]);
            return fighters;
        }
        for (int i = 0; i < SparSession::GetParticipantCount(); ++i) {
            Character* fighter = SparSession::GetParticipant(i);
            if (fighter && fighter->isValid() && !fighter->isDead() && !fighter->isUnconcious() &&
                !SparSession::IsEliminated(fighter)) fighters.push_back(fighter);
        }
        return fighters;
    }
    std::string ChatterLine() {
        static const char* generic[] = {
            "Come on!", "Hit back!", "What a hit!", "Keep fighting!",
            "Stay on them!", "Don't let up!", "Now that's a fight!",
            "This is getting good!", "Get up!", "Finish it!"
        };
        std::vector<Character*> fighters = ChatterFighters();
        const unsigned type = ChatterRandom() % 10u;
        if (fighters.empty() || type < 4u)
            return generic[ChatterRandom() % (sizeof(generic) / sizeof(generic[0]))];
        Character* fighter = fighters[ChatterRandom() % fighters.size()];
        const std::string name = fighter->getName();
        if (type < 7u) {
            switch (ChatterRandom() % 5u) {
                case 0: return "Come on, " + name + "!";
                case 1: return name + " has this!";
                case 2: return "Finish it, " + name + "!";
                case 3: return "Stay on them, " + name + "!";
                default: return "Get back in it, " + name + "!";
            }
        }
        static const int stakes[] = { 100, 500, 1000 };
        char amount[16]; sprintf_s(amount, "%d", stakes[ChatterRandom() % 3u]);
        switch (ChatterRandom() % 4u) {
            case 0: return std::string("I put ") + amount + " Cats on " + name + "!";
            case 1: return std::string(amount) + " Cats says " + name + " takes it!";
            case 2: return "My Cats are on " + name + "!";
            default: return "Don't fail me, " + name + " - " + amount + " Cats!";
        }
    }
    std::vector<std::string> ResultWinners(const SparPodium::Snapshot& result) {
        std::vector<std::string> winners;
        for (int i = 0; i < result.fighterCount; ++i) {
            const SparPodium::FighterRow& fighter = result.fighters[i];
            const bool winningTeam =
                (result.outcome == SparPodium::OutcomeTeamAWins && fighter.team == MatchRules::TeamA) ||
                (result.outcome == SparPodium::OutcomeTeamBWins && fighter.team == MatchRules::TeamB);
            const bool lastStanding = result.outcome == SparPodium::OutcomeLastStanding &&
                fighter.id == result.lastStandingId;
            if ((winningTeam || lastStanding) && fighter.name[0]) winners.push_back(fighter.name);
        }
        return winners;
    }
    std::string ResultChatterLine(const SparPodium::Snapshot& result,
        const std::vector<std::string>& winners) {
        static const char* enjoyment[] = {
            "What a fight!", "Now that was worth watching!", "Give us another one!",
            "What a finish!", "Best fight all day!", "I'd pay to see that again!"
        };
        static const char* draw[] = {
            "Run it back!", "Again! We need a winner!", "Nobody won my Cats!",
            "You can't end it there!"
        };
        if (result.outcome == SparPodium::OutcomeDraw || winners.empty())
            return draw[ChatterRandom() % (sizeof(draw) / sizeof(draw[0]))];
        const std::string& winner = winners[ChatterRandom() % winners.size()];
        const unsigned type = ChatterRandom() % 10u;
        if (type < 3u)
            return enjoyment[ChatterRandom() % (sizeof(enjoyment) / sizeof(enjoyment[0]))];
        if (type < 6u) {
            switch (ChatterRandom() % 4u) {
                case 0: return winner + " earned that one!";
                case 1: return "What a finish from " + winner + "!";
                case 2: return "That's how it's done, " + winner + "!";
                default: return "I knew " + winner + " would take it!";
            }
        }
        static const int stakes[] = { 100, 500, 1000 };
        char amount[16]; sprintf_s(amount, "%d", stakes[ChatterRandom() % 3u]);
        switch (ChatterRandom() % 4u) {
            case 0: return winner + " just won me " + amount + " Cats!";
            case 1: return std::string("There go my ") + amount + " Cats...";
            case 2: return "Should've backed " + winner + "!";
            default: return "Never betting against " + winner + " again!";
        }
    }
    void ScheduleChatterCycle() {
        std::vector<Character*> seated;
        for (size_t i = 0; i < guests.size(); ++i) {
            Character* character = guests[i].actor.getCharacter();
            if (IsSeatedGuest(character)) seated.push_back(character);
        }
        const unsigned minMs = static_cast<unsigned>(activePolicy.chatterMinSeconds * 1000.0f);
        const unsigned maxMs = static_cast<unsigned>(activePolicy.chatterMaxSeconds * 1000.0f);
        const unsigned rangeMs = maxMs >= minMs ? maxMs - minMs : 0u;
        const float duration = static_cast<float>(minMs +
            (rangeMs ? ChatterRandom() % (rangeMs + 1u) : 0u)) / 1000.0f;
        nextChatterCycle = elapsed + duration;
        if (seated.empty()) return;
        size_t voices = (seated.size() + 24u) / 25u;
        const size_t voiceLimit = activePolicy.maxChatterVoices > 0 ?
            static_cast<size_t>(activePolicy.maxChatterVoices) : 0u;
        if (voices > voiceLimit) voices = voiceLimit;
        if (voices > seated.size()) voices = seated.size();
        if (voices == 0u) return;
        size_t start = ChatterRandom() % seated.size();
        if (seated.size() > 1u && lastChatterSpeaker == seated[start])
            start = (start + 1u) % seated.size();
        const size_t step = seated.size() / voices > 0u ? seated.size() / voices : 1u;
        const float spacing = duration / static_cast<float>(voices);
        for (size_t i = 0; i < voices; ++i) {
            ChatterCue cue;
            cue.speaker = seated[(start + i * step) % seated.size()];
            cue.due = elapsed + (static_cast<float>(i) + 0.5f) * spacing;
            chatterCues.push_back(cue);
        }
    }
    void TickChatter() {
        if (!activePolicy.chatterEnabled) return;
        if (!SparSession::IsActive()) {
            ResetChatter();
            return;
        }
        if (!chatterActive) {
            chatterActive = true;
            chatterSeed = static_cast<unsigned>(GetTickCount()) ^
                static_cast<unsigned>(guests.size() * 2654435761u);
            ScheduleChatterCycle();
        } else if (elapsed >= nextChatterCycle) {
            ScheduleChatterCycle();
        }
        for (size_t i = 0; i < chatterCues.size();) {
            if (elapsed < chatterCues[i].due) { ++i; continue; }
            Character* speaker = chatterCues[i].speaker.getCharacter();
            if (IsSeatedGuest(speaker)) {
                speaker->say(ChatterLine());
                lastChatterSpeaker = speaker;
            }
            chatterCues.erase(chatterCues.begin() + i);
        }
    }
}
namespace TownSpectators {
    void ReactToResult(const SparPodium::Snapshot& result) {
        if (!activePolicy.chatterEnabled || result.outcome == SparPodium::OutcomeStopped || guests.empty()) return;
        std::vector<Character*> seated;
        for (size_t i = 0; i < guests.size(); ++i) {
            Character* character = guests[i].actor.getCharacter();
            if (IsSeatedGuest(character)) seated.push_back(character);
        }
        if (seated.empty()) return;
        size_t voices = (seated.size() + 24u) / 25u;
        const size_t voiceLimit = activePolicy.maxChatterVoices > 0 ?
            static_cast<size_t>(activePolicy.maxChatterVoices) : 0u;
        if (voices > voiceLimit) voices = voiceLimit;
        if (voices > seated.size()) voices = seated.size();
        if (voices == 0u) return;
        const std::vector<std::string> winners = ResultWinners(result);
        size_t start = ChatterRandom() % seated.size();
        if (seated.size() > 1u && lastChatterSpeaker == seated[start])
            start = (start + 1u) % seated.size();
        const size_t step = seated.size() / voices > 0u ? seated.size() / voices : 1u;
        for (size_t i = 0; i < voices; ++i) {
            Character* speaker = seated[(start + i * step) % seated.size()];
            speaker->say(ResultChatterLine(result, winners));
            lastChatterSpeaker = speaker;
        }
    }
    void ReleaseCharacter(Character* character) {
        if (!character) return;
        for (size_t i = 0; i < guests.size(); ++i) if (guests[i].actor == character) {
            ReleaseGuest(guests[i]);
            guests.erase(guests.begin() + i);
            break;
        }
        // A previously unhealthy guest may still be queued for a delayed
        // native-AI resume. Cleanup now owns that handoff, so remove it.
        for (size_t i = 0; i < pendingReleases.size();) {
            if (pendingReleases[i].actor == character)
                pendingReleases.erase(pendingReleases.begin() + i);
            else ++i;
        }
    }
    void Release() {
        for (size_t i = 0; i < guests.size(); ++i) ReleaseGuest(guests[i]);
        if (!guests.empty()) PGLog::Debug("Proving Grounds: arena spectators released from arena control");
        guests.clear(); ResetChatter(); activeArena.setNull(); lastTick = 0; lastMaintenance = 0;
    }
    void AbandonWorldState() {
        guests.clear(); pendingReleases.clear(); ResetChatter(); activeArena.setNull(); lastTick = 0;
        lastMaintenance = 0; elapsed = 0; nextAudience = 0; activePolicy = ArenaPerformance::Policy();
    }
    void Begin(Building* arena) {
        Release();
        if (!ou || !arena || !arena->isValid() || !arena->getTown()) return;
        activePolicy = ArenaPerformance::PolicyFor(PGConfig::PerformanceProfile());
        activeArena = arena; elapsed = 0; lastTick = GetTickCount();
        // Diagnostic discovery is deliberately wider than seat eligibility so
        // logs distinguish missing furniture from distance/ownership rejection.
        lektor<RootObject*> buildings;
        ou->getObjectsWithinSphere(buildings, arena->getPosition(), 2500, BUILDING, 2048, NULL);
        std::vector<UseableStuff*> seats;
        unsigned pillowCount = 0;
        for (uint32_t i = 0; i < buildings.size(); ++i) {
            if (!buildings[i] || !buildings[i]->isValid()) continue;
            Building* b = static_cast<Building*>(buildings[i]);
            if (TownArenaRuntimePolicy::Equals(ArenaIdentity::GetStringId(b), "192-Proving Grounds.mod"))
                ++pillowCount;
            UseableStuff* seat = Seat(b);
            if (!seat || Occupied(seat)) continue;
            const double distance = (b->getPosition() - arena->getPosition()).squaredLength();
            double otherDistance = 1.0e30;
            for (uint32_t j = 0; j < buildings.size(); ++j) {
                if (!buildings[j] || !buildings[j]->isValid() || buildings[j] == arena ||
                    !TownArenaRuntimePolicy::IsNewArena(ArenaIdentity::GetStringId(buildings[j]))) continue;
                Building* other = static_cast<Building*>(buildings[j]);
                if (other->getTown() != arena->getTown()) continue;
                const double candidate = (b->getPosition() - other->getPosition()).squaredLength();
                if (candidate < otherDistance) otherDistance = candidate;
            }
            if (TownSpectatorPolicy::BelongsToArena(b->getTown() == arena->getTown(), distance, otherDistance)) seats.push_back(seat);
            else {
                char detail[160]; sprintf_s(detail, "Proving Grounds: pillow association rejected distanceSq=%.1f otherArenaDistanceSq=%.1f sameTown=%d",
                    distance, otherDistance, b->getTown() == arena->getTown() ? 1 : 0); PGLog::Debug(detail);
            }
        }
        lektor<RootObject*> nearby;
        ou->getObjectsWithinSphere(nearby, arena->getPosition(), 2500, CHARACTER, 512, NULL);
        if (nearby.size() == 0) return;
        const unsigned audienceSequence = nextAudience++;
        const unsigned start = audienceSequence % nearby.size();
        const double gameHours = ou->getTimeStamp_inGameHours().getTotalHours();
        const int epoch = TownArenaPolicy::CardEpoch(gameHours);
        const bool daytime = epoch >= 0 && epoch % 2 == 0;
        const int audienceTarget = activePolicy.spectatorCap;
        std::vector<Character*> candidates[3];
        std::vector<Character*> fighterCandidates;
        for (uint32_t i = 0; i < nearby.size(); ++i) {
            RootObject* object = nearby[(start+i) % nearby.size()];
            if (!object || !object->isValid()) continue;
            Character* c = static_cast<Character*>(object);
            GameData* data = c->getGameData();
            const TownSpectatorPolicy::AudienceRole role =
                TownSpectatorPolicy::RoleFor(data ? data->stringID.c_str() : NULL);
            const bool fighter = data && TownArenaRuntimePolicy::IsRegularFighter(data->stringID.c_str());
            if (c->getCurrentTownLocation() != arena->getTown() || Player(c) || !Healthy(c)) continue;
            bool assigned = false;
            for (size_t j = 0; j < guests.size(); ++j) if (guests[j].actor == c) assigned = true;
            if (assigned || ReleasePending(c)) continue;
            if (fighter) {
                if (!TownArena::IsFighter(c)) fighterCandidates.push_back(c);
                continue;
            }
            if (!TownSpectatorPolicy::CanWatch(role, false, c->getPermajobCount() > 0, true)) continue;
            candidates[static_cast<int>(role)].push_back(c);
        }
        size_t candidateIndex[3] = {};
        TownSpectatorPolicy::AudienceRole roleCursor =
            static_cast<TownSpectatorPolicy::AudienceRole>(audienceSequence % 3);
        while (static_cast<int>(guests.size()) < audienceTarget) {
            const TownSpectatorPolicy::AudienceRole role = TownSpectatorPolicy::NextRole(
                static_cast<int>(candidates[0].size() - candidateIndex[0]),
                static_cast<int>(candidates[1].size() - candidateIndex[1]),
                static_cast<int>(candidates[2].size() - candidateIndex[2]), roleCursor);
            if (role == TownSpectatorPolicy::NotAudience) break;
            const int roleIndex = static_cast<int>(role);
            Character* c = candidates[roleIndex][candidateIndex[roleIndex]++];
            roleCursor = static_cast<TownSpectatorPolicy::AudienceRole>((roleIndex + 1) % 3);
            Invite(c, seats, audienceTarget);
        }
        for (size_t i = 0; i < fighterCandidates.size() && static_cast<int>(guests.size()) < audienceTarget; ++i)
            Invite(fighterCandidates[i], seats, audienceTarget);
        char text[384]; sprintf_s(text, "Proving Grounds: spectator seating - %s target=%d, %u nearby buildings, %u matching pillows, %u free pillows, %u invited (candidates: %u civilians, %u bar residents, %u cage handlers, %u idle fighters)",
            daytime ? "day" : "night", audienceTarget, buildings.size(), pillowCount,
            static_cast<unsigned>(seats.size()), static_cast<unsigned>(guests.size()),
            static_cast<unsigned>(candidates[0].size()), static_cast<unsigned>(candidates[1].size()),
            static_cast<unsigned>(candidates[2].size()), static_cast<unsigned>(fighterCandidates.size()));
        PGLog::Debug(text);
        Tick();
    }
    void Tick() {
        if (!ou) return;
        TickPendingReleases();
        if (guests.empty()) return;
        const DWORD now = GetTickCount();
        const float dt = lastTick ? (now-lastTick) / 1000.0f : 0;
        lastTick = now;
        if (ou->isPaused()) return;
        elapsed += dt;
        if (lastMaintenance && activePolicy.maintenanceMs &&
            now - lastMaintenance < activePolicy.maintenanceMs) return;
        lastMaintenance = now;
        Building* arena = activeArena.getBuilding();
        for (size_t i = 0; i < guests.size();) {
            Guest& guest = guests[i]; Character* c = guest.actor.getCharacter();
            UseableStuff* seat = Seat(guest.seat.getBuilding());
            const bool seated = seat && c && seat->getOccupant() == c;
            const bool taken = seat && Occupied(seat) && !seated;
            if (!arena || !arena->isValid() || !Healthy(c) || Player(c) || !seat || taken ||
                c->getCurrentTownLocation() != arena->getTown() || (guest.seated && !seated) || (!seated && elapsed >= 180)) {
                ReleaseGuest(guest); guests.erase(guests.begin()+i); continue;
            }
            if (seated) {
                guest.seated = true;
                c->setStandingOrder(MessageForB::M_SET_ORDER_PASSIVE, true);
            } else if (elapsed - guest.lastOrder >= 3) {
                guest.lastOrder = elapsed;
                c->addOrder(seat, guest.task, seat, false, true, seat->getPosition());
                CharMovement* movement = c->getMovement();
                if (movement) { movement->setDesiredSpeedOrders(RUN); movement->setDesiredSpeed(RUN); }
            }
            ++i;
        }
        TickChatter();
    }
}
