// [AutShadow] part 1 (docs/122 §2, §3, §12.5 F2; gate G-A1, docs/122 §13): IC guard thunks, the snapshot builder and its
// change tracking, and the F2 abort at String.ParseFormatSpecifier. Part 2 (autshadow-run.cpp, docs/122 §14): the cut
// at [MonoThread]'s Simulate slot, the reference run and worker dispatch, the AutShadowNative ICs for ts3fixes
// AutShadow, the rooted column for managed answers (AsTab::refs) and the managed-array allocator (AsNewArrayGc).
// Included by ts3native.cpp after routesnap.cpp: routesnap's World_FireEvent detour and its two build/buy IC slots call
// AsWorldEvent / AsEpochBump (one more consumer, routing behaviour unchanged), and our object-event nodes go in through
// routesnap's g_snObjReg at world event 7, like routesnap's own.
//
// Guards (docs/122 §3.1): every {fn, name} pair of the exe's IC provider tables gets a 10-byte thunk `push index; jmp entry`
// written into the table before MonoScriptHost_Init (DllMain time). Any thread but the worker: the thunk tail-jumps to
// what the slot held (the game body, or routesnap's slot stub in front of it: the thunk sits in front, routesnap keeps
// its slot). The worker: the snapshot answer, a stub, or an abort with the neutral value for the IC's return kind
// (autshadow-kinds.h: fldz for float/double, the hidden result pointer for structs, else 0 in edx:eax). Session-memo
// ICs (docs/122 §2.2) go through the slow entry on every thread: the sim thread's own call is recorded (key -> answer).
//
// Snapshot (docs/122 §2): one generic answer table. An entry is (IC, input argument dwords) -> the answer the game's own
// body gave at the cut (return value, struct bytes, out parameters, array contents), so worker answers are bit-equal by
// construction. Object rows = the entries owned by an object id (the request's rows from AsAddRow plus learned keys);
// lot table, door room pairs and geometry = entries owned by the world (rebuilt on the epoch); per-lot lists = entries
// keyed (type, lot, flags) with the list generation; constants = zero-argument entries; the memo = entries recorded on
// the sim thread's pass-through. Master table on the sim thread, two published versions (A/B) for the worker.
#include "autshadow-kinds.h"

typedef unsigned __int64 AsU64;

// Counters: separate export like ts3native_mtprobe (docs/registry.md "ts3native_autshadow").
struct Ts3nAutShadow {
    LONG version;         // +0x00, 1
    LONG ready;           // +0x04, bit 0 guards written, 1 F2 detour, 2 change tracking (routesnap's World_FireEvent detour), 3 object-event nodes
    LONG guardSlots;      // +0x08, thunks written
    LONG guardChained;    // +0x0C, of those, slots that held routesnap's stub (the thunk sits in front)
    LONG guardSkipped;    // +0x10, table entries left alone (Guard=closure, table full, write failed)
    LONG descIcs;         // +0x14, descriptor ICs found in the tables (of AS_NDESC)
    LONG guardSim;        // +0x18, IC calls passed through on other threads (copied at each cut)
    LONG guardWorker;     // +0x1C, IC calls on the worker
    LONG workerHits;      // +0x20
    LONG workerMisses;    // +0x24
    LONG cuts;            // +0x28
    LONG cutUs;           // +0x2C, sum
    LONG cutMaxUs;        // +0x30
    LONG entries;         // +0x34, snapshot entries after the last cut
    LONG refreshed;       // +0x38, body calls by the builder
    LONG learned;         // +0x3C, keys added from worker misses
    LONG learnDropped;    // +0x40, learned keys dropped (100 cuts unused, ring or table full)
    LONG epochs;          // +0x44, full rebuilds
    LONG objEvents;       // +0x48
    LONG dirtyOverflow;   // +0x4C, dirty set full -> epoch
    LONG listGen;         // +0x50, per-lot list generation
    LONG memoRecords;     // +0x54
    LONG auditN;          // +0x58
    LONG auditMis[4];     // +0x5C, S1..S4 (part 1 counts every mismatch as S1)
    LONG auditSkipped;    // +0x6C, managed-reference answers (identity, not content)
    LONG snapBytes;       // +0x70, last published version
    LONG full;            // +0x74, inserts refused (entry table or payload pool full)
    LONG abortTotal;      // +0x78
    LONG abort[20];       // +0x7C, by reason AS_AB_*
    // v2: part 2 (docs/122 §14): request, cut, reference, worker, window
    LONG mode;            // +0xCC, Mode in force (-1 = no cut: [MonoThread] Simulate slot stub missing)
    LONG req;             // +0xD0, requests posted
    LONG busy;            // +0xD4, Begin refused (request pending or run in flight)
    LONG cutReq;          // +0xD8, cuts made for a request
    LONG refRuns;         // +0xDC, reference runs (sim thread, RNG saved and restored)
    LONG refUs;           // +0xE0, their time (sum, us)
    LONG refMaxUs;        // +0xE4
    LONG refExc;          // +0xE8, reference runs that returned a managed exception
    LONG rngDraws;        // +0xEC, reference runs that moved the script RNG (restored after)
    LONG workRuns;        // +0xF0, worker runs submitted
    LONG workUs;          // +0xF4, worker time (sum, us)
    LONG workMaxUs;       // +0xF8
    LONG workExc;         // +0xFC, worker runs that returned a managed exception
    LONG workRefused;     // +0x100, submit refused or the worker skipped the job (EA memory tracking)
    LONG noWorker;        // +0x104, requests with no worker run (Mode 0, or the M5 worker not idle)
    LONG windowUs;        // +0x108, quiescent windows (cut end to the next Simulate entry; sum, us)
    LONG windowMaxUs;     // +0x10C
    LONG overrun;         // +0x110, worker still inside at the next Simulate entry (stop set, A5)
    LONG epochDrops;      // +0x114, runs dropped by an epoch bump (A5)
    LONG finishRuns;      // +0x118, finish invocations (sim thread)
    LONG finishExc;       // +0x11C
    LONG stale;           // +0x120, entries published stale at the last cut (Sims the request cannot reach)
    LONG rooted;          // +0x124, bit 0 ref column + pools rooted (GC_add_roots), 1 array allocator, 2 empty string, 3 F2 exception
    LONG icRegistered;    // +0x128, AutShadowNative ICs registered (mono_add_internal_call)
    LONG logLines;        // +0x12C, lines written to autshadow.jsonl
    LONG waitTicks;       // +0x130, Simulate ticks spent waiting for a stopped worker to leave its job
    LONG unguarded;       // +0x134, reserved: worker reached an IC with no thunk (not wired yet, stays 0)
    LONG workerClassInit; // +0x138, reserved: class init on the worker (not wired yet, stays 0)
    // v3: mono ledger probe (autledger.cpp, [AutShadow] Ledger=1; docs/51 I-121-5/7/9/12, I-120-4, I-119-44)
    LONG lgOn;            // +0x13C, bit 0 Ledger=1, 1 a Simulate tick counted, 2 a scope opened, 3 a frame chain read, 4 a task id read
    LONG lgTicks;         // +0x140, MonoScriptHost_Simulate entries counted
    LONG lgTrackTicks;    // +0x144, of those, ticks that found EA memory tracking [0x11DC18C] != 0
    LONG lgTrackLast;     // +0x148, last non-zero [0x11DC18C] (0 = never)
    LONG lgScopes;        // +0x14C, meta scopes opened (game B13 calls through ts3fixes' MetaHook)
    LONG lgScopeUs;       // +0x150, wall time inside scopes (sum, us; includes ticks a yielded B13 slept)
    LONG lgScopeFull;     // +0x154, opens refused (8 tasks already in scope) or leaves with no open scope
    LONG lgIn[4];         // +0x158, in-scope calls: 0 Random_NextDoubleImpl, 1 Slots_GetPositionOfSlotImpl, 2 Query_GetObjects, 3 Query_GetObjectsInRange
    LONG lgOut[4];        // +0x168, the same outside every scope (any thread)
    LONG lgSlotIds;       // +0x178, distinct object ids of in-scope slot position calls (saturates at 64)
    LONG lgSleep[8];      // +0x17C, Sleep probe: Sleep(0) resumes 0/1/2/3+ ticks later, then the same for Sleep(1)
    LONG lgDropped;       // +0x19C, in-scope calls whose class or caller record did not fit
    LONG lgCls[16][3];    // +0x1A0, in-scope query classes: {watch 2|3, MonoClass*, calls}
    LONG lgCall[32][10];  // +0x260, in-scope callers: {watch, calls, MonoMethod* of interpreter frames 0..7, innermost first}
    // v4: mod guards and docs/125 addendum / docs/127 (abort reasons 20+, reference RNG pause brackets)
    LONG abort2[2];       // +0x760, abort reasons AS_AB_RAIN (20, H11, docs/127 §4), AS_AB_POSTURE (21, null posture list)
    LONG rngPause;        // +0x768, RngPause brackets opened in the reference (hooked calls draw from a scratch stream)
    LONG rngPauseDraws;   // +0x76C, reference draws inside a bracket (would have been one-sided on the unit stream)
};
extern "C" __declspec(dllexport) Ts3nAutShadow ts3native_autshadow = {4};

