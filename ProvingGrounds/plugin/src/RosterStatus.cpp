#include "RosterStatus.h"

#include <kenshi/Character.h>
#include <kenshi/MedicalSystem.h>
#include <cstdio>

namespace RosterStatus
{
    bool Read(Character* character, Snapshot& snapshot, std::string* error)
    {
        snapshot.lowestLimb = 0.0f;
        snapshot.blood = 0.0f;
        snapshot.recovery = 0.0f;
        snapshot.band = RosterStatusPolicy::Red;
        if (error) *error = "health data unavailable";
        if (!character || !character->isValid()) return false;

        MedicalSystem* medical = character->getMedical();
        if (!medical || medical->anatomy.size() == 0)
        {
            if (error) *error = !medical ? "medical system unavailable" : "anatomy is empty";
            return false;
        }

        const float maxBlood = medical->getMaxBlood();
        snapshot.blood = maxBlood > 0.0f ? medical->blood / maxBlood : 0.0f;
        snapshot.lowestLimb = 1.0f;
        for (uint32_t i = 0; i < medical->anatomy.size(); ++i)
        {
            MedicalSystem::HealthPartStatus* part = medical->anatomy[i];
            if (!part || !(part->maxHealth() > 0.0f))
            {
                if (error)
                {
                    char detail[128];
                    sprintf_s(detail, "anatomy part %u/%u has no valid maximum health", i, medical->anatomy.size());
                    *error = detail;
                }
                return false;
            }
            const float ratio = (part->flesh - part->fleshStun) / part->maxHealth();
            snapshot.lowestLimb = RosterStatusPolicy::LowestLimb(snapshot.lowestLimb, ratio);
        }
        snapshot.recovery = RosterStatusPolicy::Recovery(snapshot.lowestLimb, snapshot.blood);
        snapshot.band = RosterStatusPolicy::BandFor(snapshot.recovery);
        if (error) error->clear();
        return true;
    }
}
