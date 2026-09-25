#include "ContextMenuHooks.h"
#include "ArenaIdentity.h"
#include "ArenaIngress.h"
#include "RewardsDialoguePolicy.h"
#include "RewardsUI.h"
#include "TownBookie.h"

#include "PGLog.h"
#include <core/Functions.h>

#pragma warning(push)
#pragma warning(disable: 4091)
#include <kenshi/Building/Building.h>
#include <kenshi/Character.h>
#include <kenshi/Enums.h>
#include <kenshi/gui/ContextMenu.h>
#include <kenshi/gui/ForgottenGUI.h>
#include <kenshi/RootObject.h>
#include <kenshi/util/hand.h>
#include <ogre/OgreVector3.h>
#pragma warning(pop)

#ifndef NULL
#define NULL 0
#endif

namespace
{
    void (*showContextMenu_orig)(ContextMenu* thisptr, bool on, RootObject* what) = NULL;
    CursorType (*getMouseCursor_orig)(Building* thisptr) = NULL;
    TaskType (*getDefaultTask_orig)(Building* thisptr) = NULL;
    void (*changeMouseCursorTarget_orig)(
        ForgottenGUI* thisptr, CursorType cursor, const hand& player, const hand& target) = NULL;
    void (*changeMouseCursor_orig)(ForgottenGUI* thisptr, CursorType cursor) = NULL;
    void (*showTradeWindow_orig)(
        ForgottenGUI* thisptr,
        const hand& first,
        const hand& second,
        TradeWindowType type) = NULL;
    void (*addOrder_orig)(
        Character* thisptr,
        Building* dest,
        TaskType t,
        RootObject* subject,
        bool shift,
        bool clear,
        const Ogre::Vector3& location) = NULL;

    enum PendingTradeUi { NoTradeUi, RewardsTradeUi, BookieTradeUi };
    PendingTradeUi g_pendingTradeUi = NoTradeUi;
    hand g_pendingTradeCharacter;
    hand g_pendingBookie;
    hand g_suppressedInteractCursorTarget;
    bool g_resetInteractCursor = false;

    const char* StringId(const hand& value)
    {
        if (!value.isValid())
            return NULL;
        return ArenaIdentity::GetStringId(value.getRootObject());
    }

    bool IsMatchUiOpenerHand(const hand& h)
    {
        if (!h.isValid())
            return false;
        Building* b = h.getBuilding();
        if (b && ArenaIdentity::IsMatchUiOpener(b))
            return true;
        RootObject* obj = h.getRootObject();
        return obj && ArenaIdentity::IsMatchUiOpener(obj);
    }

    bool IsLeaderboardHand(const hand& h)
    {
        if (!h.isValid())
            return false;
        Building* b = h.getBuilding();
        if (b && ArenaIdentity::IsLeaderboard(b))
            return true;
        RootObject* obj = h.getRootObject();
        return obj && ArenaIdentity::IsLeaderboard(obj);
    }

    bool IsInteractUiHand(const hand& h)
    {
        return IsMatchUiOpenerHand(h) || IsLeaderboardHand(h);
    }

    bool SameRootObject(const hand& a, const hand& b)
    {
        if (!a.isValid() || !b.isValid())
            return false;
        return a.getRootObject() == b.getRootObject();
    }

    bool IsSuppressedInteractCursorTarget(const hand& h)
    {
        return g_suppressedInteractCursorTarget.isValid()
            && SameRootObject(g_suppressedInteractCursorTarget, h);
    }

    bool IsInteractUiBuilding(RootObject* obj)
    {
        return obj && (ArenaIdentity::IsMatchUiOpener(obj) || ArenaIdentity::IsLeaderboard(obj));
    }

    bool IsInteractUiFinished(Building* building)
    {
        return ArenaIdentity::IsMatchUiOpenerFinished(building)
            || ArenaIdentity::IsLeaderboardFinished(building);
    }

    bool IsRegistryUseTask(TaskType t)
    {
        return t == USE_TRAINING_DUMMY
            || t == OPERATE_MACHINERY
            || t == OPERATE_AUTOMATIC_MACHINERY
            || t == PRETEND_TO_OPERATE_MACHINERY;
    }

    void showContextMenu_hook(ContextMenu* thisptr, bool on, RootObject* what)
    {
        if (on && what)
            ArenaIdentity::LogInteractIdentity(what, "RMB");

        if (on && IsInteractUiBuilding(what))
        {
            Building* building = static_cast<Building*>(what);
            if (IsInteractUiFinished(building))
            {
                PGLog::Debug("Proving Grounds: interact RMB → approach for UI (no context menu)");
                ArenaIngress::BeginApproachForUI(building);
                g_suppressedInteractCursorTarget = building;
                g_resetInteractCursor = true;
                showContextMenu_orig(thisptr, false, what);
                return;
            }
            PGLog::Debug("Proving Grounds: interact RMB ignored (not finished)");
        }

        if (on)
            ArenaIngress::NotifyExternalOrder(NULL);
        showContextMenu_orig(thisptr, on, what);
    }

