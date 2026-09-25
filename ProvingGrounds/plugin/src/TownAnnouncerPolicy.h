#pragma once

#include <cstring>

namespace TownAnnouncerPolicy
{
    struct ApproachProgress
    {
        float bestDistance;
        float stalledElapsed;
        ApproachProgress() : bestDistance(0.0f), stalledElapsed(0.0f) {}
    };

    struct ReleasePlan
    {
        bool releaseOwnership;
        bool resumeNative;
        bool deferNative;
        ReleasePlan(bool release, bool resume, bool defer)
            : releaseOwnership(release), resumeNative(resume), deferNative(defer) {}
    };

    enum PendingReleaseDisposition
    {
        ForgetPendingRelease = 0,
        WaitPendingRelease = 1,
        ResumePendingRelease = 2
    };

    enum Program
    {
        LegacyCountdown = 0,
        HouseAnnouncer = 1,
        SkarnAnnouncer = 2,
        SennAndTorkaAnnouncers = 3
    };

    inline bool Equals(const char* a, const char* b)
    {
        return a && b && std::strcmp(a, b) == 0;
    }

    inline bool IsSpot(const char* id)
    {
        return Equals(id, "261-Proving Grounds.mod");
    }

    inline Program Select(bool playerMatch, int challengeSlot,
        const char* const* opponentRoles, int opponentCount)
    {
        if (!playerMatch)
            return LegacyCountdown;
        if (challengeSlot == 5)
        {
            // Some unlocked Skarn rematches also put Senn and/or Torka in the
            // pit. They cannot occupy the viewing stand at the same time.
            for (int i = 0; opponentRoles && i < opponentCount; ++i)
                if (Equals(opponentRoles[i], "169-Proving Grounds.mod") ||
                    Equals(opponentRoles[i], "168-Proving Grounds.mod"))
                    return HouseAnnouncer;
            return SennAndTorkaAnnouncers;
        }
        for (int i = 0; opponentRoles && i < opponentCount; ++i)
        {
            if (Equals(opponentRoles[i], "169-Proving Grounds.mod") ||
                Equals(opponentRoles[i], "168-Proving Grounds.mod"))
                return SkarnAnnouncer;
        }
        return HouseAnnouncer;
    }

    inline int RequiredSpeakers(Program program)
    {
        if (program == SennAndTorkaAnnouncers)
            return 2;
        return program == HouseAnnouncer || program == SkarnAnnouncer ? 1 : 0;
    }

    inline const char* SpeakerRole(Program program, int index)
    {
        if (index < 0 || index >= RequiredSpeakers(program))
            return NULL;
        if (program == HouseAnnouncer)
            return "260-Proving Grounds.mod";
        if (program == SkarnAnnouncer)
            return "167-Proving Grounds.mod";
        return index == 0 ? "169-Proving Grounds.mod" : "168-Proving Grounds.mod";
    }

    inline Program Resolve(Program requested, bool hasSpot, int speakerCount)
    {
        return hasSpot && speakerCount == RequiredSpeakers(requested)
            ? requested
            : LegacyCountdown;
    }

    inline Program NextFallback(Program requested)
    {
        return requested == SkarnAnnouncer || requested == SennAndTorkaAnnouncers
            ? HouseAnnouncer
            : LegacyCountdown;
    }

    inline int VariantCount(Program program)
    {
        if (program == HouseAnnouncer)
            return 8;
        if (program == SkarnAnnouncer)
            return 5;
        if (program == SennAndTorkaAnnouncers)
            return 4;
        return 1;
    }

    inline int Variant(Program program, unsigned roll)
    {
        return static_cast<int>(roll % static_cast<unsigned>(VariantCount(program)));
    }

    inline int SetupCueCount(Program program, int variant)
    {
        if (program == SennAndTorkaAnnouncers)
            return 4;
        const int selected = Variant(program, static_cast<unsigned>(variant));
        if (program == HouseAnnouncer)
            return 2 + selected % 2;
        if (program == SkarnAnnouncer)
            return 3 + selected % 2;
        return 0;
    }

