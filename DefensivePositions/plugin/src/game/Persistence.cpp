#include "Persistence.h"

#include "store/LayoutStore.h"

#include <Debug.h>
#include <core/Functions.h>

#include <kenshi/Enums.h>
#include <kenshi/Faction.h>
#include <kenshi/GameData.h>
#include <kenshi/GameDataManager.h>
#include <kenshi/GameWorld.h>
#include <kenshi/Kenshi.h>
#include <kenshi/SaveManager.h>

#include <cstddef>
#include <string>

namespace
{
    LayoutStore* g_store = nullptr;
    Persistence::WorldResetCallback g_onWorldReset = nullptr;

    // kenshi/Enums.h's itemType enum only runs up to OBJECT_TYPE_MAX (well
    // under 200 entries); KenshiLib_Examples/WorldStates's own comment
    // recommends "a starting enum between 1100 and 55000 to avoid clashes"
    // for a mod's private GameData entries, so we pick an arbitrary value
    // in that range.
    const itemType kLayoutItemType = static_cast<itemType>(45000);
    // Current save key. Legacy key kept so saves made before the rename still load.
    const char* const kLayoutStringID = "DefensivePositions.layout";
    const char* const kLegacyLayoutStringID = "CombatFormations.layout";
    const char* const kLayoutFieldName = "blob";

    const char kBase64Chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    // LayoutStore::serialize() returns raw binary (embedded NUL bytes,
    // arbitrary float/int bit patterns). Whether Kenshi's save file writer
    // treats GameData::sdata strings as NUL-terminated internally is not
    // something we can verify from these headers alone, so we base64-encode
    // before handing the blob to the game and decode on the way back out —
    // that removes the risk entirely regardless of the on-disk format.
    std::string base64Encode(const std::string& input)
    {
        std::string output;
        output.reserve(((input.size() + 2) / 3) * 4);

        const unsigned char* data = reinterpret_cast<const unsigned char*>(input.data());
        const size_t len = input.size();
        size_t i = 0;

        while (i + 3 <= len)
        {
            const unsigned int n = (static_cast<unsigned int>(data[i]) << 16) |
                                    (static_cast<unsigned int>(data[i + 1]) << 8) |
                                    static_cast<unsigned int>(data[i + 2]);
            output += kBase64Chars[(n >> 18) & 0x3F];
            output += kBase64Chars[(n >> 12) & 0x3F];
            output += kBase64Chars[(n >> 6) & 0x3F];
            output += kBase64Chars[n & 0x3F];
            i += 3;
        }

        const size_t remaining = len - i;
        if (remaining == 1)
        {
            const unsigned int n = static_cast<unsigned int>(data[i]) << 16;
            output += kBase64Chars[(n >> 18) & 0x3F];
            output += kBase64Chars[(n >> 12) & 0x3F];
            output += '=';
            output += '=';
        }
        else if (remaining == 2)
        {
            const unsigned int n = (static_cast<unsigned int>(data[i]) << 16) |
                                    (static_cast<unsigned int>(data[i + 1]) << 8);
            output += kBase64Chars[(n >> 18) & 0x3F];
            output += kBase64Chars[(n >> 12) & 0x3F];
            output += kBase64Chars[(n >> 6) & 0x3F];
            output += '=';
        }

        return output;
    }

    int base64CharValue(char c)
    {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    }

    std::string base64Decode(const std::string& input)
    {
        std::string output;
        output.reserve((input.size() / 4) * 3);

        unsigned int buffer = 0;
        int bitsCollected = 0;

        for (size_t i = 0; i < input.size(); ++i)
        {
            const char c = input[i];
            if (c == '=')
                break;

            const int value = base64CharValue(c);
            if (value < 0)
                continue; // ignore stray/whitespace bytes defensively

            buffer = (buffer << 6) | static_cast<unsigned int>(value);
            bitsCollected += 6;
            if (bitsCollected >= 8)
            {
                bitsCollected -= 8;
                output += static_cast<char>((buffer >> bitsCollected) & 0xFF);
            }
        }

        return output;
    }

    // --- Save hook -------------------------------------------------------
    //
    // FactionManager::saveGameState(GameDataContainer*) is the exact function
    // KenshiLib_Examples/WorldStates.cpp hooks to write custom data into the
    // file being saved ("data here is written to quick.save"). It's public
    // and non-virtual in the current kenshi/Faction.h, so it hooks the same
    // way as the reference example, no adaptation needed here.
    //
    // Ordering matters: WorldStates.cpp populates the container *before*
    // calling the original, because the original is what actually serialises
    // the container out to the save file. Writing afterwards would drop our
    // entry from that save.
    void (*FactionManager_saveGameState_orig)(FactionManager*, GameDataContainer*) = nullptr;
    void FactionManager_saveGameState_hook(FactionManager* thisptr, GameDataContainer* container)
    {
        if (g_store && container)
        {
            GameData* data = container->createNewData(kLayoutItemType, kLayoutStringID, "Defensive Positions Layout");
            if (data)
                data->sdata[kLayoutFieldName] = base64Encode(g_store->serialize());
            else
                ErrorLog("DefensivePositions: Persistence - could not create save GameData entry");
        }

        FactionManager_saveGameState_orig(thisptr, container);
    }