enum { AO_OBJ, AO_LIST, AO_CUT, AO_WORLD, AO_MEMO, AO_STUB, AO_ABORT, AO_RNG };  // AO_RNG: per-unit MT streams (docs/125 §2)
enum { AK_INT, AK_FP, AK_STRUCT, AK_REF, AK_ARRAY };
enum { AS_AB_ROW, AS_AB_POS, AS_AB_MEMO, AS_AB_RNG, AS_AB_SLOTPOS, AS_AB_ATTACH, AS_AB_QUERY, AS_AB_IC, AS_AB_SF,
       AS_AB_PRIVACY, AS_AB_ROOMCACHE, AS_AB_SIMSTALE, AS_AB_EXC, AS_AB_STOP, AS_AB_FMT, AS_AB_DESIREFN, AS_AB_PUNISH,
       AS_AB_REF, AS_AB_LIST, AS_AB_OTHER, AS_AB_RAIN, AS_AB_POSTURE, AS_AB_N };
static const char* const kAsAbortName[AS_AB_N] = {"row", "pos", "memo", "rng", "slotpos", "attach", "query", "ic", "sf",
    "privacy", "roomcache", "simstale", "exc", "stop", "fmt", "desirefn", "punish", "ref", "list", "other", "rain", "posture"};

// One row per IC of docs/122 §3.2. nArg = stack dwords after the hidden result pointer (struct kinds); sz = INT width in
// bytes or struct size; at = the key dword where the owner id (u64) starts; strMask = input dwords that are MonoString*
// (keyed by content); outs = (argument dword, bytes) of the out parameters. Signatures: managed externs (methods.csv);
// the cdecl layout (hidden struct pointer first) is verified in the disassembly for rows 0-24 and the 7 memo rows
// (docs/122 §14.8, §14.9); stub and abort rows never call the body.
struct AsDesc {
    const char* name;
    u8 owner, kind, nArg, sz, at, reason, strMask, lotClear, nOut;
    u8 outIdx[5], outSz[5];
};
#define AS_D(n, o, k, a, s, at, r) {n, o, k, a, s, at, r, 0, 0, 0, {0}, {0}}
static const AsDesc kAsDesc[] = {
    AS_D("ScriptCore.Slots::Slots_GetParentIdImpl", AO_OBJ, AK_INT, 2, 8, 0, AS_AB_ROW),  // index 0: our parent index (docs/79)
    AS_D("ScriptCore.Objects::Objects_GetPosition", AO_OBJ, AK_STRUCT, 2, 12, 0, AS_AB_ROW),
    AS_D("ScriptCore.World::World_ObjectGetHiddenFlags", AO_OBJ, AK_INT, 2, 4, 0, AS_AB_ROW),
    AS_D("ScriptCore.ScriptProxy::ScriptProxy_GetResourceKeyImpl", AO_OBJ, AK_STRUCT, 2, 16, 0, AS_AB_ROW),
    AS_D("ScriptCore.World::World_GetObjectTombRoomIdImpl", AO_OBJ, AK_INT, 2, 2, 0, AS_AB_ROW),
    AS_D("ScriptCore.World::World_StaysAfterEvictImpl", AO_OBJ, AK_INT, 2, 1, 0, AS_AB_ROW),
    AS_D("ScriptCore.CASUtils::CASUtils_GetOutfitInGameObjectImpl", AO_OBJ, AK_STRUCT, 2, 16, 0, AS_AB_ROW),
    AS_D("ScriptCore.Simulator::Simulator_GetTaskImpl", AO_OBJ, AK_REF, 3, 4, 0, AS_AB_ROW),
    AS_D("ScriptCore.Slots::Slots_GetContainedObjectImpl", AO_OBJ, AK_INT, 3, 8, 0, AS_AB_ROW),
    AS_D("ScriptCore.Slots::Slots_GetContainmentSlotNamesImpl", AO_OBJ, AK_ARRAY, 2, 4, 0, AS_AB_ROW),
    AS_D("ScriptCore.World::World_GetRoomIdAtPosImpl", AO_WORLD, AK_INT, 4, 2, 0, AS_AB_POS),
    AS_D("ScriptCore.World::World_IsPositionOutsideImpl", AO_WORLD, AK_INT, 3, 1, 0, AS_AB_POS),
    AS_D("ScriptCore.World::World_HasAnyRoofOrFloorAbovePosImpl", AO_WORLD, AK_INT, 3, 1, 0, AS_AB_POS),
    {"ScriptCore.World::World_GetLotLocationImpl", AO_WORLD, AK_INT, 5, 8, 0, AS_AB_POS, 0, 0, 1, {3}, {12}},  // LotLocation (int,int,sbyte,short) = 12 B [verified, §14.8]
    AS_D("ScriptCore.World::World_GetDoorRoomsImpl", AO_WORLD, AK_ARRAY, 3, 4, 0, AS_AB_POS),
    AS_D("ScriptCore.Queries::Query_CountObjectsOnLot", AO_LIST, AK_INT, 4, 4, 1, AS_AB_LIST),
    AS_D("ScriptCore.Queries::Query_GetObjectsOnLot", AO_LIST, AK_ARRAY, 4, 4, 1, AS_AB_LIST),
    AS_D("ScriptCore.Queries::Query_GetObjectsInRange", AO_CUT, AK_ARRAY, 5, 4, 0, AS_AB_LIST),
    AS_D("ScriptCore.GameUtils::GameUtils_GetWorldName", AO_WORLD, AK_INT, 0, 4, 0, AS_AB_OTHER),
    AS_D("ScriptCore.GameUtils::GameUtils_GetWorldType", AO_WORLD, AK_INT, 0, 4, 0, AS_AB_OTHER),
    AS_D("ScriptCore.World::World_GetWorldNameKey", AO_WORLD, AK_REF, 0, 4, 0, AS_AB_OTHER),
    AS_D("ScriptCore.World::World_GetSeaLevelImpl", AO_WORLD, AK_FP, 0, 4, 0, AS_AB_OTHER),
    AS_D("ScriptCore.World::World_GetSunriseTime", AO_CUT, AK_FP, 0, 4, 0, AS_AB_OTHER),
    AS_D("ScriptCore.World::World_GetSunsetTime", AO_CUT, AK_FP, 0, 4, 0, AS_AB_OTHER),
    AS_D("ScriptCore.World::World_GetMoonPhaseImpl", AO_CUT, AK_INT, 0, 4, 0, AS_AB_OTHER),
    {"ScriptCore.DeviceConfig::DeviceConfig_GetOption", AO_MEMO, AK_INT, 2, 1, 0, AS_AB_MEMO, 1, 0, 1, {1}, {4}},
    AS_D("ScriptCore.GameUtils::GameUtils_IsWorldInstalled", AO_MEMO, AK_INT, 1, 1, 0, AS_AB_MEMO),
    AS_D("ScriptCore.UserToolUtils::UserToolUtils_GetProductImpl", AO_MEMO, AK_INT, 5, 4, 0, AS_AB_MEMO),
    {"ScriptCore.UserToolUtils::UserToolUtils_ProductGenericImpl", AO_MEMO, AK_REF, 7, 4, 0, AS_AB_MEMO, 0, 0, 5,
     {2, 3, 4, 5, 6}, {4, 8, 4, 4, 4}},
    {"ScriptCore.World::World_GetLotName", AO_MEMO, AK_REF, 2, 4, 0, AS_AB_MEMO, 0, 1, 0, {0}, {0}},
    {"ScriptCore.World::World_GetLotNameKey", AO_MEMO, AK_REF, 2, 4, 0, AS_AB_MEMO, 0, 1, 0, {0}, {0}},
    AS_D("ScriptCore.DeviceConfig::DeviceConfig_GetProcessorLevel", AO_MEMO, AK_INT, 0, 4, 0, AS_AB_MEMO),
    AS_D("ScriptCore.EAText::EAText_GetMoneyString", AO_STUB, AK_REF, 2, 4, 0, AS_AB_REF),
    AS_D("ScriptCore.EAText::EAText_GetNumberString", AO_STUB, AK_REF, 2, 4, 0, AS_AB_REF),
    AS_D("ScriptCore.EAText::EAText_GetTimeString", AO_STUB, AK_REF, 4, 4, 0, AS_AB_REF),
    AS_D("ScriptCore.LocalizedStringService::LocalizedStringService_GetLocalizedStringByString", AO_STUB, AK_REF, 1, 4, 0, AS_AB_REF),
    AS_D("Sims3.UI.LocalizedStringService::LocalizedStringService_GetLocalizedStringByString", AO_STUB, AK_REF, 1, 4, 0, AS_AB_REF),
    AS_D("ScriptCore.OnlineFeatures::OnlineFeaturesHost_GetPersonaName", AO_STUB, AK_REF, 0, 4, 0, AS_AB_REF),
    // docs/125: the RNG draws from per-unit streams; slot positions (handle, slot hash, age/gender/species, exit) and
    // Query_GetObjects(Type) are learned keys re-asked at every cut through the game's body (AO_CUT: no event needed)
    AS_D("ScriptCore.Random::Random_NextDoubleImpl", AO_RNG, AK_FP, 0, 8, 0, AS_AB_RNG),
    AS_D("ScriptCore.Slots::Slots_GetPositionOfSlotImpl", AO_CUT, AK_STRUCT, 5, 12, 0, AS_AB_SLOTPOS),
    AS_D("ScriptCore.Slots::Slots_CanAttachToSlotImpl", AO_ABORT, AK_INT, 7, 4, 0, AS_AB_ATTACH),
    AS_D("ScriptCore.Queries::Query_GetObjects", AO_CUT, AK_ARRAY, 1, 4, 0, AS_AB_QUERY),
};
static const int AS_NDESC = sizeof(kAsDesc) / sizeof(kAsDesc[0]);

