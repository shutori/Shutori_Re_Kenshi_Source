#include "TownArena.h"
#include "PGConfig.h"
#include "CombatBalanceLog.h"
#include "TownAftercare.h"
#include "TownLimbShop.h"
#include "TownCorpseCleanup.h"
#include "TownAnnouncer.h"
#include "TownBookie.h"
#include "ArenaSnapshot.h"
#include "TownSpectators.h"
#include "TownAftercarePolicy.h"
#include "ArenaRingGuard.h"

#include "BalanceTuning.h"
#include "TownArenaRuntimePolicy.h"
#include "TownArenaPolicy.h"
#include "TownChallengePolicy.h"
#include "TownChallengeBuyInPolicy.h"
#include "TownChallengeWallet.h"
#include "TownMatchmakingRuntime.h"
#include "TownMatchmakingSearch.h"
#include "TownMatchmakingPolicy.h"
#include "TownDiagnosticRun.h"
#include "LeaderboardStore.h"
#include "ArenaIdentity.h"
#include "ArenaIngress.h"
#include "ArenaMedical.h"
#include "ArenaMarks.h"
#include "RosterStatus.h"
#include "SparSession.h"
#include "SparPodium.h"
#include "WorldLifecycle.h"
#include "PGLog.h"
#include <Windows.h>
#include <algorithm>
#include <sstream>
#include <vector>
#include <cstdio>
#include <climits>
#include <cmath>

#pragma warning(push)
#pragma warning(disable: 4091)
#include <kenshi/Building/Building.h>
#include <kenshi/Character.h>
#include <kenshi/InstanceID.h>
#include <kenshi/CharMovement.h>
#include <kenshi/GameData.h>
#include <kenshi/GameWorld.h>
#include <kenshi/Globals.h>
#include <kenshi/MedicalSystem.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/util/hand.h>
#pragma warning(pop)

namespace
{
    enum Phase { Idle, Preparing, Fighting, Aftercare };
    Phase g_phase = Idle;
    int g_queuedChallenge = -1;
    int g_activeChallenge = -1;
    bool g_resultRecorded = false;
    DWORD g_lastCardDiscovery = 0;
    bool g_schedulePaused = false;
    bool g_playerMatch = false;
    float g_activeMarksMultiplier = 1.0f;
    int g_activeMarksBonus = 0;
    bool g_bookingPending = false;
    bool g_quietDiscovery = false;
    hand g_bookedPlayer;
    hand g_bookedRegistry;
    hand g_townRegistry;
    hand g_activeRegistry;
    double g_nextNpcHours = 0.0;
    TownMatchmakingPolicy::Match g_plannedNpc;
    hand g_plannedRegistry;
    std::vector<hand> g_plannedFighters;
    ArenaPersistence::PlannedNpcBout g_restoredNpc;
    bool g_restoredNpcPending = false;
    double g_restoreObservedHours = 0.0;
    DWORD g_lastScheduleCheck = 0;
    hand g_fighters[16];
    MatchRules::MatchTeam g_fighterTeams[16];
    int g_fighterCount = 0;
    int g_playerCount = 0;
    std::vector<hand> g_bookedTeam;
    ArenaMedical::Protocol g_previousProtocol = ArenaMedical::RingsideAid;
    bool g_sequentialCare = false;
    float g_aftercareElapsed = 0.0f;
    DWORD g_aftercareTick = 0;
    // Ring containment (BalanceTuning::ringNearRadius). The centre is the arena
    // building's origin -- the same point the ringside medic line is measured from
    // -- captured when the bout is armed so the per-tick guard never has to
    // re-resolve a building. The two timestamps throttle the two corrections
    // independently, wrap-safe.
    DWORD g_lastAid = 0;
    std::string g_status = "Town trial idle";
    TownChallengePolicy::Offer g_previews[5], g_directPreview, g_directBooking;
    TownChallengePolicy::Offer g_challengeReservation;
    TownChallengeBuyInPolicy::Quote g_buyInQuotes[6];
    TownChallengeBuyInPolicy::Ticket g_buyInTicket;
    int g_paidRefreshCountBeforeBooking = 0;
    bool g_paidRefreshResetPending = false;
    std::string g_unavailableReasons[6];
    bool g_searching[6] = {};
    bool g_searchQueued[5] = {};
    bool g_searchCompleted[5] = {};
    bool g_bestAvailable[5] = {};
    unsigned g_searchGeneration = 0;
    TownMatchmakingRuntime::Snapshot g_previewSnapshot;
    bool g_previewSnapshotValid = false;
    DWORD g_previewSnapshotTick = 0;
    std::string g_previewKey;
    std::string g_retryContext;
    DWORD g_previewTick = 0;
    DWORD g_matchmakingDiagnosticTick = 0;
    std::vector<hand> g_ingressQuarantine;
    const bool kShowTownStatusPopups = false;

    bool IngressQuarantined(Character* character)
    {
        for (size_t i = 0; i < g_ingressQuarantine.size(); ++i)
            if (g_ingressQuarantine[i].getCharacter() == character) return true;
        return false;
    }

    void Status(const std::string& message, bool forcePlayerMessage = false)
    {
        if (g_status == message) return;
        g_status = message;
        PGLog::Debug(("Proving Grounds: town trial - " + message).c_str());
        if (kShowTownStatusPopups && ou && (!g_quietDiscovery || forcePlayerMessage))
            ou->showPlayerAMessage(message, true);
    }

    const char* Id(Character* c)
    {
        GameData* data = c ? c->getGameData() : NULL;
        return data ? data->stringID.c_str() : NULL;
    }
    std::string LegacyOpponentId(Character* c)
    {
        InstanceID* id = c ? c->getInstanceID() : NULL;
        return c ? TownChallengePolicy::OpponentKey(id ? id->uid : "", Id(c) ? Id(c) : "", c->getName()) : std::string();
    }
    std::string PersistentId(Character* c)
    {
        const std::string id = TownMatchmakingRuntime::Identity(c);
        return id.empty() ? LegacyOpponentId(c) : id;
    }
    bool MatchesOpponent(Character* c, const std::string& key)
    {
        return c && !key.empty() && (PersistentId(c) == key || LegacyOpponentId(c) == key ||
            TownChallengePolicy::OpponentKey("", Id(c) ? Id(c) : "", c->getName()) == key);
    }

    std::string PlayerLineupKey(const std::vector<Character*>& players)
    {
        std::vector<std::string> ids;
        for (size_t i = 0; i < players.size(); ++i)
            ids.push_back(TownMatchmakingRuntime::SelectionIdentity(players[i]));
        std::sort(ids.begin(), ids.end());
        std::ostringstream key;
        for (size_t i = 0; i < ids.size(); ++i) key << '|' << ids[i].size() << ':' << ids[i];
        return key.str();
    }

    bool EligibleOpponent(const TownMatchmakingRuntime::Snapshot& snapshot, const std::string& id)
    {
        for (size_t i = 0; i < snapshot.npcs.size(); ++i)
            if (snapshot.npcs[i].id == id) return true;
        return false;
    }

    bool Ready(Character* c, bool doctor, bool bookedPlayer, std::string* reason);

    std::string NamedOpponentUnavailableReason(const TownChallengePolicy::Offer& offer,
        const TownMatchmakingRuntime::Snapshot& snapshot)
    {
        const std::string label = TownChallengePolicy::Title(offer.encounter);
        const std::string role = TownChallengePolicy::Role(offer.encounter);
        const std::string opponentId = TownChallengePolicy::OpponentId(offer, 0);
        Character* match = NULL;
        bool ambiguous = false;
        for (std::map<std::string, Character*>::const_iterator it = snapshot.characters.begin();
            it != snapshot.characters.end(); ++it) {
            Character* candidate = it->second;
            if (!candidate || !candidate->isValid()) continue;
            if ((!opponentId.empty() && MatchesOpponent(candidate, opponentId)) ||
                (opponentId.empty() && TownArenaRuntimePolicy::Equals(Id(candidate), role.c_str()))) {
                if (match && match != candidate) { ambiguous = true; break; }
                match = candidate;
            }
        }
        if (ambiguous) return label + ": identity is ambiguous";
        if (!match) return label + ": not present in the registry town";
        if (TownAftercare::IsManaged(match)) return label + ": still in aftercare";
        if (TownLimbShop::IsManaged(match)) return label + ": waiting for replacement limbs";
        if (TownArena::IsFighter(match)) return label + ": already assigned to an active bout";
        std::string reason;
        const bool quiet = g_quietDiscovery;
        g_quietDiscovery = true;
        const bool ready = Ready(match, false, false, &reason);
        g_quietDiscovery = quiet;
        if (!ready) return label + ": " + (reason.empty() ? "not ready to fight" : reason);
        if (!(TownMatchmakingRuntime::Ability(match) > 0)) return label + ": combat stats unavailable";
        return label + ": unavailable for this matchup";
    }

    std::string PreviewUnavailableReason(const TownChallengePolicy::Offer& offer,
        const TownMatchmakingRuntime::Snapshot& snapshot)
    {
        if (!snapshot.playersReady) return snapshot.playerBlocker.empty() ?
            "Selected fighters are unavailable or recovering" : snapshot.playerBlocker;
        if (!TownChallengePolicy::Resolved(offer)) {
            if (TownChallengePolicy::NamedForDivision(offer.encounter, offer.division))
                return NamedOpponentUnavailableReason(offer, snapshot);
            return "No suitable recovered opponents for this team";
        }
        if (static_cast<int>(snapshot.players.size()) != TownChallengePolicy::Players(offer))
            return "Selected lineup no longer matches this challenge";
        for (int i = 0; i < TownChallengePolicy::Opponents(offer); ++i)
            if (!EligibleOpponent(snapshot, TownChallengePolicy::OpponentId(offer, i))) {
                if (i == 0 && TownChallengePolicy::NamedForDivision(offer.encounter, offer.division))
                    return NamedOpponentUnavailableReason(offer, snapshot);
                return TownChallengePolicy::OpponentName(offer, i) + " is unavailable or recovering";
            }
        const int participants = TownChallengePolicy::Players(offer) + TownChallengePolicy::Opponents(offer);
        if (static_cast<int>(snapshot.medics.size()) < TownAftercarePolicy::RequiredMedics(participants))
            return "Waiting for medical capacity";
        return "";
    }

    MatchRules::MatchMode MatchModeFor(const TownChallengePolicy::Offer& offer)
    {
        return offer.mode == TownChallengePolicy::ModeTeams1v1 ?
            MatchRules::ModeTeams1v1 : MatchRules::ModeTeamAvB;
    }

    TownChallengePolicy::Card& Challenges()
    {
        TownChallengePolicy::Card& card = LeaderboardStore::GetTownChallenges();
        if (!ou || !LeaderboardStore::HasActiveSave()) return card;
        if (card.offers[0].epoch == -2)
        {
            const std::string key = LeaderboardStore::GetActiveSaveKey();
            for (size_t i = 0; i < key.size(); ++i) card.seed = card.seed * 33u + static_cast<unsigned char>(key[i]);
        }
        const int epoch = TownArenaPolicy::ChallengeCardEpoch(
            ou->getTimeStamp_inGameHours().getTotalHours(), PGConfig::ChallengeValues().refreshHours);
        TownChallengePolicy::RefreshGenerated(card, epoch, g_activeChallenge >= 0 ? g_activeChallenge : g_queuedChallenge);
        Building* registry = g_townRegistry.getBuilding();
        const DWORD now = GetTickCount();
        if (!registry || !registry->isValid() || (g_lastCardDiscovery && now - g_lastCardDiscovery < 5000)) return card;
        g_lastCardDiscovery = now;
        lektor<RootObject*> nearby;
        ou->getObjectsWithinSphere(nearby, registry->getPosition(), 2500.0f, CHARACTER, 512, NULL);
        for (int slot = 0; slot < 5; ++slot)
        {
            TownChallengePolicy::Offer& offer = TownChallengePolicy::At(card, slot);
            if (offer.generated) continue;
            for (int member = 0; member < TownChallengePolicy::Opponents(offer); ++member)
            {
                std::string& boundId = TownChallengePolicy::OpponentId(offer, member);
                std::string& boundName = TownChallengePolicy::OpponentName(offer, member);
                if (!boundId.empty()) continue;
                const int role = TownChallengePolicy::OpponentRole(offer, member);
                for (uint32_t i = 0; i < nearby.size(); ++i)
                {
                    RootObject* obj = nearby[i];
                    if (!obj || !obj->isValid()) continue;
                    Character* c = static_cast<Character*>(obj);
                    if (c->isDead() || c->getCurrentTownLocation() != registry->getTown() ||
                        !TownArenaRuntimePolicy::Equals(Id(c), TownChallengePolicy::Role(role))) continue;
                    const std::string uid = PersistentId(c);
                    if (uid.empty()) continue;
                    bool ambiguous = false;
                    for (uint32_t j = 0; j < nearby.size(); ++j)
                    {
                        RootObject* other = nearby[j];
                        if (!other || other == c || !other->isValid()) continue;
                        Character* candidate = static_cast<Character*>(other);
                        if (candidate->getCurrentTownLocation() == registry->getTown() && MatchesOpponent(candidate, uid))
                            ambiguous = true;
                    }
                    if (ambiguous) continue;
                    boundId = uid;
                    boundName = c->getName();
                    PGLog::Debug(("Proving Grounds: challenge opponent resolved: " + boundName + " key=" + uid).c_str());
                    break;
                }
            }
        }
        return card;
    }