    CursorType getMouseCursor_hook(Building* thisptr)
    {
        if (IsInteractUiBuilding(thisptr))
            return USE_CURSOR;
        return getMouseCursor_orig(thisptr);
    }

    TaskType getDefaultTask_hook(Building* thisptr)
    {
        if (IsInteractUiBuilding(thisptr))
            return USE_TRAINING_DUMMY;
        return getDefaultTask_orig(thisptr);
    }

    void changeMouseCursorTarget_hook(
        ForgottenGUI* thisptr, CursorType cursor, const hand& player, const hand& target)
    {
        if (IsInteractUiHand(target))
            cursor = USE_CURSOR;
        changeMouseCursorTarget_orig(thisptr, cursor, player, target);
    }

    void changeMouseCursor_hook(ForgottenGUI* thisptr, CursorType cursor)
    {
        changeMouseCursor_orig(thisptr, cursor);
    }

    void showTradeWindow_hook(
        ForgottenGUI* thisptr,
        const hand& first,
        const hand& second,
        TradeWindowType type)
    {
        // Money trading with Scratch follows Null's deferred dialogue opening.
        // Keep ordinary trade/loot and either participant ordering intact.
        const bool firstIsBookie = first.isValid() && TownBookie::IsBookie(first.getCharacter());
        const bool secondIsBookie = second.isValid() && TownBookie::IsBookie(second.getCharacter());
        if (type == TW_MONEY_TRADING && firstIsBookie != secondIsBookie)
        {
            const hand& participant = firstIsBookie ? second : first;
            Character* player = participant.getCharacter();
            if (player && player->isValid() && player->isPlayerCharacter())
            {
                g_pendingTradeCharacter = participant;
                g_pendingBookie = firstIsBookie ? first : second;
                g_pendingTradeUi = BookieTradeUi;
                PGLog::Debug("Proving Grounds: bookie trade intercepted; queued until dialogue closes");
                return;
            }
            showTradeWindow_orig(thisptr, first, second, type);
            return;
        }
        const char* firstId = StringId(first);
        const char* secondId = StringId(second);
        const RewardsDialoguePolicy::TradeRoute route =
            RewardsDialoguePolicy::ResolveTrade(
                firstId, secondId,
                type == TW_MONEY_TRADING);
        if (route == RewardsDialoguePolicy::PassThrough)
        {
            showTradeWindow_orig(thisptr, first, second, type);
            return;
        }

        const hand& participant =
            route == RewardsDialoguePolicy::OpenForFirst ? first : second;
        Character* preferred = participant.getCharacter();
        if (!preferred || !preferred->isValid() || !preferred->isPlayerCharacter())
        {
            PGLog::Debug("Proving Grounds: rewards dialogue trade had no player participant; passing through");
            showTradeWindow_orig(thisptr, first, second, type);
            return;
        }

        g_pendingTradeCharacter = participant;
        g_pendingBookie.setNull();
        g_pendingTradeUi = RewardsTradeUi;
        PGLog::Debug("Proving Grounds: rewards dialogue trade intercepted; queued until GUI focus is clear");
    }

    void addOrder_hook(
        Character* thisptr,
        Building* dest,
        TaskType t,
        RootObject* subject,
        bool shift,
        bool clear,
        const Ogre::Vector3& location)
    {
        Building* building = NULL;
        if (dest && IsInteractUiBuilding(dest))
            building = dest;
        else if (subject && IsInteractUiBuilding(subject))
            building = static_cast<Building*>(subject);

        if (building)
        {
            // Same Registry pattern: swallow use/operate, walk-then-open when finished.
            if (IsInteractUiFinished(building) && IsRegistryUseTask(t))
            {
                PGLog::Debug("Proving Grounds: interact use → approach for UI");
                ArenaIngress::BeginApproachForUI(building);
                return;
            }

            if (IsRegistryUseTask(t))
            {
                PGLog::Debug("Proving Grounds: interact use ignored (not finished)");
                return;
            }

            ArenaIngress::NotifyExternalOrder(thisptr);
            addOrder_orig(thisptr, dest, t, subject, shift, clear, location);
            return;
        }

        ArenaIngress::NotifyExternalOrder(thisptr);
        addOrder_orig(thisptr, dest, t, subject, shift, clear, location);
    }
}

