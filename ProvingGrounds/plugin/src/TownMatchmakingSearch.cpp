#include "TownMatchmakingSearch.h"
#include <Windows.h>
#include <process.h>
#include <algorithm>
#include <deque>

namespace
{
    CRITICAL_SECTION g_lock;
    bool g_lockReady = false;
    HANDLE g_thread = NULL;
    HANDLE g_work = NULL;
    HANDLE g_stop = NULL;
    unsigned g_generation = 0;
    std::deque<TownMatchmakingSearch::Request> g_requests;
    std::deque<TownMatchmakingSearch::Result> g_results;

    unsigned __stdcall SearchThread(void*)
    {
        HANDLE events[2] = { g_stop, g_work };
        for (;;)
        {
            const DWORD wait = WaitForMultipleObjects(2, events, FALSE, INFINITE);
            if (wait == WAIT_OBJECT_0) return 0;
            if (wait != WAIT_OBJECT_0 + 1) return 1;

            for (;;)
            {
                if (WaitForSingleObject(g_stop, 0) == WAIT_OBJECT_0) return 0;
                TownMatchmakingSearch::Request request;
                bool found = false;
                EnterCriticalSection(&g_lock);
                if (!g_requests.empty())
                {
                    request = g_requests.front();
                    g_requests.pop_front();
                    found = true;
                }
                LeaveCriticalSection(&g_lock);
                if (!found) break;

                TownMatchmakingSearch::Result result;
                result.generation = request.generation;
                result.slot = request.slot;
                result.match = TownMatchmakingPolicy::SelectPlayer(
                    request.players, request.npcs, request.division, request.seed,
                    request.history, request.maxEnemies, request.maxTotal,
                    request.requiredRole, request.rarity, request.forceSoloNamed,
                    request.teams1v1, request.challengeDifficulty);

                EnterCriticalSection(&g_lock);
                if (request.generation == g_generation)
                {
                    g_results.push_back(result);
                    if (result.match.valid)
                        for (size_t i = 0; i < g_requests.size(); ++i)
                            if (g_requests[i].generation == request.generation) {
                                TownMatchmakingPolicy::RecordHistory(
                                    g_requests[i].history, result.match.b);
                                std::vector<TownMatchmakingPolicy::Fighter>& roster = g_requests[i].npcs;
                                for (size_t n = roster.size(); n > 0; --n)
                                    if (std::find(result.match.b.begin(), result.match.b.end(),
                                        roster[n - 1].id) != result.match.b.end())
                                        roster.erase(roster.begin() + (n - 1));
                            }
                }
                LeaveCriticalSection(&g_lock);
            }
        }
    }

    bool EnsureStarted()
    {
        if (g_thread) return true;
        if (!g_lockReady)
        {
            InitializeCriticalSection(&g_lock);
            g_lockReady = true;
        }
        g_work = CreateEvent(NULL, FALSE, FALSE, NULL);
        g_stop = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (!g_work || !g_stop)
        {
            if (g_work) CloseHandle(g_work);
            if (g_stop) CloseHandle(g_stop);
            g_work = NULL;
            g_stop = NULL;
            return false;
        }
        uintptr_t thread = _beginthreadex(NULL, 0, SearchThread, NULL, 0, NULL);
        if (!thread)
        {
            CloseHandle(g_work);
            CloseHandle(g_stop);
            g_work = NULL;
            g_stop = NULL;
            return false;
        }
        g_thread = reinterpret_cast<HANDLE>(thread);
        SetThreadPriority(g_thread, THREAD_PRIORITY_BELOW_NORMAL);
        return true;
    }
}

namespace TownMatchmakingSearch
{
    void BeginGeneration(unsigned generation)
    {
        if (!EnsureStarted()) return;
        EnterCriticalSection(&g_lock);
        g_generation = generation;
        g_requests.clear();
        g_results.clear();
        LeaveCriticalSection(&g_lock);
    }

    bool Enqueue(const Request& request)
    {
        if (!EnsureStarted()) return false;
        EnterCriticalSection(&g_lock);
        const bool current = request.generation == g_generation;
        if (current) g_requests.push_back(request);
        LeaveCriticalSection(&g_lock);
        if (current) SetEvent(g_work);
        return current;
    }

    bool Poll(Result& result)
    {
        if (!g_thread) return false;
        EnterCriticalSection(&g_lock);
        const bool found = !g_results.empty();
        if (found)
        {
            result = g_results.front();
            g_results.pop_front();
        }
        LeaveCriticalSection(&g_lock);
        return found;
    }

    void Shutdown()
    {
        if (!g_thread) return;
        SetEvent(g_stop);
        WaitForSingleObject(g_thread, INFINITE);
        CloseHandle(g_thread);
        CloseHandle(g_work);
        CloseHandle(g_stop);
        g_thread = NULL;
        g_work = NULL;
        g_stop = NULL;
        EnterCriticalSection(&g_lock);
        g_requests.clear();
        g_results.clear();
        LeaveCriticalSection(&g_lock);
    }
}
