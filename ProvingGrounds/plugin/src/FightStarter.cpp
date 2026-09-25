#include "FightStarter.h"
#include "TownArena.h"
#include "FightDisengagePolicy.h"
#include "MatchRules.h"
#include "SparSession.h"

#include "PGLog.h"
#include <vector>

class OrderData;

#pragma warning(push)
#pragma warning(disable: 4091)
#include <kenshi/Character.h>
#include <kenshi/CharMovement.h>
#include <kenshi/Enums.h>
#include <kenshi/GameWorld.h>
#include <kenshi/Globals.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/gui/ForgottenGUI.h>
#include <kenshi/gui/MainBarGUI.h>
#include <kenshi/gui/OrdersPanel.h>
#include <ogre/OgreVector3.h>
#pragma warning(pop)
#include <mygui/MyGUI_Button.h>

#ifndef NULL
#define NULL 0
#endif

namespace
{
    struct FighterOrderState
    {
        Character* fighter;
        bool passive;
        bool hold;
        bool defensive;
        bool aggressive;
        bool jobsToggledOff;
        MoveSpeed moveSpeed;
    };

    struct FocusAssignment
    {
        Character* fighter;
        Character* target;
    };

    std::vector<FighterOrderState> g_originalStates;
    std::vector<FocusAssignment> g_focusAssignments;

    // Native InputHandler command value registered for "toggle_jobs".
    const unsigned int kToggleJobsCommand = 26;

    bool IsPlayerCharacter(Character* c)
    {
        if (!c || !ou || !ou->player)
            return false;
        const lektor<Character*>& playerCharacters =
            ou->player->getAllPlayerCharacters();
        for (uint32_t i = 0; i < playerCharacters.size(); ++i)
        {
            if (playerCharacters[i] == c)
                return true;
        }
        return false;
    }

    bool SetJobsEnabled(Character* c, bool enabled)
    {
        if (!c || !c->isValid() || !IsPlayerCharacter(c) ||
            !ou || !ou->player || !gui || !gui->mainbar)
        {
            return false;
        }

        PlayerInterface* player = ou->player;
        lektor<RootObject*> oldSelection;
        player->getAllSelectedObjects(oldSelection, NULL_ITEM);

        player->unselectAll();
        player->_selectPlayerCharacter(c, false, false);

        OrdersPanel* orders = gui->mainbar->ordersDataPanel;
        if (orders)
            orders->update(c);
        MyGUI::Button* jobsButton = orders ? orders->chaseCheckBox : NULL;
        bool changed = false;
        if (jobsButton && jobsButton->getStateSelected() != enabled)
        {
            gui->mainbar->pressedKey(kToggleJobsCommand);
            changed = true;
        }

        player->unselectAll();
        for (uint32_t i = 0; i < oldSelection.size(); ++i)
        {
            RootObject* selected = oldSelection[i];
            if (selected && selected->isValid())
                player->selectObject(selected, i > 0);
        }
        return changed;
    }

    int FindOriginalState(Character* c)
    {
        for (size_t i = 0; i < g_originalStates.size(); ++i)
        {
            if (g_originalStates[i].fighter == c)
                return static_cast<int>(i);
        }
        return -1;
    }

    void RememberOriginalState(Character* c)
    {
        if (!c || !c->isValid() || FindOriginalState(c) >= 0)
            return;

        FighterOrderState state = {};
        state.fighter = c;
        state.passive = c->getStandingOrder(MessageForB::M_SET_ORDER_PASSIVE);
        state.hold = c->getStandingOrder(MessageForB::M_SET_ORDER_HOLD);
        state.defensive = c->getStandingOrder(MessageForB::M_SET_ORDER_DEF);
        state.aggressive = c->getStandingOrder(MessageForB::M_SET_ORDER_AGG);
        state.moveSpeed = c->getMovementSpeedOrders();
        state.jobsToggledOff = SetJobsEnabled(c, false);
        g_originalStates.push_back(state);
    }

    bool IsLivingFighter(Character* c)
    {
        return c && c->isValid() && !c->isDead() && !c->isUnconcious();
    }

    Character* FindAssignedTarget(Character* fighter)
    {
        for (size_t i = 0; i < g_focusAssignments.size(); ++i)
        {
            if (g_focusAssignments[i].fighter == fighter)
                return g_focusAssignments[i].target;
        }
        return NULL;
    }

