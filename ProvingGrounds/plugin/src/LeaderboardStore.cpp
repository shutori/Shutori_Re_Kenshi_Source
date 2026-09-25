#include "LeaderboardStore.h"
#include "ArenaLedger.h"
#include "RewardTransaction.h"
#include "FighterIdentity.h"
#include "TownChallengeBuyInPolicy.h"
#include "ArenaMarks.h"
#include "MmrRating.h"
#include "TownDiagnosticData.h"
#include "PrisonerUtil.h"
#include "SparSession.h"
#include "SquadUtil.h"
#include "PGLog.h"
#include <Windows.h>
#include <cstdio>
#include <algorithm>
#pragma warning(push)
#pragma warning(disable: 4091)
#include <kenshi/Character.h>
#include <kenshi/Building/Building.h>
#include <kenshi/CharStats.h>
#include <kenshi/GameWorld.h>
#include <kenshi/Globals.h>
#include <kenshi/RootObject.h>
#include <kenshi/util/hand.h>
#pragma warning(pop)

namespace
{
    ArenaLedger::Ledger g_ledger;
    LeaderboardData::Collections& g_data = g_ledger.State().fighters;
    TownChallengePolicy::Card& g_townChallenges = g_ledger.State().challenges;
    int& g_bookieCredit = g_ledger.State().bookieCredit;
    TownChallengeBuyInPolicy::Account g_challengeAccount;
    TownDiagnosticData::State& g_townDiagnostic = g_ledger.State().diagnostic;
    std::string g_persistenceError("Arena world state has not been activated.");
    std::vector<Character*> g_missingPersistentIdentityLogged;

    std::string CharacterKey(Character* character, bool create = false)
    {
        std::string id;
        if (!g_ledger.Ready() || !character || !character->isValid()) return id;
        const bool resolved = create ? FighterIdentity::GetOrCreate(character, id)
                                     : FighterIdentity::Find(character, id);
        return resolved ? id : std::string();
    }

    void LogMissingPersistentIdentity(Character* character, const char* operation)
    {
        if (!character || std::find(g_missingPersistentIdentityLogged.begin(),
            g_missingPersistentIdentityLogged.end(), character) != g_missingPersistentIdentityLogged.end()) return;
        g_missingPersistentIdentityLogged.push_back(character);
        PGLog::Error((std::string("Proving Grounds: unavailable or conflicting fighter identity during ") + operation).c_str());
    }

    LeaderboardData::Standing* FindStanding(LeaderboardData::Kind kind, const std::string& id)
    {
        if (!g_ledger.Ready() || id.empty()) return NULL;
        std::vector<LeaderboardData::Standing>& rows = LeaderboardData::Standings(g_data, kind);
        for (size_t i = 0; i < rows.size(); ++i) if (rows[i].id == id) return &rows[i];
        return NULL;
    }

    LeaderboardData::Standing* FindStandingForCharacter(Character* character, LeaderboardData::Kind kind)
    {
        return FindStanding(kind, CharacterKey(character));
    }

    LeaderboardData::Standing& EnsureStandingForCharacter(Character* character, LeaderboardData::Kind kind)
    {
        return LeaderboardData::EnsureStanding(g_data, kind, CharacterKey(character, true), character->getName());
    }

    LeaderboardData::Progression* FindProgressionForCharacter(Character* character)
    {
        return g_ledger.Find(CharacterKey(character));
    }

    LeaderboardData::Progression& EnsureProgressionForCharacter(Character* character)
    {
        return LeaderboardData::EnsureProgression(g_data, CharacterKey(character, true), character->getName());
    }

    bool StandingsLess(const LeaderboardStore::Record& a, const LeaderboardStore::Record& b)
    {
        return a.mmr != b.mmr ? a.mmr > b.mmr : a.name < b.name;
    }
}

namespace LeaderboardStore
{
    TownDiagnosticData::State& GetTownDiagnostic()
    {
        if (g_ledger.Ready()) return g_townDiagnostic;
        static TownDiagnosticData::State unavailable;
        unavailable = TownDiagnosticData::State();
        return unavailable;
    }

    std::string GetDiagnosticError()
    {
        return g_ledger.Ready() ? g_ledger.State().diagnosticError : std::string();
    }

    void ClearDiagnosticError()
    {
        if (g_ledger.Ready()) g_ledger.State().diagnosticError.clear();
    }


