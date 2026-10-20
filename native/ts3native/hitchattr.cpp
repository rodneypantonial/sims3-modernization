// [HitchAttr] (0.34.10, docs/96 §9, counters v66): how much of each sim frame the sim thread spent waiting for the route job.
// Observe only, no hook: the four places where the job owner (the sim thread) already times a wait for the job call HaNote
// after the wait. Classes (HA_*): 0 the routing lock world+0x918 (LockAcquireAsync pump; gate waits from RgEnter land here
// keyed by the leaf's caller, docs/84 keys), 1 another game lock taken while a job runs or is queued (same pump), 2 a wait
// for the running job itself (RtWaitJob: destroy / drain keys 0x02xxxxxx; the discourage drain 0x06000000), 3 a plan that
// waited for the job first (RunPlan, key 0x05000000).
// Frames: tools/frida/stutter-profile.js reads `ts3native_hitchattr` at each iteration boundary (GC_try_to_collect entry, on
// the sim thread) and zeroes it, so the frame split needs no clock fit: hitches.jsonl gets rtLock/rtOtherLock/rtJob/rtPlanWait
// ms, rtWaits, rtMaxMs, rtMaxKey; seconds.csv the same sums per second. tools/harness/hitch-attr-report.ts joins them.
// shortcut: a wait nested inside another wait's pump (a managed callback for the worker that waits again) is counted twice;
// H6 sends plans made in such callbacks to the worker and borrows the lock, so it is rare; split it if rtWaits ever shows it.
// Included by ts3native.cpp.
// HA_* classes: enum next to WaitNote in ts3native.cpp (the hook lines come before this file)
struct HaFrame {
    LONG version;      // +0x00, 1
    LONG enabled;      // +0x04, [HitchAttr] Enabled
    LONG us[HA_N];     // +0x08, wait us by class since the reader last zeroed this frame block
    LONG n;            // +0x18, waits
    LONG maxUs;        // +0x1C, the longest of them
    LONG maxKey;       // +0x20, its key
};
extern "C" __declspec(dllexport) HaFrame ts3native_hitchattr = {1, 0, {0, 0, 0, 0}, 0, 0, 0};

static void HaNote(DWORD key, LONG us, int cls) {
    if (!ts3native_hitchattr.enabled || us <= 0 || GetCurrentThreadId() != g_async.owner) return;
    HaFrame& f = ts3native_hitchattr;  // the owner thread only (the reader zeroes it on the same thread)
    f.us[cls] += us;
    f.n++;
    if (us > f.maxUs) f.maxUs = us, f.maxKey = (LONG)key;
    Ts3nCounters& k = ts3native_counters;
    InterlockedIncrement(&k.haWaits[cls]);
    InterlockedExchangeAdd(&k.haUs[cls], us);
    Max(&k.haMaxUs, us);
}

static bool ApplyHitchAttr(const wchar_t* ini) {
    if (!GetPrivateProfileIntW(L"HitchAttr", L"Enabled", 0, ini)) return false;
    ts3native_hitchattr.enabled = 1;
    ts3native_counters.haReady = 1;
    Log("OK   HitchAttr: sim waits for the route job per frame (ts3native_hitchattr, read by stutter-profile.js)");
    return true;
}
