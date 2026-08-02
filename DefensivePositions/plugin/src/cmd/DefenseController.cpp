#include "cmd/DefenseController.h"

#include "game/GameAdapters.h"
#include "cmd/CommandIssuer.h"
#include "store/LayoutStore.h"
#include "report/StatusReport.h"
#include "types/DefenseTypes.h"
#include "monitor/ArrivalMonitor.h"
#include "ui/DefenseButton.h"

#include <Debug.h>

#include <sstream>
#include <vector>

namespace DefenseController
{
    namespace
    {
        LayoutStore* g_store = nullptr;
        ArrivalMonitor g_monitor;

        // Rows for units that were already terminal (dead/KO/imprisoned/no
        // assignment/etc.) at the moment Return was issued, so they never
        // got added to g_monitor. Combined with g_monitor.results() to form
        // the final report.
        std::vector<UnitStatus> g_immediateResults;

        // A Return can stay in flight for up to kDefaultTimeoutSec (45s) of
        // game time, during which Kenshi may destroy any of the tracked
        // characters (death cleanup, zone unload, save reload). Only the
        // packed handle is stored, and it is re-resolved through
        // GameAdapters::resolveCharacter() on every tick; a raw Character*
        // held across ticks would be a use-after-free waiting to happen.
        //
        // Parallel to whatever TrackedUnit list was last passed to
        // g_monitor.begin() (same size, same order) — ArrivalMonitor itself
        // has zero KenshiLib includes, so it can't drive facing/snapshot
        // building; DefenseController (a game-layer module) keeps that
        // context here instead.
        struct TrackedContext
        {
            uint64_t characterId;
            DefenseAssignment assignment;
            bool facingApplied;
            float elapsedSec;
            float lastDiagLogSec;
            bool remountNudged;
        };
        std::vector<TrackedContext> g_trackedContext;

        // A turret's couldIOperate() only means "this unit cannot man it"
        // once the unit has actually had a chance to get there; asked on the
        // first frame it can just mean "still standing where it was". Wait
        // for the unit to be next to the turret, or for this much time to
        // pass, before treating a negative answer as a real remount failure.
        const float kRemountCheckGraceSec = 5.f;
        const float kTurretProximityMeters = 8.f;

        void resetTrackingState()
        {
            g_trackedContext.clear();
            g_immediateResults.clear();
        }

        void showReportAndReset()
        {
            std::vector<UnitStatus> rows = g_immediateResults;
            const std::vector<UnitStatus> monitorRows = g_monitor.results();
            rows.insert(rows.end(), monitorRows.begin(), monitorRows.end());

            const std::string report = formatStatusReport(rows);
            DebugLog("DefensivePositions: Return finished:\n" + report);
            // Keep the per-unit report in the log only — the hover tooltip stays
            // on the short "Returning N positions" / help text, not "Name: unavailable".

            resetTrackingState();
        }
    }

    void configure(LayoutStore* store)
    {
        g_store = store;
    }

    std::string saveSelectedDefense(LayoutStore& store)
    {
        std::vector<Character*> chars = GameAdapters::getSelectedCharacters();
        if (chars.empty())
            return "No characters selected";

        size_t positionsSaved = 0;
        size_t turretsSaved = 0;

        for (size_t i = 0; i < chars.size(); ++i)
        {
            Character* c = chars[i];
            if (!GameAdapters::isPlayerControllable(c))
                continue;

            DefenseAssignment a = {};
            a.characterId = GameAdapters::stableCharacterId(c);
            a.position = GameAdapters::getPosition(c);

            ObjectRef turretRef;
            if (GameAdapters::isMountedOnTurret(c, turretRef))
            {
                a.hasTurret = true;
                a.turret = turretRef;
                a.facingYaw = 0.f;
                ++turretsSaved;

                std::ostringstream msg;
                msg << "DefensivePositions: Save turret for \""
                    << GameAdapters::getDisplayName(c) << "\": "
                    << GameAdapters::diagnoseTurretRemount(c, turretRef);
                DebugLog(msg.str());
            }
            else
            {
                a.hasTurret = false;
                a.turret = ObjectRef();
                a.facingYaw = GameAdapters::getFacingYaw(c);
            }

            store.upsert(a);
            ++positionsSaved;
        }

        return formatSaveConfirm(positionsSaved, turretsSaved);
    }

    std::string discardSelectedDefense(LayoutStore& store)
    {
        std::vector<Character*> chars = GameAdapters::getSelectedCharacters();
        if (chars.empty())
            return "No characters selected";

        size_t positionsDiscarded = 0;

        for (size_t i = 0; i < chars.size(); ++i)
        {
            Character* c = chars[i];
            if (!GameAdapters::isPlayerControllable(c))
                continue;

            const uint64_t id = GameAdapters::stableCharacterId(c);
            if (store.erase(id))
                ++positionsDiscarded;
        }

        return formatDiscardConfirm(positionsDiscarded);
    }

