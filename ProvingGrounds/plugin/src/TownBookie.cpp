#include "TownBookie.h"
#include "CombatBalanceLog.h"
#include "PGLog.h"
#include "TownBookieUI.h"
#include "TownBookieResultUI.h"
#include "TownBettingPolicy.h"
#include "PGConfig.h"
#include "TownArena.h"
#include "TownArenaUI.h"
#include "TownMatchmakingRuntime.h"
#include "ArenaCombatProfile.h"
#include "LeaderboardStore.h"
#include <kenshi/Character.h>
#include <kenshi/GameData.h>
#include <kenshi/GameWorld.h>
#include <kenshi/Globals.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Faction.h>
#include <kenshi/Platoon.h>
#include <Windows.h>
#include <climits>
#include <cstdio>
#include <cmath>
#include <algorithm>

namespace {
    const int kMaxTeam = 4;
    hand teams[2][kMaxTeam], bookie;
    int teamCounts[2] = {0, 0};
    std::vector<TownBookieUI::FighterView> fighterViews[2];
    std::vector<TownBookieUI::FighterView> resultFighterViews[2];
    double ability[2] = {1, 1};
    bool market = false, fighting = false, completed = false;
    float elapsed = 0;
    DWORD lastTick = 0, lastUiTick = 0;
    TownBettingPolicy::Ticket ticket;
    TownBettingPolicy::Result result;
    bool resultScheduled = false, resultReady = false;
    DWORD resultScheduledAt = 0;
    int stake = 100, selectedSide = -1;
    std::string actionMessage;
    std::string message = "NPC bouts only. Arrange a bout to open betting.";
    Ownerships* Wallet() {
        Faction* faction = ou && ou->player ? ou->player->getFaction() : NULL;
        return faction ? faction->factionOwnerships : NULL;
    }
    bool Player(Character* c) {
        if (!c || !ou || !ou->player) return true;
        const lektor<Character*>& all = ou->player->getAllPlayerCharacters();
        for (uint32_t i = 0; i < all.size(); ++i) if (all[i] == c) return true;
        return false;
    }
    bool Eligible() {
        if (teamCounts[0] <= 0 || teamCounts[1] <= 0) return false;
        for (int side = 0; side < 2; ++side) {
            for (int i = 0; i < teamCounts[side]; ++i) {
                Character* c = teams[side][i].getCharacter();
                if (!c || !c->isValid() || Player(c)) return false;
            }
        }
        return true;
    }
    bool AtBookie() {
        Character* b = bookie.getCharacter();
        if (!b || !b->isValid() || b->isDead() || b->isUnconcious() || Player(b) || !ou || !ou->player) return false;
        const lektor<Character*>& all = ou->player->getAllPlayerCharacters();
        for (uint32_t i = 0; i < all.size(); ++i) {
            Character* c = all[i];
            if (c && c->isValid() && !c->isDead() && !c->isUnconcious() &&
                c->getCurrentTownLocation() == b->getCurrentTownLocation() &&
                (c->getPosition() - b->getPosition()).squaredLength() <= 40000.0f) return true;
        }
        return false;
    }
    double Ability(Character* c) {
        return TownMatchmakingRuntime::Ability(c);
    }
    void ClearTeams() {
        for (int side = 0; side < 2; ++side) {
            teamCounts[side] = 0;
            fighterViews[side].clear();
            for (int i = 0; i < kMaxTeam; ++i) teams[side][i].setNull();
        }
    }
    void PayCredit() {
        if (!LeaderboardStore::HasActiveSave()) return;
        int& credit = LeaderboardStore::GetBookieCredit();
        Ownerships* wallet = Wallet();
        if (credit <= 0 || !wallet || wallet->getMoney() < 0 || wallet->getMoney() > INT_MAX - credit) return;
        wallet->addMoney(credit);
        credit = 0;
    }
    void Wager(int side) {
        Ownerships* wallet = Wallet();
        Character* b = bookie.getCharacter();
        Character* a = teams[0][0].getCharacter();
        if (!LeaderboardStore::HasActiveSave() || !AtBookie() || !a || !b ||
            a->getCurrentTownLocation() != b->getCurrentTownLocation()) { message = "Bring a squad member close to this town's bookie."; return; }
        if (LeaderboardStore::GetBookieCredit() != 0 || !wallet ||
            !TownBettingPolicy::CanAccept(ticket, market && Eligible(), !fighting, side, stake, wallet->getMoney())) {
            message = "Bet unavailable: check Cats, existing wager, and that combat has not started."; return;
        }
        const int payout = TownBettingPolicy::ReturnCats(stake, ability[side], ability[1-side]);
        if (!payout || !wallet->takeMoney(stake)) { message = "Could not accept the Cats wager."; return; }
        ticket.side = side; ticket.stake = stake; ticket.payout = payout; ticket.pending = true;
        char text[128]; sprintf_s(text, "Accepted: %d Cats. Winning total return: %d Cats.", stake, payout);
        message = text;
        char fields[160]; sprintf_s(fields, ",\"bet_side\":%d,\"stake\":%d,\"quoted_return\":%d", side, stake, payout);
        CombatBalanceLog::MarketEvent("wager_accepted", fields);
    }

