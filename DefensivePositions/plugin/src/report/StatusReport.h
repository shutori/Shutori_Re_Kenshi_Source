#pragma once

// Pure status-report formatting for the tactical defense feature.
//
// IMPORTANT: This header (and its .cpp) must have ZERO KenshiLib includes so it
// can be unit-tested outside the game process.
//
// Nested enum (not `enum class`) so the VS2010 (v100) toolset can compile the
// plugin while still allowing `UnitResult::Ok` qualification without the MSVC
// C4482 "nonstandard extension" warning that plain `enum UnitResult` triggers.

#include <stdint.h>
#include <string>
#include <vector>

struct UnitResult
{
    enum Type
    {
        Ok,
        NoAssignment,
        NotControllable,
        Dead,
        Ko,
        Imprisoned,
        Unavailable,
        TurretMissing,
        TurretRemountFailed,
        Timeout
    };
};

struct UnitStatus
{
    uint64_t characterId;
    std::string displayName;
    UnitResult::Type result;
    // Optional short reason shown after the label, e.g.
    // "Rex: turret remount failed (occupied 1/1; dist=2.1m)".
    // Empty means no parenthetical — keep the report readable for OK rows.
    std::string detail;
};

std::string formatStatusReport(const std::vector<UnitStatus>& rows);

std::string formatSaveConfirm(size_t positions, size_t turrets);

std::string formatDiscardConfirm(size_t positions);