namespace ContextMenuHooks
{
    void AbandonWorldState()
    {
        g_pendingTradeUi = NoTradeUi;
        g_pendingTradeCharacter.setNull();
        g_pendingBookie.setNull();
        g_suppressedInteractCursorTarget.setNull();
        g_resetInteractCursor = false;
    }

    void Tick(ForgottenGUI* currentGui)
    {
        if (!currentGui)
            return;

        if (g_resetInteractCursor &&
            IsSuppressedInteractCursorTarget(currentGui->selectedObject) &&
            currentGui->currentCursor == USE_CURSOR)
        {
            currentGui->changeMouseCursor(DEFAULT_CURSOR);
        }
        g_resetInteractCursor = false;

        if (g_pendingTradeUi == NoTradeUi)
            return;

        Character* preferred = g_pendingTradeCharacter.getCharacter();
        Character* bookie = g_pendingBookie.getCharacter();
        const bool openBookie = g_pendingTradeUi == BookieTradeUi;
        const RewardsDialoguePolicy::PendingDecision decision =
            RewardsDialoguePolicy::ResolvePending(
                currentGui->inDialogue(),
                preferred && preferred->isValid() && preferred->isPlayerCharacter() &&
                (!openBookie || TownBookie::IsBookie(bookie)));
        if (decision == RewardsDialoguePolicy::WaitForDialogue)
            return;

        g_pendingTradeUi = NoTradeUi;
        g_pendingTradeCharacter.setNull();
        g_pendingBookie.setNull();
        if (decision == RewardsDialoguePolicy::DiscardPending)
        {
            PGLog::Debug("Proving Grounds: discarded stale trade UI request");
            return;
        }

        if (openBookie)
        {
            TownBookie::Show(bookie);
            PGLog::Debug("Proving Grounds: bookie UI requested from dialogue");
        }
        else
        {
            RewardsUI::Show(preferred);
            PGLog::Debug("Proving Grounds: rewards UI opened from dialogue");
        }
    }

    void RefreshArenaCursor(ForgottenGUI* gui)
    {
        if (!gui)
            return;

        if (IsSuppressedInteractCursorTarget(gui->selectedObject))
            return;

        if (g_suppressedInteractCursorTarget.isValid())
            g_suppressedInteractCursorTarget.setNull();

        if (!IsInteractUiHand(gui->selectedObject))
            return;
        if (!gui->selectedPlayerCharacter.isValid())
            return;
        if (gui->currentCursor == USE_CURSOR)
            return;

        gui->changeMouseCursor(USE_CURSOR, gui->selectedPlayerCharacter, gui->selectedObject);
    }

    bool Install()
    {
        bool ok = true;

        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(&ContextMenu::showContextMenu),
                showContextMenu_hook,
                &showContextMenu_orig))
        {
            PGLog::Error("Proving Grounds: failed to hook ContextMenu::showContextMenu");
            ok = false;
        }

        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(&Building::_NV_getMouseCursor),
                getMouseCursor_hook,
                &getMouseCursor_orig))
        {
            PGLog::Error("Proving Grounds: failed to hook Building::getMouseCursor");
            ok = false;
        }

        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(&Building::_NV_getDefaultTask),
                getDefaultTask_hook,
                &getDefaultTask_orig))
        {
            PGLog::Error("Proving Grounds: failed to hook Building::getDefaultTask");
            ok = false;
        }

        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(
                    static_cast<void (ForgottenGUI::*)(CursorType, const hand&, const hand&)>(
                        &ForgottenGUI::changeMouseCursor)),
                changeMouseCursorTarget_hook,
                &changeMouseCursorTarget_orig))
        {
            PGLog::Error("Proving Grounds: failed to hook ForgottenGUI::changeMouseCursor(target)");
            ok = false;
        }

        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(
                    static_cast<void (ForgottenGUI::*)(CursorType)>(&ForgottenGUI::changeMouseCursor)),
                changeMouseCursor_hook,
                &changeMouseCursor_orig))
        {
            PGLog::Error("Proving Grounds: failed to hook ForgottenGUI::changeMouseCursor(simple)");
        }

        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(&ForgottenGUI::showTradeWindow),
                showTradeWindow_hook,
                &showTradeWindow_orig))
        {
            PGLog::Error("Proving Grounds: failed to hook ForgottenGUI::showTradeWindow");
            ok = false;
        }

        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(&Character::addOrder),
                addOrder_hook,
                &addOrder_orig))
        {
            PGLog::Error("Proving Grounds: failed to hook Character::addOrder");
            ok = false;
        }

        PGLog::Debug("Proving Grounds: registry/banner/leaderboard interact hooks install done");
        return ok;
    }
}
