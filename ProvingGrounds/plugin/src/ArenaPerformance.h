#pragma once

#include <string>

namespace ArenaPerformance
{
    enum Profile
    {
        High = 0,
        Normal = 1,
        Potato = 2
    };

    struct Policy
    {
        int spectatorCap;
        unsigned maintenanceMs;
        float chatterMinSeconds;
        float chatterMaxSeconds;
        int maxChatterVoices;
        bool chatterEnabled;

        Policy(
            int cap = 120,
            unsigned maintenance = 0,
            float chatterMin = 4.0f,
            float chatterMax = 8.0f,
            int voices = 5,
            bool chatter = true)
            : spectatorCap(cap), maintenanceMs(maintenance),
              chatterMinSeconds(chatterMin), chatterMaxSeconds(chatterMax),
              maxChatterVoices(voices), chatterEnabled(chatter) {}
    };

    bool IsValid(int value);
    const char* Name(Profile profile);
    const char* PersistedName(Profile profile);
    bool Parse(const std::string& value, Profile& profile);
    Policy PolicyFor(Profile profile);
}
