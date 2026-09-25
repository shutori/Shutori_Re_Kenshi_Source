#include "ArenaMedical.h"

namespace ArenaMedical
{
    bool ShouldBlockTreatment(
        Protocol protocol,
        bool matchActive,
        bool targetIsParticipant,
        bool targetIsEliminated)
    {
        if (!matchActive || !targetIsParticipant)
            return false;
        if (protocol == Bloodsport)
            return true;
        return !targetIsEliminated;
    }

    bool ShouldStabilizePrisoner(Protocol protocol)
    {
        return protocol != Bloodsport;
    }

    const char* GetProtocolName(Protocol protocol)
    {
        switch (protocol)
        {
        case Bloodsport: return "Bloodsport";
        case RingsideAid:
        default: return "Ringside Aid";
        }
    }

    Protocol NextProtocol(Protocol protocol)
    {
        if (protocol == RingsideAid)
            return Bloodsport;
        return RingsideAid;
    }
}
