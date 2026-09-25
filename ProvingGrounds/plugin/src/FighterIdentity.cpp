#include "FighterIdentity.h"
#include "FighterIdentityPolicy.h"

#include <windows.h>
#include <rpc.h>
#include "PGLog.h"
#include <core/Functions.h>
#pragma warning(push)
#pragma warning(disable: 4091)
#include <kenshi/GameData.h>
#include <kenshi/GameDataManager.h>
#include <kenshi/GameSaveState.h>
#include <kenshi/Character.h>
#include <ogre/OgreLogManager.h>
#pragma warning(pop)

#pragma comment(lib, "Rpcrt4.lib")

// Enable the candidate adapter for user-owned in-game testing. This build
// switch is not a validation claim: the native runtime matrix remains pending.
#ifndef PG_ENABLE_FIGHTER_IDENTITY
#define PG_ENABLE_FIGHTER_IDENTITY 1
#endif

namespace
{
    const std::string kIdentityKey("proving_grounds.fighter_id");
    using FighterIdentityPolicy::Lifetime;
    FighterIdentityPolicy::Registry registry;
    bool ready = false;
    bool healthy = true;
    bool attempted = false;

    struct RegistryMutex
    {
        CRITICAL_SECTION value;
        RegistryMutex() { InitializeCriticalSection(&value); }
        ~RegistryMutex() { DeleteCriticalSection(&value); }
    } mutex;
    struct Guard
    {
        Guard() { EnterCriticalSection(&mutex.value); }
        ~Guard() { LeaveCriticalSection(&mutex.value); }
    private:
        Guard(const Guard&);
        Guard& operator=(const Guard&);
    };

    void Log(const std::string& message)
    {
        const std::string line = "Proving Grounds fighter identity: " + message;
        PGLog::Debug(line.c_str());
    }

    bool NewId(std::string& id)
    {
        id.clear();
        UUID uuid;
        const RPC_STATUS status = UuidCreate(&uuid);
        // RPC_S_UUID_LOCAL_ONLY cannot promise uniqueness across machines.
        if (status != RPC_S_OK) return false;
        RPC_CSTR text = NULL;
        if (UuidToStringA(&uuid, &text) != RPC_S_OK) return false;
        id.assign(reinterpret_cast<const char*>(text));
        RpcStringFreeA(&text);
        return !id.empty();
    }

    GameData* InstanceState(GameSaveState* save)
    {
        GameData* state = save ? save->getState(GAMESTATE_CHARACTER) : NULL;
        return state && state != save->baseData && state->type == GAMESTATE_CHARACTER ? state : NULL;
    }

    std::string ReadId(GameData* state)
    {
        if (!state) return std::string();
        typedef boost::unordered::unordered_map<std::string, std::string,
            boost::hash<std::string>, std::equal_to<std::string>,
            Ogre::STLAllocator<std::pair<std::string const, std::string>, Ogre::GeneralAllocPolicy> > Strings;
        const Strings::const_iterator found = state->sdata.find(kIdentityKey);
        return found == state->sdata.end() ? std::string() : found->second;
    }

    // Verified installed x64 member ABI: RCX=this, RDX=hidden result,
    // R8=container, R9=refs, stack+0x28=offset; RAX=result. Do not return
    // GameSaveState by value from this free function (different hidden sret).
    typedef GameSaveState* (*SerialiseFn)(Character*, GameSaveState*, GameDataContainer*, GameData*, PosRotPair*);
    SerialiseFn serialiseOriginal = NULL;
    void (*loadOriginal)(Character*, GameSaveState*) = NULL;
    void (*destroyOriginal)(Character*) = NULL;