    void onSave()
    {
        if (!g_store)
        {
            ErrorLog("DefensivePositions: DefenseController::onSave - not configured (missing store)");
            return;
        }

        const std::string status = saveSelectedDefense(*g_store);
        DebugLog("DefensivePositions: " + status);
        DefenseButton::setStatusText(status);
    }

    void onDiscard()
    {
        if (!g_store)
        {
            ErrorLog("DefensivePositions: DefenseController::onDiscard - not configured (missing store)");
            return;
        }

        const std::string status = discardSelectedDefense(*g_store);
        DebugLog("DefensivePositions: " + status);
        DefenseButton::setStatusText(status);
    }

    void onReturn()
    {
        if (!g_store)
        {
            ErrorLog("DefensivePositions: DefenseController::onReturn - not configured (missing store)");
            return;
        }

        if (g_monitor.isActive())
        {
            DebugLog("DefensivePositions: Return already in progress");
            DefenseButton::setStatusText("Return already in progress");
            return;
        }

        std::vector<Character*> chars = GameAdapters::getSelectedCharacters();
        if (chars.empty())
        {
            DebugLog("DefensivePositions: Return - no characters selected");
            DefenseButton::setStatusText("No characters selected");
            return;
        }

        resetTrackingState();

        std::vector<TrackedUnit> tracked;

        for (size_t i = 0; i < chars.size(); ++i)
        {
            Character* c = chars[i];
            const uint64_t id = GameAdapters::stableCharacterId(c);

            const std::string displayName = GameAdapters::getDisplayName(c);

            UnitStatus status;
            status.characterId = id;
            status.displayName = displayName;

            const UnitResult::Type availability = GameAdapters::classifyAvailability(c);
            if (availability != UnitResult::Ok)
            {
                status.result = availability;
                g_immediateResults.push_back(status);
                continue;
            }

            DefenseAssignment assignment = {};
            if (!g_store->tryGet(id, assignment))
            {
                status.result = UnitResult::NoAssignment;
                g_immediateResults.push_back(status);
                continue;
            }

            const bool issued = assignment.hasTurret
                ? CommandIssuer::issueMountTurret(c, assignment.turret)
                : CommandIssuer::issueMoveTo(c, assignment.position);

            if (!issued)
            {
                status.result = assignment.hasTurret ? UnitResult::TurretRemountFailed : UnitResult::Unavailable;
                if (assignment.hasTurret)
                {
                    status.detail = std::string("order rejected; ")
                        + GameAdapters::diagnoseTurretRemount(c, assignment.turret);
                    DebugLog(std::string("DefensivePositions: Return remount order failed for \"")
                             + displayName + "\": " + status.detail);
                }
                g_immediateResults.push_back(status);
                continue;
            }

            TrackedUnit unit = {};
            unit.characterId = id;
            unit.displayName = displayName;
            unit.assignment = assignment;
            unit.result = UnitResult::Unavailable; // placeholder until ArrivalMonitor::tick sets a real result
            unit.done = false;
            unit.elapsedSec = 0.f;
            tracked.push_back(unit);

            TrackedContext ctx;
            ctx.characterId = id;
            ctx.assignment = assignment;
            ctx.facingApplied = false;
            ctx.elapsedSec = 0.f;
            ctx.lastDiagLogSec = -10.f;
            ctx.remountNudged = false;
            g_trackedContext.push_back(ctx);
        }

        g_monitor.begin(tracked);

        if (!g_monitor.isActive())
        {
            // Everyone was already terminal (no assignment/unavailable/etc.)
            // or nothing needed tracking — report immediately instead of
            // waiting for a tick.
            showReportAndReset();
            return;
        }

        {
            std::ostringstream msg;
            msg << "DefensivePositions: Return - tracking " << tracked.size() << " unit(s)";
            DebugLog(msg.str());
        }
        {
            std::ostringstream ui;
            ui << "Returning " << tracked.size() << " positions";
            DefenseButton::setStatusText(ui.str());
        }
    }

