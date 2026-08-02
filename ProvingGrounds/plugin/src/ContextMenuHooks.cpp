#include "ContextMenuHooks.h"
#include "ArenaIdentity.h"
#include "ArenaIngress.h"

#include <Debug.h>
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
    void (*addOrder_orig)(
        Character* thisptr,
        Building* dest,
        TaskType t,
        RootObject* subject,
        bool shift,
        bool clear,
        const Ogre::Vector3& location) = NULL;

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
                DebugLog("Proving Grounds: interact RMB → approach for UI (no context menu)");
                ArenaIngress::BeginApproachForUI(building);
                showContextMenu_orig(thisptr, false, what);
                return;
            }
            DebugLog("Proving Grounds: interact RMB ignored (not finished)");
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
                DebugLog("Proving Grounds: interact use → approach for UI");
                ArenaIngress::BeginApproachForUI(building);
                return;
            }

            if (IsRegistryUseTask(t))
            {
                DebugLog("Proving Grounds: interact use ignored (not finished)");
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
    void RefreshArenaCursor(ForgottenGUI* gui)
    {
        if (!gui)
            return;

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
            ErrorLog("Proving Grounds: failed to hook ContextMenu::showContextMenu");
            ok = false;
        }

        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(&Building::_NV_getMouseCursor),
                getMouseCursor_hook,
                &getMouseCursor_orig))
        {
            ErrorLog("Proving Grounds: failed to hook Building::getMouseCursor");
            ok = false;
        }

        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(&Building::_NV_getDefaultTask),
                getDefaultTask_hook,
                &getDefaultTask_orig))
        {
            ErrorLog("Proving Grounds: failed to hook Building::getDefaultTask");
            ok = false;
        }

        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(
                    static_cast<void (ForgottenGUI::*)(CursorType, const hand&, const hand&)>(
                        &ForgottenGUI::changeMouseCursor)),
                changeMouseCursorTarget_hook,
                &changeMouseCursorTarget_orig))
        {
            ErrorLog("Proving Grounds: failed to hook ForgottenGUI::changeMouseCursor(target)");
            ok = false;
        }

        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(
                    static_cast<void (ForgottenGUI::*)(CursorType)>(&ForgottenGUI::changeMouseCursor)),
                changeMouseCursor_hook,
                &changeMouseCursor_orig))
        {
            ErrorLog("Proving Grounds: failed to hook ForgottenGUI::changeMouseCursor(simple)");
        }

        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(&Character::addOrder),
                addOrder_hook,
                &addOrder_orig))
        {
            ErrorLog("Proving Grounds: failed to hook Character::addOrder");
            ok = false;
        }

        DebugLog("Proving Grounds: registry/banner/leaderboard interact hooks install done");
        return ok;
    }
}