// guard state
static const int AS_MAXIC = 2048;
static void* volatile g_asOrig[AS_MAXIC];  // what each slot held: the thunk's sim-thread target
static const AsDesc* g_asDescOf[AS_MAXIC];
static char g_asKind[AS_MAXIC];            // 'f', 'd', 's' or 0 (autshadow-kinds.h)
static const char* g_asName[AS_MAXIC];
static int g_asIcN;
static u8* g_asThunks;
static volatile DWORD g_asWorkerTid;       // 0 = no worker: every guard passes through
static volatile LONG g_asSimCalls;         // non-locked: the sim thread is the only hot caller
static volatile LONG g_asStop;             // set by the sim when the window closes (A5)
static volatile LONG g_asAbortReason = -1; // first abort of the current meta object wins (docs/122 §3.3)
static bool g_asRefsRooted;                // part 2: managed answers are in a rooted column, safe to hand out
static void* g_asEmptyStr;                 // part 2: the rooted empty string of the a-stub ICs
typedef void*(__cdecl* AsNewArrayFn)(DWORD vtable, DWORD n);  // part 2: mono_array_new_specific under the P3 lock
static AsNewArrayFn g_asNewArray;
static int g_asAudit;                      // [AutShadow] Audit=N (0 = off)
static bool g_asTracked;                   // routesnap's World_FireEvent detour feeds the epoch and object events
static volatile LONG g_asLogged;

struct AsRet { DWORD lo, hi; double f; };

static int AsKindOfName(const char* name) {
    int lo = 0, hi = (int)(sizeof(kAsKindNames) / sizeof(kAsKindNames[0])) - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2, c = strcmp(name, kAsKindNames[mid]);
        if (!c) return kAsKindOf[mid];
        if (c < 0) hi = mid - 1; else lo = mid + 1;
    }
    return 0;
}

static void AsAbort(int reason, DWORD idx) {
    Ts3nAutShadow& k = ts3native_autshadow;
    InterlockedIncrement(&k.abortTotal);
    InterlockedIncrement(reason < 20 ? &k.abort[reason] : &k.abort2[reason - 20]);
    InterlockedCompareExchange(&g_asAbortReason, reason, -1);
    if (InterlockedIncrement(&g_asLogged) <= 64)
        Log("autshadow abort %s at %s", kAsAbortName[reason], idx < (DWORD)g_asIcN && g_asName[idx] ? g_asName[idx] : "-");
}
// part 2 registers this as AutShadowNative::Abort for the managed hooks (H4-H6: desirefn, punish; privacy, roomcache, sf)
extern "C" void __cdecl AsManagedAbort(int reason) {
    if (reason >= 0 && reason < AS_AB_N) AsAbort(reason, (DWORD)-1); else AsAbort(AS_AB_OTHER, (DWORD)-1);
}
int AsAborted() { return g_asAbortReason; }  // part 2: the driver checks after each meta object, then resets
void AsResetAbort() { InterlockedExchange(&g_asAbortReason, -1); }

// ---- answer tables ----
static const int AS_CAP = 1024, AS_IDX = 2048, AS_POOL = 8192, AS_KEY = 8;
static const LONG AS_QUERY_MAX = 256;  // Query_GetObjects answers kept up to this length (docs/125 §4)
enum { AE_LEARNED = 1, AE_FRESH = 2, AE_NOPAY = 4, AE_STALE = 8 };
struct AsEnt {
    WORD ic; u8 nKey; u8 flags;
    DWORD key[AS_KEY];
    AsU64 owner;
    DWORD r[2];
    double f;
    u8 st[16];
    u8 out[24];
    LONG pay, payN;   // array answer in the pool: vtable, length, elements (payN 0 = null array)
    LONG gen;         // list generation at the refresh
    LONG lastUsed;    // cut of the last worker hit
};
struct AsTab {
    LONG n, poolN;
    AsEnt e[AS_CAP];
    WORD idx[AS_IDX];  // entry + 1, 0 = empty
    DWORD refs[AS_CAP];  // rooted column (part 2): entry i's managed answer (AK_REF), so the GC keeps it while listed
    DWORD pool[AS_POOL];  // array answers; rooted with refs (one GC_add_roots range): object-array elements stay alive
    u8 used[AS_CAP];
};
struct AsSet { AsU64 k[4096]; LONG n; };  // open addressing, 0 = empty (object id 0 is not an object)
struct AsMem {
    AsTab m, v[2];           // master (sim thread), published versions (worker)
    DWORD poolTmp[AS_POOL];  // rooted too (holds fresh array payloads during a cut)
    DWORD keep[4];           // rooted: empty string, F2 exception
    AsSet dirty, always, work;
    AsSet sims, learnSims;   // part 2: Sims the request cannot reach (stale unless refreshed), Sims a worker read promoted
};
static AsMem* g_as;  // ~0.6 MB, allocated only with [AutShadow] Enabled=1
static bool AsAlloc() { return g_as || (g_as = (AsMem*)VirtualAlloc(NULL, sizeof(AsMem), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)) != NULL; }
static volatile LONG g_asPub = -1, g_asHeld = -1;
static LONG g_asCut;              // cuts so far (sim)
static volatile LONG g_asEpoch = 1, g_asListGen;
static LONG g_asEpochSeen;