    GameSaveState* Serialise(Character* self, GameSaveState* result,
        GameDataContainer* container, GameData* refs, PosRotPair* offset)
    {
        Lifetime lifetime = 0;
        {
            Guard lock;
            if (ready) lifetime = registry.Current(self);
        }
        // Native callbacks may reenter load/destruction: never hold our lock.
        GameSaveState* out = serialiseOriginal(self, result, container, refs, offset);
        std::string id;
        {
            Guard lock;
            if (!ready || !registry.SavedId(self, lifetime, id)) return out;
        }
        GameData* state = InstanceState(out);
        if (!state)
        {
            // A missing owner means future persistence is no longer assured.
            { Guard lock; healthy = false; }
            Log("missing GAMESTATE_CHARACTER instance state; progression disabled");
            return out;
        }
        state->addString(kIdentityKey, id, "", false);
        if (ReadId(state) != id)
        {
            { Guard lock; healthy = false; }
            Log("identity metadata write failed; progression disabled");
        }
        return out;
    }

    void Load(Character* self, GameSaveState* save)
    {
        bool active;
        { Guard lock; active = ready; }
        if (active)
        {
            // The native loader may destroy save; capture and bind beforehand.
            const std::string id = ReadId(InstanceState(save));
            bool conflict = false;
            {
                Guard lock;
                if (ready)
                {
                    registry.Load(self, save != NULL, id, conflict);
                }
            }
            if (conflict) Log("duplicate live fighter ID " + id + "; both careers blocked until corrected reload");
        }
        loadOriginal(self, save);
    }

    void Destroy(Character* self)
    {
        {
            Guard lock;
            // Non-deleting Character::_DESTRUCTOR, not a deleting destructor
            // with an extra flags argument. Erase before native destruction.
            registry.End(self, registry.Current(self));
        }
        destroyOriginal(self);
    }
}

namespace FighterIdentity
{
    bool InstallHooks()
    {
        {
            Guard lock;
            if (attempted) return ready && healthy;
            attempted = true;
        }
#if !PG_ENABLE_FIGHTER_IDENTITY
        Log("native adapter disabled in this build");
        return false;
#else
        // KenshiLib exposes no rollback API. Already-installed detours remain
        // inert passthroughs if any subsequent install fails; no retries.
        bool ok = KenshiLib::SUCCESS == KenshiLib::AddHook(
            KenshiLib::GetRealAddress(&Character::_DESTRUCTOR), Destroy, &destroyOriginal);
        if (ok) ok = destroyOriginal && KenshiLib::SUCCESS == KenshiLib::AddHook(
            KenshiLib::GetRealAddress(&Character::_NV_loadFromSerialise), Load, &loadOriginal);
        if (ok) ok = loadOriginal && KenshiLib::SUCCESS == KenshiLib::AddHook(
            KenshiLib::GetRealAddress(&Character::_NV_serialise), Serialise, &serialiseOriginal);
        ok = ok && serialiseOriginal != NULL;
        { Guard lock; ready = ok; }
        Log(ok ? "hooks ready" : "hook installation failed; progression disabled");
        return ok;
#endif
    }

    bool CanPersist()
    {
        Guard lock;
        return ready && healthy;
    }

    bool GetOrCreate(Character* character, std::string& id)
    {
        id.clear();
        Lifetime lifetime;
        {
            Guard lock;
            if (!ready || !healthy || !character) return false;
            lifetime = registry.Current(character);
            if (!lifetime) lifetime = registry.Begin(character);
            std::string existing;
            if (registry.SavedId(character, lifetime, existing))
                return registry.Find(character, lifetime, id);
        }
        std::string generated;
        if (!NewId(generated)) { Log("Windows UUID generation failed"); return false; }
        bool conflict = false, found = false;
        {
            Guard lock;
            if (!ready || !healthy || registry.Current(character) != lifetime) return false;
            std::string existing;
            if (!registry.SavedId(character, lifetime, existing))
                registry.Bind(character, lifetime, generated, conflict);
            found = registry.Find(character, lifetime, id);
        }
        if (conflict) Log("duplicate generated fighter ID " + generated + "; careers blocked");
        return found;
    }

    bool Find(Character* character, std::string& id)
    {
        id.clear();
        Guard lock;
        return ready && healthy && character && registry.Find(character, registry.Current(character), id);
    }

    void AbandonWorldState()
    {
        Guard lock;
        registry.Clear();
    }
}