    inline int CueCount(Program program, int variant = 0)
    {
        const int setup = SetupCueCount(program, variant);
        return setup > 0 ? setup + 1 : 0;
    }

    inline int CueSpeaker(Program program, int cue, int variant = 0)
    {
        if (cue < 0 || cue >= CueCount(program, variant))
            return -1;
        if (program == SennAndTorkaAnnouncers)
            return cue == 1 || cue == 3 ? 1 : 0;
        return 0;
    }

    inline float CueStart(Program program, int cue, int variant = 0)
    {
        return cue >= 0 && cue < CueCount(program, variant)
            ? static_cast<float>(cue) * 3.25f
            : -1.0f;
    }

    inline int CueAt(Program program, int lastCue, float elapsed, int variant = 0)
    {
        const int next = lastCue + 1;
        return next < CueCount(program, variant) && elapsed >= CueStart(program, next, variant)
            ? next
            : -1;
    }

    inline int CuesDue(Program program, int lastCue, float elapsed, int variant = 0)
    {
        int count = 0;
        for (int cue = lastCue + 1; cue < CueCount(program, variant); ++cue)
        {
            if (elapsed < CueStart(program, cue, variant))
                break;
            ++count;
        }
        return count;
    }

    inline float Duration(Program program, int variant = 0)
    {
        const int count = CueCount(program, variant);
        return count > 0 ? CueStart(program, count - 1, variant) + 0.75f : 0.0f;
    }

    inline float AdvanceApproach(float elapsed, float delta, bool paused)
    {
        return paused ? elapsed : elapsed + delta;
    }

    inline void ResetApproachProgress(ApproachProgress& progress, float distance)
    {
        progress.bestDistance = distance;
        progress.stalledElapsed = 0.0f;
    }

    inline bool ApproachStalled(ApproachProgress& progress, float distance,
        float delta, bool paused)
    {
        static const float meaningfulProgress = 2.0f;
        static const float stallTimeout = 20.0f;
        if (paused)
            return false;
        if (distance <= progress.bestDistance - meaningfulProgress)
        {
            progress.bestDistance = distance;
            progress.stalledElapsed = 0.0f;
        }
        else
        {
            progress.stalledElapsed += delta;
        }
        return progress.stalledElapsed >= stallTimeout;
    }

    inline ReleasePlan PlanRelease(bool owned, bool directed, bool safeToResume)
    {
        return ReleasePlan(owned, directed && safeToResume,
            directed && !safeToResume);
    }

    inline PendingReleaseDisposition PendingReleaseFor(bool valid, bool dead,
        bool safeToResume)
    {
        if (!valid || dead)
            return ForgetPendingRelease;
        return safeToResume ? ResumePendingRelease : WaitPendingRelease;
    }

    inline float CountdownElapsedAfterFallback(bool announcementLost, float elapsed)
    {
        return announcementLost ? 0.0f : elapsed;
    }

