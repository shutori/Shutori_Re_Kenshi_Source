#include "ArenaNativePersistence.h"
#include "ArenaSaveCoordinator.h"
#include "ArenaSnapshotFiles.h"
#include "ArenaNativePath.h"
#include "NativeSaveEvidence.h"
#include "NativeSyncBoundary.h"
#include "LeaderboardStore.h"
#include "FighterIdentity.h"
#include "WorldLifecycle.h"
#include "TownArena.h"
#include "CombatBalanceLog.h"
#include "TownDiagnosticRun.h"
#include <windows.h>
#include <rpc.h>
#include <algorithm>
#include <vector>
#include "PGLog.h"
#include <core/Functions.h>
#pragma warning(push)
#pragma warning(disable:4091)
#include <kenshi/GameData.h>
#include <kenshi/GameDataManager.h>
#include <kenshi/GameWorld.h>
#include <kenshi/Globals.h>
#include <kenshi/SaveManager.h>
#include <kenshi/SaveFileSystem.h>
#include <ogre/OgreLogManager.h>
#pragma warning(pop)
#pragma comment(lib,"Rpcrt4.lib")

namespace {
const std::string generationKey("proving_grounds.generation");
const char* sidecarName="proving_grounds_mmr.json";
bool ready=false;
struct Mutex { CRITICAL_SECTION value; Mutex() { InitializeCriticalSection(&value); } ~Mutex() { DeleteCriticalSection(&value); } } mutex;
struct Guard { Guard() { EnterCriticalSection(&mutex.value); } ~Guard() { LeaveCriticalSection(&mutex.value); } };
ArenaPersistence::SnapshotFiles files;
ArenaPersistence::SaveCoordinator coordinator(files);
ArenaPersistence::NativeSaveEvidence evidence;
ArenaPersistence::Snapshot captured;
unsigned long long epoch=0;
bool attempt=false;
std::string notice;
bool resetAllowed=false;
std::string resetPath, resetKey, resetGeneration;
// Native serializers execute synchronously inside SaveManager::saveGame.
// TLS prevents an unrelated thread/container from acquiring that association.
__declspec(thread) unsigned long long activeSave=0;
struct LoadContext {
    bool imported, worldRead, worldOk;
    std::string source, name, generation;
    LoadContext(bool importing,const std::string& key) : imported(importing),worldRead(false),worldOk(true),name(key) {}
};
__declspec(thread) LoadContext* activeLoad=NULL;
struct SaveScope { unsigned long long prior; SaveScope(unsigned long long n):prior(activeSave) { activeSave=n; } ~SaveScope(){activeSave=prior;} };
struct LoadScope { LoadContext* prior; LoadScope(LoadContext* p):prior(activeLoad){activeLoad=p;} ~LoadScope(){activeLoad=prior;} };
void Log(const std::string& text) {
    const std::string line="Proving Grounds persistence: "+text;
    PGLog::Debug(line.c_str());
}
void Report(const std::string& text) { Log(text); Guard lock; notice=text; }
std::string Sidecar(const std::string& folder) {
    if(folder.empty()) return "";
    const char last=folder[folder.size()-1];
    return folder+((last=='/' || last=='\\') ? "" : "/")+sidecarName;
}
std::string SaveFolder(const std::string& location,const std::string& name) {
    if(location.empty() || name.empty()) return location;
    size_t end=location.size();
    while(end && (location[end-1]=='/' || location[end-1]=='\\')) --end;
    const size_t slash=location.find_last_of("/\\",end ? end-1 : 0);
    const size_t start=slash==std::string::npos ? 0 : slash+1;
    if(end-start==name.size() && location.compare(start,name.size(),name)==0)
        return location.substr(0,end);
    return location.substr(0,end)+"/"+name;
}
std::string ParentFolder(const std::string& folder) {
    size_t end=folder.size();
    while(end && (folder[end-1]=='/' || folder[end-1]=='\\')) --end;
    if(!end) return "";
    const size_t slash=folder.find_last_of("/\\",end-1);
    return slash==std::string::npos ? "" : folder.substr(0,slash);
}
bool FindEmergencySnapshot(const LoadContext& context,
    ArenaPersistence::LoadResult& recovered,std::string& recoveredPath,
    std::string& error) {
    error.clear(); recoveredPath.clear();
    if(context.generation.empty()) return false;
    const std::string root=ParentFolder(context.source);
    if(root.empty()) return false;
    std::wstring pattern;
    UINT codepage;
    if(!ArenaPersistence::NativePath::Resolve(root+"/emergency_save_*",
        pattern,codepage,error)) return false;
    WIN32_FIND_DATAW entry;
    HANDLE search=FindFirstFileW(pattern.c_str(),&entry);
    if(search==INVALID_HANDLE_VALUE) {
        const DWORD failure=GetLastError();
        if(failure!=ERROR_FILE_NOT_FOUND && failure!=ERROR_PATH_NOT_FOUND)
            error="Could not inspect emergency save folders (Win32 error "+
                std::to_string(static_cast<unsigned long long>(failure))+")";
        return false;
    }
    std::vector<std::string> candidates;
    do {
        if((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)!=0 &&
            entry.cFileName[0]!=L'.')
            candidates.push_back(ArenaPersistence::NativePath::Utf8(
                ArenaPersistence::NativePath::Parent(pattern)+L"/"+entry.cFileName));
    } while(FindNextFileW(search,&entry));
    const DWORD failure=GetLastError();
    if(!FindClose(search)) {
        error="Could not close emergency save search"; return false;
    }
    if(failure!=ERROR_NO_MORE_FILES) {
        error="Could not inspect emergency save folders (Win32 error "+
            std::to_string(static_cast<unsigned long long>(failure))+")";
        return false;
    }
    std::sort(candidates.begin(),candidates.end());
    bool found=false;
    for(size_t i=0;i<candidates.size();++i) {
        if(candidates[i]==context.source) continue;
        const std::string candidatePath=Sidecar(candidates[i]);
        const ArenaPersistence::LoadResult candidate=
            ArenaPersistence::LoadSnapshot(files,candidatePath,context.name,
                context.generation);
        if(candidate.code==ArenaPersistence::IoFailure) {
            error=candidatePath+" "+candidate.message; return false;
        }
        if(candidate.code!=ArenaPersistence::Ready ||
            candidate.snapshot.saveKey!=context.name)
            continue;
        if(found) {
            error="Multiple emergency arena snapshots match native generation "+
                context.generation; return false;
        }
        recovered=candidate; recoveredPath=candidatePath; found=true;
    }
    return found;
}
bool NewGeneration(std::string& value) {
    UUID uuid; if(UuidCreate(&uuid)!=RPC_S_OK) return false;
    RPC_CSTR text=NULL; if(UuidToStringA(&uuid,&text)!=RPC_S_OK) return false;
    value.assign(reinterpret_cast<const char*>(text)); RpcStringFreeA(&text); return true;
}
GameData* WorldMetadata(GameDataContainer* container) {
    GameData* found=NULL;
    const ogre_unordered_map<int,GameData*>::type& all=container->_getAllData();
    for(ogre_unordered_map<int,GameData*>::type::const_iterator it=all.begin();it!=all.end();++it) {
        if(it->second && it->second->type==CAMERA) { if(found) return NULL; found=it->second; }
    }
    return found;
}
std::string ReadGeneration(GameData* world) {
    if(!world) return "";
    typedef boost::unordered::unordered_map<std::string,std::string,boost::hash<std::string>,std::equal_to<std::string>,Ogre::STLAllocator<std::pair<const std::string,std::string>,Ogre::GeneralAllocPolicy> > Strings;
    const Strings::const_iterator it=world->sdata.find(generationKey);
    return it==world->sdata.end() ? "" : it->second;
}
void PrepareImportedSnapshot(ArenaPersistence::Snapshot& snapshot) {
    // The imported world has no destination save identity until its first
    // native save. Keep durable progression, but discard state tied to the
    // source world's currently announced or active arena session.
    snapshot.saveKey.clear();
    snapshot.generation.clear();
    snapshot.challenges.seed=GetTickCount() | 1u;
    snapshot.challenges.ambientSeed=(GetTickCount() * 1664525u) | 1u;
    snapshot.challenges.playerHistory.clear();
    snapshot.challenges.ambientHistory.clear();
    for(int i=0;i<5;++i) snapshot.challenges.offers[i]=TownChallengePolicy::Offer();
    snapshot.challenges.skarnOffer=TownChallengePolicy::Offer();
    snapshot.challenges.skarnGenerated=false;
    snapshot.challenges.skarnInProgress=false;
    snapshot.challenges.skarnLastEnd=0.0;
    snapshot.diagnostic.pending=TownDiagnosticData::Pending();
    // Import discards the source world's announced lineup. The native Cats
    // still reflect its accepted stake, so carry that stake back as a credit.
    if(snapshot.plannedNpc.wagerPending)
        snapshot.bookieCredit=snapshot.plannedNpc.stake;
    snapshot.plannedNpc=ArenaPersistence::PlannedNpcBout();
}
// Only called under mutex. Storage operations are serialized against world
// abandonment; no original engine calls, UI calls or logging happen here.
std::string FinishIfReady() {
    if(!attempt || !evidence.Ready()) return "";
    const bool nativeOk=evidence.Succeeded();
    std::string key, keyError;
    if(nativeOk && !ArenaPersistence::SnapshotFiles::SaveKey(
        evidence.target,captured.saveKey,key,keyError))
        keyError="Cannot resolve Unicode save identity: "+keyError;
    if(nativeOk && keyError.empty()) coordinator.SetSaveKey(key);
    const bool published=coordinator.FinishSave(nativeOk && keyError.empty());
    attempt=false;
    if(published) return "";
    const std::string reason=evidence.identityChecked && !evidence.identityOk ?
        "fighter identity metadata could not be persisted; the identity adapter must be repaired/restarted before later arena saves can succeed" :
        (!keyError.empty() ? keyError : coordinator.Error());
    return "Arena portion of save FAILED at "+Sidecar(evidence.target)+": "+reason+
        ". Live arena progress is retained. Preserve this slot and its backups; retry saving before leaving this world.";
}
int (*saveOriginal)(SaveManager*,const std::string&,const std::string&)=NULL;
int (*loadOriginal)(SaveManager*,const std::string&,const std::string&)=NULL;
int (*importOriginal)(SaveManager*,const std::string&,const std::string&,int)=NULL;
bool (*containerSaveOriginal)(GameDataContainer*,const std::string&,Serialisable*)=NULL;
bool (*containerLoadOriginal)(GameDataContainer*,const std::string&,const std::string&,int,Serialisable*,bool)=NULL;
bool (*filesystemSaveOriginal)(SaveFileSystem*,const std::string&)=NULL;
void (*filesystemLoadOriginal)(SaveFileSystem*,const std::string&)=NULL;
void (*syncOriginal)(SaveFileSystem*)=NULL;

int Save(SaveManager* self,const std::string& location,const std::string& name) {
    if(!ready) return saveOriginal(self,location,name);
    ArenaPersistence::Snapshot snapshot; std::string generation;
    const bool identityReady=FighterIdentity::CanPersist();
    if(!identityReady || !LeaderboardStore::CaptureSnapshot(snapshot) ||
        !TownArena::CapturePlannedNpcBout(snapshot.plannedNpc) || !NewGeneration(generation)) {
        ArenaNativePersistence::AbandonWorld();
        std::string reason=identityReady ? LeaderboardStore::GetPersistenceError() :
            "fighter identity persistence is unhealthy; preserve this slot/backups and restart the game before reloading a known-good slot";
        if(reason.empty()) reason="the arena ledger, announced lineup identity, or snapshot generation could not be captured";
        Report("Arena portion of save unavailable: "+reason+". Wait for any reward delivery to finish, then retry saving. Native game save will continue; arena data will not be published.");
        return saveOriginal(self,location,name);
    }
    snapshot.saveKey=name; snapshot.generation=generation;
    unsigned long long id;
    { Guard lock; coordinator.AbandonWorld(); id=++epoch; attempt=true; captured=snapshot;
      evidence=ArenaPersistence::NativeSaveEvidence(); evidence.generation=generation; evidence.IdentityPersisted(identityReady); }
    SaveScope scope(id);
    const int result=saveOriginal(self,location,name);
    const bool identityPersisted=FighterIdentity::CanPersist();
    std::string error;
    { Guard lock; if(attempt && epoch==id) {
        evidence.IdentityPersisted(identityPersisted);
        evidence.managerReturned=true; evidence.managerResult=result;
        if(!evidence.scheduled) {
            coordinator.AbandonWorld(); attempt=false;
            error="Arena portion of save FAILED: native copying was not scheduled. Live progress is retained.";
        } else error=FinishIfReady();
    } }
    if(!error.empty()) Report(error);
    return result;
}
bool ContainerSave(GameDataContainer* self,const std::string& path,Serialisable* more) {
    const unsigned long long id=activeSave;
    std::string generation;
    { Guard lock; if(ready && attempt && id && id==epoch) generation=evidence.generation; }
    if(!generation.empty() && ou && self==&ou->savedata) {
        GameData* world=WorldMetadata(self);
        if(world) world->addString(generationKey,generation,"",false);
        const bool written=world && ReadGeneration(world)==generation;
        Guard lock; if(attempt && id==epoch) evidence.metadataWritten=written;
    }
    const bool result=containerSaveOriginal(self,path,more);
    { Guard lock; if(ready && attempt && id && id==epoch) evidence.Serialized(result); }
    return result;
}
bool FilesystemSave(SaveFileSystem* self,const std::string& destination) {
    const unsigned long long id=activeSave;
    // Copy the actual full native argument before any engine call can mutate it.
    const std::string target=destination;
    { Guard lock;
      if(ready && attempt && id && id==epoch && evidence.target.empty()) {
        evidence.filesystem=self; evidence.target=target;
        coordinator.BeginSave(Sidecar(target),evidence.generation,captured);
      } else { coordinator.AbandonWorld(); attempt=false; ++epoch; }
    }
    const bool result=filesystemSaveOriginal(self,destination);
    { Guard lock; if(attempt && id==epoch) evidence.scheduled=result; }
    return result;
}
struct NativeSyncAdapter {
    enum { Saving=SaveFileSystem::SAVING, Complete=SaveFileSystem::COMPLETE };
    SaveFileSystem* filesystem;
    std::string error;
    explicit NativeSyncAdapter(SaveFileSystem* value) : filesystem(value) {}
    int State() const { return filesystem->state; }
    bool WorkerRunning() const { return filesystem->isRunning(); }
    void StoppedWorker() {
        // Native sync's SAVING/non-running branch only logs; preserve a visible
        // diagnostic without letting it re-read and consume COMPLETE.
        bool failed=false;
        { Guard lock; if(attempt && evidence.filesystem==filesystem) {
            coordinator.AbandonWorld(); attempt=false; ++epoch; failed=true;
        } }
        if(failed) error="Arena portion of save FAILED: native save worker stopped before completion. Live progress is retained; preserve this slot and retry saving.";
    }
    void ObserveCompletion() {
        const std::string target=filesystem->currentSave;
        const std::string copyError=filesystem->failedToCopyError;
        const bool identityPersisted=FighterIdentity::CanPersist();
        Guard lock;
        if(attempt) evidence.IdentityPersisted(identityPersisted);
        if(attempt && evidence.Complete(filesystem,target,copyError)) error=FinishIfReady();
        else if(attempt && evidence.filesystem==filesystem) {
            coordinator.AbandonWorld(); attempt=false; ++epoch;
        }
    }
    void NativeSync() { syncOriginal(filesystem); }
};
void Sync(SaveFileSystem* self) {
    if(!ready) { syncOriginal(self); return; }
    NativeSyncAdapter adapter(self);
    ArenaPersistence::RunNativeSync(adapter);
    if(!adapter.error.empty()) Report(adapter.error);
}
void FilesystemLoad(SaveFileSystem* self,const std::string& source) {
    if(activeLoad) activeLoad->source=source;
    filesystemLoadOriginal(self,source);
}
bool ContainerLoad(GameDataContainer* self,const std::string& path,const std::string& mod,int index,Serialisable* more,bool keep) {
    const bool result=containerLoadOriginal(self,path,mod,index,more,keep);
    if(activeLoad && ou && self==&ou->savedata) {
        activeLoad->worldRead=true;
        GameData* world=result ? WorldMetadata(self) : NULL;
        activeLoad->worldOk=activeLoad->worldOk && result && world!=NULL;
        if(world) activeLoad->generation=ReadGeneration(world);
    }
    return result;
}
void ActivateLoad(const LoadContext& context,int result) {
    if(result!=0) {
        if(result!=2) { LeaderboardStore::BlockPersistence("Native world load failed"); Report("Native world load failed; arena persistence remains blocked."); }
        return;
    }
    if(!context.worldRead || !context.worldOk || context.source.empty()) {
        LeaderboardStore::BlockPersistence("Native world metadata/source was unavailable");
        Report("Cannot activate arena progress: native world metadata/source was unavailable. Reload a known-good slot; arena data will not be overwritten."); return;
    }
    const std::string path=Sidecar(context.source);
    std::string loadedPath=path;
    std::string saveKey, keyError;
    if(!ArenaPersistence::SnapshotFiles::SaveKey(context.source,context.name,
        saveKey,keyError)) {
        LeaderboardStore::BlockPersistence(keyError);
        Report("Arena progress blocked: "+keyError+". Preserve the save and reload after correcting its path.");
        return;
    }
    ArenaPersistence::LoadResult loaded=ArenaPersistence::LoadSnapshot(files,path,saveKey,context.generation);
    std::string emergencyError;
    if(!context.imported && loaded.code==ArenaPersistence::WrongSnapshot) {
        ArenaPersistence::LoadResult emergency;
        std::string emergencyPath;
        LoadContext normalized=context; normalized.name=saveKey;
        if(FindEmergencySnapshot(normalized,emergency,emergencyPath,
            emergencyError)) {
            loaded=emergency; loadedPath=emergencyPath;
            loaded.message="Recovered generation-matched arena progress from emergency save "+emergencyPath;
        }
    }
    if(loaded.code==ArenaPersistence::Ready) {
        ArenaPersistence::Snapshot snapshot=loaded.snapshot;
        if(context.imported) PrepareImportedSnapshot(snapshot);
        else {
            // Emergency saves clone native metadata and the sidecar without
            // changing its old slot name. The generation is the authoritative
            // identity; bind the recovered snapshot to the slot now loaded.
            snapshot.saveKey=saveKey;
            snapshot.generation=context.generation;
        }
        LeaderboardStore::ActivateSnapshot(snapshot);
        if(!context.imported) {
            TownArena::RestorePlannedNpcBout(snapshot.plannedNpc);
            if(!snapshot.plannedNpc.active) TownDiagnosticRun::RecoverPendingAfterLoad();
        }
        Log(std::string(context.imported ? "Imported" : "Loaded")+
            " arena snapshot from "+loadedPath+" generation="+context.generation+
            (context.imported ? "; destination identity will bind on first save" : ""));
        if(!loaded.snapshot.diagnosticError.empty()) {
            const std::string fields=",\"error\":\"invalid diagnostic state; see arena persistence log\"";
            CombatBalanceLog::DiagnosticEvent("diagnostic_persistence_error",fields);
            Report("Diagnostic selection disabled: "+loaded.snapshot.diagnosticError+". Ordinary arena matchmaking remains active.");
        }
        if(!loaded.message.empty()) Report(loaded.message+". The primary JSON remains untouched; preserve both files until you verify the recovered arena progress.");
        return;
    }
    if(!emergencyError.empty()) {
        const std::string error=path+" emergency recovery: "+emergencyError;
        LeaderboardStore::BlockPersistence(error);
        Report("Arena progress blocked: "+error+". Preserve the file/backups and reload a known-good save.");
        return;
    }
    if(loaded.code==ArenaPersistence::Missing || loaded.code==ArenaPersistence::LegacyReset) {
        ArenaPersistence::Snapshot fresh; fresh.saveKey=saveKey;
        LeaderboardStore::ActivateSnapshot(fresh);
        if(loaded.code==ArenaPersistence::LegacyReset) Report("Legacy arena progress reset. Original JSON will be backed up when this slot is next saved.");
        else if(context.imported) Log("Imported world has no matching arena sidecar; activated a fresh arena ledger.");
        return;
    }
    const std::string error=path+" "+loaded.field+": "+loaded.message;
    if(loaded.code==ArenaPersistence::WrongSnapshot) {
        Guard lock; resetAllowed=true; resetPath=path;
        resetKey=saveKey; resetGeneration=context.generation;
    }
    LeaderboardStore::BlockPersistence(error);
    Report("Arena progress blocked: "+error+". Preserve the file/backups or use the leaderboard's explicit reset to start arena progress from zero for this slot.");
}
int Load(SaveManager* self,const std::string& location,const std::string& name) {
    if(!ready) return loadOriginal(self,location,name);
    WorldLifecycle::AbandonWorldState();
    LoadContext context(false,name); LoadScope scope(&context);
    const int result=loadOriginal(self,location,name); ActivateLoad(context,result); return result;
}
int Import(SaveManager* self,const std::string& location,const std::string& name,int flags) {
    if(!ready) return importOriginal(self,location,name,flags);
    WorldLifecycle::AbandonWorldState();
    LoadContext context(true,name);
    // Unlike a normal load, Kenshi's import path reads quick.save directly and
    // does not call SaveFileSystem::loadGame. SaveInfo supplies the save root
    // and slot name separately, so rebuild the complete source folder here.
    context.source=SaveFolder(location,name);
    LoadScope scope(&context);
    const int result=importOriginal(self,location,name,flags); ActivateLoad(context,result); return result;
}
}
namespace ArenaNativePersistence {
void Disable() { ready=false; AbandonWorld(); LeaderboardStore::BlockPersistence("Required native lifecycle/identity hooks are unavailable"); }
void AbandonWorld() { Guard lock; ++epoch; attempt=false; coordinator.AbandonWorld(); evidence=ArenaPersistence::NativeSaveEvidence(); captured=ArenaPersistence::Snapshot(); notice.clear(); resetAllowed=false; resetPath.clear(); resetKey.clear(); resetGeneration.clear(); }
bool IsLoading() { return activeLoad!=NULL; }
bool IsReady() { return ready; }
bool CanResetUnmatchedSidecar() { Guard lock; return resetAllowed; }
bool ResetUnmatchedSidecar() {
    Guard lock;
    if(!ready || !resetAllowed) return false;
    const ArenaPersistence::LoadResult check=ArenaPersistence::LoadSnapshot(
        files,resetPath,resetKey,resetGeneration);
    if(check.code!=ArenaPersistence::WrongSnapshot) return false;
    ArenaPersistence::Snapshot fresh;
    fresh.saveKey=resetKey; fresh.generation=resetGeneration;
    resetAllowed=false; resetPath.clear(); resetKey.clear(); resetGeneration.clear();
    LeaderboardStore::ActivateSnapshot(fresh);
    Log("Player explicitly reset unmatched arena progress; save this slot now to publish a matching sidecar.");
    return true;
}
void Tick() { std::string text; { Guard lock; text.swap(notice); } if(!text.empty() && ou) ou->showPlayerAMessage(text,true); }
bool InstallHooks() {
    bool ok=true;
#define PG_HOOK(method, hook, original) ok=(KenshiLib::SUCCESS==KenshiLib::AddHook(KenshiLib::GetRealAddress(method),hook,&original)) && ok
    PG_HOOK(&SaveManager::saveGame,Save,saveOriginal);
    PG_HOOK(&SaveManager::loadGame,Load,loadOriginal);
    PG_HOOK(&SaveManager::importGame,Import,importOriginal);
    PG_HOOK(&GameDataContainer::save,ContainerSave,containerSaveOriginal);
    PG_HOOK(&GameDataContainer::load,ContainerLoad,containerLoadOriginal);
    PG_HOOK(&SaveFileSystem::saveGame,FilesystemSave,filesystemSaveOriginal);
    PG_HOOK(&SaveFileSystem::loadGame,FilesystemLoad,filesystemLoadOriginal);
    PG_HOOK(&SaveFileSystem::sync,Sync,syncOriginal);
#undef PG_HOOK
    ready=ok;
    if(!ok) LeaderboardStore::BlockPersistence("Native persistence hook installation failed");
    Log(ok ? "Native save/load candidate hooks installed; runtime acceptance pending." : "Native persistence hook installation FAILED; arena publication disabled.");
    return ok;
}
}
