#include "WorldLifecycle.h"
#include "TownArena.h"
#include "TownLimbShop.h"
#include "WorldLifecyclePolicy.h"
#include "LeaderboardSavePolicy.h"
#include "ArenaNativePersistence.h"
#include "FighterIdentity.h"

#include "ArenaIngress.h"
#include "ArenaUI.h"
#include "ContextMenuHooks.h"
#include "DebugMenu.h"
#include "FightStarter.h"
#include "LeaderboardStore.h"
#include "LeaderboardUI.h"
#include "PGSettingsUI.h"
#include "PrisonerUtil.h"
#include "ResultsUI.h"
#include "SparSession.h"
#include "SparStats.h"

#include "PGLog.h"
#include <core/Functions.h>

#pragma warning(push)
#pragma warning(disable: 4091)
#include <kenshi/GameWorld.h>
#include <kenshi/Globals.h>
#include <kenshi/SaveManager.h>
#include <kenshi/SaveFileSystem.h>
#include <ogre/OgreLogManager.h>
#pragma warning(pop)

#include <string>

#ifndef NULL
#define NULL 0
#endif

namespace
{
    void LogLifecycle(const char* message)
    {
        if (!message)
            return;
        PGLog::Debug(message);

    }

    void (*SaveManager_updateAutoSave_orig)(SaveManager*) = NULL;
    void (*SaveManager_save_orig)(SaveManager*, const std::string&, bool) = NULL;
    void (*SaveManager_execute_orig)(SaveManager*) = NULL;
    void (*GameWorld_clear_orig)(GameWorld*) = NULL;

    bool g_autosavePauseLogged = false;
    bool g_deferredAutosave = false;
    std::string g_deferredAutosaveName;
    bool g_abandoning = false;
    LeaderboardSavePolicy::ManualSaveGate g_manualGate;
    bool g_manualSaveDeferred = false;
    std::string g_manualSaveName;