    void SetAssignedTarget(Character* fighter, Character* target)
    {
        if (!fighter)
            return;
        for (size_t i = 0; i < g_focusAssignments.size(); ++i)
        {
            if (g_focusAssignments[i].fighter != fighter)
                continue;
            g_focusAssignments[i].target = target;
            return;
        }

        FocusAssignment assignment = {};
        assignment.fighter = fighter;
        assignment.target = target;
        g_focusAssignments.push_back(assignment);
    }

    void ClearAssignedTarget(Character* fighter)
    {
        for (size_t i = 0; i < g_focusAssignments.size(); ++i)
        {
            if (g_focusAssignments[i].fighter != fighter)
                continue;
            g_focusAssignments.erase(g_focusAssignments.begin() + i);
            return;
        }
    }

    MatchRules::MatchParticipant MakeParticipant(int index, MatchRules::MatchTeam team, Character* c)
    {
        MatchRules::MatchParticipant p = {};
        p.id = index;
        p.team = team;
        p.conscious = IsLivingFighter(c);
        p.dead = c && c->isDead();
        return p;
    }

    bool IsLivingOpponent(
        MatchRules::MatchMode mode,
        int attackerIdx,
        int targetIdx,
        Character** fighters,
        MatchRules::MatchTeam* teams)
    {
        if (attackerIdx == targetIdx)
            return false;

        Character* target = fighters[targetIdx];
        if (!IsLivingFighter(target))
            return false;

        const MatchRules::MatchParticipant attacker = MakeParticipant(attackerIdx, teams[attackerIdx], fighters[attackerIdx]);
        const MatchRules::MatchParticipant opponent = MakeParticipant(targetIdx, teams[targetIdx], target);
        return MatchRules::IsOpponent(mode, attacker, opponent);
    }

    float HorizontalDistSq(const Ogre::Vector3& a, const Ogre::Vector3& b)
    {
        const float dx = a.x - b.x;
        const float dz = a.z - b.z;
        return (dx * dx) + (dz * dz);
    }

    int FindNearestLivingOpponent(
        MatchRules::MatchMode mode,
        int attackerIdx,
        Character** fighters,
        MatchRules::MatchTeam* teams,
        int count)
    {
        Character* attacker = fighters[attackerIdx];
        if (!attacker)
            return -1;

        const Ogre::Vector3 attackerPos = attacker->getPosition();
        int nearestIdx = -1;
        float nearestDistSq = 1.0e30f;
        for (int targetIdx = 0; targetIdx < count; ++targetIdx)
        {
            if (!IsLivingOpponent(mode, attackerIdx, targetIdx, fighters, teams))
                continue;

            const float distSq =
                HorizontalDistSq(attackerPos, fighters[targetIdx]->getPosition());
            if (distSq < nearestDistSq)
            {
                nearestDistSq = distSq;
                nearestIdx = targetIdx;
            }
        }
        return nearestIdx;
    }

    int FindMatchedLivingTeamOpponent(
        MatchRules::MatchMode mode,
        int attackerIdx,
        Character** fighters,
        MatchRules::MatchTeam* teams,
        int count)
    {
        const MatchRules::MatchTeam attackerTeam = teams[attackerIdx];
        int attackerSlot = 0;
        for (int i = 0; i < attackerIdx; ++i)
        {
            if (teams[i] == attackerTeam)
                ++attackerSlot;
        }

        int livingOpponentCount = 0;
        for (int targetIdx = 0; targetIdx < count; ++targetIdx)
        {
            if (IsLivingOpponent(mode, attackerIdx, targetIdx, fighters, teams))
                ++livingOpponentCount;
        }
        if (livingOpponentCount <= 0)
            return -1;

        const int desiredSlot = attackerSlot % livingOpponentCount;
        int livingSlot = 0;
        for (int targetIdx = 0; targetIdx < count; ++targetIdx)
        {
            if (!IsLivingOpponent(mode, attackerIdx, targetIdx, fighters, teams))
                continue;
            if (livingSlot == desiredSlot)
                return targetIdx;
            ++livingSlot;
        }
        return -1;
    }

    void SetCombatStance(Character* c)
    {
        if (!c)
            return;
        RememberOriginalState(c);
        // Drop passive/hold so they will actually fight.
        c->setStandingOrder(MessageForB::M_SET_ORDER_PASSIVE, false);
        c->setStandingOrder(MessageForB::M_SET_ORDER_HOLD, false);
        c->setStandingOrder(MessageForB::M_SET_ORDER_DEF, false);
        c->setStandingOrder(MessageForB::M_SET_ORDER_AGG, true);
    }