    TownBookieUI::FighterView Capture(Character* character,
        const std::vector<LeaderboardStore::Record>& standings) {
        TownBookieUI::FighterView result;
        result.actor = character;
        result.name = character && character->isValid() ? character->getName() : "Unavailable fighter";
        const ArenaCombatProfile::Profile profile = ArenaCombatProfile::Read(character);
        result.style = profile.style;
        result.stats = ArenaCombatProfile::StatText(profile);
        result.record = "Unrated | 0 W / 0 L";
        const std::string identity = TownMatchmakingRuntime::Identity(character);
        for (size_t i = 0; !identity.empty() && i < standings.size(); ++i) {
            if (standings[i].id != identity || standings[i].matches < 1) continue;
            char text[128]; sprintf_s(text, "MMR %.0f | %d W / %d L",
                standings[i].mmr, standings[i].wins, standings[i].losses);
            result.record = text; break;
        }
        return result;
    }
    TownBookieUI::View MakeView() {
        TownBookieUI::View view;
        Ownerships* wallet = Wallet();
        const int money = wallet ? wallet->getMoney() : 0;
        const bool chosen = selectedSide == 0 || selectedSide == 1;
        const bool valid = market && Eligible();
        const bool resultBlocking = resultScheduled || resultReady;
        const bool nearby = AtBookie();
        Character* first = teams[0][0].getCharacter();
        Character* clerk = bookie.getCharacter();
        const bool sameTown = first && clerk && first->isValid() && clerk->isValid() &&
            first->getCurrentTownLocation() == clerk->getCurrentTownLocation();
        const bool ready = LeaderboardStore::HasActiveSave() && nearby && sameTown;
        // A configured stake ceiling can be lower than the stake the window
        // opened with, and the bet button is gated on this exact value.
        stake = PGConfig::ClampStake(stake);
        view.selectedSide = selectedSide;
        view.stake = ticket.stake > 0 ? ticket.stake : stake;
        view.canSelect = valid && !fighting && !ticket.pending && !completed && !resultBlocking;
        view.canStake = view.canSelect;
        view.canBet = !resultBlocking && ready && LeaderboardStore::GetBookieCredit() == 0 &&
            TownBettingPolicy::CanAccept(ticket, valid && !completed, !fighting,
                selectedSide, stake, money);
        view.canArrange = !resultBlocking && nearby && LeaderboardStore::HasActiveSave() && !market && !fighting && !ticket.pending;
        char text[384];
        const char* state = fighting ? "COMBAT - BETS CLOSED" : market ? "BETTING OPEN" : completed ? "BOUT FINISHED" : "NO BOUT OPEN";
        sprintf_s(text, "%s | %d Cats | NPC bout %dv%d", state, money, teamCounts[0], teamCounts[1]);
        view.header = text;
        if (TownArena::HasPlannedNpcBout() && ou) {
            const double start = TownArena::GetPlannedNpcStartHours();
            const double now = ou->getTimeStamp_inGameHours().getTotalHours();
            const int minuteOfDay = static_cast<int>(std::ceil(start * 60.0)) % (24 * 60);
            const int remaining = static_cast<int>(std::ceil((std::max)(0.0, start - now) * 60.0));
            if (remaining > 0) sprintf_s(text, "NEXT BOUT  %02d:%02d  |  STARTS IN %dh %02dm",
                minuteOfDay / 60, minuteOfDay % 60, remaining / 60, remaining % 60);
            else sprintf_s(text, "NEXT BOUT  %02d:%02d  |  START TIME REACHED - WAITING FOR ARENA / MEDICS",
                minuteOfDay / 60, minuteOfDay % 60);
            view.timer = text;
        }
        for (int side = 0; side < 2; ++side) {
            view.fighters[side] = fighterViews[side];
            const int payout = TownBettingPolicy::ReturnCats(1000, ability[side], ability[1-side]);
            if (teamCounts[side] > 0 && payout > 0) {
                sprintf_s(text, "%.2fx return", payout / 1000.0); view.odds[side] = text;
            } else view.odds[side] = "Awaiting fighters";
        }
        view.status = actionMessage.empty() ? message : actionMessage;
        if (ticket.stake > 0 && ticket.side >= 0 && ticket.side <= 1) {
            sprintf_s(text, "%s: Team %s | Stake %d Cats | Quoted return %d Cats | Potential profit +%d Cats",
                ticket.pending ? "ACCEPTED" : "SETTLED", ticket.side == 0 ? "A" : "B", ticket.stake,
                ticket.payout, ticket.payout - ticket.stake);
            view.slip = text;
        } else if (chosen && market && !fighting) {
            const int payout = TownBettingPolicy::ReturnCats(stake, ability[selectedSide], ability[1-selectedSide]);
            sprintf_s(text, "YOUR BET: Team %s | Stake %d Cats | Total return %d Cats | Profit +%d Cats",
                selectedSide == 0 ? "A" : "B", stake, payout, payout - stake);
            view.slip = text;
        } else view.slip = fighting ? "Bets closed. No ticket placed." : completed ? "No ticket placed for this bout." :
            "YOUR BET: Choose a team and stake. Place bet confirms your wager.";
        if (!ticket.pending && market && !fighting) {
            if (!ready) view.status = "Bring a squad member close to this town's bookie; an active save is required.";
            else if (!valid) view.status = "Betting unavailable: fighter data is incomplete.";
            else if (LeaderboardStore::GetBookieCredit() != 0) view.status = "Pending Cats return must be collected before betting.";
            else if (money < stake) view.status = "Not enough Cats for this stake.";
            else if (!chosen) view.status = "Select a team, then choose your stake. One wager per bout.";
        }
        return view;
    }
    TownBookieResultUI::View MakeResultView() {
        TownBookieResultUI::View view;
        view.fighters[0] = resultFighterViews[0];
        view.fighters[1] = resultFighterViews[1];
        view.winningSide = result.winningSide;
        view.selectedSide = result.selectedSide;
        view.tone = result.outcome == TownBettingPolicy::ResultWon ? 1 :
            result.outcome == TownBettingPolicy::ResultLost ? 2 : 3;
        char text[256];
        if (result.outcome == TownBettingPolicy::ResultWon) {
            view.title = "WAGER WON";
            sprintf_s(text, "Team %s claimed the arena. Your pick delivered.",
                result.winningSide == 0 ? "A" : "B");
            view.context = text;
            sprintf_s(text, "STAKE  %d CATS     RETURNED  %d CATS\nNET WIN  +%d CATS",
                result.stake, result.returned, result.net);
        } else if (result.outcome == TownBettingPolicy::ResultLost) {
            view.title = "WAGER LOST";
            sprintf_s(text, "Team %s claimed the arena. You backed Team %s.",
                result.winningSide == 0 ? "A" : "B", result.selectedSide == 0 ? "A" : "B");
            view.context = text;
            sprintf_s(text, "STAKE  %d CATS     RETURNED  0 CATS\nNET LOSS  -%d CATS",
                result.stake, -result.net);
        } else {
            view.title = "WAGER REFUNDED";
            view.context = "No victor was declared. The bookie returns your stake.";
            sprintf_s(text, "STAKE  %d CATS     RETURNED  %d CATS\nNET  0 CATS",
                result.stake, result.returned);
        }
        view.payout = text;
        return view;
    }
    void RefreshUi() { TownBookieUI::Refresh(MakeView()); }
    void SelectSide(int side) {
        if (side >= 0 && side <= 1 && market && !fighting && !ticket.pending && !completed) {
            selectedSide = side; actionMessage.clear();
        }
        RefreshUi();
    }
    void ChangeStake(int delta) {
        if (market && !fighting && !ticket.pending && !completed &&
            (delta == -1000 || delta == -100 || delta == -10 ||
             delta == 10 || delta == 100 || delta == 1000)) {
            stake = TownBettingPolicy::AdjustStake(stake, delta); actionMessage.clear();
        }
        RefreshUi();
    }
    void PlaceBet() {
        if (selectedSide == 0 || selectedSide == 1) Wager(selectedSide);
        actionMessage = ticket.pending ? std::string() : message;
        RefreshUi();
    }
    void Arrange() {
        if (!AtBookie()) { actionMessage = "Move a squad member close to the bookie."; RefreshUi(); return; }
        if (market || fighting || ticket.pending) { actionMessage = "Finish the current bout first."; RefreshUi(); return; }
        if (!TownArena::BindRegistryNear(bookie.getCharacter())) {
            actionMessage = "No finished Town Registry found near this bookie."; RefreshUi(); return;
        }
        TownArena::BeginTrial();
        actionMessage = TownArena::GetStatus();
        RefreshUi();
    }
    TownBookieUI::Actions Callbacks() {
        TownBookieUI::Actions callbacks;
        callbacks.selectSide = SelectSide; callbacks.changeStake = ChangeStake;
        callbacks.placeBet = PlaceBet; callbacks.arrange = Arrange; callbacks.close = TownBookie::Close;
        return callbacks;
    }
    TownBookieResultUI::Actions ResultCallbacks() {
        TownBookieResultUI::Actions callbacks;
        callbacks.close = TownBookie::Close;
        return callbacks;
    }
}
namespace TownBookie {
    bool IsBookie(RootObject* object) {
        GameData* data = object && object->isValid() ? object->getGameData() : NULL;
        return data && data->stringID == "111-Proving Grounds.mod";
    }
    bool IsVisible() { return TownBookieUI::IsVisible() || TownBookieResultUI::IsVisible(); }
    void Close() {
        TownBookieUI::Close();
        TownBookieResultUI::Close();
        if (resultReady) {
            result = TownBettingPolicy::Result();
            resultFighterViews[0].clear(); resultFighterViews[1].clear();
            resultReady = false;
            PGLog::Debug("Proving Grounds: bookie wager result dismissed");
        }
    }
    void BeginNpcBout(Character* const* fighters, const MatchRules::MatchTeam* matchTeams, int count, double scoreA, double scoreB) {
        Settle(-1);
        ClearTeams();
        completed = false;
        std::vector<LeaderboardStore::Record> standings;
        LeaderboardStore::GetStandings(LeaderboardData::Town, standings);
        double memberAbility[2][kMaxTeam] = {{0}};
        for (int i = 0; fighters && matchTeams && i < count; ++i) {
            const int side = matchTeams[i] == MatchRules::TeamA ? 0 : matchTeams[i] == MatchRules::TeamB ? 1 : -1;
            if (side < 0 || teamCounts[side] >= kMaxTeam) continue;
            const int slot = teamCounts[side]++;
            teams[side][slot] = fighters[i];
            fighterViews[side].push_back(Capture(fighters[i], standings));
            memberAbility[side][slot] = Ability(fighters[i]);
        }
        ability[0] = scoreA; ability[1] = scoreB;
        market = Eligible() && ability[0] > 0 && ability[1] > 0 &&
            TownBettingPolicy::TeamAbility(memberAbility[0], teamCounts[0]) > 0 &&
            TownBettingPolicy::TeamAbility(memberAbility[1], teamCounts[1]) > 0;
        CombatBalanceLog::OpenMarket(fighters, matchTeams, count, ability[0], ability[1], market);
        fighting = false; elapsed = 0; lastTick = GetTickCount();
        ticket = TownBettingPolicy::Ticket();
        if (!resultScheduled && !resultReady) {
            result = TownBettingPolicy::Result();
            resultFighterViews[0].clear(); resultFighterViews[1].clear();
        }
        selectedSide = -1;
        actionMessage.clear();
        message = market ? "Lineup announced. Betting open until combat begins. One wager per bout." :
            "Team bout unavailable for betting: fighter data is incomplete.";
    }
    bool PreparationValid() {
        if (!Eligible()) return false;
        for (int side = 0; side < 2; ++side)
            for (int i = 0; i < teamCounts[side]; ++i) {
                Character* c = teams[side][i].getCharacter();
                if (!TownArena::IsNpcReadyForPreparation(c)) return false;
            }
        return true;
    }
    bool HasPendingWager() { return ticket.pending; }
    void CombatStarted() {
        // TownArena also signals player challenges. Only an open NPC market
        // belongs to this controller; player bouts must not lock its UI forever.
        if (!market) return;
        if (!fighting) CombatBalanceLog::MarketEvent("market_combat_started");
        fighting = true;
        if (market && !Eligible()) Settle(-1);
    }
    void NpcAssembling() {
        elapsed = 20.0f; // The scheduled market already provided its betting window.
        actionMessage.clear();
        message = "Scheduled bout assembling. Betting closes when combat begins.";
    }
    void CancelNpcBout(const std::string& reason) {
        CombatBalanceLog::MarketEvent("market_cancelled", ",\"reason\":" + PGLog::Quote(reason));
        Settle(-1);
        message = reason + ". " + message;
        actionMessage.clear();
    }
    bool HoldForBets() { return market && !fighting && elapsed < 20.0f; }
    void Settle(int winner) {
        const bool hadBout = market || fighting || ticket.pending;
        if (!hadBout) return;
        if (!Eligible()) winner = -1;
        CombatBalanceLog::CloseMarket(winner, ticket.side, ticket.stake, ticket.payout,
            ticket.pending ? (winner < 0 ? ticket.stake : winner == ticket.side ? ticket.payout : 0) : 0, ticket.pending);
        char text[192];
        if (ticket.pending) {
            result = TownBettingPolicy::Resolve(ticket, winner);
            resultFighterViews[0] = fighterViews[0];
            resultFighterViews[1] = fighterViews[1];
            const int credit = TownBettingPolicy::Settle(ticket, winner);
            LeaderboardStore::GetBookieCredit() += credit;
            if (winner < 0) sprintf_s(text, "Bout cancelled/drawn: %d Cats refunded.", credit);
            else sprintf_s(text, "Team %s wins. Bet %s: %d Cats returned.",
                winner == 0 ? "A" : "B", winner == ticket.side ? "won" : "lost", credit);
            message = text; PayCredit();
            resultScheduled = true;
            resultReady = false;
            resultScheduledAt = GetTickCount();
            PGLog::Debug("Proving Grounds: bookie wager result scheduled");
        } else {
            message = winner < 0 ? "Bout cancelled or drawn. No wager placed." :
                winner == 0 ? "Team A wins. No wager placed." : "Team B wins. No wager placed.";
        }
        actionMessage.clear(); completed = true;
        market = false; fighting = false;
    }
    void AbandonWorldState() {
        // Never move money during world teardown; the loaded native save and
        // sidecar replace both balances and pending credits together.
        CombatBalanceLog::Abandon();
        TownBookieUI::Destroy(); TownBookieResultUI::Destroy(); ClearTeams(); bookie.setNull();
        completed = false;
        ticket = TownBettingPolicy::Ticket(); market = false; fighting = false; lastTick = 0;
        result = TownBettingPolicy::Result(); resultScheduled = resultReady = false; resultScheduledAt = 0;
        resultFighterViews[0].clear(); resultFighterViews[1].clear();
        selectedSide = -1;
        actionMessage.clear();
        lastUiTick = 0;
        message = "NPC bouts only. Choose a side before combat.";
    }
    void Tick() {
        const DWORD now = GetTickCount();
        if (lastTick && ou && !ou->isPaused() && market && !fighting) elapsed += (now - lastTick) / 1000.0f;
        lastTick = now;
        PayCredit();
        if (resultScheduled && now - resultScheduledAt >= 1000) {
            resultScheduled = false;
            resultReady = true;
            TownArenaUI::Close();
            TownBookieUI::Close();
            const bool shown = TownBookieResultUI::Show(MakeResultView(), ResultCallbacks());
            PGLog::Debug(shown ? "Proving Grounds: bookie wager result shown" :
                "Proving Grounds: bookie wager result show failed");
            lastUiTick = 0;
        }
        if (!TownBookieUI::IsVisible()) return;
        if (lastUiTick && now - lastUiTick < 250) return;
        lastUiTick = now;
        RefreshUi();
    }
    void Show(RootObject* object) {
        if (!IsBookie(object)) return;
        bookie = static_cast<Character*>(object);
        if (!AtBookie()) { if (ou) ou->showPlayerAMessage("Move a squad member closer to the Scratch bookie.", true); return; }
        TownArena::BindRegistryNear(bookie.getCharacter());
        TownArenaUI::Close();
        if (!TownBookieUI::Show(MakeView(), Callbacks())) return;
        lastUiTick = 0; Tick();
    }
}
