#include "CombatHooks.h"
#include "TownArena.h"
#include "TownArenaRuntimePolicy.h"
#include "FightStarter.h"
#include "PrisonerMatchLogic.h"
#include "PrisonerUtil.h"
#include "SparHitLogic.h"
#include "SparSession.h"
#include "SparStats.h"

#include "PGLog.h"
#include <cstdio>
#include <core/Functions.h>

#pragma warning(push)
#pragma warning(disable: 4091)
#include <kenshi/Character.h>
#include <kenshi/CombatClass.h>
#include <kenshi/CombatTechniqueData.h>
#include <kenshi/Damages.h>
#include <kenshi/Enums.h>
#include <kenshi/FactionRelations.h>
#include <kenshi/BountyManager.h>
#pragma warning(pop)

#ifndef NULL
#define NULL 0
#endif

namespace
{
    bool CollateralPair(Character* a, Character* b)
    {
        return TownArena::IsActiveCollateralIncidentPair(a, b);
    }

    bool SanctionedPair(Character* a, Character* b)
    {
        return TownArena::IsIncidentPair(a, b) || CollateralPair(a, b);
    }

    // Thread-local, nested context: only relation effects synchronously caused
    // by this sanctioned attack are exempt. Other squad activity stays native.
    __declspec(thread) Faction* g_incidentA = NULL;
    __declspec(thread) Faction* g_incidentB = NULL;
    __declspec(thread) bool g_collateralIncident = false;