    bool ResetTownStandings()
    {
        if (!g_ledger.Ready()) return false;
        g_data.townStandings.clear();
        return true;
    }

    bool RemoveTownNpcProgression(Character* character)
    {
        if (!character || !character->isValid() || character->isPlayerCharacter() ||
            character->isChainedMode() || PrisonerUtil::IsRosterPrisoner(character) ||
            PrisonerUtil::IsMatchPrisoner(character)) return false;

        const std::string id = CharacterKey(character);
        if (!id.empty())
        {
            for (size_t i = 0; i < g_data.progression.size();)
            {
                if (g_data.progression[i].id == id)
                    g_data.progression.erase(g_data.progression.begin() + i);
                else ++i;
            }
        }
        return true;
    }

    TownChallengePolicy::Card& GetTownChallenges()
    {
        if (g_ledger.Ready()) return g_townChallenges;
        static TownChallengePolicy::Card unavailable;
        unavailable = TownChallengePolicy::Card();
        return unavailable;
    }
    int& GetBookieCredit()
    {
        if (g_ledger.Ready()) return g_bookieCredit;
        static int unavailable = 0;
        unavailable = 0;
        return unavailable;
    }
    TownChallengeBuyInPolicy::Account& GetChallengeAccount()
    {
        if (g_ledger.Ready()) return g_challengeAccount;
        static TownChallengeBuyInPolicy::Account unavailable;
        unavailable = TownChallengeBuyInPolicy::Account();
        return unavailable;
    }

    void EnsureLoaded()
    {
        // Activation is an explicit native lifecycle event. UI/gameplay reads
        // must never discover a path, parse a file, or switch the active world.
    }

    void BlockPersistence(const std::string& reason)
    {
        g_ledger.Block();
        g_challengeAccount = TownChallengeBuyInPolicy::Account();
        g_missingPersistentIdentityLogged.clear();
        g_persistenceError = reason;
    }

    std::string GetPersistenceError() { return g_persistenceError; }
    void Unload() { BlockPersistence("Arena world state is unloaded."); }

    void ActivateSnapshot(const ArenaPersistence::Snapshot& snapshot)
    {
        g_ledger.Activate(snapshot);
        g_challengeAccount = TownChallengeBuyInPolicy::Account();
        g_challengeAccount.credit = snapshot.challengeCredit;
        g_missingPersistentIdentityLogged.clear();
        g_persistenceError.clear();
    }

    bool CaptureSnapshot(ArenaPersistence::Snapshot& out)
    {
        if (!g_ledger.Capture(out)) return false;
        out.challengeCredit = TownChallengeBuyInPolicy::SaveCredit(g_challengeAccount);
        for (int slot = 0; slot < 5; ++slot)
            TownArenaPolicy::CancelBooking(out.challenges.offers[slot].state);
        TownArenaPolicy::CancelBooking(out.challenges.skarnOffer.state);
        return true;
    }

    void CompleteWorldTransition(bool newGame)
    {
        if (!newGame) { BlockPersistence("Waiting for native arena snapshot ownership metadata."); return; }
        ArenaPersistence::Snapshot fresh;
        fresh.challenges.seed = GetTickCount() | 1u;
        ActivateSnapshot(fresh);
    }

    bool CommitSaveSnapshot(const std::string&, const std::string&)
    {
        // Legacy ABI retained for the lifecycle integration task. Publishing
        // requires its captured native generation and verified success result.
        PGLog::Error("Proving Grounds: legacy arena sidecar publication is disabled.");
        return false;
    }