    void MarkTempEnemy(Character* self, Character* other)
    {
        if (!self || !other)
            return;
        // Game-native temporary hostility (same faction, no dismiss).
        self->rememberCharacter(other, ST_TEMPORARY_ENEMY);
    }

    void IssueAttackOrder(Character* attacker, Character* target, bool clearExistingOrders)
    {
        if (!attacker || !target)
            return;

        const bool playerControlled = IsPlayerCharacter(attacker);
        Ogre::Vector3 loc = target->getPosition();
        if (playerControlled)
        {
            // Player characters need an actual player order; attackTarget alone
            // is not enough to keep them moving toward the opponent.
            attacker->addOrder(
                NULL,
                FOCUSED_MELEE_ATTACK,
                target,
                false,              // shift
                clearExistingOrders,
                loc);
        }
        else
        {
            // Released prisoners remain NPC-controlled. addOrder takes the
            // player-command path, while addJob feeds the persistent jobs list.
            // Neither reliably replaces an NPC's active package goal (for
            // example Patrolling). Replace the active goal stack directly.
            attacker->clearAllAIGoals();
            attacker->addGoal(
                UNPROVOKED_FOCUSED_MELEE_ATTACK,
                static_cast<RootObjectBase*>(target));
        }

        // Also poke combat targeting + awareness both ways.
        attacker->attackTarget(target);
        target->attackingYou(attacker, true, false);
        if (!playerControlled)
        {
            // Arena staging deliberately parks released prisoners and asks
            // their NPC AI to rethink. Force another evaluation now that its
            // old package goal is gone and the native attack goal exists.
            attacker->reThinkCurrentAIAction();
        }
        if (clearExistingOrders)
            SetAssignedTarget(attacker, target);
    }

    void ClearPlayerAttackOrders(Character* c)
    {
        if (!c || (!IsPlayerCharacter(c) && !TownArena::IsFighter(c)))
            return;

        // Player spars and town staging use addOrder. removeJob alone can
        // leave the order queue armed so they stay in a fighting pose after
        // temp-enemy (and therefore damage) is already gone.
        const Ogre::Vector3 loc = c->getPosition();
        c->addOrder(NULL, MOVE_CUS_ORDERED, NULL, false, true, loc);
        c->removeJob(MOVE_CUS_ORDERED);
    }

    void DisengageFighter(Character* c)
    {
        const int stateIndex = FindOriginalState(c);
        const FightDisengagePolicy::Plan plan =
            FightDisengagePolicy::BuildFullDisengagePlan();

        if (c && c->isValid())
        {
            if (plan.clearTempEnemies)
                c->clearAllTempEnemyStatuses(ST_TEMPORARY_ENEMY);

            if (!c->isDead())
            {
                if (plan.removeFocusedMeleeJobs)
                {
                    c->removeJob(FOCUSED_MELEE_ATTACK);
                    c->removeJob(UNPROVOKED_FOCUSED_MELEE_ATTACK);
                }

                if (plan.clearPlayerAttackOrders)
                    ClearPlayerAttackOrders(c);

                if (plan.endCombatMode)
                    c->endCombatMode();

                if (plan.clearAiGoals)
                    c->clearAllAIGoals();

                if (plan.rethinkAi)
                    c->reThinkCurrentAIAction();

                if (plan.restoreStandingOrders)
                {
                    if (stateIndex >= 0)
                    {
                        const FighterOrderState state =
                            g_originalStates[static_cast<size_t>(stateIndex)];
                        c->setStandingOrder(MessageForB::M_SET_ORDER_PASSIVE, state.passive);
                        c->setStandingOrder(MessageForB::M_SET_ORDER_HOLD, state.hold);
                        c->setStandingOrder(MessageForB::M_SET_ORDER_DEF, state.defensive);
                        c->setStandingOrder(MessageForB::M_SET_ORDER_AGG, state.aggressive);
                        CharMovement* movement = c->getMovement();
                        if (movement)
                        {
                            movement->setDesiredSpeedOrders(state.moveSpeed);
                            movement->restoreDesiredSpeed();
                        }
                    }
                    else
                    {
                        c->setStandingOrder(MessageForB::M_SET_ORDER_AGG, false);
                    }
                }

                if (stateIndex >= 0 &&
                    g_originalStates[static_cast<size_t>(stateIndex)].jobsToggledOff)
                {
                    SetJobsEnabled(c, true);
                }
            }
            else if (plan.endCombatMode)
            {
                c->endCombatMode();
            }
        }
        if (stateIndex >= 0)
        {
            g_originalStates.erase(
                g_originalStates.begin() + static_cast<size_t>(stateIndex));
        }
        ClearAssignedTarget(c);
    }

