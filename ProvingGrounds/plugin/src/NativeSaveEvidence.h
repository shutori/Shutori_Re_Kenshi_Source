#pragma once
#include <string>
namespace ArenaPersistence {
struct NativeSaveEvidence {
    const void* filesystem;
    std::string target, generation;
    bool serializationSeen, serializationOk, metadataWritten, scheduled, managerReturned, completed;
    int managerResult;
    std::string copyError;
    NativeSaveEvidence() : filesystem(0),serializationSeen(false),serializationOk(true),metadataWritten(false),scheduled(false),managerReturned(false),completed(false),managerResult(-1),identityOk(false),identityChecked(false) {}
    void Serialized(bool ok) { serializationSeen=true; serializationOk=serializationOk && ok; }
    bool Complete(const void* object,const std::string& path,const std::string& error) { if (!scheduled || !filesystem || filesystem!=object || target.empty() || target!=path || generation.empty() || completed) return false; completed=true; copyError=error; return true; }
    bool identityOk, identityChecked;
    void IdentityPersisted(bool ok) { identityOk=identityChecked ? identityOk && ok : ok; identityChecked=true; }
    bool Ready() const { return completed && managerReturned; }
    bool Succeeded() const { return Ready() && identityChecked && identityOk && scheduled && managerResult==0 && serializationSeen && serializationOk && metadataWritten && copyError.empty(); }
};
}
