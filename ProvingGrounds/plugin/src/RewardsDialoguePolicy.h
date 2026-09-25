#pragma once

#include <cstring>

namespace RewardsDialoguePolicy
{
    static const char* const kNullStringId =
        "166-Proving Grounds.mod";

    enum TradeRoute
    {
        PassThrough = 0,
        OpenForFirst = 1,
        OpenForSecond = 2
    };

    enum PendingDecision
    {
        WaitForDialogue = 0,
        DiscardPending = 1,
        OpenPending = 2
    };

    inline bool IsNullId(const char* stringId)
    {
        return stringId
            && std::strcmp(stringId, kNullStringId) == 0;
    }

    inline TradeRoute ResolveTrade(
        const char* firstStringId,
        const char* secondStringId,
        bool moneyTrade)
    {
        if (!moneyTrade)
            return PassThrough;
        const bool firstIsNull = IsNullId(firstStringId);
        const bool secondIsNull = IsNullId(secondStringId);
        if (firstIsNull == secondIsNull)
            return PassThrough;
        return firstIsNull ? OpenForSecond : OpenForFirst;
    }

    inline PendingDecision ResolvePending(
        bool dialogueActive,
        bool participantValid)
    {
        if (!participantValid)
            return DiscardPending;
        return dialogueActive ? WaitForDialogue : OpenPending;
    }
}
