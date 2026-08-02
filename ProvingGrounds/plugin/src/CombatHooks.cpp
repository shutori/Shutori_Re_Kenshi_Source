#include "CombatHooks.h"
#include "PrisonerMatchLogic.h"
#include "PrisonerUtil.h"
#include "SparHitLogic.h"
#include "SparSession.h"
#include "SparStats.h"

#include <Debug.h>
#include <core/Functions.h>

#pragma warning(push)
#pragma warning(disable: 4091)
#include <kenshi/Character.h>
#include <kenshi/CombatClass.h>
#include <kenshi/CombatTechniqueData.h>
#include <kenshi/Damages.h>
#include <kenshi/Enums.h>
#pragma warning(pop)

#ifndef NULL
#define NULL 0
#endif

namespace
{
    bool (*isEnemy_orig)(Character* thisptr, Character* who, bool factorInDisguises) = NULL;
    bool (*isAlly_orig)(Character* thisptr, Character* who, bool factorInDisguises) = NULL;
    bool (*isFightingAnAllyOfMine_orig)(CombatClass* thisptr, Character* who) = NULL;
    HitMaterialType (*iHitYouAreYouHit_orig)(CombatClass* thisptr, CutDirection dir, Damages& damage, Character* who) = NULL;
    void (*attackTarget_orig)(Character* thisptr, Character* who) = NULL;
    bool (*iShouldntAggravateThisTarget_orig)(Character* thisptr, Character* target) = NULL;

    bool ShouldIsolatePair(Character* a, Character* b)
    {
        if (!a || !b)
            return false;
        if (SparSession::IsSparringOpponent(a, b))
            return false;
        return PrisonerMatchLogic::ShouldSuppressNaturalEnemy(
            false,
            PrisonerUtil::IsMatchPrisoner(a),
            PrisonerUtil::IsMatchPrisoner(b));
    }

    bool isEnemy_hook(Character* thisptr, Character* who, bool factorInDisguises)
    {
        if (SparSession::IsSparringOpponent(thisptr, who))
            return true;
        if (ShouldIsolatePair(thisptr, who))
            return false;
        return isEnemy_orig(thisptr, who, factorInDisguises);
    }

    bool isAlly_hook(Character* thisptr, Character* who, bool factorInDisguises)
    {
        // Same-faction melee damage path often checks isAlly and skips applying hits.
        if (SparSession::IsSparringOpponent(thisptr, who))
            return false;
        // Treat match prisoners as allies to outsiders so AI will not hunt them.
        if (ShouldIsolatePair(thisptr, who))
            return true;
        return isAlly_orig(thisptr, who, factorInDisguises);
    }

    bool isFightingAnAllyOfMine_hook(CombatClass* thisptr, Character* who)
    {
        if (thisptr && SparSession::IsSparringOpponent(thisptr->me, who))
            return false;
        return isFightingAnAllyOfMine_orig(thisptr, who);
    }

    HitMaterialType iHitYouAreYouHit_hook(CombatClass* thisptr, CutDirection dir, Damages& damage, Character* who)
    {
        const float incomingDamage = damage.total();
        HitMaterialType result = iHitYouAreYouHit_orig(thisptr, dir, damage, who);

        // _iHitYouAreYouHit runs on the defender's CombatClass; who is the
        // attacker passed onward to _getHit. Keep that direction when recording
        // dealt/taken stats.
        if (thisptr && who && SparSession::IsSparringOpponent(thisptr->me, who))
        {
            // Fallback: if the ally-safe combat path still soft-misses a spar
            // hit that carried real damage, force the get-hit path so limbs/XP
            // update. Do not rewrite a successful dodge into a hit.
            CombatTechniqueData* technique = thisptr->getCurrentTechnique();
            const bool defenderDodging = technique && technique->isDodge;
            if (SparHitLogic::ShouldForceApplySparMiss(
                    result == HIT_MISSED,
                    damage.total(),
                    defenderDodging))
            {
                DebugLog("Proving Grounds: forcing spar hit apply (ally path missed)");
                thisptr->_getHit(dir, damage, who, true);
                result = HIT_FLESH;
            }

            Character* defender = thisptr->me;
            Character* attacker = who;
            if (result == HIT_SWORD)
            {
                SparStats::OnBlock(defender, attacker);
            }
            else
            {
                const bool missed = result == HIT_MISSED;
                const float appliedDamage = missed ? 0.0f : damage.total();
                SparStats::OnHit(
                    attacker,
                    defender,
                    appliedDamage,
                    incomingDamage,
                    missed);
            }
        }
        return result;
    }

    void attackTarget_hook(Character* thisptr, Character* who)
    {
        if (ShouldIsolatePair(thisptr, who))
            return;
        attackTarget_orig(thisptr, who);
    }

    bool iShouldntAggravateThisTarget_hook(Character* thisptr, Character* target)
    {
        if (ShouldIsolatePair(thisptr, target))
            return true;
        return iShouldntAggravateThisTarget_orig(thisptr, target);
    }
}

namespace CombatHooks
{
    bool Install()
    {
        bool ok = true;

        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(&Character::_NV_isEnemy),
                isEnemy_hook,
                &isEnemy_orig))
        {
            ErrorLog("Proving Grounds: failed to hook Character::isEnemy");
            ok = false;
        }
        else
        {
            DebugLog("Proving Grounds: hooked Character::isEnemy");
        }

        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(&Character::_NV_isAlly),
                isAlly_hook,
                &isAlly_orig))
        {
            ErrorLog("Proving Grounds: failed to hook Character::isAlly");
            ok = false;
        }
        else
        {
            DebugLog("Proving Grounds: hooked Character::isAlly");
        }

        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(&CombatClass::isFightingAnAllyOfMine),
                isFightingAnAllyOfMine_hook,
                &isFightingAnAllyOfMine_orig))
        {
            ErrorLog("Proving Grounds: failed to hook CombatClass::isFightingAnAllyOfMine");
            ok = false;
        }
        else
        {
            DebugLog("Proving Grounds: hooked CombatClass::isFightingAnAllyOfMine");
        }

        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(&CombatClass::_iHitYouAreYouHit),
                iHitYouAreYouHit_hook,
                &iHitYouAreYouHit_orig))
        {
            ErrorLog("Proving Grounds: failed to hook CombatClass::_iHitYouAreYouHit");
            ok = false;
        }
        else
        {
            DebugLog("Proving Grounds: hooked CombatClass::_iHitYouAreYouHit");
        }

        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(&Character::attackTarget),
                attackTarget_hook,
                &attackTarget_orig))
        {
            ErrorLog("Proving Grounds: failed to hook Character::attackTarget");
            ok = false;
        }
        else
        {
            DebugLog("Proving Grounds: hooked Character::attackTarget");
        }

        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(&Character::iShouldntAggravateThisTarget),
                iShouldntAggravateThisTarget_hook,
                &iShouldntAggravateThisTarget_orig))
        {
            ErrorLog("Proving Grounds: failed to hook Character::iShouldntAggravateThisTarget");
            ok = false;
        }
        else
        {
            DebugLog("Proving Grounds: hooked Character::iShouldntAggravateThisTarget");
        }

        return ok;
    }
}