    bool Ready(Character* c, bool doctor, bool bookedPlayer = false, std::string* reason = NULL)
    {
        if (reason) *reason = "health data unavailable";
        if (!c || !c->isValid()) return false;
        if (!doctor && IngressQuarantined(c)) {
            if (reason) *reason = "could not reach arena formation";
            return false;
        }
        TownArenaRuntimePolicy::Readiness r = {};
        r.valid = true;
        if (!ou || !ou->player) return false;
        const lektor<Character*>& playerCharacters = ou->player->getAllPlayerCharacters();
        for (uint32_t i = 0; i < playerCharacters.size(); ++i)
            if (playerCharacters[i] == c) r.player = true;
        if (bookedPlayer)
        {
            if (!r.player) return false;
            r.player = false; // Same recovery thresholds, explicitly player-owned.
        }
        r.dead = c->isDead();
        r.unconscious = c->isUnconcious();
        r.inCombat = c->isInCombatMode(true, true);
        r.needsAid = ArenaMedical::NeedsStabilization(c);
        RosterStatus::Snapshot health = {};
        if (!RosterStatus::Read(c, health, reason)) return false;
        r.blood = health.blood;
        r.minimumHealth = health.lowestLimb;
        MedicalSystem* med = c->getMedical();
        const bool medicallyAble = !med->isInBloodlossTrauma() &&
            !med->isProbablyDying() && !med->isCrippled() && med->hasAnArmToFightWith();
        const bool ready = doctor ? TownArenaRuntimePolicy::CanProvideAid(r, medicallyAble) :
            TownArenaRuntimePolicy::CanParticipate(r);
        if (reason)
        {
            if (ready) reason->clear();
            else if (r.player) *reason = "belongs to the player squad";
            else if (r.dead) *reason = "dead";
            else if (r.unconscious) *reason = "unconscious";
            else if (r.inCombat) *reason = "already in combat";
            else if (r.needsAid) *reason = "needs first aid";
            else if (doctor && !medicallyAble) *reason = "native medical capacity check failed";
            else *reason = "recovering";
        }
        char detail[256];
        sprintf_s(detail, "Town candidate %s: ready=%d health=%.3f blood=%.3f player=%d dead=%d ko=%d combat=%d aid=%d",
            Id(c) ? Id(c) : "unknown", ready ? 1 : 0, r.minimumHealth, r.blood,
            r.player, r.dead, r.unconscious, r.inCombat, r.needsAid);
        if (!g_quietDiscovery) PGLog::Debug(detail);
        return ready;
    }
    void PayChallengeCredit() {
        if (!LeaderboardStore::HasActiveSave()) return;
        TownChallengeBuyInPolicy::Account& account = LeaderboardStore::GetChallengeAccount();
        if (TownChallengeWallet::Credit(account.credit)) account.credit = 0;
    }
    void SettleChallenge(int outcome) {
        if (!g_buyInTicket.pending) return;
        const int credit = TownChallengeBuyInPolicy::Settle(LeaderboardStore::GetChallengeAccount(), g_buyInTicket, outcome);
        if (g_paidRefreshResetPending) {
            if (outcome < 0 && LeaderboardStore::HasActiveSave())
                LeaderboardStore::GetTownChallenges().paidRefreshCount = g_paidRefreshCountBeforeBooking;
            g_paidRefreshResetPending = false;
        }
        CombatBalanceLog::ChallengeSettled(outcome, credit);
        PayChallengeCredit();
        char message[128];
        sprintf_s(message, outcome < 0 ? "Challenge cancelled/drawn: %d Cats refunded." :
            outcome == 1 ? "Challenge won: %d Cats total return." : "Challenge lost: buy-in consumed.", credit);
        Status(message, true);
    }

    bool DiscoverTownRegistry()
    {
        if (!ou || !ou->player) return false;
        const lektor<Character*>& players = ou->player->getAllPlayerCharacters();
        for (uint32_t i = 0; i < players.size(); ++i)
        {
            Character* player = players[i];
            if (player && player->isValid() && player->getCurrentTownLocation() &&
                TownArena::BindRegistryNear(player))
                return true;
        }
        return false;
    }

    bool HasPlayerInRegistryTown(Building* registry)
    {
        if (!ou || !ou->player || !registry || !registry->isValid() || !registry->getTown())
            return false;
        const lektor<Character*>& players = ou->player->getAllPlayerCharacters();
        for (uint32_t i = 0; i < players.size(); ++i)
        {
            Character* player = players[i];
            if (player && player->isValid() &&
                player->getCurrentTownLocation() == registry->getTown())
                return true;
        }
        return false;
    }

    void Finish(const std::string& message)
    {
        TownAnnouncer::Release();
        TownSpectators::Release();
        TownCorpseCleanup::Release();
        // The next market can already be open while this bout's medics finish.
        if (!g_plannedNpc.valid) TownBookie::Settle(-1);
        // An ambient bout may finish while a paid player booking is queued.
        // Only the active player event owns settlement here.
        if (g_playerMatch && g_queuedChallenge < 0) SettleChallenge(-1);
        if (g_activeChallenge >= 0)
        {
            if (g_activeChallenge != g_queuedChallenge)
                TownArenaPolicy::CancelBooking(TownChallengePolicy::At(LeaderboardStore::GetTownChallenges(), g_activeChallenge).state);
            g_activeChallenge = -1;
        }
        TownAftercare::Release();
        ArenaMedical::SetProtocol(g_previousProtocol);
        g_phase = Idle;
        g_sequentialCare = false;
        g_playerMatch = false;
        g_activeMarksMultiplier = 1.0f;
        g_activeMarksBonus = 0;
        g_activeRegistry.setNull();
        if (!g_plannedNpc.valid) g_nextNpcHours = 0.0;
        for (int i = 0; i < 16; ++i) g_fighters[i].setNull();
        g_fighterCount = 0;
        g_playerCount = 0;
        g_previewTick = 0;
        if (g_queuedChallenge < 0) g_challengeReservation = TownChallengePolicy::Offer();
        Status(message);
    }
}

