#pragma once

// Pure in-memory store for DefenseAssignment records, keyed by character id.
//
// IMPORTANT: This header (and its .cpp) must have ZERO KenshiLib includes so it
// can be unit-tested outside the game process. Game code depends on this API;
// the "ground vs turret" placement rule is enforced by callers, not here — this
// class only stores whatever fields it is given.

#include <cstddef>
#include <stdint.h>
#include <map>
#include <string>
#include <vector>

#include "types/DefenseTypes.h"

class LayoutStore
{
public:
    // Inserts a new assignment, or overwrites the existing one for the same
    // characterId (merge-by-characterId, last write wins).
    void upsert(const DefenseAssignment& a);

    // Looks up the assignment for characterId. Returns false (leaving `out`
    // unmodified) if no assignment exists for that character.
    bool tryGet(uint64_t characterId, DefenseAssignment& out) const;

    // Removes the assignment for characterId. Returns true if a record was
    // erased, false if that character had no assignment.
    bool erase(uint64_t characterId);

    size_t size() const;

    void clear();

    std::vector<DefenseAssignment> all() const;

    // Serializes all assignments to a versioned binary blob (magic "CF02").
    std::string serialize() const;

    // Restores state from a blob produced by serialize(). Returns false (and
    // leaves this store unmodified) if the blob is malformed or has an
    // unrecognized version header (including older "CF01" blobs — re-Save).
    bool deserialize(const std::string& blob);

private:
    std::map<uint64_t, DefenseAssignment> m_assignments;
};