    void onTick(float dt)
    {
        if (!g_monitor.isActive())
            return;

        // Re-resolved once per tick and reused below, so a character that is
        // destroyed mid-tick can't be dereferenced twice with different
        // outcomes. Entries stay null for units whose handle went stale.
        std::vector<Character*> live(g_trackedContext.size(), nullptr);

        GameSnapshot snap;
        snap.units.reserve(g_trackedContext.size());
        for (size_t i = 0; i < g_trackedContext.size(); ++i)
        {
            TrackedContext& ctx = g_trackedContext[i];
            ctx.elapsedSec += dt;

            Character* c = GameAdapters::resolveCharacter(ctx.characterId);
            if (!c)
            {
                g_monitor.markUnavailable(ctx.characterId);
                continue;
            }

            live[i] = c;
            snap.units.push_back(GameAdapters::buildUnitSnapshot(c, ctx.assignment.turret));
        }

        g_monitor.tick(dt, snap);

        // Progress + early remount-failure detection. Rate-limit progress
        // dumps so RE_Kenshi.log stays readable across a 45s wait.
        for (size_t i = 0; i < g_trackedContext.size(); ++i)
        {
            TrackedContext& ctx = g_trackedContext[i];
            if (!ctx.assignment.hasTurret || !live[i])
                continue;

            if (ctx.elapsedSec - ctx.lastDiagLogSec >= 5.f)
            {
                ctx.lastDiagLogSec = ctx.elapsedSec;
                std::ostringstream msg;
                msg << "DefensivePositions: remount wait \""
                    << GameAdapters::getDisplayName(live[i]) << "\" t="
                    << ctx.elapsedSec << "s: "
                    << GameAdapters::diagnoseTurretRemount(live[i], ctx.assignment.turret);
                DebugLog(msg.str());

                // Periodic near-range remount nudge (with the progress log).
                ObjectRef mounted;
                if (!GameAdapters::isMountedOnTurret(live[i], mounted) || mounted != ctx.assignment.turret)
                {
                    if (!GameAdapters::isTurretRemountBlocked(live[i], ctx.assignment.turret))
                        CommandIssuer::tryOperateTurret(live[i], ctx.assignment.turret);
                }
            }

            const bool checkable = ctx.elapsedSec >= kRemountCheckGraceSec ||
                                   GameAdapters::isNearTurret(live[i], ctx.assignment.turret, kTurretProximityMeters);
            if (!checkable)
                continue;

            // First time we become checkable, nudge tryOperate once.
            if (!ctx.remountNudged)
            {
                ctx.remountNudged = true;
                ObjectRef mountedNow;
                if (!GameAdapters::isMountedOnTurret(live[i], mountedNow) || mountedNow != ctx.assignment.turret)
                {
                    if (!GameAdapters::isTurretRemountBlocked(live[i], ctx.assignment.turret))
                        CommandIssuer::tryOperateTurret(live[i], ctx.assignment.turret);
                }
            }

            if (GameAdapters::isTurretRemountBlocked(live[i], ctx.assignment.turret))
            {
                const std::string detail = GameAdapters::diagnoseTurretRemount(live[i], ctx.assignment.turret);
                DebugLog(std::string("DefensivePositions: remount blocked for \"")
                         + GameAdapters::getDisplayName(live[i]) + "\": " + detail);
                g_monitor.markRemountFailed(ctx.characterId, detail);
            }
        }

        // Annotate turret Timeout / TurretMissing rows with live diagnosis
        // before the final report (ArrivalMonitor itself stays Kenshi-free).
        {
            const std::vector<UnitStatus> current = g_monitor.results();
            for (size_t i = 0; i < g_trackedContext.size() && i < current.size(); ++i)
            {
                const TrackedContext& ctx = g_trackedContext[i];
                if (!ctx.assignment.hasTurret || !live[i])
                    continue;

                const UnitResult::Type r = current[i].result;
                if (r != UnitResult::Timeout && r != UnitResult::TurretMissing)
                    continue;
                if (!current[i].detail.empty())
                    continue;

                const std::string detail = GameAdapters::diagnoseTurretRemount(live[i], ctx.assignment.turret);
                g_monitor.setDetail(ctx.characterId, detail);
                DebugLog(std::string("DefensivePositions: turret result detail for \"")
                         + GameAdapters::getDisplayName(live[i]) + "\": " + detail);
            }
        }

        // Apply facing the moment a ground unit first reaches its position
        // (turret mounts don't need a facing restore).
        const std::vector<UnitStatus> current = g_monitor.results();
        for (size_t i = 0; i < g_trackedContext.size() && i < current.size(); ++i)
        {
            TrackedContext& ctx = g_trackedContext[i];
            if (ctx.facingApplied || ctx.assignment.hasTurret || !live[i])
                continue;

            if (current[i].result == UnitResult::Ok)
            {
                CommandIssuer::applyFacing(live[i], ctx.assignment.facingYaw);
                ctx.facingApplied = true;
            }
        }

        if (!g_monitor.isActive())
        {
            showReportAndReset();
        }
    }

    void abortReturn()
    {
        if (g_monitor.isActive())
            DebugLog("DefensivePositions: abandoning in-progress Return (world was reloaded)");

        g_monitor.reset();
        resetTrackingState();
    }
}
