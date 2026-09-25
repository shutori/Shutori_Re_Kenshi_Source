#pragma once

namespace TownMedicAIPolicy {
    template<class Native> void PrepareOwnedOrder(Native& native) {
        // addOrder(clearOld=true) performs native order replacement. Rethink
        // additionally queues body completion for a later update, which can
        // remove the replacement order after its first treatment frame.
        native.RestoreGoals();
    }
    template<class Native> void ResumeNative(Native& native) {
        native.ClearOrders();
        native.Rethink();
        native.RestoreGoals();
        native.ChooseGoal();
        native.Schedule();
    }
    class Deliveries {
        struct Receipt {
            const void* patient;
            const void* bed;
            bool complete;
            Receipt() : patient(0), bed(0), complete(false) {}
        } receipts[16];
    public:
        void Clear() { for (int i = 0; i < 16; ++i) receipts[i] = Receipt(); }
        void Watch(const void* patient, const void* bed) {
            if (!patient || !bed) return;
            for (int i = 0; i < 16; ++i) if (receipts[i].patient == patient) {
                if (!receipts[i].complete) receipts[i].bed = bed;
                return;
            }
            for (int i = 0; i < 16; ++i) if (!receipts[i].patient) {
                receipts[i].patient = patient; receipts[i].bed = bed; return;
            }
        }
        void Entered(const void* patient, const void* bed) {
            if (!patient || !bed) return;
            for (int i = 0; i < 16; ++i)
                if (receipts[i].patient == patient && receipts[i].bed == bed) receipts[i].complete = true;
        }
        bool Completed(const void* patient) const {
            for (int i = 0; i < 16; ++i)
                if (receipts[i].patient == patient && receipts[i].complete) return true;
            return false;
        }
    };
    // Native checkOrders still executes first. Reporting the reserved receiver
    // as handled prevents chooseGoal from falling through into autonomous jobs.
    inline bool OrdersHandled(bool reserved, bool nativeResult) {
        return reserved || nativeResult;
    }
    inline bool OrdersHandled(bool medicReserved, bool patientReserved,
        bool externalReserved, bool nativeResult) {
        return medicReserved || patientReserved || externalReserved || nativeResult;
    }
    inline bool CanReserveExternal(bool medicReserved, bool patientReserved,
        bool externalReserved) {
        return !medicReserved && !patientReserved && !externalReserved;
    }
    template<int Capacity> class ReceiverReservations {
        const void* receivers[Capacity];
    public:
        ReceiverReservations() { Clear(); }
        void Clear() { for (int i = 0; i < Capacity; ++i) receivers[i] = 0; }
        bool Contains(const void* receiver) const {
            if (!receiver) return false;
            for (int i = 0; i < Capacity; ++i) if (receivers[i] == receiver) return true;
            return false;
        }
        bool Add(const void* receiver) {
            if (!receiver) return false;
            if (Contains(receiver)) return true;
            for (int i = 0; i < Capacity; ++i) if (!receivers[i]) { receivers[i] = receiver; return true; }
            return false;
        }
        void Remove(const void* receiver) {
            for (int i = 0; i < Capacity; ++i) if (receivers[i] == receiver) receivers[i] = 0;
        }
    };
    typedef ReceiverReservations<10> Reservations;
    typedef ReceiverReservations<16> PatientReservations;
    typedef ReceiverReservations<4> ExternalReservations;
}