    void FinishManualCleanup()
    {
        ArenaIngress::AbandonWorldState();
        SparSession::AbandonWorldState();
        PrisonerUtil::AbandonWorldState();
        FightStarter::AbandonWorldState();
        ArenaUI::AbandonWorldState();
        DebugMenu::AbandonWorldState();
        g_manualGate.Abandon();
        LogLifecycle("Proving Grounds: safe-save combat/AI cleanup complete; cage locks verified");
    }
    void ReportManualCancellation()
    {
        const std::string message = "Manual save CANCELLED: prisoner return was not secured: " +
            PrisonerUtil::GetCageReturnStatus() + ". Check the prisoner/cage. A later manual save can save the current unsecured world.";
        LogLifecycle(message.c_str());
        if (ou) ou->showPlayerAMessage(message, true);
    }
    bool HasSaveSensitiveActivity()
    {
        return WorldLifecycle::IsArenaOperationBusy() || TownLimbShop::IsVisitActive();
    }
    bool PrepareForManualSave()
    {
        const LeaderboardSavePolicy::ManualSaveDecision decision =
            g_manualGate.Request(PrisonerUtil::HasCageReturnFailure());
        if (decision == LeaderboardSavePolicy::DeferManualSave) return false;
        if (decision == LeaderboardSavePolicy::WarnUnsecuredSave)
        {
            const std::string warning = "Saving current world, but prisoner return was NOT secured: " +
                PrisonerUtil::GetCageReturnStatus() + ". Check the prisoner and cage after saving.";
            LogLifecycle(warning.c_str());
            if (ou) ou->showPlayerAMessage(warning, true);
        }
        TownLimbShop::SuspendVisit();
        if (!WorldLifecycle::IsArenaOperationBusy())
        {
            // Idle reservations do not postpone autosave, but a manual save
            // explicitly clears the session-only player queue. Ambient NPC
            // scheduling is a default runtime policy, not an idle operation.
            if (TownArena::HasBooking()) TownArena::Cancel();
            return true;
        }

        LogLifecycle("Proving Grounds: manual save requested during arena operation - safely aborting");
        if (ou)
        {
            ou->showPlayerAMessage(
                "Proving Grounds match cancelled to safely save the game.",
                true);
        }

        // This path runs before Kenshi serialises the world, while actors are
        // still valid. Restore native combat state and cage prisoners first.
        TownArena::CancelForSave();
        ArenaIngress::Cancel();
        SparSession::AbortForSave();

        if (PrisonerUtil::HasRuntimeActivity() && decision != LeaderboardSavePolicy::WarnUnsecuredSave)
        {
            std::string prisonerStatus;
            PrisonerUtil::ReturnAllToCages(prisonerStatus);
            if (!prisonerStatus.empty())
                LogLifecycle(("Proving Grounds: safe-save prisoner cleanup - " + prisonerStatus).c_str());
        }

        ResultsUI::Close();
        SparStats::AbandonWorldState();

        // Placement can queue native lock orders. Keep actor/handler references
        // until the normal GUI TickReturn verifies them, even after spar abort.
        if (PrisonerUtil::HasPendingCageLocks())
        {
            g_manualGate.Defer();
            const std::string waiting = "Manual save waiting for cage locks. Unpause so the handler can finish locking; a timeout will cancel this save.";
            LogLifecycle(waiting.c_str());
            if (ou) ou->showPlayerAMessage(waiting, true);
            return false;
        }
        if (PrisonerUtil::HasCageReturnFailure())
        {
            if (decision == LeaderboardSavePolicy::WarnUnsecuredSave) return true;
            ReportManualCancellation();
            return false;
        }
        FinishManualCleanup();
        return true;
    }
    void SaveManager_updateAutoSave_hook(SaveManager* self)
    {
        const bool busy = HasSaveSensitiveActivity();
        if (busy)
        {
            if (!g_autosavePauseLogged)
            {
                LogLifecycle("Proving Grounds: autosave postponed until arena operation completes");
                g_autosavePauseLogged = true;
            }
        }
        else if (g_autosavePauseLogged)
        {
            LogLifecycle("Proving Grounds: arena operation complete - autosave hold released");
            g_autosavePauseLogged = false;
        }

        // Keep Kenshi's native timer running.  Pausing it here prevented the
        // autosave request from ever reaching SaveManager::save, so there was
        // nothing to defer and no autosave to release after a match.  The save
        // hook below is the safe interception point: when the timer expires it
        // records that request while an arena operation is active.
        SaveManager_updateAutoSave_orig(self);

        if (!busy && !self->pauseAutoSaveTimer && g_deferredAutosave)
        {
            const std::string name = g_deferredAutosaveName;
            g_deferredAutosave = false;
            g_deferredAutosaveName.clear();
            LogLifecycle("Proving Grounds: running postponed autosave");
            SaveManager_save_orig(self, name, true);
        }
    }

    void SaveManager_save_hook(
        SaveManager* self, const std::string& name, bool autosave)
    {
        if (autosave && HasSaveSensitiveActivity())
        {
            g_deferredAutosave = true;
            g_deferredAutosaveName = name;
            LogLifecycle("Proving Grounds: queued autosave postponed until arena operation completes");
            return;
        }

        if (!autosave && !PrepareForManualSave())
        {
            g_manualSaveDeferred = g_manualGate.Waiting();
            g_manualSaveName = g_manualSaveDeferred ? name : std::string();
            return;
        }
        SaveManager_save_orig(self, name, autosave);
    }

    void SaveManager_execute_hook(SaveManager* self)
    {
        if (self->signal == SaveManager::SAVEGAME &&
            HasSaveSensitiveActivity())
        {
            // A save may already have been queued immediately before a fight
            // started. Leave the signal pending; execute will be called again.
            return;
        }

        // execute may return solely to consume delay or wait for filesystem.
        // Only New Game needs activation here; Load/Import hook their actual
        // native result and explicit source in ArenaNativePersistence.
        const bool newGame = self->signal == SaveManager::NEWGAME &&
            LeaderboardSavePolicy::NativeDispatchReady(self->delay,
                SaveFileSystem::getSingleton()->state == SaveFileSystem::NORMAL);
        if (newGame) WorldLifecycle::AbandonWorldState();
        SaveManager_execute_orig(self);
        if (newGame && self->signal == 0 && ArenaNativePersistence::IsReady())
            LeaderboardStore::CompleteWorldTransition(true);
    }
    void GameWorld_clear_hook(GameWorld* self)
    {
        // Backstop for teardown routes which do not go through SaveManager.
        if (!ArenaNativePersistence::IsLoading())
            WorldLifecycle::AbandonWorldState();
        GameWorld_clear_orig(self);
    }
}