static DWORD AsHash(int ic, const DWORD* key, int n) {
    DWORD h = 0x811C9DC5u ^ (DWORD)ic;
    for (int i = 0; i < n; i++) { h ^= key[i]; h *= 0x1000193u; h ^= h >> 15; }
    return h;
}
static int AsFind(const AsTab& t, int ic, const DWORD* key, int n) {
    for (DWORD h = AsHash(ic, key, n), i = 0; i < (DWORD)AS_IDX; i++) {
        const int s = t.idx[(h + i) & (AS_IDX - 1)];
        if (!s) return -1;
        const AsEnt& e = t.e[s - 1];
        if (e.ic == ic && e.nKey == n && !memcmp(e.key, key, n * 4)) return s - 1;
    }
    return -1;
}
static void AsIndexAdd(AsTab& t, int at) {
    const AsEnt& e = t.e[at];
    for (DWORD h = AsHash(e.ic, e.key, e.nKey), i = 0;; i++) {
        WORD& s = t.idx[(h + i) & (AS_IDX - 1)];
        if (!s) { s = (WORD)(at + 1); return; }
    }
}
static void AsReindex(AsTab& t) {
    memset(t.idx, 0, sizeof(t.idx));
    for (int i = 0; i < t.n; i++) AsIndexAdd(t, i);
}

// MonoString {vtable, sync, int length, UTF-16 chars} (Mono 1.x layout [inferred]): keyed by content, not identity.
// shortcut: a 32-bit content hash stands for the string; a collision would answer a different option's memo (none seen
// offline), upgrade to the full content if the memo ever holds long keys
static DWORD AsStrKey(DWORD s) {
    if (!s) return 0;
    const LONG len = *(const LONG*)(uintptr_t)(s + 8);
    const u8* c = (const u8*)(uintptr_t)(s + 12);
    DWORD h = 0x811C9DC5u ^ (DWORD)len;
    for (LONG i = 0; i < len * 2; i++) { h ^= c[i]; h *= 0x1000193u; }
    return h | 1;
}

// input dwords of a call (args = first stack argument, the hidden result pointer included). Memo rows also key on the
// value an out/ref parameter holds before the call: their bodies leave some outs unwritten (DeviceConfig_GetOption
// 0x741230 on a missing option, UserToolUtils_ProductGenericImpl 0xCE99E0 per funcID), and the game's call then leaves
// the caller's own value there, so a replay is exact only for the same in-values (docs/122 §14.9). Keys fit AS_KEY.
static int AsKeyOf(const AsDesc& d, const DWORD* args, DWORD* key) {
    const DWORD* a = args + (d.kind == AK_STRUCT);
    int n = 0;
    for (int i = 0; i < d.nArg && n < AS_KEY; i++) {
        int j = 0;
        while (j < d.nOut && d.outIdx[j] != i) j++;
        if (j == d.nOut) key[n++] = (d.strMask >> i) & 1 ? AsStrKey(a[i]) : a[i];
        else if (d.owner == AO_MEMO)
            for (int q = 0; q < d.outSz[j] / 4 && n < AS_KEY; q++) key[n++] = ((const DWORD*)(uintptr_t)a[i])[q];
    }
    return n;
}
static int AsOutOff(const AsDesc& d, int j) { int o = 0; for (int q = 0; q < j; q++) o += d.outSz[q]; return o; }

// cdecl call of an IC body with n stack dwords (selftest and builder); FP returns popped into r->f
static __declspec(naked) void __cdecl AsCallRaw(void* /*fn*/, const DWORD* /*args*/, int /*n*/, AsRet* /*r*/, int /*fp*/) {
    __asm {
        push ebp
        mov ebp, esp
        push esi
        push edi
        mov ecx, [ebp + 16]
        mov esi, [ebp + 12]
        lea eax, [ecx * 4]
        sub esp, eax
        mov edi, esp
        cld
        rep movsd
        call dword ptr [ebp + 8]
        mov ecx, [ebp + 20]
        cmp dword ptr [ebp + 24], 0
        je intr
        fstp qword ptr [ecx + 8]
        jmp done
    intr:
        mov [ecx], eax
        mov [ecx + 4], edx
    done:
        lea esp, [ebp - 8]
        pop edi
        pop esi
        pop ebp
        ret
    }
}

static void AsMaskRet(const AsDesc& d, DWORD* r) {
    if (d.kind == AK_INT && d.sz < 8) { r[1] = 0; if (d.sz < 4) r[0] &= (1u << (d.sz * 8)) - 1; }
    else if (d.kind == AK_STRUCT) r[0] = r[1] = 0;  // eax = our own result pointer, not part of the answer
    else if (d.kind != AK_INT) r[1] = 0;
}

// the body's answer for entry e, into e itself; payload appended to pool (AE_NOPAY when it does not fit)
static void AsRefresh(int idx, AsEnt& e, DWORD* pool, LONG* poolN) {
    const AsDesc& d = *g_asDescOf[idx];
    DWORD a[1 + 8 + 5];
    int n = 0, k = 0;
    if (d.kind == AK_STRUCT) a[n++] = (DWORD)(uintptr_t)e.st;
    for (int i = 0; i < d.nArg; i++) {
        int j = 0;
        while (j < d.nOut && d.outIdx[j] != i) j++;
        a[n++] = j < d.nOut ? (DWORD)(uintptr_t)(e.out + AsOutOff(d, j)) : e.key[k++];
    }
    AsRet r = {0, 0, 0};
    AsCallRaw(g_asOrig[idx], a, n, &r, d.kind == AK_FP);
    e.r[0] = r.lo, e.r[1] = r.hi, e.f = r.f;
    AsMaskRet(d, e.r);
    if (d.kind == AK_REF && &e >= g_as->m.e && &e < g_as->m.e + AS_CAP) g_as->m.refs[&e - g_as->m.e] = e.r[0];  // rooted at once
    e.flags &= ~(AE_FRESH | AE_NOPAY);
    e.gen = g_asListGen;
    e.pay = e.payN = 0;
    if (d.kind == AK_ARRAY && r.lo) {  // MonoArray {vtable, sync, max_length, bounds, data at +16} [verified in monohost, docs/122 §14.4]
        const DWORD* arr = (const DWORD*)(uintptr_t)r.lo;
        const LONG len = (LONG)arr[2];
        if (d.reason == AS_AB_QUERY && len > AS_QUERY_MAX) { e.flags |= AE_NOPAY; return; }  // world-wide type: abort query
        if (len < 0 || *poolN + 2 + len > AS_POOL) { e.flags |= AE_NOPAY; ts3native_autshadow.full++; return; }
        e.pay = *poolN, e.payN = 2 + len;
        pool[*poolN] = arr[0], pool[*poolN + 1] = (DWORD)len;
        memcpy(pool + *poolN + 2, arr + 4, len * 4);
        *poolN += 2 + len;
    }
    ts3native_autshadow.refreshed++;
}

