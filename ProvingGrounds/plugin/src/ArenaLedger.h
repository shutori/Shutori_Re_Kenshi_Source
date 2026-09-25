#pragma once
#include "ArenaSnapshot.h"
#include <climits>
namespace ArenaLedger {
class Ledger;
struct Purchase {
    int cost, previousTier, nextTier;
    std::string reward, unlock;
    bool requireNewUnlock;
    Purchase(int amount = 0) : cost(amount), previousTier(-1), nextTier(-1), requireNewUnlock(false) {}
};
class Reservation {
    friend class Ledger;
    const Ledger* owner;
    unsigned long long epoch, sequence;
public:
    Reservation() : owner(0), epoch(0), sequence(0) {}
};
// One active world owns this value. No filesystem or native identity fallback
// belongs here: callers supply a currently verified FighterIdentity token.
class Ledger {
    ArenaPersistence::Snapshot snapshot;
    bool ready;
    unsigned long long epoch, sequence;
    bool reserved;
    struct Change {
        std::string id;
        Purchase purchase;
        bool unlockAdded;
        Change() : unlockAdded(false) {}
    } change;
    void EndWorld() { reserved=false; if (epoch != ~0ULL) ++epoch; }
public:
    Ledger() : ready(false), epoch(0), sequence(0), reserved(false) {}
    void Activate(const ArenaPersistence::Snapshot& value) { EndWorld(); snapshot = value; ready = epoch != ~0ULL; }
    void Block() { EndWorld(); snapshot = ArenaPersistence::Snapshot(); ready = false; }
    bool Ready() const { return ready; }
    ArenaPersistence::Snapshot& State() { return snapshot; }
    bool Capture(ArenaPersistence::Snapshot& out) const { if (!ready || reserved) return false; out = snapshot; return true; }
    LeaderboardData::Progression* Find(const std::string& id) {
        if (!ready || id.empty()) return 0;
        for (size_t i = 0; i < snapshot.fighters.progression.size(); ++i)
            if (snapshot.fighters.progression[i].id == id) return &snapshot.fighters.progression[i];
        return 0;
    }
    int Marks(const std::string& id) { LeaderboardData::Progression* row = Find(id); return row ? row->marks : 0; }
    int Tier(const std::string& id, const std::string& reward) {
        LeaderboardData::Progression* row = Find(id);
        return row ? RewardProgression::HighestTier(row->rewardTiers, reward) : -1;
    }
    bool Spend(const std::string& id, const std::string& name, int cost) {
        if (!ready || id.empty() || cost < 0 || Marks(id) < cost) return false;
        LeaderboardData::EnsureProgression(snapshot.fighters, id, name).marks -= cost;
        return true;
    }
    bool Refund(const std::string& id, const std::string& name, int amount) {
        if (!ready || id.empty() || amount < 0 || Marks(id) > INT_MAX - amount) return false;
        LeaderboardData::EnsureProgression(snapshot.fighters, id, name).marks += amount;
        return true;
    }
    bool BeginTier(const std::string& id, const std::string& name, int cost, const std::string& reward, int previous, int next) {
        if (!ready || id.empty() || cost < 0 || reward.empty() || Marks(id) < cost) return false;
        LeaderboardData::Progression& row = LeaderboardData::EnsureProgression(snapshot.fighters, id, name);
        if (!RewardProgression::SetHighestTier(row.rewardTiers, reward, previous, next)) return false;
        row.marks -= cost;
        return true;
    }
    bool RollbackTier(const std::string& id, int cost, const std::string& reward, int previous) {
        LeaderboardData::Progression* row = Find(id);
        if (!row || cost < 0 || row->marks > INT_MAX-cost ||
            RewardProgression::HighestTier(row->rewardTiers, reward) <= previous) return false;
        RewardProgression::RestoreHighestTier(row->rewardTiers, reward, previous);
        row->marks += cost;
        return true;
    }
    bool BeginUnlock(const std::string& id, const std::string& name, int cost, const std::string& unlock) {
        if (unlock.empty() || RewardProgression::HasUnlock(snapshot.fighters.factionUnlocks, unlock) || !Spend(id, name, cost)) return false;
        RewardProgression::AddUnlock(snapshot.fighters.factionUnlocks, unlock);
        return true;
    }
    bool RollbackUnlock(const std::string& id, const std::string& name, int cost, const std::string& unlock) {
        if (!ready || id.empty() || !RewardProgression::HasUnlock(snapshot.fighters.factionUnlocks, unlock) || !Refund(id,name,cost)) return false;
        RewardProgression::RemoveUnlock(snapshot.fighters.factionUnlocks, unlock);
        return true;
    }
    bool Reserve(const std::string& id, const std::string& name, const Purchase& purchase, Reservation& token) {
        // Reward callbacks are synchronous. Refuse nested reservations instead
        // of letting two operations own the same fighter tier or global unlock.
        if (!ready || reserved || id.empty() || sequence == ~0ULL) return false;
        const bool hasUnlock = RewardProgression::HasUnlock(snapshot.fighters.factionUnlocks, purchase.unlock);
        if (purchase.requireNewUnlock && (purchase.unlock.empty() || hasUnlock)) return false;
        const bool paid = purchase.reward.empty() ? Spend(id,name,purchase.cost) :
            BeginTier(id,name,purchase.cost,purchase.reward,purchase.previousTier,purchase.nextTier);
        if (!paid) return false;
        change.id=id; change.purchase=purchase;
        change.unlockAdded = !purchase.unlock.empty() && !hasUnlock;
        if (change.unlockAdded) RewardProgression::AddUnlock(snapshot.fighters.factionUnlocks,purchase.unlock);
        reserved=true; token.owner=this; token.epoch=epoch; token.sequence=++sequence;
        return true;
    }
    bool Pending(const Reservation& token) const {
        return ready && reserved && token.owner==this && token.epoch==epoch && token.sequence==sequence;
    }
    bool Cancel(const Reservation& token) {
        if (!Pending(token)) return false;
        LeaderboardData::Progression* row=Find(change.id);
        const Purchase& purchase=change.purchase;
        // Validate all components before undoing any of them. Never overwrite
        // a later unrelated edit or overflow a payment restoration.
        if (!row || row->marks > INT_MAX-purchase.cost ||
            (!purchase.reward.empty() && Tier(change.id,purchase.reward)!=purchase.nextTier) ||
            (change.unlockAdded && !RewardProgression::HasUnlock(snapshot.fighters.factionUnlocks,purchase.unlock))) return false;
        row->marks += purchase.cost;
        if (!purchase.reward.empty()) RewardProgression::RestoreHighestTier(row->rewardTiers,purchase.reward,purchase.previousTier);
        if (change.unlockAdded) RewardProgression::RemoveUnlock(snapshot.fighters.factionUnlocks,purchase.unlock);
        reserved=false; return true;
    }
    bool Accept(const Reservation& token) {
        if (!Pending(token)) return false;
        reserved=false; return true;
    }
    bool Complete() const { return ready; }
};
}
