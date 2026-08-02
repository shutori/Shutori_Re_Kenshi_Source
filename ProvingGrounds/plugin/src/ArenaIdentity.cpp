#include "ArenaIdentity.h"

#include <Debug.h>
#include <string>

#pragma warning(push)
#pragma warning(disable: 4091)
#include <kenshi/Building/Building.h>
#include <kenshi/Enums.h>
#include <kenshi/GameData.h>
#include <kenshi/RootObject.h>
#pragma warning(pop)

#ifndef NULL
#define NULL 0
#endif

namespace ArenaIdentity
{
    // FCS stringIDs — match building and/or function (use-node) ids.
    const char* kRegistryStringId = "45-Proving Grounds.mod";
    const char* kRegistryFunctionStringId = "47-Proving Grounds.mod";
    const char* kArenaStringId = "36-Proving Grounds.mod";
    const char* kBannerStringId = "42-Proving Grounds.mod";
    const char* kBannerFunctionStringId = "60-Proving Grounds.mod";
    const char* kLeaderboardStringId = "53-Proving Grounds.mod";
    const char* kLeaderboardFunctionStringId = "57-Proving Grounds.mod";

    namespace
    {
        bool IdEquals(GameData* data, const char* id)
        {
            return data && id && data->stringID == id;
        }

        bool IsLeaderboardData(GameData* data)
        {
            return IdEquals(data, kLeaderboardStringId)
                || IdEquals(data, kLeaderboardFunctionStringId);
        }

        bool IsRegistryData(GameData* data)
        {
            return IdEquals(data, kRegistryStringId)
                || IdEquals(data, kRegistryFunctionStringId);
        }

        bool IsBannerData(GameData* data)
        {
            return IdEquals(data, kBannerStringId)
                || IdEquals(data, kBannerFunctionStringId);
        }

        bool LooksLikeBuilding(RootObject* obj)
        {
            if (!obj)
                return false;
            const itemType type = obj->getDataType();
            return type == BUILDING
                || type == BUILDING_FUNCTIONALITY
                || type == FOLIAGE_BUILDING;
        }

        // UseableStuff::functionalityData is at offset 0x400 (KenshiLib header).
        // Avoid including UseableStuff.h (pulls InventoryLayout/MyGUI).
        GameData* TryGetFunctionalityData(Building* building)
        {
            if (!building)
                return NULL;
            UseableStuff* useable = building->getUseableStuff();
            if (!useable)
                return NULL;
            return *reinterpret_cast<GameData**>(
                reinterpret_cast<char*>(useable) + 0x400);
        }
    }

    const char* GetStringId(RootObject* obj)
    {
        if (!obj)
            return "";
        GameData* data = obj->getGameData();
        if (!data)
            return "";
        return data->stringID.c_str();
    }

    bool MatchesId(RootObject* obj, const char* id)
    {
        if (!obj || !id)
            return false;
        GameData* data = obj->getGameData();
        if (!data)
            return false;
        return data->stringID == id;
    }

    bool IsRegistry(RootObject* obj)
    {
        if (!obj)
            return false;
        if (IsRegistryData(obj->getGameData()))
            return true;

        if (!LooksLikeBuilding(obj))
            return false;

        Building* building = static_cast<Building*>(obj);
        if (IsRegistryData(building->getGameData()))
            return true;
        if (IsRegistryData(TryGetFunctionalityData(building)))
            return true;
        return false;
    }

    bool IsArena(RootObject* obj) { return MatchesId(obj, kArenaStringId); }
    bool IsBanner(RootObject* obj)
    {
        if (!obj)
            return false;
        if (IsBannerData(obj->getGameData()))
            return true;

        if (!LooksLikeBuilding(obj))
            return false;

        Building* building = static_cast<Building*>(obj);
        if (IsBannerData(building->getGameData()))
            return true;
        return IsBannerData(TryGetFunctionalityData(building));
    }

    bool IsMatchUiOpener(RootObject* obj)
    {
        return IsRegistry(obj) || IsBanner(obj);
    }

    bool IsLeaderboard(RootObject* obj)
    {
        if (!obj)
            return false;
        if (IsLeaderboardData(obj->getGameData()))
            return true;

        if (!LooksLikeBuilding(obj))
            return false;

        Building* building = static_cast<Building*>(obj);
        if (IsLeaderboardData(building->getGameData()))
            return true;
        if (IsLeaderboardData(TryGetFunctionalityData(building)))
            return true;
        return false;
    }

    bool IsBuildingFinished(Building* building)
    {
        if (!building || !building->isValid())
            return false;
        Building::ConstructionState* state = building->getBuildState();
        // No construction state → treat as always-usable (furniture / prebuilt).
        if (!state)
            return true;
        return state->isComplete;
    }

    bool IsRegistryFinished(Building* building)
    {
        return IsRegistry(building) && IsBuildingFinished(building);
    }

    bool IsMatchUiOpenerFinished(Building* building)
    {
        return IsMatchUiOpener(building) && IsBuildingFinished(building);
    }

    bool IsLeaderboardFinished(Building* building)
    {
        return IsLeaderboard(building) && IsBuildingFinished(building);
    }

    void LogInteractIdentity(RootObject* obj, const char* where)
    {
        if (!obj)
            return;
        GameData* data = obj->getGameData();
        const char* sid = (data && !data->stringID.empty()) ? data->stringID.c_str() : "(none)";

        const char* funcSid = "(none)";
        if (LooksLikeBuilding(obj))
        {
            Building* building = static_cast<Building*>(obj);
            GameData* func = TryGetFunctionalityData(building);
            if (func && !func->stringID.empty())
                funcSid = func->stringID.c_str();
        }

        std::string msg = "Proving Grounds: ";
        msg += where ? where : "interact";
        msg += " stringID=";
        msg += sid;
        msg += " func=";
        msg += funcSid;
        msg += IsLeaderboard(obj) ? " [leaderboard]" : "";
        msg += IsRegistry(obj) ? " [registry]" : "";
        msg += IsBanner(obj) ? " [banner]" : "";
        DebugLog(msg.c_str());
    }
}