namespace TownArena
{
    bool ReservePlayer(Character* player);
    bool IsNpcReadyForPreparation(Character* character) {
        const bool previousQuiet = g_quietDiscovery;
        g_quietDiscovery = true;
        const bool ready = character && character->isValid() && !character->isBeingCarried() &&
            !character->isCarryingSomething && Ready(character, false);
        g_quietDiscovery = previousQuiet;
        return ready;
    }
    void QuarantineIngressFighter(Character* character) {
        if (!character || !character->isValid() || IngressQuarantined(character)) return;
        g_ingressQuarantine.push_back(hand(character));
        PGLog::Debug(("Proving Grounds: ingress fighter quarantined for this world session name=" +
            character->getName() + " identity=" + TownMatchmakingRuntime::Identity(character)).c_str());
    }
    struct NpcPreparation {
        bool Ready() { return TownBookie::PreparationValid(); }
        void Cancel() {
            const bool wagerPending = TownBookie::HasPendingWager();
            const std::string ingressStatus = ArenaIngress::GetStatus();
            const bool formationFailure = !ArenaIngress::IsPending() &&
                ingressStatus.find("NPC formation ") == 0;
            ArenaIngress::Cancel();
            TownDiagnosticRun::RetryPending();
            Finish((formationFailure ? ingressStatus :
                "NPC bout cancelled: locked fighter unavailable or recovering") +
                (wagerPending && !formationFailure ? "; wager refunded" : ""));
        }
    };
    bool ValidatePreparation() {
        NpcPreparation preparation;
        return TownArenaRuntimePolicy::GuardPreparation(g_phase == Preparing && !g_playerMatch, preparation);
    }
    int GetDivision() { return LeaderboardStore::GetTownChallenges().division; }
    std::string GetNextChallengeRefreshCaption() {
        if (!ou) return "Next Refresh: unavailable";
        const double hours = TownArenaPolicy::HoursUntilChallengeRefresh(
            ou->getTimeStamp_inGameHours().getTotalHours(), PGConfig::ChallengeValues().refreshHours);
        if (!(hours >= 0.0)) return "Next Refresh: unavailable";
        const int totalMinutes = static_cast<int>(std::ceil(hours * 60.0));
        char caption[64];
        sprintf_s(caption, "Next Refresh: %dh %02dm", totalMinutes / 60, totalMinutes % 60);
        return caption;
    }
    int GetChallengeRefreshCost() {
        const PGConfig::Challenges& settings = PGConfig::ChallengeValues();
        const TownChallengePolicy::Card& card = LeaderboardStore::GetTownChallenges();
        return TownArenaPolicy::PaidRefreshCost(settings.refreshBaseCostCats,
            settings.refreshCostMultiplier, card.paidRefreshCount);
    }
    bool CanBuyChallengeRefresh() {
        Building* registry = g_townRegistry.getBuilding();
        if (!LeaderboardStore::HasActiveSave() || !ou || !registry || !registry->isValid() ||
            g_bookingPending || g_queuedChallenge >= 0 || g_activeChallenge >= 0 ||
            g_phase == Preparing || g_phase == Fighting) return false;
        const int cost = GetChallengeRefreshCost();
        return cost > 0 && TownChallengeWallet::Balance() >= cost;
    }
    bool BuyChallengeRefresh() {
        if (!LeaderboardStore::HasActiveSave() || !ou) {
            Status("Load or start a game before refreshing challenge offers");
            return false;
        }
        Building* registry = g_townRegistry.getBuilding();
        if (!registry || !registry->isValid()) {
            Status("Open the town registry before refreshing challenge offers");
            return false;
        }
        if (g_bookingPending || g_queuedChallenge >= 0 || g_activeChallenge >= 0 ||
            g_phase == Preparing || g_phase == Fighting) {
            Status("Finish or cancel the current booking before refreshing challenge offers");
            return false;
        }
        TownChallengePolicy::Card& card = Challenges();
        const PGConfig::Challenges& settings = PGConfig::ChallengeValues();
        const int epoch = TownArenaPolicy::ChallengeCardEpoch(
            ou->getTimeStamp_inGameHours().getTotalHours(), settings.refreshHours);
        if (epoch < -1) {
            Status("Challenge refresh is unavailable because the in-game clock is invalid");
            return false;
        }
        const int cost = TownArenaPolicy::PaidRefreshCost(settings.refreshBaseCostCats,
            settings.refreshCostMultiplier, card.paidRefreshCount);
        const int balance = TownChallengeWallet::Balance();
        if (cost <= 0 || balance < cost) {
            char message[128];
            sprintf_s(message, "Refresh costs %d Cats; faction balance: %d Cats", cost, balance);
            Status(message);
            return false;
        }
        if (!TownChallengeWallet::Take(cost)) {
            Status("Could not collect the challenge refresh cost");
            return false;
        }
        TownChallengePolicy::Card refreshed = card;
        refreshed.seed = card.seed ^ (0x9e3779b9u * card.paidRefreshNonce);
        if (!refreshed.seed) refreshed.seed = 0x6d2b79f5u;
        for (int slot = 0; slot < 5; ++slot)
            refreshed.offers[slot] = TownChallengePolicy::Offer();
        TownChallengePolicy::RefreshGenerated(refreshed, epoch, -1);
        for (int slot = 0; slot < 5; ++slot)
            card.offers[slot] = refreshed.offers[slot];
        if (card.paidRefreshCount < INT_MAX) ++card.paidRefreshCount;
        if (card.paidRefreshNonce < UINT_MAX) ++card.paidRefreshNonce;

        g_previewKey.clear();
        g_retryContext.clear();
        g_previewTick = 0;
        g_previewSnapshotValid = false;
        g_previewSnapshotTick = 0;
        ++g_searchGeneration;
        if (!g_searchGeneration) ++g_searchGeneration;
        TownMatchmakingSearch::BeginGeneration(g_searchGeneration);
        for (int slot = 0; slot < 5; ++slot) {
            g_previews[slot] = TownChallengePolicy::Offer();
            g_searchQueued[slot] = false;
            g_searchCompleted[slot] = false;
            g_searching[slot] = false;
            g_bestAvailable[slot] = false;
            g_unavailableReasons[slot].clear();
            g_buyInQuotes[slot] = TownChallengeBuyInPolicy::Quote();
        }
        g_directPreview = TownChallengePolicy::Offer();
        g_buyInQuotes[5] = TownChallengeBuyInPolicy::Quote();
        char message[128];
        const int nextCost = TownArenaPolicy::PaidRefreshCost(settings.refreshBaseCostCats,
            settings.refreshCostMultiplier, card.paidRefreshCount);
        sprintf_s(message, "Challenge offers refreshed for %d Cats; next refresh costs %d Cats",
            cost, nextCost);
        Status(message);
        return true;
    }
    bool SetDivision(int division) {
        if (!TownChallengePolicy::SetDivision(LeaderboardStore::GetTownChallenges(), division,
            IsLineupLocked())) return false;
        g_previewKey.clear(); g_previewTick = 0;
        return true;
    }
    bool IsChallengeVisible(int slot) {
        return TownChallengePolicy::Visible(LeaderboardStore::GetTownChallenges(), slot);
    }
    void RefreshMatchmakingRoster() {
        g_previewSnapshotValid = false;
        g_previewSnapshotTick = 0;
        g_previewTick = 0;
    }
    static TownMatchmakingRuntime::Snapshot SnapshotFor(const std::vector<Character*>& players) {
        const bool previousQuiet = g_quietDiscovery; g_quietDiscovery = true;
        TownMatchmakingRuntime::Snapshot result = TownMatchmakingRuntime::Collect(
            g_townRegistry.getBuilding(), players, Ready, true);
        g_quietDiscovery = previousQuiet;
        const DWORD now = GetTickCount();
        if (!g_matchmakingDiagnosticTick || now - g_matchmakingDiagnosticTick >= 30000) {
            PGLog::Debug(("Proving Grounds: matchmaking discovery - " + result.diagnostics).c_str());
            g_matchmakingDiagnosticTick = now;
        }
        return result;
    }
    bool HasPlannedNpcBout() { return g_plannedNpc.valid; }
    double GetPlannedNpcStartHours() { return g_plannedNpc.valid ? g_nextNpcHours : 0.0; }
    bool CapturePlannedNpcBout(ArenaPersistence::PlannedNpcBout& out) {
        out = ArenaPersistence::PlannedNpcBout();
        if (!g_plannedNpc.valid) return !TownBookie::HasPendingWager();
        if (g_restoredNpcPending) { out = g_restoredNpc; return true; }
        Building* registry = g_plannedRegistry.getBuilding();
        InstanceID* id = registry && registry->isValid() ? registry->getInstanceID() : NULL;
        if (!id || id->uid.empty()) return false;
        out.active = true;
        out.registryId = id->uid;
        out.startHours = g_nextNpcHours;
        out.scoreA = g_plannedNpc.scoreA; out.scoreB = g_plannedNpc.scoreB;
        out.teamA = g_plannedNpc.a; out.teamB = g_plannedNpc.b;
        TownBookie::CapturePlannedNpcBout(out);
        return true;
    }
    void RestorePlannedNpcBout(const ArenaPersistence::PlannedNpcBout& saved) {
        if (!saved.active) return;
        g_restoredNpc = saved;
        g_restoredNpcPending = true;
        g_restoreObservedHours = 0.0;
        g_plannedNpc = TownMatchmakingPolicy::Match();
        g_plannedNpc.valid = true;
        g_plannedNpc.a = saved.teamA; g_plannedNpc.b = saved.teamB;
        g_plannedNpc.scoreA = saved.scoreA; g_plannedNpc.scoreB = saved.scoreB;
        g_nextNpcHours = saved.startHours;
        Status("Saved NPC lineup and wager waiting for the original fighters to load");
    }
    void CancelPlannedNpcBout(const std::string& reason) {
        if (!g_plannedNpc.valid) return;
        if (g_restoredNpcPending) {
            TownBookie::RefundUnrestoredWager(g_restoredNpc);
            g_restoredNpcPending = false;
            g_restoredNpc = ArenaPersistence::PlannedNpcBout();
            g_restoreObservedHours = 0.0;
        }
        g_plannedNpc = TownMatchmakingPolicy::Match();
        g_plannedRegistry.setNull(); g_plannedFighters.clear();
        g_nextNpcHours = 0.0;
        g_lastScheduleCheck = GetTickCount();
        TownBookie::CancelNpcBout(reason);
        TownDiagnosticRun::CancelPending();
    }
    static bool PlannedNpcReady() {
        if (g_restoredNpcPending) return false;
        Building* registry = g_plannedRegistry.getBuilding();
        if (!registry || !registry->isValid() || !ArenaIdentity::IsRegistryFinished(registry)) return false;
        for (size_t i = 0; i < g_plannedFighters.size(); ++i) {
            Character* fighter = g_plannedFighters[i].getCharacter();
            if (!fighter || !fighter->isValid() || fighter->getCurrentTownLocation() != registry->getTown() ||
                !IsNpcReadyForPreparation(fighter) || TownAftercare::IsManaged(fighter) ||
                TownLimbShop::IsManaged(fighter)) return false;
        }
        return TownBookie::PreparationValid();
    }
    static bool PlanNpcBout() {
        if (g_plannedNpc.valid) return true;
        if (!ou || !LeaderboardStore::HasActiveSave() || g_bookingPending || g_schedulePaused ||
            (g_phase != Idle && g_phase != Aftercare)) return false;
        Building* registry = g_townRegistry.getBuilding();
        if (!registry || !registry->isValid() || !ArenaIdentity::IsRegistryFinished(registry)) return false;
        const std::vector<Character*> noPlayers;
        const TownMatchmakingRuntime::Snapshot snapshot = SnapshotFor(noPlayers);
        // Doctors caring for the previous bout are expected back by start time.
        // Readiness and actual capacity are checked again before staging.
        const int capacity = (std::min)(8, static_cast<int>(snapshot.medics.size()) +
            (g_phase == Aftercare ? g_fighterCount : 0));
        TownChallengePolicy::Card& card = LeaderboardStore::GetTownChallenges();
        const TownMatchmakingPolicy::DiagnosticRequest diagnostic=TownDiagnosticRun::NextRequest();
        const TownMatchmakingPolicy::Match match = diagnostic.active ? TownMatchmakingPolicy::SelectAmbientDiagnostic(
            snapshot.npcs, card.ambientSeed, card.ambientHistory, diagnostic, capacity) : TownMatchmakingPolicy::SelectAmbient(
            snapshot.npcs, card.ambientSeed, card.ambientHistory, capacity);
        if (!match.valid) return false;
        std::vector<Character*> fighters;
        std::vector<MatchRules::MatchTeam> teams;
        for (int side = 0; side < 2; ++side) {
            const std::vector<std::string>& ids = side == 0 ? match.a : match.b;
            for (size_t i = 0; i < ids.size(); ++i) {
                Character* fighter = TownMatchmakingRuntime::Resolve(snapshot, ids[i]);
                if (!fighter) return false;
                fighters.push_back(fighter);
                teams.push_back(side == 0 ? MatchRules::TeamA : MatchRules::TeamB);
            }
        }
        g_plannedNpc = match;
        g_plannedRegistry = registry;
        g_plannedFighters.clear();
        for (size_t i = 0; i < fighters.size(); ++i) g_plannedFighters.push_back(hand(fighters[i]));
        // Lead time comes from BalanceTuning; defaults reproduce the shipped
        // 1.0-2.0 h announcement window exactly.
        const BalanceTuning::Values& tuning = BalanceTuning::Get();
        const unsigned delay = TownMatchmakingPolicy::Detail::Hash(card.ambientSeed, "next NPC start") % 61;
        g_nextNpcHours = std::ceil((ou->getTimeStamp_inGameHours().getTotalHours() +
            tuning.npcLeadHoursMin + (delay / 60.0) * tuning.npcLeadHoursSpread) * 60.0) / 60.0;
        if(match.diagnostic)TownDiagnosticRun::Assign(match,ou->getTimeStamp_inGameHours().getTotalHours());
        TownBookie::BeginNpcBout(&fighters[0], &teams[0], static_cast<int>(fighters.size()), match.scoreA, match.scoreB);
        Status("Next NPC lineup announced; betting open for the scheduled bout");
        return true;
    }
    static void CancelUpcomingForPlayer() {
        CancelPlannedNpcBout("Player booking took the next slot");
        if (g_phase == Preparing && !g_playerMatch) {
            ArenaIngress::Cancel();
            Finish("Upcoming NPC bout cancelled for player booking; wagers refunded");
        }
    }
    static void PopulatePreview(TownChallengePolicy::Offer& offer,
        const TownMatchmakingRuntime::Snapshot& snapshot, const TownMatchmakingPolicy::Match& match) {
        TownChallengePolicy::ClearPreview(offer);
        if (!match.valid) return;
        offer.playerCount = static_cast<int>(match.a.size());
        offer.enemyCount = static_cast<int>(match.b.size());
        offer.context = snapshot.context;
        for (int i = 0; i < offer.enemyCount; ++i) {
            Character* c = TownMatchmakingRuntime::Resolve(snapshot, match.b[i]);
            TownChallengePolicy::OpponentId(offer,i) = match.b[i];
            TownChallengePolicy::OpponentName(offer,i) = TownMatchmakingRuntime::Label(c);
            offer.roles[i] = TownMatchmakingRuntime::Role(c);
        }
    }
    // ownScore / enemyScore are the two TeamScores the quote is derived from. They
    // are handed back so the combat log can record what actually priced the
    // challenge rather than recomputing it and drifting from what was charged.
    static TownChallengeBuyInPolicy::Quote QuoteFor(const TownChallengePolicy::Offer& offer,
        const TownMatchmakingRuntime::Snapshot& snapshot, int division,
        double* ownScore = NULL, double* enemyScore = NULL) {
        TownChallengeBuyInPolicy::Quote unavailable;
        if (!snapshot.playersReady || !TownChallengePolicy::Resolved(offer) ||
            static_cast<int>(snapshot.players.size()) != TownChallengePolicy::Players(offer) ||
            (offer.generated && !PreviewUnavailableReason(offer, snapshot).empty())) return unavailable;
        double own[3], ownRatings[3], enemy[4], enemyRatings[4];
        const int count = TownChallengePolicy::Opponents(offer);
        if (count < 1 || count > 4 || snapshot.players.size() > 3) return unavailable;
        for (size_t i = 0; i < snapshot.players.size(); ++i) {
            own[i] = snapshot.players[i].ability;
            ownRatings[i] = snapshot.players[i].mmr;
        }
        for (int member = 0; member < count; ++member) {
            Character* match = NULL;
            for (std::map<std::string, Character*>::const_iterator it = snapshot.characters.begin(); it != snapshot.characters.end(); ++it)
                if (it->second && MatchesOpponent(it->second, TownChallengePolicy::OpponentId(offer, member))) {
                    if (match && match != it->second) return unavailable;
                    match = it->second;
                }
            if (!match) return unavailable;
            enemy[member] = TownMatchmakingRuntime::Ability(match);
            enemyRatings[member] = LeaderboardStore::GetRating(match, LeaderboardData::Town);
        }
        const double own_ = TownMatchmakingPolicy::TeamScore(own, ownRatings, static_cast<int>(snapshot.players.size()));
        const double enemy_ = TownMatchmakingPolicy::TeamScore(enemy, enemyRatings, count);
        if (ownScore) *ownScore = own_;
        if (enemyScore) *enemyScore = enemy_;
        return TownChallengeBuyInPolicy::Price(division, own_, enemy_);
    }
    void UpdateMatchmakingPreview(const std::vector<Character*>& players) {
        TownChallengePolicy::Card& card = Challenges();
        if (!LeaderboardStore::HasActiveSave() || IsLineupLocked()) return;
        const DWORD now = GetTickCount();
        // Polling and world discovery are intentionally throttled. The worker
        // keeps searching between UI refreshes without touching Kenshi state.
        if (g_previewTick && now - g_previewTick < 1000) return;
        g_previewTick = now;
        const std::string lineupKey = PlayerLineupKey(players);
        std::ostringstream key;
        key << lineupKey << ':' << card.division;
        for (int i = 0; i < 5; ++i)
            key << ':' << card.offers[i].epoch << ':' << card.offers[i].draw <<
                ':' << card.offers[i].encounter << ':' << card.offers[i].mode;
        const bool sameCards = key.str() == g_previewKey;
        g_previewKey = key.str();
        bool searchPending = false;
        for (int slot = 0; slot < 5; ++slot)
            searchPending = searchPending || g_searchQueued[slot];
        // Freeze one immutable roster for the whole queued generation. Exact
        // live-stat fingerprints can change every frame while actors recover;
        // rebuilding them during a search starves the worker by cancelling all
        // of its results. Once the queue drains, refresh health periodically.
        if (!g_previewSnapshotValid || !sameCards ||
            (!searchPending && (!g_previewSnapshotTick || now - g_previewSnapshotTick >= 10000)))
        {
            g_previewSnapshot = SnapshotFor(players);
            g_previewSnapshotValid = true;
            g_previewSnapshotTick = now;
        }
        const TownMatchmakingRuntime::Snapshot& snapshot = g_previewSnapshot;
        for (int slot = 0; slot < 6; ++slot)
            g_buyInQuotes[slot] = TownChallengeBuyInPolicy::Quote();

        const std::string searchContext = g_previewKey + ':' + snapshot.context;
        if (searchContext != g_retryContext)
        {
            g_retryContext = searchContext;
            ++g_searchGeneration;
            if (!g_searchGeneration) ++g_searchGeneration;
            TownMatchmakingSearch::BeginGeneration(g_searchGeneration);
            PGLog::Debug("Proving Grounds: started lazy challenge search generation");
            for (int slot = 0; slot < 5; ++slot)
            {
                g_searchQueued[slot] = false;
                g_searchCompleted[slot] = false;
                g_searching[slot] = false;
            }
        }
        TownMatchmakingSearch::Result result;
        while (TownMatchmakingSearch::Poll(result))
        {
            if (result.generation != g_searchGeneration || result.slot < 0 || result.slot >= 5)
                continue;
            const int slot = result.slot;
            TownChallengePolicy::Offer& offer = g_previews[slot];
            g_searchQueued[slot] = false;
            g_searchCompleted[slot] = true;
            g_searching[slot] = false;
            g_bestAvailable[slot] = false;
            PopulatePreview(offer, snapshot, result.match);
            offer.context = lineupKey;
            if (result.match.valid)
            {
                g_bestAvailable[slot] = result.match.bestAvailable;
                g_unavailableReasons[slot] = PreviewUnavailableReason(offer, snapshot);
                PGLog::Debug((std::string("Proving Grounds: challenge search resolved slot=") +
                    static_cast<char>('0' + slot) + (result.match.bestAvailable ? " best-available" : " in-band")).c_str());
            }
            else if (result.match.failure == TownMatchmakingPolicy::Match::PlayersTooWeak)
                g_unavailableReasons[slot] = "Selected fighter is too weak for this challenge.";
            else
            {
                const int rarity = TownChallengePolicy::OfferRarity(offer, card.division);
                const std::string anchor = rarity == 3 &&
                    TownChallengePolicy::NamedForDivision(offer.encounter, card.division) ?
                    TownChallengePolicy::Role(offer.encounter) : "";
                bool anchorReady = anchor.empty();
                for (size_t i = 0; i < snapshot.npcs.size(); ++i)
                    if (snapshot.npcs[i].role == anchor) anchorReady = true;
                g_unavailableReasons[slot] = anchorReady ?
                    "No suitable recovered opponents for this challenge." :
                    NamedOpponentUnavailableReason(offer, snapshot);
            }
        }

        TownMatchmakingPolicy::History history = card.playerHistory;
        for (int slot = 0; slot < 5; ++slot)
        {
            g_searching[slot] = g_searchQueued[slot];
            if (sameCards && card.offers[slot].state == TownArenaPolicy::OfferAvailable &&
                TownChallengePolicy::Resolved(g_previews[slot]))
            {
                bool opponentsReady = true;
                std::vector<std::string> opponentIds;
                for (int member = 0; member < TownChallengePolicy::Opponents(g_previews[slot]); ++member)
                {
                    const std::string id = TownChallengePolicy::OpponentId(g_previews[slot], member);
                    opponentIds.push_back(id);
                    if (!EligibleOpponent(snapshot, id)) opponentsReady = false;
                }
                if (opponentsReady)
                {
                    g_unavailableReasons[slot] = PreviewUnavailableReason(g_previews[slot], snapshot);
                    TownMatchmakingPolicy::RecordHistory(history, opponentIds);
                    continue;
                }
            }

            if (!g_searchQueued[slot] && !g_searchCompleted[slot])
            {
                g_previews[slot] = TownChallengePolicy::PreviewSource(card, slot);
                g_bestAvailable[slot] = false;
            }
            TownChallengePolicy::Offer& offer = g_previews[slot];
            if (offer.state != TownArenaPolicy::OfferAvailable) continue;
            if (!offer.generated)
            {
                g_unavailableReasons[slot] = PreviewUnavailableReason(offer, snapshot);
                continue;
            }
            if (g_searchCompleted[slot]) continue;
            TownChallengePolicy::ClearPreview(offer);
            offer.division = card.division;
            g_unavailableReasons[slot] = snapshot.playersReady ?
                "Searching eligible opponents..." : snapshot.playerBlocker;
            if (!snapshot.playersReady || g_searchQueued[slot]) continue;

            const int rarity = TownChallengePolicy::OfferRarity(offer, card.division);
            TownMatchmakingSearch::Request request;
            request.generation = g_searchGeneration;
            request.slot = slot;
            request.players = snapshot.players;
            request.npcs = snapshot.npcs;
            request.division = static_cast<TownMatchmakingPolicy::Division>(card.division);
            request.seed = offer.draw ^ (static_cast<unsigned>(offer.epoch) * 1664525u) ^
                (slot * 1013904223u);
            request.history = history;
            request.maxEnemies = 4;
            request.maxTotal = static_cast<int>(snapshot.medics.size());
            request.rarity = rarity;
            request.teams1v1 = offer.mode == TownChallengePolicy::ModeTeams1v1;
            request.challengeDifficulty = PGConfig::ChallengeValues().difficulty;
            if (rarity == 3 && TownChallengePolicy::NamedForDivision(offer.encounter, card.division)) {
                request.requiredRole = TownChallengePolicy::Role(offer.encounter);
                const unsigned victory = TownChallengePolicy::UniqueVictoryForEncounter(offer.encounter);
                const bool defeated = offer.encounter == TownChallengePolicy::EncounterSkarn ?
                    card.skarnWon : victory && (card.uniqueWins & victory) != 0;
                request.forceSoloNamed = !defeated;
            }
            g_searchQueued[slot] = TownMatchmakingSearch::Enqueue(request);
            g_searching[slot] = g_searchQueued[slot];
            if (!g_searchQueued[slot])
            {
                g_searchCompleted[slot] = true;
                g_unavailableReasons[slot] = "Background matchmaking is unavailable.";
            }
        }

        g_unavailableReasons[5].clear();
        g_searching[5] = false;
        g_directPreview = TownChallengePolicy::Offer();
        g_directPreview.generated = true;
        g_directPreview.division = card.division;
        if (snapshot.playersReady && snapshot.players.size() == 1)
        {
            const TownMatchmakingPolicy::Match direct = TownMatchmakingPolicy::SelectPlayer(
                snapshot.players, snapshot.npcs,
                static_cast<TownMatchmakingPolicy::Division>(card.division),
                card.seed ^ 0x12345678u, card.playerHistory, 1,
                static_cast<int>(snapshot.medics.size()));
            PopulatePreview(g_directPreview, snapshot, direct);
            g_directPreview.context = lineupKey;
        }
        for (int slot = 0; slot < 5; ++slot)
            if (TownChallengePolicy::Visible(card, slot))
                g_buyInQuotes[slot] = QuoteFor(g_previews[slot], snapshot, card.division);
    }
    TownChallengeBuyInPolicy::Quote GetChallengeBuyIn(int slot) {
        if (slot < 0 || slot >= 6 || !IsChallengeVisible(slot)) return TownChallengeBuyInPolicy::Quote();
        if ((slot == g_queuedChallenge || slot == g_activeChallenge) && g_buyInTicket.quote.Valid()) return g_buyInTicket.quote;
        const TownChallengePolicy::Card& card = LeaderboardStore::GetTownChallenges();
        if (card.offers[slot].state != TownArenaPolicy::OfferAvailable) return TownChallengeBuyInPolicy::Quote();
        return g_buyInQuotes[slot];
    }
    bool GetPlayerReadiness(Character* player, std::string& reason)
    {
        struct QuietScope {
            bool previous;
            QuietScope() : previous(g_quietDiscovery) { g_quietDiscovery = true; }
            ~QuietScope() { g_quietDiscovery = previous; }
        } quiet;
        if (!Ready(player, false, true, &reason)) return false;
        Building* registry = g_townRegistry.getBuilding();
        if (!registry || !registry->isValid() || player->getCurrentTownLocation() != registry->getTown())
        {
            reason = "not at this town registry";
            return false;
        }
        if (TownAftercare::IsManaged(player)) { reason = "still in aftercare"; return false; }
        if (TownMatchmakingRuntime::SelectionIdentity(player).empty()) { reason = "live character handle unavailable"; return false; }
        if (!(TownMatchmakingRuntime::Ability(player) > 0)) { reason = "combat stats unavailable"; return false; }
        reason.clear();
        return true;
    }
    static TownChallengePolicy::Offer DisplayedOffer(const TownChallengePolicy::Card& card, int slot)
    {
        TownChallengePolicy::Offer offer = slot == g_queuedChallenge || slot == g_activeChallenge ?
            g_challengeReservation : card.offers[slot].state == TownArenaPolicy::OfferAvailable ? g_previews[slot] :
            TownChallengePolicy::PreviewSource(card, slot);
        offer.state = card.offers[slot].state;
        return offer;
    }
    static std::string StoredPortraitName(const TownChallengePolicy::Offer& offer, int member) {
        std::string name = TownChallengePolicy::OpponentName(offer, member);
        // Older generated previews stored the catalog description with the name.
        // Remove only that exact known suffix, preserving parentheses in names.
        const TownFighterCatalog::Entry* entry = TownFighterCatalog::Find(offer.roles[member].c_str());
        if (entry) {
            const std::string suffix = std::string(" (") + TownChallengePolicy::DivisionName(entry->tier) + " " + entry->type + ")";
            if (name.size() >= suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
                name.resize(name.size() - suffix.size());
        }
        return name.empty() ? "Unavailable" : name;
    }
    void GetChallengePortraits(std::vector<Character*> (&opponents)[6], std::vector<std::string> (&names)[6])
    {
        const TownChallengePolicy::Card& card = Challenges();
        TownChallengePolicy::Offer offers[6];
        bool any = false;
        for (int slot = 0; slot < 6; ++slot) {
            opponents[slot].clear();
            names[slot].clear();
            if (slot == 5) {
                offers[slot] = TownChallengePolicy::Offer();
                offers[slot].encounter = TownChallengePolicy::EncounterSkarn;
                offers[slot].division = 2;
                opponents[slot].resize(1, NULL);
                names[slot].push_back(TownChallengePolicy::Title(TownChallengePolicy::EncounterSkarn));
                any = true;
                continue;
            }
            if (!TownChallengePolicy::Visible(card, slot)) continue;
            offers[slot] = DisplayedOffer(card, slot);
            if (TownChallengePolicy::Resolved(offers[slot])) {
                opponents[slot].resize(TownChallengePolicy::Opponents(offers[slot]), NULL);
                for (size_t member = 0; member < opponents[slot].size(); ++member)
                    names[slot].push_back(StoredPortraitName(offers[slot], static_cast<int>(member)));
            } else if (offers[slot].generated &&
                TownChallengePolicy::OfferRarity(offers[slot], offers[slot].division) == 3 &&
                TownChallengePolicy::NamedForDivision(offers[slot].encounter, offers[slot].division)) {
                // A named Legendary anchor remains the intended opponent even
                // while recovering, so the card can show a faded live portrait.
                opponents[slot].resize(1, NULL);
                names[slot].push_back(TownChallengePolicy::Title(offers[slot].encounter));
            } else continue;
            any = true;
        }
        Building* registry = g_townRegistry.getBuilding();
        if (!any || !ou || !registry || !registry->isValid() || !registry->getTown()) return;
        lektor<RootObject*> nearby;
        ou->getObjectsWithinSphere(nearby, registry->getPosition(), 2500.0f, CHARACTER, 512, NULL);
        std::vector<Character*> actors;
        for (uint32_t i = 0; i < nearby.size(); ++i) {
            Character* c = static_cast<Character*>(nearby[i]);
            if (!c || !c->isValid() || c->getCurrentTownLocation() != registry->getTown() ||
                std::find(actors.begin(), actors.end(), c) != actors.end()) continue;
            actors.push_back(c);
        }
        // Match the booking identity, including legacy keys. An ambiguous key
        // gets no portrait, even when only one of its namesakes is fight-ready.
        for (int slot = 0; slot < 6; ++slot)
            for (size_t member = 0; member < opponents[slot].size(); ++member) {
                Character* match = NULL;
                const std::string& opponentId = TownChallengePolicy::OpponentId(offers[slot], static_cast<int>(member));
                const char* intendedRole = opponentId.empty() && member == 0 ? TownChallengePolicy::Role(offers[slot].encounter) : "";
                for (size_t i = 0; i < actors.size(); ++i)
                    if ((!opponentId.empty() && MatchesOpponent(actors[i], opponentId)) ||
                        (opponentId.empty() && intendedRole[0] && TownArenaRuntimePolicy::Equals(Id(actors[i]), intendedRole))) {
                        if (match) { match = NULL; break; }
                        match = actors[i];
                    }
                opponents[slot][member] = match;
                if (match) names[slot][member] = match->getName();
            }
    }
    int GetChallengeRarity(int slot)
    {
        if (slot < 0 || slot >= 5) return -1;
        const TownChallengePolicy::Card& card = Challenges();
        if (!TownChallengePolicy::Visible(card, slot)) return -1;
        const TownChallengePolicy::Offer offer = DisplayedOffer(card, slot);
        return TownChallengePolicy::OfferRarity(offer, card.division);
    }
    bool IsUniqueLegendaryChallenge(int slot)
    {
        if (slot < 0 || slot >= 5) return false;
        const TownChallengePolicy::Card& card = Challenges();
        if (!TownChallengePolicy::Visible(card, slot)) return false;
        const TownChallengePolicy::Offer offer = DisplayedOffer(card, slot);
        return TownChallengePolicy::OfferRarity(offer, card.division) == 3 &&
            TownChallengePolicy::NamedForDivision(offer.encounter, card.division);
    }
    std::string GetChallengeUnavailableReason(int slot)
    {
        if (slot < 0 || slot >= 5) return "";
        const TownChallengePolicy::Card& card = Challenges();
        const TownChallengePolicy::Offer offer = DisplayedOffer(card, slot);
        return offer.state == TownArenaPolicy::OfferAvailable ? g_unavailableReasons[slot] : std::string();
    }
    std::string GetDirectCaption() {
        if (g_bookingPending) return "Cancel booking";
        if (!TownChallengePolicy::Resolved(g_directPreview)) return "1v1: select one fighter with suitable recovered opposition";
        char marks[40]; sprintf_s(marks, " | Marks: %.2fx", ArenaMarks::DivisionMultiplier(GetDivision()));
        return "Book " + std::string(TownChallengePolicy::DivisionName(GetDivision())) + " 1v1 vs " + g_directPreview.name + marks;
    }
    std::string GetProgressCaption()
    {
        const TownChallengePolicy::Card& card = LeaderboardStore::GetTownChallenges();
        char text[128];
        unsigned completed = 0;
        for (unsigned bit = 1; bit <= TownChallengePolicy::UniqueVeyr; bit <<= 1)
            if (card.uniqueWins & bit) ++completed;
        sprintf_s(text, "Challenge wins: %u | Skarn unlock: %u / 5 unique fighters defeated",
            card.challengeWins, completed);
        return text;
    }

    void OnMatchResult(const SparPodium::Snapshot& result)
    {
        if (g_resultRecorded || !OwnsMatch()) return;
        TownSpectators::ReactToResult(result);
        TownSpectators::Release();
        g_resultRecorded = true;
        for (int i = 0; i < g_fighterCount; ++i) {
            if (g_sequentialCare && SparSession::IsEliminated(g_fighters[i].getCharacter())) continue;
            if ((result.outcome == SparPodium::OutcomeTeamAWins && g_fighterTeams[i] == MatchRules::TeamA) ||
                (result.outcome == SparPodium::OutcomeTeamBWins && g_fighterTeams[i] == MatchRules::TeamB))
                TownAftercare::MarkWinner(g_fighters[i].getCharacter());
        }
        if (!g_playerMatch)
        {
            std::vector<std::string> history;
            for (int i = 0; i < g_fighterCount; ++i) {
                const std::string id = PersistentId(g_fighters[i].getCharacter());
                if (!id.empty()) history.push_back(id);
            }
            TownMatchmakingPolicy::RecordHistory(LeaderboardStore::GetTownChallenges().ambientHistory, history);
            TownBookie::Settle(result.outcome == SparPodium::OutcomeTeamAWins ? 0 :
                result.outcome == SparPodium::OutcomeTeamBWins ? 1 : -1);
            TownDiagnosticRun::CompletePending(result,ou ? ou->getTimeStamp_inGameHours().getTotalHours() : 0);
            return;
        }
        const bool playerWon = result.outcome == SparPodium::OutcomeTeamAWins;
        SettleChallenge(playerWon ? 1 : result.outcome == SparPodium::OutcomeTeamBWins ? 0 : -1);
        const double endedHours = ou ? ou->getTimeStamp_inGameHours().getTotalHours() : -1.0;
        if (g_activeChallenge >= 0)
            TownChallengePolicy::RecordSkarnChallengeEnd(
                LeaderboardStore::GetTownChallenges(), g_challengeReservation, endedHours);
        std::vector<std::string> history;
        for (int i = g_playerCount; i < g_fighterCount; ++i) {
            const std::string id = PersistentId(g_fighters[i].getCharacter());
            if (!id.empty()) history.push_back(id);
        }
        TownMatchmakingPolicy::RecordHistory(LeaderboardStore::GetTownChallenges().playerHistory, history);
        // Town challenges win with Team A. Stops, draws and Team B victories
        // never grant unique-fighter progress.
        if (!playerWon) return;
        unsigned defeated = 0;
        // Use the reserved lineup first: defeated/removed actors can already
        // have an invalid live handle by the time the podium result arrives.
        for (int member = 0; member < TownChallengePolicy::Opponents(g_challengeReservation); ++member)
            for (int encounter = TownChallengePolicy::EncounterSenn;
                encounter <= TownChallengePolicy::EncounterVeyr; ++encounter)
                if (encounter != TownChallengePolicy::EncounterSkarn &&
                    TownArenaRuntimePolicy::Equals(g_challengeReservation.roles[member].c_str(),
                        TownChallengePolicy::Role(encounter)))
                    defeated |= TownChallengePolicy::UniqueVictoryForEncounter(encounter);
        for (int i = g_playerCount; i < g_fighterCount; ++i)
        {
            Character* opponent = g_fighters[i].getCharacter();
            if (!opponent || !opponent->isValid()) continue;
            for (int encounter = TownChallengePolicy::EncounterSenn;
                encounter <= TownChallengePolicy::EncounterVeyr; ++encounter)
                if (encounter != TownChallengePolicy::EncounterSkarn &&
                    TownArenaRuntimePolicy::Equals(Id(opponent), TownChallengePolicy::Role(encounter)))
                    defeated |= TownChallengePolicy::UniqueVictoryForEncounter(encounter);
        }
        TownChallengePolicy::Card& card = LeaderboardStore::GetTownChallenges();
        TownChallengePolicy::RecordVictory(card, g_activeChallenge >= 0, true, defeated);
        PGLog::Debug(("Proving Grounds: shared town progression updated - " + GetProgressCaption()).c_str());
    }
    void OnMatchAborted()
    {
        if (!g_playerMatch)
        {
            TownDiagnosticRun::AbortPending();
            return;
        }
        const double endedHours = ou ? ou->getTimeStamp_inGameHours().getTotalHours() : -1.0;
        if (g_activeChallenge >= 0)
            TownChallengePolicy::RecordSkarnChallengeEnd(
                LeaderboardStore::GetTownChallenges(), g_challengeReservation, endedHours);
    }
    std::string GetChallengeCaption(int slot, bool includeLineup)
    {
        if (slot < 0 || slot >= 5) return "Unavailable";
        const TownChallengePolicy::Card& card = Challenges();
        if (!TownChallengePolicy::Visible(card, slot)) return "";
        TownChallengePolicy::Offer offer = DisplayedOffer(card, slot);
        const int rarity = TownChallengePolicy::OfferRarity(offer, card.division);
        const char* challengeLabel = rarity == 3 &&
            TownChallengePolicy::NamedForDivision(offer.encounter, card.division) ?
            "Unique" : TownChallengePolicy::RarityName(rarity);
        if (offer.generated && !TownChallengePolicy::Resolved(offer) && offer.state == TownArenaPolicy::OfferConsumed)
            return std::string(challengeLabel) +
                " | " + TownChallengePolicy::DivisionName(card.division) + " | " +
                TownChallengePolicy::ModeName(offer.mode) + "\nAttempt used";
        char format[32];
        sprintf_s(format, "%dv%d", TownChallengePolicy::Players(offer), TownChallengePolicy::Opponents(offer));
        std::string lineup = offer.name.empty() ? TownChallengePolicy::Title(offer.encounter) : offer.name;
        for (int i = 1; i < TownChallengePolicy::Opponents(offer); ++i)
            lineup += " + " + (TownChallengePolicy::OpponentName(offer, i).empty() ?
                std::string(TownChallengePolicy::Title(TownChallengePolicy::OpponentRole(offer, i))) : TownChallengePolicy::OpponentName(offer, i));
        const bool resolved = TownChallengePolicy::Resolved(offer);
        std::string text = std::string(challengeLabel) +
            " | " + TownChallengePolicy::DivisionName(card.division) + " | " +
            TownChallengePolicy::ModeName(offer.mode) + " | " + format + "\n" +
            (includeLineup ? lineup + " | " : std::string());
        if (!offer.generated) text = "Legacy | " + text;
        if (offer.generated && !resolved && offer.state == TownArenaPolicy::OfferAvailable)
            return std::string(challengeLabel) +
                " | " + TownChallengePolicy::DivisionName(card.division) + " | " +
                TownChallengePolicy::ModeName(offer.mode) + "\n" + g_unavailableReasons[slot];
        return text + (offer.state == TownArenaPolicy::OfferConsumed ? "Attempt used" :
            offer.state == TownArenaPolicy::OfferBooked ? "Reserved" : !resolved ? "Opponent unavailable" : "Book");
    }
    bool BookChallenge(const std::vector<Character*>& players, int slot)
    {
        if (slot < 0 || slot >= 5 || !LeaderboardStore::HasActiveSave())
        {
            Status("Load or start a game before booking challenges");
            return false;
        }
        if (g_activeChallenge >= 0 || g_bookingPending)
        {
            Status("Finish the current challenge or cancel the pending booking first");
            return false;
        }
        TownChallengePolicy::Card& card = Challenges();
        TownChallengePolicy::Offer& stored = TownChallengePolicy::At(card, slot);
        TownChallengePolicy::Offer offer = g_previews[slot];
        TownChallengePolicy::Card proposed = card;
        TownChallengePolicy::At(proposed, slot) = offer;
        if (!TownChallengePolicy::CanBookDivision(proposed, slot)) {
            Status("This encounter is not bookable in the selected division"); return false;
        }
        if (stored.state != TownArenaPolicy::OfferAvailable || offer.epoch != stored.epoch || !TownChallengePolicy::Resolved(offer))
        {
            if (slot < 5 && stored.state == TownArenaPolicy::OfferAvailable && offer.generated &&
                !TownChallengePolicy::Resolved(offer) && !g_unavailableReasons[slot].empty())
                Status(g_unavailableReasons[slot], true);
            else Status("Challenge is reserved, used, or an opponent is unavailable");
            return false;
        }
        const int required = TownChallengePolicy::Players(offer);
        const double gameHours = ou ? ou->getTimeStamp_inGameHours().getTotalHours() : -1.0;
        if (TownChallengePolicy::ChallengeCooldownActive(card, offer, gameHours,
            PGConfig::ChallengeValues().cooldownHours))
        {
            const double remaining = PGConfig::ChallengeValues().cooldownHours -
                (gameHours - card.skarnLastEnd);
            char message[128];
            sprintf_s(message, "Skarn cooldown: %.1f in-game hours remaining", remaining);
            Status(message);
            return false;
        }
        if (static_cast<int>(players.size()) != required)
        {
            char message[96];
            sprintf_s(message, "Select exactly %d squad fighter%s for this challenge", required, required == 1 ? "" : "s");
            Status(message);
            return false;
        }
        Building* registry = g_townRegistry.getBuilding();
        for (size_t i = 0; i < players.size(); ++i)
        {
            if (!registry || !registry->isValid() || !Ready(players[i], false, true) || players[i]->getCurrentTownLocation() != registry->getTown())
            {
                Status("Every selected fighter must be recovered and in town");
                return false;
            }
            for (size_t j = 0; j < i; ++j) if (players[i] == players[j]) return false;
        }
        const TownMatchmakingRuntime::Snapshot fresh = SnapshotFor(players);
        if (offer.generated) {
            if (PlayerLineupKey(players) != offer.context || offer.division != card.division) {
                g_previewKey.clear(); g_previewTick = 0;
                UpdateMatchmakingPreview(players);
                Status("Selected lineup changed; review the refreshed cards before booking");
                return false;
            }
        }
        double ownScore = 0, enemyScore = 0;
        const TownChallengeBuyInPolicy::Quote price = QuoteFor(offer, fresh, card.division, &ownScore, &enemyScore);
        const TownChallengeBuyInPolicy::Quote displayed = GetChallengeBuyIn(slot);
        if (!price.Valid() || price.buyIn != displayed.buyIn || price.payout != displayed.payout) {
            g_previewTick = 0; UpdateMatchmakingPreview(players);
            Status("Review the refreshed buy-in and odds before booking this challenge"); return false;
        }
        PayChallengeCredit();
        TownChallengeBuyInPolicy::Account& account = LeaderboardStore::GetChallengeAccount();
        const int balance = TownChallengeWallet::Balance();
        if (!TownChallengeBuyInPolicy::CanAccept(account, g_buyInTicket, price, balance)) {
            char message[128]; sprintf_s(message, "Challenge requires %d Cats; balance: %d Cats. Collect any pending return first.", price.buyIn, balance);
            Status(message, true); return false;
        }
        if (!ReservePlayer(players[0])) return false;
        g_bookedTeam.clear();
        for (size_t i = 0; i < players.size(); ++i) { hand fighter; fighter = players[i]; g_bookedTeam.push_back(fighter); }
        g_queuedChallenge = slot;
        if (!TownChallengePolicy::ReserveOffer(stored, offer, g_challengeReservation)) {
            CancelBooking(); return false;
        }
        if (!TownChallengeWallet::Take(price.buyIn)) {
            CancelBooking(); Status("Could not collect the challenge buy-in", true); return false;
        }
        TownChallengeBuyInPolicy::Accept(account, g_buyInTicket, price, balance);
        g_paidRefreshCountBeforeBooking = card.paidRefreshCount;
        g_paidRefreshResetPending = true;
        card.paidRefreshCount = 0;
        CombatBalanceLog::ChallengeCard booked;
        booked.slot = slot; booked.division = GetDivision();
        booked.rarity = TownChallengePolicy::OfferRarity(offer, card.division);
        booked.encounter = offer.encounter; booked.format = offer.format; booked.mode = offer.mode;
        booked.players = static_cast<int>(players.size());
        booked.enemies = TownChallengePolicy::Opponents(offer);
        booked.ownScore = ownScore; booked.enemyScore = enemyScore;
        for (int i = 0; i < booked.enemies && i < 4; ++i) {
            booked.opponents[i] = TownChallengePolicy::OpponentId(offer, i);
            booked.opponentNames[i] = TownChallengePolicy::OpponentName(offer, i);
            booked.opponentRoles[i] = offer.roles[i];
        }
        booked.stake = price.buyIn; booked.payout = price.payout;
        CombatBalanceLog::ChallengeAccepted(booked);
        CancelUpcomingForPlayer();
        char receipt[128]; sprintf_s(receipt, " | Buy-in: %d Cats | Winning total return: %d Cats", price.buyIn, price.payout);
        Status("Challenge reserved: " + GetChallengeCaption(slot) + receipt);
        return true;
    }
    void BindRegistry(Building* registry)
    {
        if (ArenaIdentity::IsTownRegistry(registry) && ArenaIdentity::IsRegistryFinished(registry))
            g_townRegistry = registry;
    }
    bool BindRegistryNear(Character* character)
    {
        if (!ou || !character || !character->isValid() || !character->getCurrentTownLocation()) return false;
        lektor<RootObject*> nearby;
        ou->getObjectsWithinSphere(nearby, character->getPosition(), 2500, BUILDING, 512, NULL);
        for (uint32_t i = 0; i < nearby.size(); ++i)
        {
            if (!nearby[i] || !nearby[i]->isValid() || !ArenaIdentity::IsTownRegistry(nearby[i])) continue;
            Building* registry = static_cast<Building*>(nearby[i]);
            if (registry->getTown() == character->getCurrentTownLocation() && ArenaIdentity::IsRegistryFinished(registry))
            {
                BindRegistry(registry);
                return true;
            }
        }
        return false;
    }
    bool IsScheduleEnabled() { return !g_schedulePaused; }
    bool HasBooking() { return g_bookingPending; }
    bool IsPlayerMatch() { return g_playerMatch; }
    bool IsLineupLocked() { return g_bookingPending || (g_playerMatch && g_phase != Aftercare); }
    bool IsChallengeSearching(int slot) {
        return slot >= 0 && slot < 6 && !IsLineupLocked() && g_searching[slot];
    }
    bool IsBestAvailableChallenge(int slot) {
        return slot >= 0 && slot < 5 && TownChallengePolicy::Resolved(g_previews[slot]) &&
            g_bestAvailable[slot];
    }
    unsigned GetUniqueWins() { return Challenges().uniqueWins; }
    bool IsSkarnUnlocked() { return TownChallengePolicy::SkarnUnlocked(Challenges()); }
    float GetActiveMarksMultiplier() {
        return g_playerMatch && OwnsMatch() ? g_activeMarksMultiplier : 1.0f;
    }
    int GetActiveMarksBonus() {
        return g_playerMatch && OwnsMatch() ? g_activeMarksBonus : 0;
    }
    void CancelBooking()
    {
        if (g_queuedChallenge >= 0) SettleChallenge(-1);
        if (g_queuedChallenge >= 0)
            TownArenaPolicy::CancelBooking(TownChallengePolicy::At(LeaderboardStore::GetTownChallenges(), g_queuedChallenge).state);
        g_queuedChallenge = -1;
        g_bookingPending = false;
        g_bookedPlayer.setNull();
        g_bookedTeam.clear();
        g_bookedRegistry.setNull();
        Status("Town booking cancelled");
        g_directBooking = TownChallengePolicy::Offer();
        if (g_activeChallenge < 0) g_challengeReservation = TownChallengePolicy::Offer();
        g_previewTick = 0;
    }
    void SetScheduleEnabled(bool enabled)
    {
        g_schedulePaused = !enabled;
        if (!enabled) CancelPlannedNpcBout("NPC scheduling paused");
        g_lastScheduleCheck = 0;
        Status(enabled ? "Ambient NPC bouts resumed" :
            "Ambient NPC bouts paused; current fight continues");
    }
    bool ReservePlayer(Character* player)
    {
        if (g_bookingPending)
        {
            Status("A town booking already holds the next slot");
            return false;
        }
        Building* registry = g_townRegistry.getBuilding();
        if (!registry || !ArenaIdentity::IsRegistryFinished(registry) ||
            !Ready(player, false, true) || player->getCurrentTownLocation() != registry->getTown())
        {
            Status("Booking needs a recovered squad member at Scratch's Registry");
            return false;
        }
        g_bookedPlayer = player;
        g_bookedTeam.clear();
        g_bookedTeam.push_back(g_bookedPlayer);
        g_bookedRegistry = registry;
        g_bookingPending = true;
        g_lastScheduleCheck = 0;
        Status("Next town 1v1 slot reserved for " + player->getName());
        return true;
    }
    bool BookPlayer(Character* player) {
        if (!LeaderboardStore::HasActiveSave()) { Status("Arena progress is unavailable; resolve the persistence error and reload", true); return false; }
        if (g_bookingPending || g_playerMatch) { Status("Finish or cancel the current player booking first"); return false; }
        std::vector<Character*> players(1, player);
        const TownMatchmakingRuntime::Snapshot fresh = SnapshotFor(players);
        if (g_directPreview.division != GetDivision() || g_directPreview.context != PlayerLineupKey(players)) {
            g_previewKey.clear(); g_previewTick = 0;
            UpdateMatchmakingPreview(players);
            Status("Selected lineup changed; review the current 1v1 opponent before booking");
            return false;
        }
        if (!fresh.playersReady || !TownChallengePolicy::Resolved(g_directPreview) ||
            !PreviewUnavailableReason(g_directPreview, fresh).empty()) {
            Status("The displayed 1v1 lineup is currently unavailable");
            return false;
        }
        if (!ReservePlayer(player)) return false;
        g_directBooking = g_directPreview;
        CancelUpcomingForPlayer();
        Status("1v1 reserved vs " + g_directBooking.name);
        return true;
    }
    bool IsBusy() { return g_phase != Idle; }
    bool IsAftercare() { return g_phase == Aftercare; }
    const std::string& GetStatus() { return g_status; }

    bool IsFighter(Character* c)
    {
        if (!c || (g_phase != Preparing && g_phase != Fighting)) return false;
        for (int i = 0; i < g_fighterCount; ++i) if (g_fighters[i] == c) return true;
        return false;
    }
    bool IsIncidentPair(Character* a, Character* b)
    {
        if (!a || !b) return false;
        int sideA = -1, sideB = -1;
        for (int i = 0; i < g_fighterCount; ++i)
        {
            const int side = g_fighterTeams[i] == MatchRules::TeamA ? 0 : 1;
            if (g_fighters[i] == a) sideA = side;
            if (g_fighters[i] == b) sideB = side;
        }
        return TownArenaRuntimePolicy::RetainedIncidentPair(
            g_phase == Fighting || g_phase == Aftercare || (g_phase == Preparing && OwnsMatch()), sideA, sideB);
    }
    bool IsActiveCollateralIncidentPair(Character* a, Character* b)
    {
        if (!a || !b || g_phase != Fighting || !OwnsMatch()) return false;
        bool fighterA = false, fighterB = false;
        for (int i = 0; i < g_fighterCount; ++i)
        {
            if (g_fighters[i] == a) fighterA = true;
            if (g_fighters[i] == b) fighterB = true;
        }
        return fighterA != fighterB;
    }
    bool IsIncidentFactionPair(Faction* a, Faction* b)
    {
        if (!a || !b || a == b || (g_phase != Fighting && g_phase != Aftercare))
            return false;
        for (int i = 0; i < g_fighterCount; ++i)
        {
            Character* first = g_fighters[i].getCharacter();
            if (!first || !first->isValid() || first->getFaction() != a) continue;
            for (int j = 0; j < g_fighterCount; ++j)
            {
                if (g_fighterTeams[i] == g_fighterTeams[j]) continue;
                Character* second = g_fighters[j].getCharacter();
                if (second && second->isValid() && second->getFaction() == b)
                    return true;
            }
        }
        return false;
    }
    bool OwnsMatch()
    {
        if (!SparSession::IsActive() || g_fighterCount < 2 || SparSession::GetParticipantCount() != g_fighterCount) return false;
        for (int i = 0; i < g_fighterCount; ++i) if (!IsFighter(SparSession::GetParticipant(i))) return false;
        return true;
    }

    void OnMatchStarted()
    {
        if (g_phase == Preparing && OwnsMatch())
        {
            // The preview refresh replaces only unavailable opponents, keeping
            // unrelated advertised cards stable when an ambient bout starts.
            g_previewTick = 0;
            if (g_activeChallenge >= 0)
                TownArenaPolicy::BeginOffer(TownChallengePolicy::At(LeaderboardStore::GetTownChallenges(), g_activeChallenge).state);
            g_phase = Fighting;
            g_sequentialCare = SparSession::GetMode() == MatchRules::ModeTeams1v1;
            g_aftercareElapsed = 0.0f; g_aftercareTick = GetTickCount(); g_lastAid = 0;
            const bool wagerPending = TownBookie::HasPendingWager();
            TownBookie::CombatStarted();
            if (wagerPending && ou)
                ou->showPlayerAMessage("Your wagered fight is starting.", true);
            TownAftercare::Standby();
            Status(g_playerMatch ? "Booked town fight underway" : "Town 1v1 underway");
        }
    }

    bool BeginEvent(Character* player, Building* requestedRegistry)
    {
        if (!LeaderboardStore::HasActiveSave()) { Status("Arena progress is unavailable; resolve the persistence error and reload", true); return false; }
        if (!ou || WorldLifecycle::IsArenaOperationBusy())
        {
            Status("Arena is busy; finish the current operation first");
            return false;
        }
        Building* registry = requestedRegistry;
        if (!registry || !ArenaIdentity::IsRegistryFinished(registry) || !registry->getTown())
        {
            Status("Interact with Scratch's Registry before starting the town trial");
            return false;
        }
        lektor<RootObject*> buildings;
        ou->getObjectsWithinSphere(buildings, registry->getPosition(), 2500.0f,
            BUILDING, 512, NULL);
        Building* arena = NULL;
        float nearest = 1.0e30f;
        for (uint32_t i = 0; i < buildings.size(); ++i)
        {
            RootObject* obj = buildings[i];
            if (!obj || !obj->isValid() ||
                !TownArenaRuntimePolicy::IsNewArena(ArenaIdentity::GetStringId(obj))) continue;
            Building* b = static_cast<Building*>(obj);
            if (b->getTown() != registry->getTown()) continue;
            Building::ConstructionState* state = b->getBuildState();
            if (state && !state->isComplete) continue;
            const float distance = (b->getPosition() - registry->getPosition()).squaredLength();
            if (distance < nearest) { arena = b; nearest = distance; }
        }
        if (!arena)
        {
            Status("No finished Rust Crucible found in this Registry's town");
            return false;
        }

        lektor<RootObject*> nearby;
        ou->getObjectsWithinSphere(nearby, arena->getPosition(), 2500.0f,
            CHARACTER, 512, NULL);
        // Include the same search area that resolved the advertised card NPC.
        lektor<RootObject*> registryNearby;
        const bool challenge = player != NULL; // Direct duels also have a frozen opposing lineup.
        TownChallengePolicy::Offer* queuedOffer = !player ? NULL : g_queuedChallenge >= 0 ?
            &g_challengeReservation : &g_directBooking;
        TownChallengePolicy::Card reservedCard = LeaderboardStore::GetTownChallenges();
        if (player && g_queuedChallenge >= 0)
            TownChallengePolicy::At(reservedCard, g_queuedChallenge) = *queuedOffer;
        if (player && (!TownChallengePolicy::Resolved(*queuedOffer) ||
            (g_queuedChallenge >= 0 && !TownChallengePolicy::CanBookDivision(reservedCard, g_queuedChallenge)))) {
            Status("The reserved lineup or division is no longer valid; cancel the booking"); return false;
        }
        ou->getObjectsWithinSphere(registryNearby, registry->getPosition(), 2500.0f, CHARACTER, 512, NULL);
        std::vector<Character*> fighters;
        std::vector<Character*> challengeMatches;
        std::string challengeBlocker;
        std::vector<Character*> doctors;
        for (uint32_t i = 0; i < nearby.size() + registryNearby.size(); ++i)
        {
            RootObject* obj = i < nearby.size() ? nearby[i] : registryNearby[i - nearby.size()];
            if (!obj || !obj->isValid()) continue;
            Character* c = static_cast<Character*>(obj);
            if (c->getCurrentTownLocation() != arena->getTown()) continue;
            bool fighter = !challenge && TownArenaRuntimePolicy::IsRegularFighter(Id(c));
            if (queuedOffer)
                for (int enemy = 0; enemy < TownChallengePolicy::Opponents(*queuedOffer); ++enemy)
                    if (MatchesOpponent(c, TownChallengePolicy::OpponentId(*queuedOffer, enemy))) fighter = true;
            if (challenge && fighter)
            {
                bool seen = false;
                for (size_t j = 0; j < challengeMatches.size(); ++j)
                    if (challengeMatches[j] == c) seen = true;
                if (!seen) challengeMatches.push_back(c);
            }
            const bool doctor = TownArenaRuntimePolicy::IsMedic(Id(c));
            if (!fighter && !doctor) continue;
            if (!Ready(c, doctor, false, challenge && fighter ? &challengeBlocker : NULL)) continue;
            if (fighter)
            {
                bool duplicate = false;
                for (size_t j = 0; j < fighters.size(); ++j)
                    if (fighters[j] == c) duplicate = true;
                if (!duplicate) fighters.push_back(c);
            }
            if (doctor && !c->isCarryingSomething && !c->isBeingCarried())
            {
                bool duplicate = false;
                for (size_t j = 0; j < doctors.size(); ++j) if (doctors[j] == c) duplicate = true;
                if (!duplicate && doctors.size() < 10) doctors.push_back(c);
            }
        }
        int npcTeamSize = 0;
        TownMatchmakingPolicy::Match ambientMatch;
        int neededEnemies = challenge ? TownChallengePolicy::Opponents(*queuedOffer) : player ? 1 : 0;
        if (challenge)
        {
            fighters.clear();
            const TownChallengePolicy::Offer& offer = *queuedOffer;
            for (int enemy = 0; enemy < neededEnemies; ++enemy)
            {
                const std::string& id = TownChallengePolicy::OpponentId(offer, enemy);
                const std::string& name = TownChallengePolicy::OpponentName(offer, enemy);
                int matches = 0;
                Character* candidate = NULL;
                for (size_t j = 0; j < challengeMatches.size(); ++j)
                    if (MatchesOpponent(challengeMatches[j], id)) { ++matches; candidate = challengeMatches[j]; }
                if (matches != 1)
                {
                    Status("Challenge waiting for " + name + (matches == 0 ? ": not found near town" : ": ambiguous identity"));
                    return false;
                }
                if (!Ready(candidate, false, false, &challengeBlocker))
                {
                    Status("Challenge waiting for " + name + ": " + challengeBlocker);
                    return false;
                }
                if (TownAftercare::IsManaged(candidate) || TownLimbShop::IsManaged(candidate)) {
                    Status("Reserved opponent is still recovering"); return false;
                }
                fighters.push_back(candidate);
            }
        }
        if (!challenge && !player)
        {
            const std::vector<Character*> noPlayers;
            const TownMatchmakingRuntime::Snapshot snapshot = SnapshotFor(noPlayers);
            // Never reroll or reprice an advertised lineup at staging time.
            ambientMatch = g_plannedNpc;
            if (!ambientMatch.valid)
            {
                Status("Town bout waiting: " + snapshot.availability +
                    (snapshot.npcs.size() < 2 ? "; need at least 2 eligible opponents" :
                    snapshot.medics.size() < 2 ? "; need at least 2 available medics" :
                    "; no balanced lineup within medical capacity"));
                return false;
            }
            if (g_plannedRegistry.getBuilding() != registry || !PlannedNpcReady()) {
                CancelPlannedNpcBout("An announced fighter or arena is unavailable");
                return false;
            }
            for (int side = 0; side < 2; ++side) {
                const std::vector<std::string>& ids = side == 0 ? ambientMatch.a : ambientMatch.b;
                for (size_t i = 0; i < ids.size(); ++i) {
                    bool eligible = false;
                    for (size_t j = 0; j < snapshot.npcs.size(); ++j)
                        if (snapshot.npcs[j].id == ids[i]) eligible = true;
                    if (!eligible) {
                        CancelPlannedNpcBout("An announced fighter is no longer eligible");
                        return false;
                    }
                }
            }
            doctors = snapshot.medics;
            fighters.clear();
            npcTeamSize = static_cast<int>(ambientMatch.a.size());
            for (size_t i = 0; i < ambientMatch.a.size(); ++i) fighters.push_back(TownMatchmakingRuntime::Resolve(snapshot, ambientMatch.a[i]));
            for (size_t i = 0; i < ambientMatch.b.size(); ++i) fighters.push_back(TownMatchmakingRuntime::Resolve(snapshot, ambientMatch.b[i]));
            neededEnemies = static_cast<int>(fighters.size());
            char balance[160];
            sprintf_s(balance, "Proving Grounds: ambient matchup %uv%u scores %.2f / %.2f", static_cast<unsigned>(ambientMatch.a.size()),
                static_cast<unsigned>(ambientMatch.b.size()), ambientMatch.scoreA, ambientMatch.scoreB);
            PGLog::Debug(balance);
        }
        if (static_cast<int>(fighters.size()) < neededEnemies)
        {
            Status("Town booking waiting for recovered opponents");
            return false;
        }
        if (doctors.empty())
        {
            Status("Pit fighters are ready, but no Scratch medic is fit and available");
            return false;
        }
        std::vector<Character*> pair;
        std::vector<MatchRules::MatchTeam> teams;
        if (player)
        {
            for (size_t i = 0; i < g_bookedTeam.size(); ++i)
            {
                Character* member = g_bookedTeam[i].getCharacter();
                if (!Ready(member, false, true) || member->getCurrentTownLocation() != registry->getTown())
                {
                    Status("Town booking waiting for every selected fighter to recover and return");
                    return false;
                }
                pair.push_back(member);
                teams.push_back(MatchRules::TeamA);
            }
            if (pair.empty()) return false;
        }
        const int playerCount = static_cast<int>(pair.size());
        for (int i = 0; i < neededEnemies; ++i)
        {
            if (!fighters[i] || !fighters[i]->isValid()) return false;
            pair.push_back(fighters[i]);
            teams.push_back(!player && i < npcTeamSize ? MatchRules::TeamA : MatchRules::TeamB);
        }
        const MatchRules::MatchMode matchMode = player && g_queuedChallenge >= 0 ?
            MatchModeFor(g_challengeReservation) : MatchRules::ModeTeamAvB;
        if (pair.size() > 16) return false;
        if (!TownAftercare::ControlAvailable()) {
            Status("Town booking unavailable: medic AI control could not be installed");
            return false;
        }
        const int requiredMedics = TownAftercarePolicy::RequiredMedics(static_cast<int>(pair.size()));
        if (static_cast<int>(doctors.size()) < requiredMedics)
        {
            char waiting[128];
            sprintf_s(waiting, "Town booking waiting for medics: %u/%d fit and available",
                static_cast<unsigned>(doctors.size()), requiredMedics);
            Status(waiting);
            return false;
        }
        doctors.resize(requiredMedics);
        g_fighterCount = static_cast<int>(pair.size());
        g_playerCount = playerCount;
        for (int i = 0; i < g_fighterCount; ++i)
        {
            g_fighters[i] = pair[i];
            g_fighterTeams[i] = teams[i];
        }
        float standbyX, standbyZ;
        const Ogre::Vector3 towardRegistry = registry->getPosition() - arena->getPosition();
        TownArenaRuntimePolicy::MedicStandbyOffset(towardRegistry.x, towardRegistry.z, standbyX, standbyZ);
        // Hand the ring guard the arena it is containing fighters in. The arena

        // building is the ring's centre, resolved here at the moment the bout is

        // armed, so the guard never has to re-derive the site on a tick.

        ArenaRingGuard::SetArena(arena);

        TownAftercare::Begin(doctors, pair, arena, arena->getPosition() + Ogre::Vector3(standbyX, 0.0f, standbyZ));
        TownCorpseCleanup::Begin(arena, pair);
        g_previousProtocol = ArenaMedical::GetProtocol();
        ArenaMedical::SetProtocol(ArenaMedical::RingsideAid);
        g_phase = Preparing;
        g_resultRecorded = false;
        g_activeChallenge = player ? g_queuedChallenge : -1;
        g_playerMatch = player != NULL;
        g_activeMarksMultiplier = player ? ArenaMarks::ChallengeMultiplier(GetDivision(),
            g_activeChallenge >= 0 ? GetChallengeRarity(g_activeChallenge) : 0) *
            static_cast<float>(PGConfig::ChallengeValues().winMarksMultiplier) : 1.0f;
        const int activeEncounter = g_activeChallenge >= 0 ? g_challengeReservation.encounter : -1;
        const bool skarnChallenge = activeEncounter == TownChallengePolicy::EncounterSkarn;
        const bool uniqueChallenge = skarnChallenge ||
            TownChallengePolicy::UniqueVictoryForEncounter(activeEncounter) != 0;
        g_activeMarksBonus = player && uniqueChallenge ?
            ArenaMarks::UniqueChallengeBonus(GetDivision(), skarnChallenge) : 0;
        const char* opponentRoles[16] = {};
        int opponentCount = 0;
        for (int i = playerCount; i < g_fighterCount && opponentCount < 16; ++i)
            opponentRoles[opponentCount++] = Id(pair[static_cast<size_t>(i)]);
        TownAnnouncer::Begin(arena, g_playerMatch, g_activeChallenge,
            opponentRoles, opponentCount, &pair[0], g_fighterCount);
        if (!player) {
            g_plannedNpc = TownMatchmakingPolicy::Match();
            g_plannedRegistry.setNull(); g_plannedFighters.clear();
            g_nextNpcHours = 0.0;
            TownBookie::NpcAssembling();
        }
        TownSpectators::Begin(arena);
        const ArenaIngress::LocationMode previousMode = ArenaIngress::GetLocationMode();
        ArenaIngress::SetLocationMode(ArenaIngress::LocationArena);
        const bool started = ArenaIngress::Begin(matchMode, &pair[0], &teams[0], g_fighterCount,
            NULL, 0, arena);
        ArenaIngress::SetLocationMode(previousMode);
        if (!started)
        {
            Finish("Town staging failed: " + ArenaIngress::GetStatus());
            return false;
        }
        g_activeRegistry = registry;
        char assembling[96];
        sprintf_s(assembling, "Town %s assembling: %d vs %d",
            matchMode == MatchRules::ModeTeams1v1 ? "Teams 1v1" : "Teams",
            player ? playerCount : npcTeamSize, player ? neededEnemies : neededEnemies - npcTeamSize);
        Status(assembling, true);
        return true;
    }

    bool BeginTrial()
    {
        if (g_bookingPending)
        {
            Status("A player booking holds the next town slot");
            return false;
        }
        if (g_phase == Preparing || g_phase == Fighting) {
            Status("Current bout is already underway"); return false;
        }
        const bool planned = PlanNpcBout();
        if (!planned) Status("Waiting for recovered NPC fighters and medical capacity to announce the next bout");
        return planned;
    }

    void Cancel()
    {
        CancelPlannedNpcBout("Scheduled bout cancelled");
        CancelForSave();
    }
    void CancelForSave()
    {
        // A distinct next market may already be announced during aftercare.
        // Abort the active operation without refunding that future wager.
        if (g_bookingPending) CancelBooking();
        if (!IsBusy()) return;
        if (g_phase == Preparing && ArenaIngress::IsPending()) ArenaIngress::Cancel();
        if (OwnsMatch()) SparSession::AbortForSave();
        Finish("Town trial cancelled; town AI released");
    }

    void AbandonWorldState()
    {
        TownMatchmakingSearch::Shutdown();
        TownAnnouncer::AbandonWorldState();
        TownCorpseCleanup::AbandonWorldState();
        TownLimbShop::AbandonWorldState();
        TownAftercare::AbandonWorldState();
        TownSpectators::AbandonWorldState();
        TownBookie::AbandonWorldState();
        TownDiagnosticRun::CancelPending();
        // No actor access: world teardown may already have destroyed them.
        g_queuedChallenge = -1;
        g_activeChallenge = -1;
        g_lastCardDiscovery = 0;
        g_townRegistry.setNull();
        g_activeRegistry.setNull();
        g_resultRecorded = false;
        if (IsBusy()) ArenaMedical::SetProtocol(g_previousProtocol);
        g_phase = Idle;
        g_sequentialCare = false;
        g_schedulePaused = false;
        g_playerMatch = false;
        g_activeMarksMultiplier = 1.0f;
        g_activeMarksBonus = 0;
        g_bookingPending = false;
        g_bookedPlayer.setNull();
        g_bookedTeam.clear();
        g_bookedRegistry.setNull();
        g_lastScheduleCheck = 0;
        g_nextNpcHours = 0.0;
        g_plannedNpc = TownMatchmakingPolicy::Match();
        g_plannedRegistry.setNull(); g_plannedFighters.clear();
        g_restoredNpc = ArenaPersistence::PlannedNpcBout();
        g_restoredNpcPending = false;
        g_restoreObservedHours = 0.0;
        for (int i = 0; i < 16; ++i) g_fighters[i].setNull();
        g_fighterCount = 0;
        g_playerCount = 0;
        g_lastAid = 0;
        g_aftercareElapsed = 0.0f;
        g_aftercareTick = 0;
        g_quietDiscovery = false;
        g_status = "Town trial idle";
        g_previewKey.clear(); g_retryContext.clear(); g_previewTick = 0;
        g_previewSnapshot = TownMatchmakingRuntime::Snapshot();
        g_previewSnapshotValid = false;
        g_previewSnapshotTick = 0;
        g_searchGeneration = 0;
        g_matchmakingDiagnosticTick = 0;
        g_ingressQuarantine.clear();
        for (int i = 0; i < 5; ++i) g_previews[i] = TownChallengePolicy::Offer();
        g_directPreview = TownChallengePolicy::Offer(); g_directBooking = TownChallengePolicy::Offer();
        g_challengeReservation = TownChallengePolicy::Offer();
        g_buyInTicket = TownChallengeBuyInPolicy::Ticket();
        for (int i = 0; i < 6; ++i) g_buyInQuotes[i] = TownChallengeBuyInPolicy::Quote();
        for (int i = 0; i < 6; ++i) { g_unavailableReasons[i].clear(); g_searching[i] = false; }
        for (int i = 0; i < 5; ++i) {
            g_searchQueued[i] = false;
            g_searchCompleted[i] = false;
            g_bestAvailable[i] = false;
        }
    }

    void Tick()
    {
        if (!ou) return;

        // Scratch's actors may unload after the last player character leaves
        // town. Stop owning them before any arena subsystem ticks again.
        if (IsBusy() && !HasPlayerInRegistryTown(g_activeRegistry.getBuilding()))
        {
            if (g_phase == Preparing && ArenaIngress::IsPending()) ArenaIngress::Cancel();
            if (OwnsMatch()) SparSession::AbortForSave();
            Finish("Town event stopped: no player characters remain in Scratch");
            return;
        }

        // An announced ambient bout must not survive after Scratch becomes
        // unobserved. Paid player bookings remain queued for the squad's return.
        if (g_plannedNpc.valid && !g_restoredNpcPending &&
            !HasPlayerInRegistryTown(g_plannedRegistry.getBuilding()))
        {
            CancelPlannedNpcBout("Scheduled bout cancelled: no player characters remain in Scratch");
            return;
        }

        if (TownLimbShop::IsActive()) TownLimbShop::Tick();
        PayChallengeCredit();
        TownSpectators::Tick();
        TownAnnouncer::TickReleases();
        TownCorpseCleanup::Tick();
        TownAftercare::TickBedRecovery();
        const DWORD now = GetTickCount();
        if (g_restoredNpcPending) {
            if (ou->isPaused()) return;
            if (g_lastScheduleCheck && now - g_lastScheduleCheck < 5000) return;
            g_lastScheduleCheck = now;
            Building* registry = g_townRegistry.getBuilding();
            InstanceID* id = registry && registry->isValid() ? registry->getInstanceID() : NULL;
            if (!id || id->uid != g_restoredNpc.registryId) {
                DiscoverTownRegistry();
                registry = g_townRegistry.getBuilding();
                id = registry && registry->isValid() ? registry->getInstanceID() : NULL;
            }
            if (id && id->uid == g_restoredNpc.registryId &&
                ArenaIdentity::IsRegistryFinished(registry) &&
                HasPlayerInRegistryTown(registry)) {
                const double hours = ou->getTimeStamp_inGameHours().getTotalHours();
                if (!g_restoreObservedHours) g_restoreObservedHours = hours;
                const std::vector<Character*> noPlayers;
                const TownMatchmakingRuntime::Snapshot snapshot = SnapshotFor(noPlayers);
                std::vector<Character*> fighters;
                std::vector<MatchRules::MatchTeam> teams;
                bool ready = true;
                for (int side = 0; side < 2; ++side) {
                    const std::vector<std::string>& ids = side ? g_restoredNpc.teamB : g_restoredNpc.teamA;
                    for (size_t i = 0; i < ids.size(); ++i) {
                        Character* fighter = TownMatchmakingRuntime::Resolve(snapshot, ids[i]);
                        if (!fighter || !IsNpcReadyForPreparation(fighter) ||
                            fighter->getCurrentTownLocation() != registry->getTown() ||
                            TownAftercare::IsManaged(fighter) || TownLimbShop::IsManaged(fighter)) ready = false;
                        fighters.push_back(fighter);
                        teams.push_back(side ? MatchRules::TeamB : MatchRules::TeamA);
                    }
                }
                if (ready) {
                    TownBookie::BeginNpcBout(&fighters[0], &teams[0],
                        static_cast<int>(fighters.size()), g_restoredNpc.scoreA, g_restoredNpc.scoreB);
                    if (TownBookie::RestorePlannedNpcBout(g_restoredNpc)) {
                        g_plannedRegistry = registry;
                        g_plannedFighters.clear();
                        for (size_t i = 0; i < fighters.size(); ++i)
                            g_plannedFighters.push_back(hand(fighters[i]));
                        g_restoredNpcPending = false;
                        g_restoredNpc = ArenaPersistence::PlannedNpcBout();
                        g_restoreObservedHours = 0.0;
                        Status("Saved NPC lineup and wager restored");
                    } else CancelPlannedNpcBout("Saved NPC lineup could not reopen its market");
                } else if (hours - g_restoreObservedHours >= 1.0)
                    CancelPlannedNpcBout("Saved NPC lineup unavailable after one game hour in Scratch");
            }
            if (g_restoredNpcPending) return;
        }
        if (g_plannedNpc.valid && !ou->isPaused() && !PlannedNpcReady())
            CancelPlannedNpcBout("An announced fighter or arena is unavailable");
        if (!IsBusy())
        {
            if (ou->isPaused() || (g_schedulePaused && !g_bookingPending)) return;
            if (g_lastScheduleCheck && now - g_lastScheduleCheck < 5000) return;
            g_lastScheduleCheck = now;

            Building* ambientRegistry = g_townRegistry.getBuilding();
            bool registryReady = ambientRegistry && ambientRegistry->isValid() &&
                ArenaIdentity::IsRegistryFinished(ambientRegistry);
            if (!registryReady) g_townRegistry.setNull();
            if (TownArenaPolicy::ShouldDiscoverRegistry(
                g_schedulePaused, registryReady, g_bookingPending))
            {
                DiscoverTownRegistry();
                ambientRegistry = g_townRegistry.getBuilding();
                registryReady = ambientRegistry && ambientRegistry->isValid() &&
                    ArenaIdentity::IsRegistryFinished(ambientRegistry);
            }

            if (!HasPlayerInRegistryTown(ambientRegistry)) return;

            if (!g_bookingPending && registryReady && !WorldLifecycle::IsArenaOperationBusy()) {
                PlanNpcBout();
                if (!g_plannedNpc.valid) return;
                ambientRegistry = g_plannedRegistry.getBuilding();
            }
            const TownArenaPolicy::EventKind next = TownArenaPolicy::AmbientNext(
                g_schedulePaused, registryReady, WorldLifecycle::IsArenaOperationBusy(), g_bookingPending,
                ou->getTimeStamp_inGameHours().getTotalHours(), g_nextNpcHours);
            if (next == TownArenaPolicy::EventNone) return;
            Building* registry = (next == TownArenaPolicy::EventPlayer ?
                g_bookedRegistry.getBuilding() : ambientRegistry);
            // Never silently move a reservation to a different Registry/town.
            if (!registry || !registry->isValid())
            {
                if (next == TownArenaPolicy::EventPlayer)
                    Status("Town queue waiting: return to the booked Registry");
                return;
            }
            Character* player = next == TownArenaPolicy::EventPlayer ? g_bookedPlayer.getCharacter() : NULL;
            g_quietDiscovery = next == TownArenaPolicy::EventNpc;
            if (next == TownArenaPolicy::EventPlayer &&
                (!Ready(player, false, true) || player->getCurrentTownLocation() != registry->getTown()))
            {
                Status("Town booking waiting for its fighter to recover and return; cancel to release slot");
                g_quietDiscovery = false;
                return;
            }
            const bool started = BeginEvent(player, registry);
            g_quietDiscovery = false;
            if (started && next == TownArenaPolicy::EventPlayer)
            {
                g_queuedChallenge = -1;
                g_bookingPending = false;
                g_bookedPlayer.setNull();
                g_bookedTeam.clear();
                g_bookedRegistry.setNull();
            }
            return;
        }
        if (g_phase == Aftercare || (g_phase == Fighting && g_sequentialCare))
        {
            const float dt = g_aftercareTick ? (now - g_aftercareTick) / 1000.0f : 0.0f;
            g_aftercareElapsed = TownArenaRuntimePolicy::AdvanceAftercare(
                g_aftercareElapsed, dt, ou->isPaused());
            g_aftercareTick = now;
        }
        // Assemble the medical team while fighters are preparing, not after
        // combat has already begun. Standby also observes pauses for its clock.
        if (g_phase == Preparing || (g_phase == Fighting && !g_sequentialCare)) TownAftercare::Standby();
        if (ou->isPaused()) return;
        if (g_phase == Preparing)
        {
            if (!ValidatePreparation()) return;
            if (OwnsMatch())
                OnMatchStarted();
            else if (!ArenaIngress::IsPending())
                Finish("Town staging ended: " + ArenaIngress::GetStatus());
            return;
        }
        if (g_phase == Fighting)
        {
            if (OwnsMatch())
            {
                if (g_sequentialCare && TownAftercarePolicy::MedicPollDue(g_lastAid, now)) {
                    g_lastAid = now;
                    TownAftercare::Treat(g_aftercareElapsed, true);
                }
                return;
            }
            TownBookie::Settle(-1);
            TownSpectators::Release();
            TownCorpseCleanup::Seal();
            // One event-driven limb check at match end. Queued visits cannot
            // take ownership until aftercare releases the fighters.
            Character* participants[16] = {};
            for (int i = 0; i < g_fighterCount; ++i)
                participants[i] = g_fighters[i].getCharacter();
            TownLimbShop::Begin(g_activeRegistry.getBuilding(), participants, g_fighterCount);
            g_phase = Aftercare;
            if (!g_sequentialCare) g_aftercareElapsed = 0.0f;
            g_aftercareTick = now;
            g_lastAid = 0;
            Status("Town fight finished; medics treating and transferring fighters independently");
        }
        if (g_phase != Aftercare) return;
        if (!g_bookingPending && !g_schedulePaused &&
            (!g_lastScheduleCheck || now - g_lastScheduleCheck >= 5000)) {
            g_lastScheduleCheck = now;
            PlanNpcBout();
        }
        if (!TownAftercarePolicy::MedicPollDue(g_lastAid, now)) return;
        g_lastAid = now;
        if (TownAftercare::Treat(g_aftercareElapsed)) {
            Status(TownAftercare::GetStatus());
            return;
        }
        TownCorpseCleanup::AdmitArenaDeaths();
        if (TownCorpseCleanup::HasPending()) {
            Status(TownCorpseCleanup::GetStatus());
            return;
        }
        const std::string medicalStatus = TownAftercare::GetStatus();
        TownAftercare::Release();
        Finish(medicalStatus + (TownLimbShop::IsActive() ? "; replacement-limb visits pending" : ""));
    }
}
