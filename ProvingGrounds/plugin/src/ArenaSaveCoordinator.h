#pragma once
#include "ArenaSnapshotRepository.h"
namespace ArenaPersistence {
// Engine independent transaction. Caller serializes access across Begin/Finish/
// Abandon; neither gameplay nor save request/dispatch writes any storage.
class SaveCoordinator {
    Storage& files;
    Snapshot captured;
    std::string target, error;
    bool pending;
public:
    explicit SaveCoordinator(Storage& storage) : files(storage), pending(false) {}
    void BeginSave(const std::string& destination, const std::string& generation, const Snapshot& snapshot) {
        captured=snapshot; captured.generation=generation; target=destination;
        error.clear(); pending=!target.empty() && !generation.empty();
    }
    bool FinishSave(bool nativeSucceeded) {
        if (!pending) return false;
        pending=false;
        if (!nativeSucceeded) { error="Native save did not complete successfully"; return false; }
        return PublishSnapshot(files,target,captured,error);
    }
    void AbandonWorld() { pending=false; target.clear(); captured=Snapshot(); }
    const std::string& Error() const { return error; }
};
}