    void ClearFighterForRetarget(Character* c)
    {
        const FightDisengagePolicy::Plan plan =
            FightDisengagePolicy::BuildInMatchRetargetPlan();

        if (!c || !c->isValid())
            return;

        if (plan.clearTempEnemies)
            c->clearAllTempEnemyStatuses(ST_TEMPORARY_ENEMY);

        if (c->isDead())
            return;

        if (plan.removeFocusedMeleeJobs)
        {
            c->removeJob(FOCUSED_MELEE_ATTACK);
            c->removeJob(UNPROVOKED_FOCUSED_MELEE_ATTACK);
        }

        if (plan.clearPlayerAttackOrders)
            ClearPlayerAttackOrders(c);
        ClearAssignedTarget(c);
    }
}

namespace FightStarter
{
    void AbandonWorldState()
    {
        g_originalStates.clear();
        g_focusAssignments.clear();
    }

    void RememberMatchFighters(Character** fighters, int count)
    {
        if (!fighters || count <= 0)
            return;
        for (int i = 0; i < count; ++i)
            RememberOriginalState(fighters[i]);
    }

    void EngageMatch(Character** fighters, MatchRules::MatchTeam* teams, int count, MatchRules::MatchMode mode)
    {
        if (!fighters || !teams || count < 2)
            return;

        PGLog::Debug("Proving Grounds: FightStarter EngageMatch (temp-enemy + focused melee orders)");

        for (int i = 0; i < count; ++i)
        {
            Character* fighter = fighters[i];
            if (!IsLivingFighter(fighter))
                continue;

            SetCombatStance(fighter);

            if (mode == MatchRules::ModeLastStanding)
            {
                // Everyone is hostile, but only order each fighter toward their
                // nearest opponent. Long index-based paths crossed in the middle
                // and collapsed the whole field into one central melee.
                for (int j = 0; j < count; ++j)
                {
                    if (IsLivingOpponent(mode, i, j, fighters, teams))
                        MarkTempEnemy(fighter, fighters[j]);
                }

                const int targetIdx =
                    FindNearestLivingOpponent(mode, i, fighters, teams, count);
                if (targetIdx >= 0)
                    IssueAttackOrder(fighter, fighters[targetIdx], true);
                continue;
            }

            if (mode == MatchRules::ModeTeamAvB ||
                mode == MatchRules::ModeTeams1v1)
            {
                // Establish hostility with the full opposing team, then assign
                // one distributed opponent per fighter. Large matches used to
                // send everyone at the first enemy and leave a queue behind the
                // frontline.
                for (int j = 0; j < count; ++j)
                {
                    if (IsLivingOpponent(mode, i, j, fighters, teams))
                        MarkTempEnemy(fighter, fighters[j]);
                }

                const int targetIdx =
                    FindMatchedLivingTeamOpponent(mode, i, fighters, teams, count);
                if (targetIdx >= 0)
                    IssueAttackOrder(fighter, fighters[targetIdx], true);
                continue;
            }

            bool firstOrder = true;
            for (int j = 0; j < count; ++j)
            {
                if (!IsLivingOpponent(mode, i, j, fighters, teams))
                    continue;

                MarkTempEnemy(fighter, fighters[j]);
                IssueAttackOrder(fighter, fighters[j], firstOrder);
                firstOrder = false;

            }
        }
    }

    void DisengageMatch(Character** fighters, int count)
    {
        if (!fighters || count <= 0)
            return;

        PGLog::Debug("Proving Grounds: FightStarter DisengageMatch");

        for (int i = 0; i < count; ++i)
            DisengageFighter(fighters[i]);
    }

    void RetargetMatchAfterElimination(
        Character** allFighters,
        int allCount,
        Character** remainingFighters,
        MatchRules::MatchTeam* remainingTeams,
        int remainingCount,
        MatchRules::MatchMode mode)
    {
        if (!allFighters || allCount <= 0)
            return;

        PGLog::Debug("Proving Grounds: FightStarter retarget after elimination");

        // Deliberately preserve g_originalStates and Jobs. Full disengage uses
        // the global toggle_jobs UI command, which clicks audibly and briefly
        // interrupts the selected squad before EngageMatch toggles it back.
        for (int i = 0; i < allCount; ++i)
            ClearFighterForRetarget(allFighters[i]);

        if (remainingFighters && remainingTeams && remainingCount >= 2)
            EngageMatch(remainingFighters, remainingTeams, remainingCount, mode);
    }