static int AsInsert(int idx, const DWORD* key, int n, int flags) {
    AsTab& m = g_as->m;
    if (m.n >= AS_CAP) { ts3native_autshadow.full++; return -1; }
    AsEnt& e = m.e[m.n];
    memset(&e, 0, sizeof(e));
    e.ic = (WORD)idx, e.nKey = (u8)n, e.flags = (u8)(flags | AE_FRESH);
    memcpy(e.key, key, n * 4);
    const AsDesc& d = *g_asDescOf[idx];
    if ((d.owner == AO_OBJ || d.owner == AO_LIST) && d.at + 1 < n) e.owner = key[d.at] | (AsU64)key[d.at + 1] << 32;
    e.lastUsed = g_asCut;
    AsIndexAdd(m, m.n);
    return m.n++;
}

// ---- change tracking ----
static bool AsSetAdd(AsSet& s, AsU64 id) {
    if (!id) return true;
    for (DWORD h = (DWORD)(id ^ id >> 29) * 0x9E3779B1u, i = 0; i < 4096; i++) {
        AsU64& v = s.k[(h + i) & 4095];
        if (v == id) return true;
        if (!v) { if (s.n >= 3072) return false; v = id; s.n++; return true; }
    }
    return false;
}
static bool AsSetHas(const AsSet& s, AsU64 id) {
    if (!id) return false;
    for (DWORD h = (DWORD)(id ^ id >> 29) * 0x9E3779B1u, i = 0; i < 4096; i++) {
        const AsU64 v = s.k[(h + i) & 4095];
        if (v == id) return true;
        if (!v) return false;
    }
    return false;
}
static volatile LONG g_asDirtyLock;

static void AsEpochBump() { InterlockedIncrement(&g_asEpoch); }
static void AsDirty(AsU64 id) {
    while (InterlockedExchange(&g_asDirtyLock, 1)) YieldProcessor();
    const bool ok = AsSetAdd(g_as->dirty, id);
    InterlockedExchange(&g_asDirtyLock, 0);
    if (!ok) { ts3native_autshadow.dirtyOverflow++; AsEpochBump(); }
}
static AsU64 AsObjId(const void* obj) { return obj ? *(const AsU64*)((const u8*)obj + 8) : 0; }  // object id at +0x08 (docs/45 §2)
// object-event handlers (docs/76 §3.3): fn(object, data), return 1 so the chain goes on
static char __cdecl AsObjEvDirty(void* obj, void*) { InterlockedIncrement(&ts3native_autshadow.objEvents); AsDirty(AsObjId(obj)); return 1; }
static char __cdecl AsObjEvList(void* obj, void*) {
    InterlockedIncrement(&ts3native_autshadow.objEvents);
    InterlockedIncrement(&g_asListGen);
    AsDirty(AsObjId(obj));
    return 1;
}
static const DWORD kAsObjIds[] = {4, 6, 0x0B, 0x0C, 0x0E, 0x12, 0x18, 0x21};  // docs/122 §2.2 object rows, docs/79 0x12

static void AsObjRegister() {
    Ts3nAutShadow& k = ts3native_autshadow;
    for (DWORD id : kAsObjIds) {
        void* fn = id == 4 || id == 6 ? (void*)AsObjEvList : (void*)AsObjEvDirty;
        if (!SnObjListed(id, fn) && !g_snObjReg(id, fn, 0x7FFFFFFF)) { Log("FAIL autshadow object event 0x%lX node", id); continue; }
        k.ready |= 8;
    }
}

// routesnap's World_FireEvent detour, sim thread (docs/76 §2): world events 2, 3, 5, 7, 9, 0x0A and lot events 0x0D-0x10,
// 0x46 force a full rebuild; 0x11 (OnObjectLotUpdate) moves objects between lot lists
static void AsWorldEvent(DWORD id) {
    if (!g_as) return;
    if (id == 7) { g_asTracked = true; ts3native_autshadow.ready |= 4; AsObjRegister(); }
    if (id == 2 || id == 3 || id == 5 || id == 7 || id == 9 || id == 0x0A || (id >= 0x0D && id <= 0x10) || id == 0x46) AsEpochBump();
    else if (id == 0x11) InterlockedIncrement(&g_asListGen);
}

// ---- worker side ----
struct AsLearnRec { DWORD idx; int n; DWORD key[AS_KEY]; };
static AsLearnRec g_asRing[64];
static volatile LONG g_asRingHead, g_asRingTail;  // head: worker, tail: sim

static void AsLearn(DWORD idx, const DWORD* key, int n) {
    const LONG h = g_asRingHead;
    if (h - g_asRingTail >= 64) { InterlockedIncrement(&ts3native_autshadow.learnDropped); return; }
    AsLearnRec& r = g_asRing[h & 63];
    r.idx = idx, r.n = n;
    memcpy(r.key, key, n * 4);
    MemoryBarrier();
    g_asRingHead = h + 1;
}

// worker run bounds (part 2): only inside the sim's quiescent window, so never during a cut; a run that overruns keeps
// its version held and the next cut writes the other one
void AsWorkerBegin() { InterlockedExchange(&g_asHeld, g_asPub); InterlockedExchange(&g_asStop, 0); }
void AsWorkerEnd() { InterlockedExchange(&g_asHeld, -1); }

static int AsRngWorker(DWORD idx, AsRet* r);  // autshadow-run.cpp (docs/125 §2)

// returns 1 when the answer is on the x87 stack (r->f)
static int AsWorkerAnswer(DWORD idx, DWORD* args, AsRet* r) {
    Ts3nAutShadow& k = ts3native_autshadow;
    InterlockedIncrement(&k.guardWorker);
    const char kind = g_asKind[idx];
    const int fp = kind == 'f' || kind == 'd';
    r->lo = kind == 's' ? args[0] : 0, r->hi = 0, r->f = 0;
    const AsDesc* d = g_asDescOf[idx];
    if (g_asStop) { AsAbort(AS_AB_STOP, idx); return fp; }
    if (!d) { AsAbort(AS_AB_IC, idx); return fp; }
    if (d->owner == AO_ABORT) { AsAbort(d->reason, idx); return fp; }
    if (d->owner == AO_RNG) return AsRngWorker(idx, r);
    if (d->owner == AO_STUB) {
        if (!g_asEmptyStr) AsAbort(AS_AB_REF, idx); else r->lo = (DWORD)(uintptr_t)g_asEmptyStr;
        return 0;
    }
    const LONG v = g_asHeld;
    if (v < 0) { AsAbort(AS_AB_OTHER, idx); return fp; }
    AsTab& t = g_as->v[v];
    DWORD key[AS_KEY];
    const int n = AsKeyOf(*d, args, key);
    const int i = AsFind(t, (int)idx, key, n);
    if (i < 0 || (t.e[i].flags & AE_NOPAY)) {
        InterlockedIncrement(&k.workerMisses);
        if (i < 0 && d->owner != AO_MEMO) AsLearn(idx, key, n);  // memo keys can only come from the sim's own calls
        AsAbort(d->reason, idx);
        return fp;
    }
    const AsEnt& e = t.e[i];
    t.used[i] = 1;
    if (e.flags & AE_STALE) {  // Sim the request could not reach: not refreshed at this cut (docs/122 §2.3 simstale)
        const DWORD owner[2] = {(DWORD)e.owner, (DWORD)(e.owner >> 32)};
        AsLearn(idx | 0x80000000u, owner, 2);
        AsAbort(AS_AB_SIMSTALE, idx);
        return fp;
    }
    if ((d->kind == AK_REF || d->kind == AK_ARRAY) && !g_asRefsRooted) { AsAbort(AS_AB_REF, idx); return fp; }
    const DWORD* a = args + (d->kind == AK_STRUCT);
    for (int j = 0; j < d->nOut; j++) memcpy((void*)(uintptr_t)a[d->outIdx[j]], e.out + AsOutOff(*d, j), d->outSz[j]);
    InterlockedIncrement(&k.workerHits);
    switch (d->kind) {
        case AK_FP: r->f = e.f; return 1;
        case AK_STRUCT: memcpy((void*)(uintptr_t)args[0], e.st, d->sz); return 0;
        case AK_ARRAY:
            if (!e.payN) return 0;
            if (!g_asNewArray) { AsAbort(AS_AB_REF, idx); return 0; }
            {
                DWORD* arr = (DWORD*)g_asNewArray(t.pool[e.pay], t.pool[e.pay + 1]);
                if (!arr) { AsAbort(AS_AB_REF, idx); return 0; }
                memcpy(arr + 4, t.pool + e.pay + 2, (e.payN - 2) * 4);
                r->lo = (DWORD)(uintptr_t)arr;
            }
            return 0;
        default: r->lo = e.r[0], r->hi = e.r[1]; return 0;
    }
}