    void ApplyFromSnapshot(
        SparPodium::Snapshot& snap,
        LeaderboardData::Kind kind,
        float marksMultiplier,
        int marksTeamBonus)
    {
        if (!MmrRating::ShouldRate(snap.outcome))
            return;

        EnsureLoaded();
        if (!g_ledger.Ready())
        {
            PGLog::Debug("Proving Grounds: rating skip - arena ledger unavailable");
            return;
        }

        const int count = snap.fighterCount;
        if (count <= 0 || count > 16)
            return;

        float mmrBefore[16];
        int matchesBefore[16];
        bool rateMask[16];
        bool marksMask[16];
        float combatSkill[16];
        std::string ids[16];
        std::string names[16];
        Character* characters[16];

        for (int i = 0; i < count; ++i)
        {
            Character* c = SparSession::GetParticipant(snap.fighters[i].id);
            characters[i] = c;
            ids[i].clear();
            names[i] = snap.fighters[i].name;
            rateMask[i] = false;
            marksMask[i] = false;
            mmrBefore[i] = MmrRating::kDefaultMmr;
            matchesBefore[i] = 0;
            combatSkill[i] = 0.0f;

            if (!c || !c->isValid())
                continue;

            ids[i] = CharacterKey(c, true);
            if (ids[i].empty())
            {
                LogMissingPersistentIdentity(c, "match award");
                continue;
            }

            if (c->getStats())
                combatSkill[i] = c->getStats()->getOverallSkillLevel_0_100();

            Record* rec = FindStanding(kind, ids[i]);
            if (rec) { mmrBefore[i] = rec->mmr; matchesBefore[i] = rec->matches; }
            rateMask[i] = true;
            marksMask[i] = kind != LeaderboardData::Town || !RemoveTownNpcProgression(c);
        }

        // A later participant may reveal a duplicate of an earlier token.
        // Revalidate all bindings before any career mutation.
        for (int i = 0; i < count; ++i)
            if (rateMask[i] && CharacterKey(characters[i]) != ids[i]) rateMask[i] = false;
        bool any = false;
        for (int i = 0; i < count; ++i)
        {
            if (rateMask[i])
            {
                any = true;
                break;
            }
        }
        if (!any)
            return;

        float mmrAfter[16];
        int winsDelta[16];
        int lossesDelta[16];
        int marksEarned[16];
        MmrRating::ApplyMatch(
            snap,
            mmrBefore,
            matchesBefore,
            rateMask,
            mmrAfter,
            winsDelta,
            lossesDelta);
        ArenaMarks::ComputeAwards(snap, combatSkill, marksEarned, marksMultiplier);
        ArenaMarks::ApplyWinningTeamBonus(snap, marksMask, marksEarned, marksTeamBonus);
        for (int i = 0; i < count; ++i)
        {
            if (!rateMask[i])
                continue;

            SparPodium::FighterRow& fighter = snap.fighters[i];
            fighter.ratingBefore = mmrBefore[i];
            fighter.ratingAfter = mmrAfter[i];
            fighter.ratingDelta = mmrAfter[i] - mmrBefore[i];
            fighter.ratingUpdated = true;
            fighter.marksBefore = fighter.marksAfter = fighter.marksEarned = 0;
            fighter.marksUpdated = marksMask[i];
            if (marksMask[i])
            {
                LeaderboardData::Progression& progress =
                    EnsureProgressionForCharacter(characters[i]);
                fighter.marksBefore = progress.marks;
                fighter.marksAfter = LeaderboardData::AwardedTotal(progress.marks, marksEarned[i]);
                fighter.marksEarned = fighter.marksAfter - fighter.marksBefore;
            }

            // The podium stores FighterRow copies built before ratings apply.
            for (int p = 0; p < snap.podiumCount; ++p)
            {
                if (snap.podium[p].fighter.id != fighter.id)
                    continue;
                snap.podium[p].fighter.ratingBefore = fighter.ratingBefore;
                snap.podium[p].fighter.ratingAfter = fighter.ratingAfter;
                snap.podium[p].fighter.ratingDelta = fighter.ratingDelta;
                snap.podium[p].fighter.ratingUpdated = true;
                snap.podium[p].fighter.marksBefore = fighter.marksBefore;
                snap.podium[p].fighter.marksAfter = fighter.marksAfter;
                snap.podium[p].fighter.marksEarned = fighter.marksEarned;
                snap.podium[p].fighter.marksUpdated = true;
                break;
            }
        }

        for (int i = 0; i < count; ++i)
        {
            if (!rateMask[i])
                continue;

            LeaderboardData::ApplyCompetitiveResult(
                g_data, kind, ids[i], names[i], mmrAfter[i],
                winsDelta[i], lossesDelta[i]);
            if (!marksMask[i]) continue;
            LeaderboardData::AddMarks(
                g_data, ids[i], names[i], marksEarned[i]);
            LeaderboardData::Progression& progress =
                LeaderboardData::EnsureProgression(
                    g_data, ids[i], names[i]);
            Character* character = SparSession::GetParticipant(snap.fighters[i].id);
            if (character && PrisonerUtil::IsRosterPrisoner(character))
            {
                char line[512];
                sprintf_s(line,
                    "Proving Grounds: prisoner Marks awarded name=%s key=%s earned=%d total=%d",
                    character->getName().c_str(),
                    ids[i].c_str(),
                    marksEarned[i],
                    progress.marks);
                PGLog::Debug(line);
            }
        }

        PGLog::Debug("Proving Grounds: ratings updated in active world snapshot");
    }

