#pragma once
#include "ArenaLedger.h"
namespace RewardTransaction {
    enum Result { Success, RemovalFailed, DeliveryFailed, CompletionFailed, InventoryRollbackFailed, AccountingRollbackFailed, RollbackFailed };
    struct Accounting {
        virtual ~Accounting() {}
        virtual bool Complete() = 0;
        virtual bool Rollback() = 0;
        virtual bool Accept() { return true; }
    };
    struct Binding {
        virtual ~Binding() {}
        virtual bool Matches(const std::string& id) const = 0;
    };
    // Shared by native purchases and recruitment; cancellation owns the exact
    // reserved change even if the live binding has since disappeared.
    class ReservedAccounting : public Accounting {
        Binding& binding;
        ArenaLedger::Ledger* ledger;
        ArenaLedger::Reservation reservation;
        std::string identity;
        ReservedAccounting(const ReservedAccounting&);
        ReservedAccounting& operator=(const ReservedAccounting&);
    public:
        ReservedAccounting(Binding& source) : binding(source), ledger(0) {}
        bool Reserve(ArenaLedger::Ledger& owner, const std::string& id,
            const std::string& name, const ArenaLedger::Purchase& purchase) {
            if (ledger && ledger->Pending(reservation)) return false;
            if (!binding.Matches(id) || !owner.Reserve(id,name,purchase,reservation)) return false;
            ledger=&owner; identity=id; return true;
        }
        bool Complete() { return ledger && ledger->Pending(reservation) && binding.Matches(identity); }
        bool Rollback() { return ledger && ledger->Cancel(reservation); }
        bool Accept() { return ledger && ledger->Accept(reservation); }
    };
    struct Inventory {
        virtual ~Inventory() {}
        virtual bool RemovePrevious() = 0;
        virtual bool DeliverNew() = 0;
        virtual bool RemoveNew() = 0;
        virtual bool RestorePrevious() = 0;
        virtual void DestroyNew() = 0;
        virtual void DestroyPrevious() = 0;
    };
    inline Result Cancel(Accounting& accounting, bool inventoryRestored, Result failure) {
        const bool refunded=accounting.Rollback();
        if (!inventoryRestored && !refunded) return RollbackFailed;
        if (!refunded) return AccountingRollbackFailed;
        return inventoryRestored ? failure : InventoryRollbackFailed;
    }
    // Accounting has already been reserved. Keep the previous item alive until
    // both delivery and the memory-only completion succeed.
    inline Result Deliver(Inventory& inventory, Accounting& accounting) {
        if (!inventory.RemovePrevious()) {
            inventory.DestroyNew(); return Cancel(accounting,true,RemovalFailed);
        }
        if (!inventory.DeliverNew()) {
            const bool restored = inventory.RestorePrevious();
            inventory.DestroyNew(); return Cancel(accounting,restored,DeliveryFailed);
        }
        if (!accounting.Complete() || !accounting.Accept()) {
            const bool removed = inventory.RemoveNew();
            if (removed) inventory.DestroyNew();
            const bool restored = inventory.RestorePrevious();
            return Cancel(accounting,removed && restored,CompletionFailed);
        }
        inventory.DestroyPrevious(); return Success;
    }
    struct Action { virtual ~Action() {} virtual bool Apply() = 0; };
    // Recruitment cannot be reversed safely after native success. Check memory
    // readiness first; failed native recruitment still cancels its reservation.
    inline Result CompleteAction(Action& action, Accounting& accounting) {
        if (!accounting.Complete()) return Cancel(accounting,true,CompletionFailed);
        if (!action.Apply()) return Cancel(accounting,true,DeliveryFailed);
        return accounting.Accept() ? Success : AccountingRollbackFailed;
    }
}
