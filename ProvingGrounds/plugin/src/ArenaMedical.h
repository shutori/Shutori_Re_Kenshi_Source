#pragma once

class Character;

namespace ArenaMedical
{
    enum Protocol
    {
        RingsideAid = 1,
        Bloodsport = 2
    };

    // Pure policy surface, kept separate from the hook details for easy testing.
    bool ShouldBlockTreatment(
        Protocol protocol,
        bool matchActive,
        bool targetIsParticipant,
        bool targetIsEliminated);
    bool ShouldStabilizePrisoner(Protocol protocol);

    void SetProtocol(Protocol protocol);
    Protocol GetProtocol();
    const char* GetProtocolName(Protocol protocol);
    Protocol NextProtocol(Protocol protocol);

    // Kenshi can retain a cached need score after the last wound record is
    // removed. Only expose that score while first aid still has work to do.
    float ActionableAidNeed(Character* target, bool robotAid);
    bool NeedsStabilization(Character* target);
    void IssueStabilizeOrder(Character* medic, Character* target);

    bool InstallHooks();
}
