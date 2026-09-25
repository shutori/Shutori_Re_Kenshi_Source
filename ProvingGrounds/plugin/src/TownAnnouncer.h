#pragma once

class Building;
class Character;

namespace TownAnnouncer
{
    // Resolves the requested cast and the placed announcer spot for a town bout.
    // Missing actors or furniture deliberately select the legacy countdown.
    void Begin(Building* arena, bool playerMatch, int challengeSlot,
        const char* const* opponentRoles, int opponentCount,
        Character* const* fighters, int fighterCount);

    void TickApproach();
    void TickReleases();
    bool WaitingForPosition();
    bool HasAnnouncement();
    void BeginAnnouncement();
    void TickAnnouncement(float elapsed);
    float AnnouncementDuration();
    const char* GetStatus();

    void Release();
    void AbandonWorldState();
}
