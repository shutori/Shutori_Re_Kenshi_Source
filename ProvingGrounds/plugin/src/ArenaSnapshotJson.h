#pragma once
#include "ArenaSnapshot.h"
#include <cstddef>
namespace ArenaPersistence {
    const size_t kMaxSnapshotBytes = 16u * 1024u * 1024u;
    // Schema 15 removes the global crowd-performance choice from per-save data.
    // Schemas 12-14 still require the legacy performance fields; they are validated
    // on read and ignored because the profile now lives in pg_config.json.
    // Schema 14 adds optional paid refresh count and isolated reroll nonce.
    // Schema 11 may omit uniqueWins; profiles for all saves now default from config.
    // Unknown fields, duplicate keys/IDs,
    // invalid UTF-8, nonfinite numbers, and wrong JSON types are rejected.
    // Marks and record counts: 0..INT_MAX. MMR: finite nonnegative float;
    // positive values must not underflow. Credits: bookie 0..the configured
    // ceiling for one winning payout (PGConfig::BookieCreditCeiling, never
    // below 50000), challenge 0..20000 (the quote's payout cap). Reward tiers
    // and unlocks use ArenaRewards.
    // Card limits follow TownChallengePolicy: unsigned 32-bit seeds; divisions
    // 0..2, milestones 0..7, uniqueWins 0..31, wins 0..1e9, lastEnd 0..24000000 hours; five regular
    // offers plus Skarn; epochs -2..1999999, draw 0..9999, generated team counts
    // 0..3, histories at most 5 bouts with 1..8 unique opponent IDs each.
    // Diagnostic targets are 1..10000, MMR influence 0..1, percentages 0..100,
    // and all lifecycle/result counters are nonnegative signed integers.
    // Fighter IDs are opaque, nonempty identifiers. Opponent/history IDs belong
    // to matchmaking and need not already have a career in a standings table.
    // Document safety limits: kMaxSnapshotBytes UTF-8 bytes and 32 container
    // nesting levels.
    // The codec does no I/O, hash sealing, or generation matching. Its only
    // migration validates but discards legacy per-save crowd profiles.
    LoadResult DecodeSnapshot(const std::string& json);
    // On failure, json is unchanged and error includes the field path.
    bool EncodeSnapshot(const Snapshot& value, std::string& json, std::string& error);
}
