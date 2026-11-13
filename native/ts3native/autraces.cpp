// [AutShadow] Races (docs/data/races-handoff.md R2c), default 0. Release backport (rc1111g, 0.36.247) of feature/races-r2c
// 0.36.244 with bit 2 only: the R1b pre-warm (bit 0) and the R2b/R3a witness (bit 1) are not in this build, those bits are
// ignored (masked off at load).
// Bit 2 (R2c): the worker never spans a world teardown or swap. World events 2 (leave not-in-world), 3 (world quit),
//   4 (quit app), 5 (load started) and 8 (load failed) (Sims3.SimIFace.WorldEvent) close the worker gate on the sim
//   thread, in routesnap's World_FireEvent detour before the game's handlers run. A job in flight gets the stop flag and
//   its result becomes an epoch drop; the sim then waits up to DrainMs for it to leave the worker (drain). Past the bound
//   the sim goes on (abort fallback): the stop flag and the closed gate make every later IC answer of that job abort, so
//   it reads no game state, and the job leaves on its own (r2cLate). While the gate is closed there is no cut (a posted
//   request finishes as an epoch drop). Event 7 opens it; the epoch bump at 7 makes the next cut rebuild the snapshot.
//   Travel and the return trip are the same sequence (3, 5, 7). Q5 (abort vs drain) was open in the handoff: drain with a
//   bounded wait and abort fallback, both logged. Before R2c the old path only set the stop flag at the next Simulate top,
//   never under Satb act, and not at all for an event fired inside the window (frame limiter).
// Household rule: no Sim, household or object state is read or written here; process globals only (handoff §2). R2c only
// drops worker answers (the game scores the request itself), so household and selected Sims see vanilla.

static int g_arOn;               // ini [AutShadow] Races (bit 2 only in this build)
static volatile LONG g_arGate;   // 1: world going away (events 2/3/4/5/8 until 7): worker answers abort, no cut
static int g_arDrainMs = 250;    // ini [AutShadow] DrainMs
static bool g_arLatePending;     // an aborted drain's job has not left the worker yet
static bool AsDrainBusy();       // autshadow-run.cpp: our job is inside the worker
static bool AsDrainStop();       // autshadow-run.cpp: stop flag + epoch drop for it; true = it runs under Satb act

static bool ArGoingAway(DWORD id) { return id == 2 || id == 3 || id == 4 || id == 5 || id == 8; }

// sim thread, World_FireEvent entry
static void ArDrain(DWORD id) {
    Ts3nCounters& c = ts3native_counters;
    if (!g_arGate) {
        InterlockedExchange(&g_arGate, 1), c.r2cCloses++;
        Log("autshadow races R2c: world event %lu closes the worker gate", id);
    }
    if (!AsDrainBusy()) { c.r2cIdle++; return; }
    c.r2cDrainReq++;
    if (AsDrainStop()) c.r2cSatb++;
    LARGE_INTEGER f, t0, t;
    QueryPerformanceFrequency(&f), QueryPerformanceCounter(&t0);
    LONG us = 0;
    for (int spin = 0;; spin++) {
        QueryPerformanceCounter(&t);
        us = (LONG)((t.QuadPart - t0.QuadPart) * 1000000 / f.QuadPart);
        if (!AsDrainBusy() || us >= g_arDrainMs * 1000) break;
        if (spin < 64) YieldProcessor();
        else if (spin < 256) SwitchToThread();
        else Sleep(1);
    }
    const bool left = !AsDrainBusy();
    c.r2cWaitUs += us;
    if (us > c.r2cWaitMaxUs) c.r2cWaitMaxUs = us;
    if (left) {
        c.r2cDrained++;
        Log("OK   autshadow races R2c: world event %lu, worker job drained in %ld us", id, us);
    } else {
        c.r2cAborted++, g_arLatePending = true;
        Log("WARN autshadow races R2c: world event %lu, worker job still inside after %d ms: abort (stop flag set, every answer of "
            "it refused, result dropped)", id, g_arDrainMs);
    }
}

// sim thread, AsSimLeave: our job left the worker (before its finish)
static void ArJobLeft() {
    if (!g_arLatePending) return;
    g_arLatePending = false, ts3native_counters.r2cLate++;
    Log("autshadow races R2c: the aborted worker job left the worker (gate %s)", g_arGate ? "closed" : "open");
}

static void ArWorldEvent(DWORD id) {
    if (!(g_arOn & 4)) return;
    if (ArGoingAway(id)) ArDrain(id);
    if (id == 7 && g_arGate) {  // no R1 pre-warm in this build: event 7 reopens on its own
        InterlockedExchange(&g_arGate, 0), ts3native_counters.r2cReopens++;
        Log("autshadow races R2c: world event 7 opens the worker gate");
    }
}

static void ApplyAutRaces(const wchar_t* ini) {
    const int raw = GetPrivateProfileIntW(L"AutShadow", L"Races", 0, ini);
    g_arOn = raw & 4;
    g_arDrainMs = max(1, min(5000, (int)GetPrivateProfileIntW(L"AutShadow", L"DrainMs", 250, ini)));
    ts3native_counters.r2cOn = g_arOn ? 1 : 0, ts3native_counters.r2cDrainMs = g_arDrainMs;
    g_arGate = g_arOn ? 1 : 0;  // no world yet: closed until the first event 7
    Log("autonomy shadow: Races=%d (in force %d: bit 2 R2c worker drain at world events 2/3/4/5/8, DrainMs=%d; bits 0/1 R1b/R2b "
        "not in this build; docs/data/races-handoff.md)", raw, g_arOn, g_arDrainMs);
}
