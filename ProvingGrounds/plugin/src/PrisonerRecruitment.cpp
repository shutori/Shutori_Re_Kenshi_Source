#include "PrisonerRecruitment.h"

#include "ArenaIngress.h"
#include "LeaderboardStore.h"
#include "RewardTransaction.h"
#include "FighterIdentity.h"
#include "PrisonerRecruitmentLogic.h"
#include "PrisonerUtil.h"
#include "SparSession.h"

#include "PGLog.h"

#include <cstdio>

#pragma warning(push)
#pragma warning(disable: 4091)
#include <kenshi/Character.h>
#include <kenshi/CharStats.h>
#include <kenshi/GameWorld.h>
#include <kenshi/Globals.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/RootObjectBase.h>
#include <kenshi/util/hand.h>
#pragma warning(pop)

#ifndef NULL
#define NULL 0
#endif

namespace PrisonerRecruitment
{
    namespace
    {
        struct RecruitmentPayment : RewardTransaction::Binding, RewardTransaction::ReservedAccounting
        {
            Character* prisoner;
            hand actor;
            RecruitmentPayment(Character* c) : RewardTransaction::ReservedAccounting(static_cast<RewardTransaction::Binding&>(*this)), prisoner(c), actor(c) {}
            bool Matches(const std::string& identity) const
            {
                std::string current;
                return actor.getCharacter() == prisoner &&
                    FighterIdentity::Find(prisoner,current) && current == identity;
            }
        };
        struct RecruitAction : RewardTransaction::Action
        {
            Character* prisoner;
            RecruitAction(Character* c) : prisoner(c) {}
            bool Apply() { return ou->player->recruit(prisoner, false); }
        };
    }

    float AverageCombatStat(Character* prisoner)
    {
        if (!prisoner || !prisoner->isValid() || !prisoner->getStats())
            return 0.0f;
        return prisoner->getStats()->getOverallSkillLevel_0_100();
    }

    int RequiredMarks(Character* prisoner)
    {
        return PrisonerRecruitmentLogic::RequiredMarks(
            AverageCombatStat(prisoner));
    }

    bool Recruit(Character* prisoner, std::string& status)
    {
        status.clear();
        if (!prisoner || !prisoner->isValid() || prisoner->isDead())
        {
            status = "Select a living prisoner.";
            return false;
        }
        if (!PrisonerUtil::IsRosterPrisoner(prisoner))
        {
            status = "This fighter is not an available arena prisoner.";
            return false;
        }
        if (ArenaIngress::IsPending() || SparSession::IsActive() ||
            PrisonerUtil::HasRuntimeActivity())
        {
            status = "Finish the current arena operation before recruiting.";
            return false;
        }
        if (prisoner->isUnconcious())
        {
            status = "The prisoner must be conscious to accept freedom.";
            return false;
        }
        if (!ou || !ou->player)
        {
            status = "The player faction is not ready.";
            return false;
        }

        const int cost = RequiredMarks(prisoner);
        if (LeaderboardStore::GetMarks(prisoner) < cost)
        {
            status = "This prisoner has not earned enough Arena Marks.";
            return false;
        }
        RecruitmentPayment payment(prisoner);
        if (!LeaderboardStore::ReserveProgression(prisoner,ArenaLedger::Purchase(cost),payment))
        {
            status = "The recruitment payment could not be reserved.";
            return false;
        }

        RecruitAction recruitment(prisoner);
        const RewardTransaction::Result result = RewardTransaction::CompleteAction(recruitment,payment);
        if (result != RewardTransaction::Success)
        {
            status = result == RewardTransaction::AccountingRollbackFailed
                ? "Recruitment failed and its payment could not be restored. Check Arena Marks before saving."
                : "Kenshi could not add this prisoner to the current squad; the payment was cancelled.";
            return false;
        }

        prisoner->setPrisonMode(false, NULL);
        if (prisoner->isChainedMode())
        {
            prisoner->setChainedMode(
                false, hand(static_cast<RootObjectBase*>(prisoner)));
        }
        PrisonerUtil::ForgetRosterPrisoner(prisoner);

        char costText[32];
        sprintf_s(costText, "%d", cost);
        status = prisoner->getName() +
            " earned freedom and joined your squad for " + costText +
            " Arena Marks.";
        PGLog::Debug((std::string("Proving Grounds: arena prisoner recruited: ") +
            prisoner->getName()).c_str());
        return true;
    }
}