    bool SetRating(Character* character, float rating)
    {
        if (!character || !character->isValid())
            return false;

        EnsureLoaded();
        if (!g_ledger.Ready())
            return false;

        const std::string id = CharacterKey(character, true);
        if (id.empty())
        {
            LogMissingPersistentIdentity(character, "rating update");
            return false;
        }

        if (rating < 0.0f)
            rating = 0.0f;
        if (rating > 9999.0f)
            rating = 9999.0f;

        Record& record = EnsureStandingForCharacter(
            character, LeaderboardData::Player);
        record.mmr = rating;
        PGLog::Debug("Proving Grounds: debug rating updated");
        return true;
    }

    void GetStandings(
        LeaderboardData::Kind kind,
        std::vector<Record>& out)
    {
        out.clear();
        EnsureLoaded();

        const std::vector<LeaderboardData::Standing>& standings =
            LeaderboardData::Standings(g_data, kind);
        for (size_t i = 0; i < standings.size(); ++i)
        {
            if (standings[i].matches < 1)
                continue;

            Record copy = standings[i];
            Character* live = FindRatedCharacter(kind, copy.id, NULL);
            if (live && live->isValid())
            {
                const std::string liveName = live->getName();
                if (!liveName.empty())
                    copy.name = liveName;
            }
            out.push_back(copy);
        }

        std::sort(out.begin(), out.end(), StandingsLess);
    }

    float GetRating(Character* character, LeaderboardData::Kind kind)
    {
        if (!character)
            return MmrRating::kDefaultMmr;

        EnsureLoaded();
        const std::string id = CharacterKey(character);
        if (id.empty())
        {
            LogMissingPersistentIdentity(character, "rating lookup");
            return MmrRating::kDefaultMmr;
        }
        Record* record = FindStandingForCharacter(character, kind);
        return record ? record->mmr : MmrRating::kDefaultMmr;
    }

    int GetMatchCount(Character* character, LeaderboardData::Kind kind)
    {
        if (!character)
            return 0;

        EnsureLoaded();
        const std::string id = CharacterKey(character);
        if (id.empty())
        {
            LogMissingPersistentIdentity(character, "match count lookup");
            return 0;
        }
        Record* record = FindStandingForCharacter(character, kind);
        return record ? record->matches : 0;
    }

    int GetMarks(Character* character)
    {
        if (!character)
            return 0;

        EnsureLoaded();
        const std::string id = CharacterKey(character);
        if (id.empty())
        {
            LogMissingPersistentIdentity(character, "Marks lookup");
            return 0;
        }
        LeaderboardData::Progression* record =
            FindProgressionForCharacter(character);
        const int marks = record ? record->marks : 0;
        return marks;
    }

    int GetRewardTier(Character* character, const char* rewardId)
    {
        if (!character || !rewardId || !rewardId[0])
            return -1;

        EnsureLoaded();
        const std::string id = CharacterKey(character);
        if (id.empty())
        {
            LogMissingPersistentIdentity(character, "reward lookup");
            return -1;
        }
        LeaderboardData::Progression* record =
            FindProgressionForCharacter(character);
        return record ? RewardProgression::HighestTier(
            record->rewardTiers, rewardId) : -1;
    }

    bool BeginRewardTierPurchase(
        Character* character,
        int cost,
        const char* rewardId,
        int expectedPreviousTier,
        int nextTier)
    {
        const std::string id = CharacterKey(character, true);
        if (id.empty()) return false;
        return rewardId && g_ledger.BeginTier(id, character ? character->getName() : "", cost,
            rewardId, expectedPreviousTier, nextTier);
    }

    bool SetMarks(Character* character, int marks)
    {
        if (!character || !character->isValid())
            return false;

        EnsureLoaded();
        if (!g_ledger.Ready())
            return false;

        const std::string id = CharacterKey(character, true);
        if (id.empty())
        {
            LogMissingPersistentIdentity(character, "Marks update");
            return false;
        }

        if (marks < 0)
            marks = 0;
        if (marks > 9999)
            marks = 9999;

        LeaderboardData::Progression& record =
            EnsureProgressionForCharacter(character);
        record.marks = marks;
        PGLog::Debug("Proving Grounds: debug Arena Marks updated");
        return true;
    }