    void ReactToIncomingAttack(Character* defender, Character* attacker)
    {
        if (!IsLivingFighter(defender) || !IsLivingFighter(attacker) ||
            !SparSession::IsActive() ||
            SparSession::GetMode() == MatchRules::ModeTeams1v1 ||
            SparSession::IsEliminated(defender) ||
            SparSession::IsEliminated(attacker) ||
            !SparSession::IsSparringOpponent(defender, attacker) ||
            FindAssignedTarget(defender) == attacker)
        {
            return;
        }

        MarkTempEnemy(defender, attacker);
        IssueAttackOrder(defender, attacker, true);
        PGLog::Debug("Proving Grounds: fighter retargeted to incoming attacker");
    }

    void KeepEngagedMatch(Character** fighters, MatchRules::MatchTeam* teams, int count, MatchRules::MatchMode mode)
    {
        if (!fighters || !teams || count < 2)
            return;

        for (int i = 0; i < count; ++i)
        {
            Character* fighter = fighters[i];
            if (!IsLivingFighter(fighter))
                continue;

            Character* currentTarget = fighter->getAttackTarget().getCharacter();
            bool validTarget = false;

            if (currentTarget)
            {
                for (int j = 0; j < count; ++j)
                {
                    if (fighters[j] != currentTarget)
                        continue;
                    validTarget = IsLivingOpponent(mode, i, j, fighters, teams);
                    break;
                }
            }

            // A non-player fighter can retain an attack target even after its
            // AI combat task stalls. Treat that as healthy only while it is
            // actually in melee combat, so the keep-alive can repair the job.
            if (validTarget &&
                (IsPlayerCharacter(fighter) || fighter->isInCombatMode(true, false)))
                continue;

            if (mode == MatchRules::ModeLastStanding)
            {
                const int targetIdx =
                    FindNearestLivingOpponent(mode, i, fighters, teams, count);
                if (targetIdx >= 0)
                {
                    MarkTempEnemy(fighter, fighters[targetIdx]);
                    IssueAttackOrder(fighter, fighters[targetIdx], true);
                }
                continue;
            }

            if (mode == MatchRules::ModeTeamAvB ||
                mode == MatchRules::ModeTeams1v1)
            {
                const int targetIdx =
                    FindMatchedLivingTeamOpponent(mode, i, fighters, teams, count);
                if (targetIdx >= 0)
                {
                    MarkTempEnemy(fighter, fighters[targetIdx]);
                    IssueAttackOrder(fighter, fighters[targetIdx], true);
                }
                continue;
            }

            for (int j = 0; j < count; ++j)
            {
                if (!IsLivingOpponent(mode, i, j, fighters, teams))
                    continue;

                MarkTempEnemy(fighter, fighters[j]);
                IssueAttackOrder(fighter, fighters[j], true);
                break;
            }
        }
    }

    void Engage(Character* a, Character* b)
    {
        if (!a || !b)
            return;

        Character* fighters[] = { a, b };
        MatchRules::MatchTeam teams[] = { MatchRules::TeamA, MatchRules::TeamB };
        EngageMatch(fighters, teams, 2, MatchRules::ModeTeamAvB);

        // Diagnostic: isEnemy should be true via memory and/or our hook.
        const bool aSeesB = a->isEnemy(b, false);
        const bool bSeesA = b->isEnemy(a, false);
        PGLog::Debug(aSeesB && bSeesA
            ? "Proving Grounds: isEnemy OK both ways"
            : "Proving Grounds: WARN isEnemy still false after setup");
    }

    void Disengage(Character* a, Character* b)
    {
        Character* fighters[] = { a, b };
        DisengageMatch(fighters, 2);
    }

    void KeepEngaged(Character* a, Character* b)
    {
        if (!a || !b || !a->isValid() || !b->isValid())
            return;
        if (a->isDead() || b->isDead() || a->isUnconcious() || b->isUnconcious())
            return;

        Character* fighters[] = { a, b };
        MatchRules::MatchTeam teams[] = { MatchRules::TeamA, MatchRules::TeamB };
        KeepEngagedMatch(fighters, teams, 2, MatchRules::ModeTeamAvB);
    }
}