namespace WorldLifecycle
{
    bool IsArenaOperationBusy()
    {
        WorldLifecyclePolicy::ArenaActivity activity = {};
        activity.ingressPending = ArenaIngress::IsPending();
        activity.walkInPending = ArenaIngress::IsWalkInPending();
        activity.matchActive = SparSession::IsActive();
        activity.prisonerActivity = LeaderboardSavePolicy::HoldForPrisoners(
            PrisonerUtil::HasRuntimeActivity(), PrisonerUtil::HasPendingCageLocks(),
            PrisonerUtil::HasCageReturnFailure()) || g_manualGate.Waiting();
        activity.townActivity = TownArena::IsBusy();
        return WorldLifecyclePolicy::ShouldPostponeAutosave(activity);
    }

    void AbandonWorldState()
    {
        if (g_abandoning)
            return;
        g_abandoning = true;
        ArenaNativePersistence::AbandonWorld();
        FighterIdentity::AbandonWorldState();

        ResultsUI::Close();
        LeaderboardUI::Close();
        ContextMenuHooks::AbandonWorldState();
        DebugMenu::AbandonWorldState();
        PGSettingsUI::AbandonWorldState();
        ArenaUI::AbandonWorldState();
        SparStats::AbandonWorldState();
        SparSession::AbandonWorldState();
        ArenaIngress::AbandonWorldState();
        TownArena::AbandonWorldState();
        PrisonerUtil::AbandonWorldState();
        FightStarter::AbandonWorldState();
        LeaderboardStore::Unload();

        g_deferredAutosave = false;
        g_deferredAutosaveName.clear();
        g_manualSaveDeferred = false;
        g_manualGate.Abandon();
        g_manualSaveName.clear();
        g_autosavePauseLogged = false;
        LogLifecycle("Proving Grounds: world transition - runtime references cleared");
        g_abandoning = false;
    }

    void Tick()
    {
        ArenaNativePersistence::Tick();
        const LeaderboardSavePolicy::ManualSaveDecision decision = g_manualGate.Poll(
            PrisonerUtil::HasPendingCageLocks(), PrisonerUtil::HasCageReturnFailure());
        if (decision == LeaderboardSavePolicy::NoDeferredSave ||
            decision == LeaderboardSavePolicy::DeferManualSave) return;
        if (decision == LeaderboardSavePolicy::CancelManualSave)
        {
            g_manualSaveDeferred = false;
            g_manualSaveName.clear();
            ReportManualCancellation();
            return;
        }
        const bool save = g_manualSaveDeferred;
        const std::string name = g_manualSaveName;
        g_manualSaveDeferred = false;
        g_manualSaveName.clear();
        FinishManualCleanup();
        if (save) SaveManager_save_orig(SaveManager::getSingleton(), name, false);
    }

    bool InstallHooks()
    {
        bool ok = ArenaNativePersistence::InstallHooks();

        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(&SaveManager::updateAutoSave),
                SaveManager_updateAutoSave_hook,
                &SaveManager_updateAutoSave_orig))
        {
            PGLog::Error("Proving Grounds: failed to hook SaveManager::updateAutoSave");
            ok = false;
        }

        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(&SaveManager::save),
                SaveManager_save_hook,
                &SaveManager_save_orig))
        {
            PGLog::Error("Proving Grounds: failed to hook SaveManager::save");
            ok = false;
        }

        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(&SaveManager::execute),
                SaveManager_execute_hook,
                &SaveManager_execute_orig))
        {
            PGLog::Error("Proving Grounds: failed to hook SaveManager::execute");
            ok = false;
        }

        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(&GameWorld::_clearAndDestroyGameWorldStuff),
                GameWorld_clear_hook,
                &GameWorld_clear_orig))
        {
            PGLog::Error("Proving Grounds: failed to hook GameWorld teardown");
            ok = false;
        }

        if (!ok) ArenaNativePersistence::Disable();
        if (ok)
            LogLifecycle("Proving Grounds: save/load lifecycle hooks installed");
        return ok;
    }
}