// the sim thread's own call of a memo IC: run the body, record (key -> answer)
static int AsSimMemo(DWORD idx, DWORD* args, AsRet* r) {
    const AsDesc& d = *g_asDescOf[idx];
    const int fp = d.kind == AK_FP;
    DWORD key[AS_KEY];
    const int n = AsKeyOf(d, args, key);  // before the call: out in-values are part of the key
    AsCallRaw(g_asOrig[idx], args, d.nArg + (d.kind == AK_STRUCT), r, fp);
    int i = AsFind(g_as->m, (int)idx, key, n);
    if (i < 0 && (i = AsInsert((int)idx, key, n, 0)) >= 0) ts3native_autshadow.memoRecords++;
    if (i >= 0) {
        AsEnt& e = g_as->m.e[i];
        e.r[0] = r->lo, e.r[1] = r->hi, e.f = r->f;
        AsMaskRet(d, e.r);
        if (d.kind == AK_REF) g_as->m.refs[i] = e.r[0];  // rooted column (memo strings live until the next cut's rebuild)
        e.flags &= ~AE_FRESH;
        const DWORD* a = args + (d.kind == AK_STRUCT);
        for (int j = 0; j < d.nOut; j++) memcpy(e.out + AsOutOff(d, j), (const void*)(uintptr_t)a[d.outIdx[j]], d.outSz[j]);
    }
    return fp;
}

static int __cdecl AsEntryCall(DWORD idx, DWORD* args, AsRet* r) {
    if (GetCurrentThreadId() == g_asWorkerTid) return AsWorkerAnswer(idx, args, r);
    return AsSimMemo(idx, args, r);  // only memo thunks reach here off the worker
}

// [esp] = index, [esp+4] = return into the caller (cdecl, caller cleans), [esp+8] = first argument
static __declspec(naked) void AsCallEntry() {
    __asm {
        mov eax, [esp]
        lea ecx, [esp + 8]
        sub esp, 16
        mov edx, esp
        push edx
        push ecx
        push eax
        call AsEntryCall
        add esp, 12
        test eax, eax
        jnz fp
        mov eax, [esp]
        mov edx, [esp + 4]
        add esp, 20
        ret
    fp:
        fld qword ptr [esp + 8]
        xor eax, eax
        xor edx, edx
        add esp, 20
        ret
    }
}
static __declspec(naked) void AsGuardEntry() {
    __asm {
        push eax
        mov eax, dword ptr fs:[0x24]
        cmp eax, dword ptr [g_asWorkerTid]
        je worker
        add dword ptr [g_asSimCalls], 1
        mov eax, [esp + 4]
        mov eax, dword ptr [g_asOrig + eax * 4]
        mov [esp + 4], eax
        pop eax
        ret
    worker:
        pop eax
        jmp AsCallEntry
    }
}

// ---- F2 (docs/122 §12.5): String.ParseFormatSpecifier 0xDA9790, the closure's only writer of the IC nesting counter
// [0x11F2D84] (non-atomic ++/--). cdecl, arg 2 = MonoObject** exception out; the body's error exit stores the exception
// there and returns [verified: 0xDA9F94..0xDAA08A]. Worker: abort `fmt` and leave through the same contract with an
// exception, never touching the counter.
static const DWORD AS_FMT_AT = 0x00DA9790;
static const u8 kAsFmtPro[6] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8};  // push ebp; mov ebp,esp; and esp,-8
static void* g_asFmtTramp;
static void* g_asFmtExc;  // part 2: a preallocated, rooted exception; else one is made on the worker (P3 lock)
typedef void*(__cdecl* AsNewExcFn)();
static AsNewExcFn g_asNewExc = (AsNewExcFn)(uintptr_t)0x00E770E0;  // mono_get_exception_null_reference (docs/118 §2)
static void __cdecl AsFmtAbort(void** exc) {
    AsAbort(AS_AB_FMT, (DWORD)-1);
    if (exc) *exc = g_asFmtExc ? g_asFmtExc : g_asNewExc();
}
static __declspec(naked) void AsFmtHook() {
    __asm {
        push eax
        mov eax, dword ptr fs:[0x24]
        cmp eax, dword ptr [g_asWorkerTid]
        pop eax
        je worker
        jmp dword ptr [g_asFmtTramp]
    worker:
        push dword ptr [esp + 8]
        call AsFmtAbort
        add esp, 4
        ret
    }
}

// ---- the cut (sim thread; part 2 calls it at the return of MonoScriptHost_Simulate with a request pending) ----
static void AsFoldUsed(AsTab& t, bool clear) {
    for (int i = 0; i < t.n; i++) {
        if (!t.used[i]) continue;
        const int m = AsFind(g_as->m, t.e[i].ic, t.e[i].key, t.e[i].nKey);
        if (m >= 0) g_as->m.e[m].lastUsed = g_asCut;
        if (clear) t.used[i] = 0;
    }
}

// arrays are fresh objects per call: compared by contents, not identity
static bool AsSame(const AsDesc& d, const AsEnt& a, const DWORD* poolA, const AsEnt& b, const DWORD* poolB) {
    if (memcmp(&a.f, &b.f, 8) || memcmp(a.st, b.st, sizeof(a.st)) || memcmp(a.out, b.out, sizeof(a.out)) ||
        ((a.flags ^ b.flags) & AE_NOPAY))
        return false;
    if (d.kind != AK_ARRAY) return a.r[0] == b.r[0] && a.r[1] == b.r[1];
    return !a.r[0] == !b.r[0] && a.payN == b.payN && !memcmp(poolA + a.pay, poolB + b.pay, a.payN * 4);
}

// add an object's parent entry when a learned key names an object we have no parent for (the dirty propagation needs it)
static int g_asIcParent = -1;  // guard index of Slots_GetParentIdImpl
static int g_asIcRng = -1;     // guard index of Random_NextDoubleImpl (the reference's draws, docs/125 §2)
static void AsEnsureParent(AsU64 id) {
    const DWORD key[2] = {(DWORD)id, (DWORD)(id >> 32)};
    if (g_asIcParent >= 0 && AsFind(g_as->m, g_asIcParent, key, 2) < 0) AsInsert(g_asIcParent, key, 2, 0);
}

// The request's rows (actor, Sims, meta objects, doors). kind 0 = object (refreshed when dirty), 1 = Sim the request can
// reach (refreshed at every cut), 2 = other Sim: listed, refreshed only when dirty, else published stale (simstale).
void AsAddRowKind(AsU64 id, int kind);
void AsAddRow(AsU64 id, bool sim) { AsAddRowKind(id, sim ? 1 : 0); }
void AsAddRowKind(AsU64 id, int kind) {
    if (!g_as) return;
    const bool sim = kind == 1;
    if (kind == 2) AsSetAdd(g_as->sims, id);
    const DWORD key[3] = {(DWORD)id, (DWORD)(id >> 32), 0};
    for (int i = 0; i < g_asIcN; i++) {
        const AsDesc* d = g_asDescOf[i];
        if (!d || d->owner != AO_OBJ || (d->nArg != 2 && d != &kAsDesc[7])) continue;  // (ulong) ICs, plus GetTask(id, false/true)
        for (DWORD b = 0; b < (d->nArg == 3 ? 2u : 1u); b++) {
            DWORD k3[3] = {key[0], key[1], b};
            if (AsFind(g_as->m, i, k3, d->nArg) < 0) AsInsert(i, k3, d->nArg, 0);
        }
    }
    if (sim) AsSetAdd(g_as->always, id);
}