    inline const char* CueText(Program program, int variant, int cue)
    {
        if (cue < 0 || cue >= CueCount(program, variant))
            return NULL;
        if (cue == CueCount(program, variant) - 1)
            return "FIGHT!";

        if (program == HouseAnnouncer)
        {
            static const char* scripts[][3] = {
                { "Fresh blood for the Rust Crucible! Let Scratch judge what the wastes have spared.",
                  "Two sides enter our sand. Only one gets the cheers when this is done.",
                  "Keep your hands off your Cats and your eyes on the pit." },
                { "Cats change hands. Teeth hit sand. Scratch, keep your eyes on the pit.",
                  "Pick a side now. Regret pays nothing once the first blow lands.",
                  "When the dust settles, remember who took your money." },
                { "The gates are shut and the medics are waiting. Let the Crucible settle the rest.",
                  "They came here standing. The medics only promised to keep them breathing.",
                  "Scratch, make some noise for the brave and the foolish." },
                { "No titles below this rail. No excuses. Only iron and whoever still stands.",
                  "Strength talks plainly in this pit. We are about to hear every word.",
                  "The Crucible remembers winners. Everyone else gets carried out." },
                { "The wastes made them hard. Scratch gets to find out how hard.",
                  "Look well. Confidence weighs nothing once the steel starts moving.",
                  "Let the pit strip away every boast they brought through the gate." },
                { "Another drifter wagers bone against steel. The Rust Crucible accepts.",
                  "One good strike buys glory. One bad step buys a bed in the hospital.",
                  "Scratch has placed its bets. Now earn them." },
                { "Steel is cheap. Blood is cheaper. Show Scratch what you are worth.",
                  "No guards will save them and no story will soften the score.",
                  "Stand tall now. The sand has room for both of you." },
                { "They walked out of the wastes and into our pit. Let us see which was kinder.",
                  "The crowd wants courage. The bookie wants Cats. The Crucible wants proof.",
                  "Give Scratch a finish worth lying about tomorrow." }
            };
            return scripts[Variant(program, static_cast<unsigned>(variant))][cue];
        }
        if (program == SkarnAnnouncer)
        {
            static const char* scripts[][4] = {
                { "One of Scratch's own stands below. The challenger came anyway.",
                  "Courage brought them through the gate. Courage will not carry them back out.",
                  "Watch closely. Scratch protects its name with fists, not promises.",
                  "The pit is ready to collect what pride owes." },
                { "That one survived my training. We will see if the challenger survives them.",
                  "I broke every soft habit they carried into this town.",
                  "The challenger sees one fighter. I see every lesson waiting to hurt them.",
                  "Do not disappoint me. Either of you." },
                { "Scratch knows this fighter. The outsider still has time to run.",
                  "No? Good. I was tired of watching sensible people.",
                  "Our sand does not care where you came from. It only remembers where you fell.",
                  "Show this crowd which name deserves to leave standing." },
                { "The pit raised one of these. The wastes dragged in the other.",
                  "Both claim the harder teacher. Claims are cheap above the rail.",
                  "Below it, every scar becomes an argument.",
                  "Settle the lesson where Scratch can see it." },
                { "I taught them to finish fights. Let us see if they listened.",
                  "A clean victory earns respect. A slow one earns practice.",
                  "The challenger has one chance to make my work look poor.",
                  "Make this worth the blood we are about to wash away." }
            };
            return scripts[Variant(program, static_cast<unsigned>(variant))][cue];
        }
        if (program == SennAndTorkaAnnouncers)
        {
            static const char* senn[][2] = {
                { "Scratch, look sharp! The old monster has stepped into his own pit.",
                  "I give the challenger thirty breaths before Skarn remembers his manners." },
                { "The boss finally came down from the rail. Try to look frightened.",
                  "I brought bandages, but not enough for his pride." },
                { "Skarn wants blood on his own stones. Rude not to give him some.",
                  "Someone tell the medics to bring the bucket with the strong handle." },
                { "Clear the rail! Scratch's oldest bad decision is fighting again.",
                  "If the challenger wins, I am claiming I trained them." }
            };
            static const char* torka[][2] = {
                { "Skarn! Leave enough challenger for the medics to identify.",
                  "Thirty breaths? I wager ten, and I want change." },
                { "Try not to break this one before I win my bet.",
                  "Keep the bandages. I put my Cats on the old man." },
                { "Old man, the crowd paid for a fight, not an execution.",
                  "Use the good bucket. This one looks expensive." },
                { "I counted their limbs. Do not make me count again.",
                  "Fine. I will claim I trained whoever is still conscious." }
            };
            const int selected = Variant(program, static_cast<unsigned>(variant));
            if (cue == 0) return senn[selected][0];
            if (cue == 1) return torka[selected][0];
            if (cue == 2) return senn[selected][1];
            return torka[selected][1];
        }
        return NULL;
    }
}