    struct ArenaIncident
    {
        Faction* oldA;
        Faction* oldB;
        bool oldCollateral;
        ArenaIncident(Character* a, Character* b)
            : oldA(g_incidentA), oldB(g_incidentB), oldCollateral(g_collateralIncident)
        {
            const bool collateral = CollateralPair(a, b);
            const bool sanctioned = TownArena::IsIncidentPair(a, b) || collateral;
            g_incidentA = sanctioned ? a->getFaction() : NULL;
            g_incidentB = sanctioned ? b->getFaction() : NULL;
            g_collateralIncident = collateral;
        }
        ~ArenaIncident()
        {
            g_incidentA = oldA;
            g_incidentB = oldB;
            g_collateralIncident = oldCollateral;
        }
    };
    bool ArenaRelation(FactionRelations* self, Faction* other)
    {
        return self && other && g_incidentA && g_incidentB &&
            ((self->me == g_incidentA && other == g_incidentB) ||
             (self->me == g_incidentB && other == g_incidentA));
    }
    bool ArenaCombatOutcome(FactionRelations::FactionEvent event)
    {
        return event >= FactionRelations::DEFEATED_ONE_OF_US_DEFENSIVELY &&
            event <= FactionRelations::EXECUTED_ONE_OF_US;
    }
    bool (*setCrime_orig)(BountyManager*, CrimeEnum, Faction*, const hand&) = NULL;
    HitMaterialType (*meleeHit_orig)(Character*, CutDirection, Damages&, Character*, CombatTechniqueData*, int) = NULL;
    HitMaterialType meleeHit_hook(Character* self, CutDirection dir, Damages& damage,
        Character* attacker, CombatTechniqueData* technique, int combo)
    {
        // Cover native notifications before/after CombatClass's inner hit call,
        // including the blow that eliminates the defender.
        ArenaIncident incident(self, attacker);
        return meleeHit_orig(self, dir, damage, attacker, technique, combo);
    }
    void (*attackingYou_orig)(Character*, Character*, bool, bool) = NULL;
    void (*campaignAttack_orig)(Character*, Character*) = NULL;
    void (*relationEvent_orig)(FactionRelations*, Faction*, FactionRelations::FactionEvent, float) = NULL;
    void (*setEnemy_orig)(FactionRelations*, Faction*) = NULL;
    bool setCrime_hook(BountyManager* self, CrimeEnum crime, Faction* faction, const hand& victim)
    {
        const bool combatCrime = crime == CRIME_ASSAULT || crime == CRIME_ASSAULT_VIP || crime == CRIME_MURDER;
        Character* attacker = self ? self->me : NULL;
        Character* target = victim.getCharacter();
        const bool sanctioned = combatCrime && attacker && SanctionedPair(attacker, target);
        if (sanctioned)
        {
            if (CollateralPair(attacker, target))
            {
                char detail[320];
                sprintf_s(detail,
                    "Proving Grounds: exempted town arena collateral crime=%d attacker=%s victim=%s",
                    static_cast<int>(crime), attacker->getName().c_str(),
                    target ? target->getName().c_str() : "<invalid>");
                PGLog::Debug(detail);
            }
            return false;
        }
        return setCrime_orig(self, crime, faction, victim);
    }
    void attackingYou_hook(Character* self, Character* attacker, bool so, bool awareness)
    {
        ArenaIncident incident(self, attacker);
        attackingYou_orig(self, attacker, so, awareness);
    }
    void campaignAttack_hook(Character* self, Character* attacker)
    {
        if (!SanctionedPair(self, attacker)) campaignAttack_orig(self, attacker);
    }
    void relationEvent_hook(FactionRelations* self, Faction* other, FactionRelations::FactionEvent event, float mult)
    {
        if (ArenaRelation(self, other))
        {
            if (g_collateralIncident)
                PGLog::Debug("Proving Grounds: exempted town arena collateral faction event");
            return;
        }
        if (self && ArenaCombatOutcome(event) &&
            TownArena::IsIncidentFactionPair(self->me, other))
        {
            char detail[256];
            sprintf_s(detail,
                "Proving Grounds: exempted asynchronous town arena faction event=%d",
                static_cast<int>(event));
            PGLog::Debug(detail);
            return;
        }
        relationEvent_orig(self, other, event, mult);
    }
    void setEnemy_hook(FactionRelations* self, Faction* other)
    {
        if (ArenaRelation(self, other)) return;
        setEnemy_orig(self, other);
    }
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
        if (TownArenaRuntimePolicy::IsolatePair(
                TownArena::IsFighter(a), TownArena::IsFighter(b), false))
            return true;
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
        if (thisptr && (SparSession::IsSparringOpponent(thisptr->me, who) ||
            ShouldIsolatePair(thisptr->me, who)))
            return false;
        return isFightingAnAllyOfMine_orig(thisptr, who);
    }

    HitMaterialType iHitYouAreYouHit_hook(CombatClass* thisptr, CutDirection dir, Damages& damage, Character* who)
    {
        ArenaIncident incident(thisptr ? thisptr->me : NULL, who);
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
                PGLog::Debug("Proving Grounds: forcing spar hit apply (ally path missed)");
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

            // A persistent focused-melee order otherwise pulls the defender
            // back to their initially assigned opponent after Kenshi briefly
            // reacts to an attacker from another direction.
            if (result != HIT_MISSED)
                FightStarter::ReactToIncomingAttack(defender, attacker);
        }
        return result;
    }

    void attackTarget_hook(Character* thisptr, Character* who)
    {
        if (ShouldIsolatePair(thisptr, who))
            return;
        ArenaIncident incident(thisptr, who);
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
            KenshiLib::GetRealAddress(&Character::_NV_hitByMeleeAttack), meleeHit_hook, &meleeHit_orig))
        {
            PGLog::Error("Proving Grounds: failed to hook full arena melee incident");
            ok = false;
        }
        else PGLog::Debug("Proving Grounds: hooked full arena melee incident (KO/aftercare identity)");
        typedef void (FactionRelations::*RelationEventMethod)(Faction*, FactionRelations::FactionEvent, float);
        if (KenshiLib::SUCCESS != KenshiLib::AddHook(KenshiLib::GetRealAddress(&BountyManager::setCrime), setCrime_hook, &setCrime_orig) ||
            KenshiLib::SUCCESS != KenshiLib::AddHook(KenshiLib::GetRealAddress(&Character::attackingYou), attackingYou_hook, &attackingYou_orig) ||
            KenshiLib::SUCCESS != KenshiLib::AddHook(KenshiLib::GetRealAddress(&Character::notifyTheCampaignOfAnAttack), campaignAttack_hook, &campaignAttack_orig) ||
            KenshiLib::SUCCESS != KenshiLib::AddHook(KenshiLib::GetRealAddress(static_cast<RelationEventMethod>(&FactionRelations::_NV_affectRelations)), relationEvent_hook, &relationEvent_orig) ||
            KenshiLib::SUCCESS != KenshiLib::AddHook(KenshiLib::GetRealAddress(&FactionRelations::_NV_setEnemy), setEnemy_hook, &setEnemy_orig))
        {
            PGLog::Error("Proving Grounds: failed to install town arena incident exemptions");
            ok = false;
        }
        else PGLog::Debug("Proving Grounds: installed town arena incident exemptions");

        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(&Character::_NV_isEnemy),
                isEnemy_hook,
                &isEnemy_orig))
        {
            PGLog::Error("Proving Grounds: failed to hook Character::isEnemy");
            ok = false;
        }
        else
        {
            PGLog::Debug("Proving Grounds: hooked Character::isEnemy");
        }

        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(&Character::_NV_isAlly),
                isAlly_hook,
                &isAlly_orig))
        {
            PGLog::Error("Proving Grounds: failed to hook Character::isAlly");
            ok = false;
        }
        else
        {
            PGLog::Debug("Proving Grounds: hooked Character::isAlly");
        }

        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(&CombatClass::isFightingAnAllyOfMine),
                isFightingAnAllyOfMine_hook,
                &isFightingAnAllyOfMine_orig))
        {
            PGLog::Error("Proving Grounds: failed to hook CombatClass::isFightingAnAllyOfMine");
            ok = false;
        }
        else
        {
            PGLog::Debug("Proving Grounds: hooked CombatClass::isFightingAnAllyOfMine");
        }

        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(&CombatClass::_iHitYouAreYouHit),
                iHitYouAreYouHit_hook,
                &iHitYouAreYouHit_orig))
        {
            PGLog::Error("Proving Grounds: failed to hook CombatClass::_iHitYouAreYouHit");
            ok = false;
        }
        else
        {
            PGLog::Debug("Proving Grounds: hooked CombatClass::_iHitYouAreYouHit");
        }

        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(&Character::attackTarget),
                attackTarget_hook,
                &attackTarget_orig))
        {
            PGLog::Error("Proving Grounds: failed to hook Character::attackTarget");
            ok = false;
        }
        else
        {
            PGLog::Debug("Proving Grounds: hooked Character::attackTarget");
        }

        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(&Character::iShouldntAggravateThisTarget),
                iShouldntAggravateThisTarget_hook,
                &iShouldntAggravateThisTarget_orig))
        {
            PGLog::Error("Proving Grounds: failed to hook Character::iShouldntAggravateThisTarget");
            ok = false;
        }
        else
        {
            PGLog::Debug("Proving Grounds: hooked Character::iShouldntAggravateThisTarget");
        }

        return ok;
    }
}