LONG AsCut() {
    if (!g_as) return 0;
    Ts3nAutShadow& k = ts3native_autshadow;
    LARGE_INTEGER t0, t1, f;
    QueryPerformanceCounter(&t0);
    AsTab& m = g_as->m;
    g_asCut++;
    k.guardSim = g_asSimCalls;
    // worker hits since the last cut keep learned keys alive
    for (int v = 0; v < 2; v++) AsFoldUsed(g_as->v[v], v != g_asHeld);
    // learned keys from the worker's misses
    for (LONG t = g_asRingTail; t != g_asRingHead; t++) {
        const AsLearnRec& r = g_asRing[t & 63];
        if (r.idx & 0x80000000u) {  // a worker read of a stale Sim row: refresh that Sim from now on
            AsSetAdd(g_as->learnSims, r.key[0] | (AsU64)r.key[1] << 32);
        } else if (AsFind(m, (int)r.idx, r.key, r.n) < 0 && AsInsert((int)r.idx, r.key, r.n, AE_LEARNED) >= 0) {
            k.learned++;
            const AsDesc& d = *g_asDescOf[r.idx];
            if (d.owner == AO_OBJ) AsEnsureParent(m.e[m.n - 1].owner);
        }
        MemoryBarrier();
        g_asRingTail = t + 1;
    }
    // what changed: the epoch (or no tracking) refreshes everything; else dirty objects, their children, list generation
    const LONG epoch = g_asEpoch;
    const bool full = epoch != g_asEpochSeen || !g_asTracked;
    if (epoch != g_asEpochSeen) {
        k.epochs++;
        int w = 0;  // lot names may change with lot events: forget them
        for (int i = 0; i < m.n; i++) if (!g_asDescOf[m.e[i].ic]->lotClear) m.refs[w] = m.refs[i], m.e[w++] = m.e[i];
        if (w != m.n) { m.n = w; AsReindex(m); }
    }
    g_asEpochSeen = epoch;
    static const DWORD none[1] = {0};
    for (int i = 0; i < g_asIcN; i++) {  // world constants (docs/122 §2.2): the zero-argument snapshot ICs
        const AsDesc* d = g_asDescOf[i];
        if (d && !d->nArg && (d->owner == AO_CUT || d->owner == AO_WORLD) && AsFind(m, i, none, 0) < 0) AsInsert(i, none, 0, 0);
    }
    while (InterlockedExchange(&g_asDirtyLock, 1)) YieldProcessor();
    memcpy(&g_as->work, &g_as->dirty, sizeof(AsSet));
    memset(&g_as->dirty, 0, sizeof(AsSet));
    InterlockedExchange(&g_asDirtyLock, 0);
    for (int i = 0; i < 4096; i++) if (g_as->always.k[i]) AsSetAdd(g_as->work, g_as->always.k[i]);
    for (int i = 0; i < 4096; i++) if (g_as->learnSims.k[i]) AsSetAdd(g_as->work, g_as->learnSims.k[i]);
    for (int pass = 0, added = 1; added && pass < 64; pass++) {  // children and grandchildren via our parent entries (docs/79)
        added = 0;
        for (int i = 0; i < m.n; i++) {
            const AsEnt& e = m.e[i];
            if (g_asDescOf[e.ic] != &kAsDesc[0]) continue;
            const AsU64 parent = e.r[0] | (AsU64)e.r[1] << 32;
            if (AsSetHas(g_as->work, parent) && !AsSetHas(g_as->work, e.owner)) { AsSetAdd(g_as->work, e.owner); added = 1; }
        }
    }
    const LONG gen = g_asListGen;
    LONG poolN = 0;
    for (int i = 0; i < m.n; i++) {
        AsEnt& e = m.e[i];
        const AsDesc& d = *g_asDescOf[e.ic];
        if (d.owner == AO_MEMO) continue;
        const bool stale = full || (e.flags & AE_FRESH) || d.owner == AO_CUT || (d.owner == AO_OBJ && AsSetHas(g_as->work, e.owner)) ||
                           (d.owner == AO_LIST && e.gen != gen);
        if (stale) AsRefresh(e.ic, e, g_as->poolTmp, &poolN);
        else if (e.payN) {  // keep the payload: move it to the new pool
            memcpy(g_as->poolTmp + poolN, m.pool + e.pay, e.payN * 4);
            e.pay = poolN, poolN += e.payN;
        }
    }
    memcpy(m.pool, g_as->poolTmp, poolN * 4);
    m.poolN = poolN;
    // learned keys unused for 100 cuts
    int w = 0;
    for (int i = 0; i < m.n; i++) {
        if ((m.e[i].flags & AE_LEARNED) && g_asCut - m.e[i].lastUsed > 100) { k.learnDropped++; continue; }
        m.refs[w] = m.refs[i], m.e[w++] = m.e[i];
    }
    // the rooted column matches the entries again; Sim rows not refreshed this cut are published stale (docs/122 §2.3)
    LONG stale = 0;
    for (int i = 0; i < w; i++) {
        AsEnt& e = m.e[i];
        const AsDesc& d = *g_asDescOf[e.ic];
        m.refs[i] = d.kind == AK_REF ? e.r[0] : 0;
        const bool st = d.owner == AO_OBJ && !full && AsSetHas(g_as->sims, e.owner) && !AsSetHas(g_as->work, e.owner);
        e.flags = (u8)(st ? e.flags | AE_STALE : e.flags & ~AE_STALE);
        stale += st;
    }
    memset(m.refs + w, 0, (AS_CAP - w) * 4);
    k.stale = stale;
    if (w != m.n) {  // payload offsets stay valid: the pool is not compacted again until the next cut
        m.n = w;
        AsReindex(m);
    }
    // audit (invariant S-A, docs/122 §2.4): every Nth cut, the incremental answers against a fresh body call
    if (g_asAudit && g_asCut % g_asAudit == 0) {
        k.auditN++;
        for (int i = 0; i < m.n; i++) {
            const AsEnt& e = m.e[i];
            const AsDesc& d = *g_asDescOf[e.ic];
            if (d.owner == AO_MEMO) continue;
            if (d.kind == AK_REF) { k.auditSkipped++; continue; }
            AsEnt x = e;
            LONG xn = 0;
            AsRefresh(e.ic, x, g_as->poolTmp, &xn);
            k.refreshed--;
            if (!AsSame(d, e, m.pool, x, g_as->poolTmp)) {
                k.auditMis[0]++;
                if (k.auditMis[0] <= 16)
                    Log("autshadow audit S1: %s owner 0x%08lX%08lX key0 0x%08lX", g_asName[e.ic], (DWORD)(e.owner >> 32), (DWORD)e.owner, e.key[0]);
            }
        }
    }
    // publish into the version the worker does not hold
    const LONG pub = g_asPub;
    const LONG v = pub < 0 ? 0 : (g_asHeld == (pub ^ 1) ? pub : pub ^ 1);
    AsTab& t = g_as->v[v];
    t.n = m.n, t.poolN = m.poolN;
    memcpy(t.e, m.e, m.n * sizeof(AsEnt));
    memcpy(t.idx, m.idx, sizeof(t.idx));
    memcpy(t.refs, m.refs, sizeof(t.refs));
    memcpy(t.pool, m.pool, m.poolN * 4);
    memset(t.used, 0, sizeof(t.used));
    InterlockedExchange(&g_asPub, v);
    k.entries = m.n;
    k.snapBytes = (LONG)(m.n * sizeof(AsEnt) + sizeof(t.idx) + m.poolN * 4);
    k.listGen = gen;
    k.cuts++;
    QueryPerformanceCounter(&t1);
    QueryPerformanceFrequency(&f);
    const LONG us = (LONG)((t1.QuadPart - t0.QuadPart) * 1000000 / f.QuadPart);
    k.cutUs += us;
    if (us > k.cutMaxUs) k.cutMaxUs = us;
    return us;
}

