#pragma once

#include <climits>
#include <cstdio>
#include <string>

namespace TownLimbShopPolicy
{
    enum Grade
    {
        Shoddy = 0,
        Standard,
        High,
        Specialist,
        Masterwork,
        GradeCount
    };

    enum OrderKind
    {
        ArmOrder = 0,
        LegOrder,
        MultipleOrder,
        OrderKindCount
    };

    inline Grade GradeFor(int division, bool legendary, unsigned roll)
    {
        roll %= 100u;
        if (legendary) return roll < 80u ? Specialist : Masterwork;
        if (division <= 0)
            return roll < 70u ? Shoddy : roll < 95u ? Standard : High;
        if (division == 1)
            return roll < 15u ? Standard : roll < 90u ? High : Specialist;
        return roll < 10u ? High : roll < 90u ? Specialist : Masterwork;
    }

    inline int QualityLevel(Grade grade)
    {
        static const int values[] = { 20, 40, 60, 80, 95 };
        return grade >= Shoddy && grade < GradeCount ? values[grade] : values[Shoddy];
    }

    inline const char* GradeName(Grade grade)
    {
        static const char* names[] = {
            "shoddy", "standard", "high-grade", "specialist", "masterwork"
        };
        return grade >= Shoddy && grade < GradeCount ? names[grade] : names[Shoddy];
    }

    inline unsigned Mix(unsigned hash, unsigned value)
    {
        for (int byte = 0; byte < 4; ++byte)
        {
            hash ^= value & 0xffu;
            hash *= 16777619u;
            value >>= 8;
        }
        return hash;
    }

    inline unsigned Seed(const std::string& identity, int matches, unsigned limbMask)
    {
        unsigned hash = 2166136261u;
        for (size_t i = 0; i < identity.size(); ++i)
        {
            hash ^= static_cast<unsigned char>(identity[i]);
            hash *= 16777619u;
        }
        hash = Mix(hash, static_cast<unsigned>(matches < 0 ? 0 : matches));
        hash = Mix(hash, limbMask & 15u);
        return hash ? hash : 0x9e3779b9u;
    }

    inline int Price(Grade grade, int limbCount)
    {
        static const int perLimb[] = { 800, 2500, 6000, 12000, 25000 };
        const int safeGrade = grade >= Shoddy && grade < GradeCount ? grade : Shoddy;
        if (limbCount <= 0) return 0;
        if (limbCount > INT_MAX / perLimb[safeGrade]) return INT_MAX;
        return perLimb[safeGrade] * limbCount;
    }

    inline OrderKind KindFor(unsigned limbMask)
    {
        const bool arms = (limbMask & 3u) != 0;
        const bool legs = (limbMask & 12u) != 0;
        const bool several = limbMask && (limbMask & (limbMask - 1u)) != 0;
        return several || (arms && legs) ? MultipleOrder : legs ? LegOrder : ArmOrder;
    }

    inline const char* OrderName(OrderKind kind)
    {
        return kind == LegOrder ? "leg" : kind == MultipleOrder ? "set" : "arm";
    }

    inline std::string TraderLine(Grade grade, unsigned variant, int kindValue)
    {
        const OrderKind kind = kindValue >= ArmOrder && kindValue < OrderKindCount ?
            static_cast<OrderKind>(kindValue) : ArmOrder;
        const char* item = OrderName(kind);
        static const char* openings[GradeCount][3] = {
            { "It's ugly, but this shoddy %s will move.", "Budget %s. It clanks, it works.", "Cheapest %s in the shop. No promises on the paint." },
            { "Standard %s. Reliable enough for the pit.", "A solid standard %s, tested this morning.", "Standard-grade %s. Nothing fancy, nothing loose." },
            { "High-grade %s. Fast response and reinforced joints.", "This high-grade %s can take a beating.", "High-grade %s. Better than what most fighters can afford." },
            { "Specialist %s. Balanced servos, reinforced fittings. Not cheap.", "Specialist-grade %s. Arena quality, through and through.", "A specialist %s. Keep it oiled and it'll outlast you." },
            { "Masterwork %s. Best piece in Scratch.", "Masterwork %s. Perfect calibration, not a fraction of play.", "This masterwork %s belongs on a champion." }
        };
        char text[192];
        sprintf_s(text, openings[grade >= Shoddy && grade < GradeCount ? grade : Shoddy][variant % 3u], item);
        return text;
    }

    inline std::string FighterLine(Grade grade, unsigned variant, int kindValue)
    {
        const OrderKind kind = kindValue >= ArmOrder && kindValue < OrderKindCount ?
            static_cast<OrderKind>(kindValue) : ArmOrder;
        const char* item = OrderName(kind);
        static const char* replies[GradeCount][3] = {
            { "Cheap gets me back in the ring.", "If it bends, I'll bend it back.", "Bolt it on. I've fought with worse." },
            { "Standard is fine. My hits do the talking.", "Good. I just need it ready for the next bout.", "That'll do. Fit the %s and make it tight." },
            { "Now that's an upgrade.", "High grade? The next opponent is in trouble.", "Fit it. I want to feel what this %s can do." },
            { "Good. Cheap wasn't going to put me on top.", "Specialist? Now we're talking.", "With that %s, nobody in my division is safe." },
            { "Oh man, this masterwork %s is gonna put me on top.", "Masterwork. Worth every Cat.", "Fit it now. I want the whole arena to see it." }
        };
        const char* format = replies[grade >= Shoddy && grade < GradeCount ? grade : Shoddy][variant % 3u];
        char text[192];
        sprintf_s(text, format, item);
        return text;
    }

    struct ApproachProgress
    {
        float elapsed;
        float bestDistance;
        float stalled;
        ApproachProgress() : elapsed(0.0f), bestDistance(1.0e30f), stalled(0.0f) {}
    };

    inline void ResetApproach(ApproachProgress& progress, float distance)
    {
        progress.elapsed = 0.0f;
        progress.bestDistance = distance;
        progress.stalled = 0.0f;
    }

    inline void UpdateApproach(ApproachProgress& progress, float distance, float dt)
    {
        if (dt < 0.0f) dt = 0.0f;
        if (dt > 1.0f) dt = 1.0f;
        progress.elapsed += dt;
        if (distance + 1.0f < progress.bestDistance)
        {
            progress.bestDistance = distance;
            progress.stalled = 0.0f;
        }
        else progress.stalled += dt;
    }

    inline bool ApproachStalled(const ApproachProgress& progress)
    {
        return progress.stalled >= 20.0f;
    }
}
