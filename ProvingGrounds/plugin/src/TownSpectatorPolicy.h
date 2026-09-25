#pragma once
#include <cstring>

namespace TownSpectatorPolicy {
    enum AudienceRole {
        NotAudience = -1,
        Civilian = 0,
        BarResident = 1,
        CageHandler = 2
    };
    enum ReleaseDisposition {
        ForgetRelease,
        DeferNativeResume,
        ResumeNativeNow
    };

    inline AudienceRole RoleFor(const char* characterId) {
        if (!characterId) return NotAudience;
        if (std::strcmp(characterId, "103-Proving Grounds.mod") == 0) return Civilian;
        // Character 258 is supplied by Scratch Bar Residents squad 257.
        if (std::strcmp(characterId, "258-Proving Grounds.mod") == 0) return BarResident;
        // Character 99 is supplied by Scratch Cage Handlers squad 108.
        if (std::strcmp(characterId, "99-Proving Grounds.mod") == 0) return CageHandler;
        return NotAudience;
    }
    inline bool CanWatch(AudienceRole role, bool player, bool permanentJob, bool healthy) {
        return role != NotAudience && !player && healthy &&
            (!permanentJob || role == BarResident || role == CageHandler);
    }
    inline bool CanReserve(bool free, bool reserved, int audience, int target) {
        return free && !reserved && audience >= 0 && target > 0 && target <= 120 && audience < target;
    }
    inline AudienceRole NextRole(int civilians, int residents, int handlers, AudienceRole cursor) {
        const int remaining[] = { civilians, residents, handlers };
        const int start = cursor >= Civilian && cursor <= CageHandler ? static_cast<int>(cursor) : 0;
        for (int offset = 0; offset < 3; ++offset) {
            const int role = (start + offset) % 3;
            if (remaining[role] > 0) return static_cast<AudienceRole>(role);
        }
        return NotAudience;
    }
    inline ReleaseDisposition ReleaseFor(bool valid, bool dead, bool player, bool healthy) {
        if (!valid || dead || player) return ForgetRelease;
        return healthy ? ResumeNativeNow : DeferNativeResume;
    }
    inline bool BelongsToArena(bool sameTown, double distanceSquared, double otherArenaDistanceSquared) {
        return sameTown && distanceSquared >= 0 && distanceSquared <= 250000 && distanceSquared < otherArenaDistanceSquared;
    }
}
