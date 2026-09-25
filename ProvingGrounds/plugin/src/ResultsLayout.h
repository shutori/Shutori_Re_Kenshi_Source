#pragma once

#include "LeaderboardLayout.h"
#include <string>

// Pure geometry adapter for the match-results presentation. Keeping the
// coordination here makes ResultsUI consume the same tested ceremonial rules as
// the leaderboards without duplicating MyGUI-independent layout decisions.
namespace ResultsLayout
{
    inline std::string PortraitInitialUtf8(const char* name)
    {
        if (!name || !name[0])
            return "?";

        const unsigned char* bytes =
            reinterpret_cast<const unsigned char*>(name);
        const unsigned char lead = bytes[0];
        int length = 0;
        if (lead <= 0x7f) length = 1;
        else if (lead >= 0xc2 && lead <= 0xdf) length = 2;
        else if (lead >= 0xe0 && lead <= 0xef) length = 3;
        else if (lead >= 0xf0 && lead <= 0xf4) length = 4;
        else return "?";

        for (int i = 1; i < length; ++i)
            if (!bytes[i] || (bytes[i] & 0xc0) != 0x80)
                return "?";

        // Reject overlong sequences, UTF-16 surrogate code points and values
        // beyond Unicode's upper bound before MyGUI validates the caption.
        if ((lead == 0xe0 && bytes[1] < 0xa0) ||
            (lead == 0xed && bytes[1] >= 0xa0) ||
            (lead == 0xf0 && bytes[1] < 0x90) ||
            (lead == 0xf4 && bytes[1] >= 0x90))
            return "?";

        return std::string(name, length);
    }

    struct Ceremony
    {
        LeaderboardLayout::CeremonialMetrics metrics;
        LeaderboardLayout::TitleRails title;
        LeaderboardLayout::Podium podium;
    };

    inline Ceremony FitCeremony(
        int availableWidth,
        int gap,
        int body,
        int resultCount,
        int measuredTitleWidth)
    {
        Ceremony result;
        result.metrics = LeaderboardLayout::FitCeremonialMetrics(body, gap);
        result.title = LeaderboardLayout::FitTitleRails(
            availableWidth, gap, measuredTitleWidth);
        result.podium = LeaderboardLayout::FitPodium(
            availableWidth, gap, resultCount,
            result.metrics.championPortrait,
            result.metrics.otherPortrait);
        return result;
    }
}