#include "autshadow-run.cpp"  // part 2: request, cut, reference, worker, window, AutShadowNative ICs (docs/122 §14)
#include "autledger.cpp"      // mono ledger probe on the guard thunks ([AutShadow] Ledger=1, docs/51)

// ---- install ----
static bool AsIsName(const char* s, DWORD hi) {
    if ((DWORD)(uintptr_t)s + 128 > hi) return false;
    if (strncmp(s, "ScriptCore.", 11) && strncmp(s, "Sims3.", 6)) return false;
    for (int i = 0; i < 128; i++) {
        if (!s[i]) return false;
        if (s[i] == ':' && s[i + 1] == ':') return true;
        if (s[i] < 0x20 || s[i] > 0x7E) return false;
    }
    return false;
}
static bool AsIsSnShim(DWORD fn) {
    for (int i = 0; i < SN_IC_N; i++) if (fn == (DWORD)(uintptr_t)kSnIcShims[i]) return true;
    return false;
}

// every {fn, name} pair in [lo, hi): fn in [textLo, textHi) or one of routesnap's slot shims, name in the image
static int AsInstallGuards(DWORD* lo, DWORD* hi, DWORD imgLo, DWORD imgHi, DWORD textLo, DWORD textHi, bool all) {
    Ts3nAutShadow& k = ts3native_autshadow;
    if (!g_asThunks && !(g_asThunks = (u8*)VirtualAlloc(NULL, AS_MAXIC * 10, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE))) return 0;
    for (DWORD* p = lo + 1; p < hi; p++) {
        const DWORD name = *p, fn = p[-1];
        if (name < imgLo || name >= imgHi || !AsIsName((const char*)(uintptr_t)name, imgHi)) continue;
        const bool chained = AsIsSnShim(fn);
        if (!chained && (fn < textLo || fn >= textHi)) continue;
        const char* s = (const char*)(uintptr_t)name;
        const AsDesc* d = NULL;
        for (int i = 0; i < AS_NDESC && !d; i++) if (!strcmp(s, kAsDesc[i].name)) d = &kAsDesc[i];
        if ((!all && !d) || g_asIcN >= AS_MAXIC) { k.guardSkipped++; continue; }
        const int idx = g_asIcN;
        u8* t = g_asThunks + idx * 10;
        const int lw = AsLgWatchOf(s);  // Ledger=1: the four watched ICs count on the way through (autledger.cpp)
        g_asLgWatch[idx] = (char)(lw + 1);
        const void* entry = lw >= 0 ? (void*)AsLedgerEntry : d && d->owner == AO_MEMO ? (void*)AsCallEntry : (void*)AsGuardEntry;
        t[0] = 0x68, memcpy(t + 1, &idx, 4);
        t[5] = 0xE9;
        const LONG rel = (LONG)((DWORD)(uintptr_t)entry - ((DWORD)(uintptr_t)t + 10));
        memcpy(t + 6, &rel, 4);
        g_asOrig[idx] = (void*)(uintptr_t)fn, g_asDescOf[idx] = d, g_asName[idx] = s, g_asKind[idx] = (char)AsKindOfName(s);
        const DWORD thunk = (DWORD)(uintptr_t)t;
        if (!Write(p - 1, &thunk, 4)) { k.guardSkipped++; continue; }
        g_asIcN++;
        k.guardSlots++;
        if (chained) k.guardChained++;
        if (d) k.descIcs++;
        if (d == &kAsDesc[0]) g_asIcParent = idx;
        if (d && d->owner == AO_RNG) g_asIcRng = idx;
    }
    if (k.guardSlots) k.ready |= 1;
    return k.guardSlots;
}

static bool AsInstallFmt(DWORD at) {
    g_asFmtTramp = InstallDetour("String.ParseFormatSpecifier (autshadow F2)", at, kAsFmtPro, sizeof(kAsFmtPro), (void*)AsFmtHook);
    if (g_asFmtTramp) ts3native_autshadow.ready |= 2;
    return g_asFmtTramp != NULL;
}

// [AutShadow] Enabled=1 (DllMain, after every other section: routesnap's slot stubs are in the tables by then)
static bool ApplyAutShadow(const wchar_t* ini) {
    if (!GetPrivateProfileIntW(L"AutShadow", L"Enabled", 0, ini)) { Log("autonomy shadow disabled (Enabled=0)"); return false; }
    if (!AsAlloc()) { Log("FAIL autonomy shadow: no memory for the snapshot tables"); return false; }
    wchar_t guard[16];
    GetPrivateProfileStringW(L"AutShadow", L"Guard", L"all", guard, 16, ini);
    g_asAudit = GetPrivateProfileIntW(L"AutShadow", L"Audit", 50, ini);
    if (GetPrivateProfileIntW(L"AutShadow", L"Ledger", 0, ini)) g_asLedger = true, ts3native_autshadow.lgOn |= 1;
    const u8* base = (const u8*)GetModuleHandleW(NULL);
    const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(base + ((const IMAGE_DOS_HEADER*)base)->e_lfanew);
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    const DWORD imgLo = (DWORD)(uintptr_t)base, imgHi = imgLo + nt->OptionalHeader.SizeOfImage;
    DWORD textLo = 0, textHi = 0;
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++)
        if (sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) { textLo = imgLo + sec[i].VirtualAddress, textHi = textLo + sec[i].Misc.VirtualSize; break; }
    const bool all = _wcsicmp(guard, L"closure") != 0;
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        const IMAGE_SECTION_HEADER& s = sec[i];
        if ((s.Characteristics & IMAGE_SCN_MEM_EXECUTE) || !(s.Characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA) || !memcmp(s.Name, ".rsrc", 5)) continue;
        DWORD* lo = (DWORD*)(base + s.VirtualAddress);
        AsInstallGuards(lo, lo + min(s.Misc.VirtualSize, s.SizeOfRawData) / 4, imgLo, imgHi, textLo, textHi, all);
    }
    AsInstallFmt(AS_FMT_AT);
    g_asTracked = false;  // until routesnap's detour delivers world event 7
    AsInstallRun(ini, (ts3native_counters.mtReady & 2) != 0);  // part 2 rides [MonoThread]'s Simulate slot stub
    const Ts3nAutShadow& k = ts3native_autshadow;
    Log("%s autonomy shadow: Guard=%ls, %ld IC slots guarded (%ld in front of routesnap's stubs, %ld left), %ld of %d "
        "descriptor ICs, F2 %s, change tracking %s; Mode=%d (%s), Sample=%d, WarmIn=%d, Ledger=%d (docs/122 §13-§14, docs/51 ledger probe)",
        k.guardSlots && g_asFmtTramp && g_snJmp[SN_WEV] && g_asMode >= 0 ? "OK  " : "PART", guard, k.guardSlots, k.guardChained,
        k.guardSkipped, k.descIcs, AS_NDESC, g_asFmtTramp ? "on" : "SKIPPED", g_snJmp[SN_WEV] ? "from routesnap's World_FireEvent detour" :
        "OFF ([RouteSnap] Count=0: every cut rebuilds in full)", g_asMode,
        g_asMode < 0 ? "no cut: needs [MonoThread] Enabled=1" : g_asMode == 0 ? "count only" : g_asMode == 1 ? "R: reference + worker" : "W: worker",
        g_asSample, g_asWarmIn, g_asLedger ? 1 : 0);
    return true;
}
