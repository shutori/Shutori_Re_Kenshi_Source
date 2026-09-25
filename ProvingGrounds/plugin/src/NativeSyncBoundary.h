#pragma once
namespace ArenaPersistence {
// Native sync samples state once. Its SAVING branch only tests worker status
// and diagnoses a stopped worker (installed RVA 0x4742B2); it cannot complete
// work. Do not let original sync re-sample COMPLETE after our SAVING read.
// A worker finishing here remains COMPLETE until the next normal GUI tick.
template<class Adapter> void RunNativeSync(Adapter& adapter) {
    const int state=adapter.State();
    if(state==Adapter::Saving) {
        if(!adapter.WorkerRunning() && adapter.State()==Adapter::Saving)
            adapter.StoppedWorker();
        return;
    }
    if(state==Adapter::Complete) adapter.ObserveCompletion();
    adapter.NativeSync();
}
}