    void RollbackRewardTierPurchase(
        Character* character,
        int cost,
        const char* rewardId,
        int previousTier)
    {
        if (rewardId) g_ledger.RollbackTier(CharacterKey(character, true), cost, rewardId, previousTier);
    }

    bool HasFactionUnlock(const char* unlockId)
    {
        if (!unlockId || !unlockId[0])
            return false;
        EnsureLoaded();
        return g_ledger.Ready() && RewardProgression::HasUnlock(
            g_data.factionUnlocks, unlockId);
    }

    bool AddFactionUnlock(const char* unlockId)
    {
        if (!unlockId || !unlockId[0])
            return false;
        EnsureLoaded();
        return g_ledger.Ready() && RewardProgression::AddUnlock(
            g_data.factionUnlocks, unlockId);
    }

    void RemoveFactionUnlock(const char* unlockId)
    {
        if (!unlockId || !unlockId[0])
            return;
        EnsureLoaded();
        if (g_ledger.Ready())
            RewardProgression::RemoveUnlock(g_data.factionUnlocks, unlockId);
    }

    bool BeginFactionUnlockPurchase(
        Character* character,
        int cost,
        const char* unlockId)
    {
        const std::string id = CharacterKey(character, true);
        if (id.empty()) return false;
        return unlockId && g_ledger.BeginUnlock(id, character ? character->getName() : "", cost, unlockId);
    }

    void RollbackFactionUnlockPurchase(
        Character* character,
        int cost,
        const char* unlockId)
    {
        const std::string id = CharacterKey(character, true);
        if (!id.empty() && unlockId) g_ledger.RollbackUnlock(id, character->getName(), cost, unlockId);
    }

    bool ReserveProgression(Character* character, const ArenaLedger::Purchase& purchase,
        RewardTransaction::ReservedAccounting& accounting)
    {
        const std::string id = CharacterKey(character, true);
        return !id.empty() && accounting.Reserve(g_ledger,id,character->getName(),purchase);
    }

    bool CompleteProgressionTransaction()
    {
        return g_ledger.Complete();
    }

    bool SpendMarks(Character* character, int cost)
    {
        const std::string id = CharacterKey(character, true);
        if (id.empty()) return false;
        return g_ledger.Spend(id, character ? character->getName() : "", cost);
    }

    void RefundMarks(Character* character, int amount)
    {
        const std::string id = CharacterKey(character, true);
        if (id.empty()) return;
        g_ledger.Refund(id, character ? character->getName() : "", amount);
    }

    Character* FindRatedCharacter(
        LeaderboardData::Kind kind,
        const std::string& id,
        Building* source)
    {
        if (id.empty())
            return NULL;

        std::vector<Character*> squad;
        SquadUtil::CollectPlayerSquad(squad);
        for (size_t i = 0; i < squad.size(); ++i)
        {
            Character* c = squad[i];
            if (c && c->isValid() && CharacterKey(c) == id)
                return c;
        }

        const std::vector<Character*>& rosterPrisoners =
            PrisonerUtil::GetRosterPrisoners();
        for (size_t i = 0; i < rosterPrisoners.size(); ++i)
        {
            Character* c = rosterPrisoners[i];
            if (c && c->isValid() && CharacterKey(c) == id)
                return c;
        }

        const std::vector<Character*>& matchPrisoners =
            PrisonerUtil::GetMatchPrisoners();
        for (size_t i = 0; i < matchPrisoners.size(); ++i)
        {
            Character* c = matchPrisoners[i];
            if (c && c->isValid() && CharacterKey(c) == id)
                return c;
        }

        if (kind == LeaderboardData::Town && source && source->isValid() && ou)
        {
            lektor<RootObject*> nearby;
            ou->getObjectsWithinSphere(
                nearby, source->getPosition(), 2500.0f,
                CHARACTER, 512, NULL);
            for (uint32_t i = 0; i < nearby.size(); ++i)
            {
                RootObject* object = nearby[i];
                if (!object || !object->isValid())
                    continue;
                Character* character = static_cast<Character*>(object);
                if (!character->isDead() && CharacterKey(character) == id)
                    return character;
            }
        }
        return NULL;
    }

    const std::string& GetActiveSaveKey()
    {
        EnsureLoaded();
        return g_ledger.State().saveKey;
    }

    bool HasActiveSave()
    {
        EnsureLoaded();
        return g_ledger.Ready();
    }
}