    // --- Load hook ---------------------------------------------------------
    //
    // WorldStates.cpp hooks GameWorld::loadAllPlatoons() to restore data
    // right after a save game finishes loading ("as far as I can tell it's
    // only called when loading a save game"), and we resolve it exactly the
    // same way: KenshiLib::GetRealAddress(&GameWorld::loadAllPlatoons).
    //
    // The `// protected RVA = 0x9ED6C0` trailing comment in
    // kenshi/GameWorld.h is header-generator metadata describing the
    // original binary, not a C++ access specifier: the declaration sits in
    // GameWorld's only `public:` section, and KenshiLib's own export stub
    // (Source/kenshi/functions/GameWorld.inc) mangles it as
    // `?loadAllPlatoons@GameWorld@@QEAAXXZ` — the `Q` marks a public member
    // function. So `&GameWorld::loadAllPlatoons` is takeable from here and
    // there is no need for a hand-copied RVA constant (which would silently
    // drift against any other game build).
    //
    // The Kenshi version is still logged at install time as a diagnostic.
    // An UNKNOWN result is not treated as fatal: KenshiLib's HashToVersionMap
    // (Source/kenshi/Kenshi.cpp) only ships hashes for Steam/GOG 1.0.65,
    // while the design doc also targets 1.0.68.
    void LogKenshiVersionForDiagnostics()
    {
        KenshiLib::BinaryVersion version = KenshiLib::GetKenshiVersion();
        if (version.GetPlatform() == KenshiLib::BinaryVersion::UNKNOWN)
            DebugLog("DefensivePositions: Persistence - KenshiLib did not recognize the running exe's hash "
                     "(may still be a supported build KenshiLib doesn't hash-check, e.g. 1.0.68, or a modified exe)");
        else
            DebugLog("DefensivePositions: Persistence - detected Kenshi " + version.ToString());
    }

    // Any in-progress Return is tracking characters (and an elapsed timeout)
    // that belong to the world being replaced, so it must be abandoned
    // whenever the world is swapped out from under us.
    void notifyWorldReset()
    {
        if (g_onWorldReset)
            g_onWorldReset();
    }

    void (*GameWorld_loadAllPlatoons_orig)(GameWorld*) = nullptr;
    void GameWorld_loadAllPlatoons_hook(GameWorld* thisptr)
    {
        GameWorld_loadAllPlatoons_orig(thisptr);

        notifyWorldReset();

        if (!g_store)
            return;

        // Clear unconditionally first: if this save has no Defensive Positions
        // data (predates this feature, or the field is missing/corrupt), the
        // store should end up empty rather than keeping a stale layout from
        // whatever was loaded/played before this load.
        g_store->clear();

        GameData* data = thisptr->savedata.getData(kLayoutStringID);
        if (!data)
            data = thisptr->savedata.getData(kLegacyLayoutStringID);
        if (!data)
            return;

        if (data->sdata.count(kLayoutFieldName) == 0)
            return;

        const std::string encoded = data->sdata[kLayoutFieldName];
        if (!g_store->deserialize(base64Decode(encoded)))
            ErrorLog("DefensivePositions: Persistence - saved layout blob is old/unrecognized (need CF02); ignoring — RMB Save again after load");
    }

    // --- New game hook -----------------------------------------------------
    //
    // SaveManager::newGame(startId) is the public entry point the game calls
    // when starting a fresh playthrough. Hooking it (rather than e.g.
    // GameWorld::resetGame, which also runs during a normal load) keeps
    // "clear on new game" from also firing every time an existing save loads.
    void (*SaveManager_newGame_orig)(SaveManager*, const std::string&) = nullptr;
    void SaveManager_newGame_hook(SaveManager* thisptr, const std::string& startId)
    {
        SaveManager_newGame_orig(thisptr, startId);

        notifyWorldReset();

        if (g_store)
            g_store->clear();
    }
}

namespace Persistence
{
    void install(LayoutStore& store, WorldResetCallback onWorldReset)
    {
        g_store = &store;
        g_onWorldReset = onWorldReset;

        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(&FactionManager::saveGameState),
                &FactionManager_saveGameState_hook,
                &FactionManager_saveGameState_orig))
            ErrorLog("DefensivePositions: Persistence - could not hook FactionManager::saveGameState");

        LogKenshiVersionForDiagnostics();

        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(&GameWorld::loadAllPlatoons),
                &GameWorld_loadAllPlatoons_hook,
                &GameWorld_loadAllPlatoons_orig))
            ErrorLog("DefensivePositions: Persistence - could not hook GameWorld::loadAllPlatoons; "
                     "saved layouts will not be restored this session");

        if (KenshiLib::SUCCESS != KenshiLib::AddHook(
                KenshiLib::GetRealAddress(&SaveManager::newGame),
                &SaveManager_newGame_hook,
                &SaveManager_newGame_orig))
            ErrorLog("DefensivePositions: Persistence - could not hook SaveManager::newGame");
    }
}
