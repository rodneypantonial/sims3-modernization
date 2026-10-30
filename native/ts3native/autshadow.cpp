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
    // v5: H12 route stub (docs/127 §7.2), worker window after the sim pump (docs/51 I-188-3, docs/122 §14.11)
    LONG abortRoute;      // +0x770, abort reason AS_AB_ROUTE (22): a ScriptCore.Route IC on the worker
    LONG cutSite;         // +0x774, where the last request cut ran: 0 none, 1 Simulate loop-back (window ~0), 2 after the sim pump
    LONG overrunIdle;     // +0x778, of the overruns, those where the worker had not entered the job yet (scheduling, not scoring)
    // v6: bounded window close (WindowWaitUs, docs/122 §14.13): the sim waits at the next Simulate entry for a running job
    LONG windowWaits;     // +0x77C, window closes where the sim waited for the worker
    LONG windowWaitUs;    // +0x780, their wait (sum, us; a sim-thread test cost, like refUs)
    // v7: table sizing and LRU eviction (docs/122 §14.14; mono-cost5 BP: 1,024 memo rows filled the table before the first cut)
    LONG evicted;         // +0x784, rows evicted (least recently used first) because the table or the memo quota was full
    LONG evictedMemo;     // +0x788, of those, memo rows
    LONG ringDropped;     // +0x78C, learn records dropped because the worker's learn ring was full (also in learnDropped)
    LONG memoAged;        // +0x790, memo rows dropped after AS_AGE cuts with no sim or worker use
    LONG entriesMax;      // +0x794, high-water of master rows
    LONG learnWantMax;    // +0x798, most new keys asked for at one cut (ring records not already listed)
    LONG memoRows;        // +0x79C, memo rows after the last cut
    LONG refreshMax;      // +0x7A0, most body calls in one cut
    LONG learnBacklog;    // +0x7A4, ring records left for the next cut (per-cut learn cap) after the last cut
    // v8: cut phases, body calls by cause, cold ICs, FPU state (docs/122 §14.15; mono-cost6 BP: 3.2 ms per cut)
    LONG cutLearnUs;      // +0x7A8, sums (us) per cut phase: learn ring + used fold
    LONG cutPrepUs;       // +0x7AC, epoch, world constants, dirty set, parent propagation
    LONG cutRefreshUs;    // +0x7B0, body calls and the payload pool
    LONG cutAgeUs;        // +0x7B4, aging, stale marks, reindex
    LONG cutAuditUs;      // +0x7B8, audit slice (Audit=N)
    LONG cutPublishUs;    // +0x7BC, copy into the published version
    LONG bodyN[4];        // +0x7C0, body calls at cuts by cause: 0 new row or full rebuild, 1 object dirty or reachable Sim, 2 list generation, 3 per-cut row
    LONG bodyUs[4];       // +0x7D0, their time (sum, us)
    LONG coldSkips;       // +0x7E0, stale rows of cold ICs left unrefreshed (ColdIc=1), published cold
    LONG coldReads;       // +0x7E4, worker reads of a cold row (abort with the IC's reason; the IC is hot from the next cut)
    LONG coldIcs;         // +0x7E8, descriptor ICs cold at the last cut
    LONG simX87;          // +0x7EC, x87 control word of the sim thread at the last cut (the worker runs with it)
    LONG workX87;         // +0x7F0, the worker thread's own x87 control word (restored after each job)
    LONG simCsr;          // +0x7F4, MXCSR of the sim thread at the last cut
    LONG workCsr;         // +0x7F8, the worker's own MXCSR
    LONG parentRows;      // +0x7FC, objects a Sim is parented to that got object rows at a cut (before any worker read)
    // v9: change-only list refresh (CutLean=1) and Sim tiers (TierMs > 0) (docs/122 §14.16; mono-cost7 BP: lists 1.98 of 2.6 ms per cut)
    LONG listPrintSkips;  // +0x800, list rows whose generation moved but whose lot input print did not: no body call
    LONG listPrintUs;     // +0x804, time computing lot prints (sum, us)
    LONG listLots;        // +0x808, lot prints computed (sum over cuts)
    LONG listSame;        // +0x80C, list rows refreshed by a print change whose answer came out the same (print too coarse)
    LONG tierMs;          // +0x810, TierMs in force (0 = tiers off)
    LONG tier1Sims;       // +0x814, Sims classified tier 1 (sum over cuts)
    LONG tier2Sims;       // +0x818, Sims classified tier 2 (far unseen; sum over cuts)
    LONG tier2Stale;      // +0x81C, tier-2 Sims left unrefreshed at a cut (rows published stale: a worker read aborts simstale)
    LONG tierBodyN[2];    // +0x820, Sim row body calls at cuts by tier
    LONG tierBodyUs[2];   // +0x828, their time (sum, us)
    LONG tierClassUs;     // +0x830, Sim classification time (camera, frustum, viewers; sum, us)
    LONG ageRuns;         // +0x834, cuts that ran the aging pass (every cut, or every AS_AGE_EVERY-th with CutLean=1)
    // v10: dirty lots (CutLean=2), tier cache, list tiers (docs/122 §14.17; mono-cost8 BP: prints 0.92 of 1.98 ms per cut)
    LONG dirtyLots;       // +0x838, lots marked dirty (membership or room) by the object-lot map (sum over cuts)
    LONG dirtyRows;       // +0x83C, list rows refreshed because their lot was dirty
    LONG dirtyKept;       // +0x840, list rows kept with no print and no body call (lot clean)
    LONG objResolved;     // +0x844, touched objects resolved at cuts (lot and room read; sum)
    LONG objResolveUs;    // +0x848, their time (sum, us)
    LONG dirtyUnknown;    // +0x84C, resync cuts: a touch could not be placed (map or touch list full, no component, unknown destroy)
    LONG listAuditN;      // +0x850, list rows audited against a fresh body call (ListAudit=N per cut)
    LONG listAuditUs;     // +0x854, their time (sum, us)
    LONG listMissed;      // +0x858, audited list rows that differed: a missed event (dirty mode trips off, list rows abort one cut)
    LONG dirtyTripped;    // +0x85C, 1 = dirty mode off for the session (back to CutLean=1 prints)
    LONG tierClassN;      // +0x860, Sim tier classifications (sum; TierClassMs cache)
    LONG tierCamInval;    // +0x864, cuts where a camera move reclassified every Sim
    LONG tierHold;        // +0x868, tier-2 Sims or lots promoted to tier 1 for AS_TIER_HOLD cuts by a worker read
    LONG listT2Wait;      // +0x86C, dirty list rows of tier-2 lots left unrefreshed (published distrusted: a read aborts `list`)
    LONG listT2Ref;       // +0x870, tier-2 lots refreshed at their TierMs interval
    LONG t1Lots;          // +0x874, tier-1 lots at the last cut (lots of the actor and of tier-1 Sims)
    // v11: list kinds, lot-vector prints for unplaceable objects, tier triggers, shared cuts, go-live rates (docs/122 §14.18)
    LONG simMarks;        // +0x878, lot marks from Sim moves that went only to the Sim sets (sum over cuts)
    LONG simFreeKept;     // +0x87C, list rows of a Sim-free type kept although their lot had Sim marks
    LONG listTypes;       // +0x880, list types classified by ts3fixes (ListTypeSet)
    LONG listTypesSimFree; // +0x884, of those, types no Sim can be (Sim moves never refresh their rows)
    LONG unplaced;        // +0x888, touches that could not be placed (no component, reused id, destroyed unplaced): lot-vector prints cover them
    LONG liteLots;        // +0x88C, lot-vector prints computed (sum)
    LONG liteUs;          // +0x890, their time (sum, us)
    LONG liteMarks;       // +0x894, lots marked by a lot-vector print change no event explained
    LONG resyncTouch;     // +0x898, resync cuts from a full touch list (lost touches)
    LONG resyncMap;       // +0x89C, resync cuts from a full object-lot map
    LONG olCompact;       // +0x8A0, object-lot map compactions (destroyed rows dropped)
    LONG olRows;          // +0x8A4, object-lot map rows after the last cut
    LONG tierKept;        // +0x8A8, Sims whose cached tier was kept (no camera, move or staleness trigger)
    LONG batchCuts;       // +0x8AC, cuts that served more than one request (shared cut)
    LONG batchReqs;       // +0x8B0, requests served by cuts (sum of batch sizes)
    LONG batchMax;        // +0x8B4, largest batch
    LONG metaCalls;       // +0x8B8, game B13 calls seen by ts3fixes (every call, sampled or not: the go-live request rate)
    LONG metaTicks;       // +0x8BC, Simulate ticks with at least one B13 call (the go-live cut rate with shared cuts)
    LONG simTicks;        // +0x8C0, Simulate ticks (AsSimEnter)
    // v12: dirtying causes, seeded object-lot map, pointer lot-vector prints, row heat (docs/122 §14.19)
    LONG markBy[8];       // +0x8C4, plain lot marks that dirtied a lot, by cause AS_MC_* (first sight of an old / a new object, lot change, room change, destroy, lost component, lot-vector print, a Sim's child)
    LONG markEv[9];       // +0x8E4, the same marks by object event in the touch (4, 5, 6, 0x0B, 0x0C, 0x0E, 0x12, 0x18, 0x21)
    LONG rowWhy[8];       // +0x908, list rows refreshed for a dirty lot, by the causes that marked the lot (a row counts once per cause)
    LONG seedLots;        // +0x928, lots whose objects were seeded into the object-lot map (new list lot or epoch)
    LONG seedObjs;        // +0x92C, map rows they added
    LONG seedUs;          // +0x930, seeding time (sum, us)
    LONG seedFull;        // +0x934, lots seeded only in part (map near full: those objects keep the first-sight mark)
    LONG liteSuspect;     // +0x938, lot-vector prints that met a pointer of a destroyed or new unplaceable object (lot marked)
    LONG rowHot;          // +0x93C, RowHot in force (0 = off)
    LONG rowColdSkips;    // +0x940, object rows not refreshed because the worker has not read them for RowHot cuts (published cold)
    LONG rowColdReads;    // +0x944, worker reads of such rows (abort with the IC's reason; the row is hot from the next cut)
    // v13: typed marks (docs/122 §14.20): objects classified against the list types, marks only for the types they can be
    LONG clsAsked;        // +0x948, object ids handed to ts3fixes for classification (ObjClassTake)
    LONG clsSet;          // +0x94C, of those, classified (ObjClassSet with a managed object)
    LONG clsNone;         // +0x950, of those, no managed object yet (the object stays unknown: untyped marks)
    LONG typedMarks;      // +0x954, lot marks made typed (the object's class known: only rows of its types are dirtied)
    LONG typedKept;       // +0x958, list rows kept because their lot's typed marks exclude their type
    LONG typedAuditN;     // +0x95C, ListAudit checks spent on such rows (every other cut with one)
    LONG typedAuditMis;   // +0x960, of those, a different answer (also in listMissed: trips the session to prints)
    LONG liteTyped;       // +0x964, lot-vector print changes marked with the classes of the cut's unplaceable objects
    LONG liteTypedNone;   // +0x968, of those, no list type among the classes (no mark)
    LONG clsUs;           // +0x96C, ts3fixes' classification time (ObjClassTake to the batch's last ObjClassSet; sum, us; sim thread at the request)
    // v14: run skip and delta publish (docs/122 §14.21)
    LONG runSkip;         // +0x970, [AutShadow] RunSkip in force
    LONG runN;            // +0x974, row runs at the last cut (an object's rows in a row, or other rows together)
    LONG runSkips;        // +0x978, runs skipped whole (clean object, nothing that changes their outcome)
    LONG rowSkips;        // +0x97C, rows in them
    LONG runBuilds;       // +0x980, run table rebuilds (rows moved: eviction, epoch filter, aging)
    LONG icStampMoves;    // +0x984, cuts where an IC's hot state (or ColdIc/RowHot) changed: runs with cold rows are walked again
    LONG pubDelta;        // +0x988, publishes that copied only changed runs
    LONG pubRows;         // +0x98C, rows copied by publishes (all of them on a full publish)
    LONG runCheckMis;     // +0x990, selftest RunSkip check: skippable runs a walk changed (must stay 0)
    // v14 cont.: Mode R consume-side counters from ts3fixes (docs/122 §14.21, docs/124 §15), added through Stat()
    LONG passPairs;       // +0x994, classified allow-listed pairs the worker scored as passing Test (code pass)
    LONG classPairs;      // +0x998, classified allow-listed pairs (A0-A5): pass share = passPairs / classPairs
    LONG freshReqs;       // +0x99C, searches re-scored fresh on the sim thread at the finish (FreshEvery sample, >= 1 tick after the cut)
    LONG freshPairs;      // +0x9A0, pairs answered by both the worker and the fresh run
    LONG freshDiff;       // +0x9A4, of those, a different code or score (bits)
    LONG freshTop1;       // +0x9A8, re-scored searches whose top passing pair differs
    LONG freshTop10;      // +0x9AC, re-scored searches whose top-10 passing pairs differ (identity or order)
    LONG freshUs;         // +0x9B0, fresh re-score time on the sim (sum, us; a Mode R test cost)
    LONG freshEvery;      // +0x9B4, [AutShadow] FreshEvery in force (1 in N finishes; 0 = off)
    LONG partialKept;     // +0x9B8, overrun runs whose meta objects finished before the stop were compared (KeepPartial)
    LONG keepPartial;     // +0x9BC, [AutShadow] KeepPartial in force
    // v15: Satb, copy on first write (docs/206 §7, satb.cpp). Span = barrier on: probe = one job length of the next tick, act = the job
    LONG satbMode;        // +0x9C0, [AutShadow] Satb in force (0 off, 1 probe, 2 act; -1 refused: table entries differ)
    LONG satbSpans;       // +0x9C4, barrier spans
    LONG satbStores;      // +0x9C8, sim store dispatches through the barrier (whole span)
    LONG satbWords;       // +0x9CC, words logged (first post-cut write of a shared word)
    LONG satbPages;       // +0x9D0, 4 KB pages first marked
    LONG satbMaxWords;    // +0x9D4, most words in one span
    LONG satbMaxPages;    // +0x9D8, most pages in one span
    LONG satbLoads;       // +0x9DC, sim loads that passed a load stub (probe: counted; act: the thread test)
    LONG satbSpanUs;      // +0x9E0, barrier-on time (sum, us)
    LONG satbIcCalls;     // +0x9E4, sim IC calls in spans (guarded EA ICs)
    LONG satbIcShared;    // +0x9E8, of those, a byref or array argument into shared memory (act: the sim waits for the worker)
    LONG satbRtCalls;     // +0x9EC, sim runtime writers in spans (Array.Copy dest, Buffer, String.InternalStrcpy, field helper)
    LONG satbRtWaits;     // +0x9F0, of those, ranges not logged (unknown or too big; act: wait)
    LONG satbBig;         // +0x9F4, single stores over 1024 words (act: wait)
    LONG satbOverflow;    // +0x9F8, spans whose log filled (act: wait)
    LONG satbWaits;       // +0x9FC, act: sim waits for the worker to leave (any of the above)
    LONG satbWaitUs;      // +0xA00, act: their time (sum, us)
    LONG satbWaitTimeouts;  // +0xA04, act: waits given up after 100 ms (the job's result is dropped as stale)
    LONG satbWLoads;      // +0xA08, act: worker loads through the redirect
    LONG satbWHits;       // +0xA0C, act: of those, a logged (cut) value used
    LONG satbWStores;     // +0xA10, act: worker stores checked
    LONG satbWConflict;   // +0xA14, act: worker stores to a word the sim had changed (into its log entry: the worker's view; kinds not emulated: skipped, job stopped)
    LONG satbWRtFix;      // +0xA18, act: worker runtime reads patched from the log (Array.Copy source ...)
    LONG satbConc;        // +0xA1C, act: jobs still running at the next Simulate entry (the sim ran script concurrently)
    LONG satbConcTicks;   // +0xA20, act: sim ends of frame while a job was in flight
    LONG satbStale;       // +0xA24, act: jobs finished as stale (overflow, unknown writer, conflict, timeout)
    LONG satbSwapUs;      // +0xA28, max table swap time (in + out)
    // probe: the counts at one job length after the span start (the high-resolution timer thread's snapshot)
    LONG satbJobSpans;    // +0xA2C, spans the timer snapshotted
    LONG satbJobStores;   // +0xA30
    LONG satbJobWords;    // +0xA34
    LONG satbJobPages;    // +0xA38
    LONG satbJobLoads;    // +0xA3C
    LONG satbJobIc;       // +0xA40, IC calls with a shared pointer argument, plus runtime writers not logged
    LONG satbJobMaxWords; // +0xA44
    LONG satbJobUs;       // +0xA48, the job lengths used (sum, us)
    LONG satbIcCallsAll;  // +0xA4C, IC calls in spans of ICs with byref or array parameters (whether shared or not)
    LONG satbSimArena;    // +0xA50, sim stores into its own stack or current arena chain (not logged: the worker never reads them)
    LONG satbWArenaIgn;   // +0xA54, act: worker log hits inside an arena chunk it took during the job (its own frame: live value kept)
    LONG satbWGiveUp;     // +0xA58, act: worker loads it could not redirect (the object is aborted `stop`, the job stops)
    LONG satbWGiveUpOp;   // +0xA5C, act: the last such opcode (diagnostic)
};
extern "C" __declspec(dllexport) Ts3nAutShadow ts3native_autshadow = {15};

enum { AO_OBJ, AO_LIST, AO_CUT, AO_WORLD, AO_MEMO, AO_STUB, AO_ABORT, AO_RNG };  // AO_RNG: per-unit MT streams (docs/125 §2)
enum { AK_INT, AK_FP, AK_STRUCT, AK_REF, AK_ARRAY };
enum { AS_AB_ROW, AS_AB_POS, AS_AB_MEMO, AS_AB_RNG, AS_AB_SLOTPOS, AS_AB_ATTACH, AS_AB_QUERY, AS_AB_IC, AS_AB_SF,
       AS_AB_PRIVACY, AS_AB_ROOMCACHE, AS_AB_SIMSTALE, AS_AB_EXC, AS_AB_STOP, AS_AB_FMT, AS_AB_DESIREFN, AS_AB_PUNISH,
       AS_AB_REF, AS_AB_LIST, AS_AB_OTHER, AS_AB_RAIN, AS_AB_POSTURE, AS_AB_ROUTE, AS_AB_N };
static const char* const kAsAbortName[AS_AB_N] = {"row", "pos", "memo", "rng", "slotpos", "attach", "query", "ic", "sf",
    "privacy", "roomcache", "simstale", "exc", "stop", "fmt", "desirefn", "punish", "ref", "list", "other", "rain", "posture", "route"};

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
    // docs/127 §7.1: (Type) -> the game's answer at the cut, NULL kept (v0 key: Callbox). Learned like Query_GetObjects.
    // shortcut: AO_CUT reruns each key's body at every cut (~25-100 us per key, docs/127 §7.1) instead of only when the
    // global container changed; add the container trigger if the cut time shows it
    AS_D("ScriptCore.Queries::Query_GetGlobalObjects", AO_CUT, AK_ARRAY, 1, 4, 0, AS_AB_QUERY),
};
static const int AS_NDESC = sizeof(kAsDesc) / sizeof(kAsDesc[0]);

// guard state
static const int AS_MAXIC = 2048;
static void* volatile g_asOrig[AS_MAXIC];  // what each slot held: the thunk's sim-thread target
static const AsDesc* g_asDescOf[AS_MAXIC];
static char g_asKind[AS_MAXIC];            // 'f', 'd', 's' or 0 (autshadow-kinds.h)
static u8 g_asNoDesc[AS_MAXIC];            // worker abort reason of an IC with no descriptor: ic, or route (H12)
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
    InterlockedIncrement(reason < 20 ? &k.abort[reason] : reason < AS_AB_ROUTE ? &k.abort2[reason - 20] : &k.abortRoute);
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
// sizes (docs/122 §14.14): mono-cost5 BP had 1,024 rows full of memo rows before the first cut (memoRecords 1,024, full
// 4,486 at load) and every learned key refused; 8,192 rows with memo rows capped at a quarter and LRU eviction, ~3.9 MB
// for AsMem (master + two published versions), allocated only with Enabled=1
static const int AS_CAP = 8192, AS_IDX = 16384, AS_POOL = 32768, AS_KEY = 8;
static const int AS_MEMO = AS_CAP / 4;  // memo rows (the sim's own calls) never take more than this
static const LONG AS_AGE = 100;         // learned and memo rows unused this many cuts are dropped
static LONG g_asMemoN;                  // memo rows in the master (sim thread)
static const LONG AS_QUERY_MAX = 256;  // Query_GetObjects answers kept up to this length (docs/125 §4)
// AE_DISTRUST (CutLean=2): no answer at this cut (a dirty list row of a tier-2 lot waiting, or the whole list column after a
// missed event): a worker read aborts with the IC's reason and promotes the lot; cleared by the next refresh
enum { AE_LEARNED = 1, AE_FRESH = 2, AE_NOPAY = 4, AE_STALE = 8, AE_COLD = 16, AE_DISTRUST = 32 };
// ColdIc=1 (docs/122 §14.15): a keyed row of an IC the worker has not read for AS_HOT cuts is not refreshed when stale; it
// is published cold (a worker read aborts with the IC's reason and makes the IC hot from the next cut). mono-cost6 BP
// refreshed ~870 rows per cut, 11 per object row, most of them for ICs the allow-listed closures never read.
static int g_asColdIc;
static const LONG AS_HOT = 1000;
// RowHot=N (docs/122 §14.19): the same per row: an object row the worker has not read for N cuts is not refreshed and is
// published cold (a read aborts with the IC's reason and makes the row hot from the next cut). The actors' rows and the
// parent rows (the dirty propagation reads them) are always refreshed. 0 = off.
static LONG g_asRowHot;
static LONG g_asIcHit[AS_MAXIC];  // cut of the IC's last worker read (0 = never)
static const DWORD AS_LEARN_SIM = 0x80000000u, AS_LEARN_HEAT = 0x40000000u, AS_LEARN_LOT = 0x20000000u;  // ring record flags (idx field)
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
    union {
        AsU64 lp;       // list rows (CutLean=1): the lot input print at the refresh (AsLotPrint)
        LONG lastRead;  // object rows (RowHot, docs/122 §14.19): cut of the last worker read of this row (not request adds)
    };
};
struct AsTab {
    LONG n, poolN;
    AsEnt e[AS_CAP];
    WORD idx[AS_IDX];  // entry + 1, 0 = empty
    DWORD refs[AS_CAP];  // rooted column (part 2): entry i's managed answer (AK_REF), so the GC keeps it while listed
    DWORD pool[AS_POOL];  // array answers; rooted with refs (one GC_add_roots range): object-array elements stay alive
    u8 used[AS_CAP];
};
// open addressing, 0 = empty (object id 0 is not an object); at[] lists the used slots so a clear or a walk costs n, not
// the 32 KB of k (docs/122 §14.18: seven per-cut memsets of 32 KB each were part of the 0.3 ms cut prep)
struct AsSet { AsU64 k[4096]; LONG n; WORD at[3072]; };
static bool AsSetAdd(AsSet& s, AsU64 id);
// CutLean=2 object-lot map: per object id, the pointer from its last object event and the lot and room read at the last
// cut that resolved it (seen = lot known). Destroyed rows (gone) are dropped by a compaction when the map fills
// (docs/122 §14.18); cleared at each epoch.
// unpl: last placement failed; placedCut: last cut a touch placed it (tier move trigger); ev: object events of the pending
// touch (AS_EV_* bits, docs/122 §14.19 attribution)
// cls/clsN (docs/122 §14.20): bit i = the object is of list type i (AsLTypeOf order), known for types 0..clsN-1 (clsN 0 =
// not classified); clsQ: waiting in the classification queue
struct AsOl { AsU64 id, lot; const void* obj; DWORD room; LONG placedCut; u8 seen : 1, gone : 1, touched : 1, unpl : 1, clsQ : 1; u8 clsN; WORD ev; DWORD cls; };
// 65,536 rows (2 MB): the seeded rows of every list lot (docs/122 §14.19) come on top of mono-cost10 BP's 17,655 touched rows
static const int AS_OL = 65536, AS_OL_MAX = 49152, AS_TOUCH = 4096;
enum { AS_EV_ADD = 1, AS_EV_GONE = 2 };  // bit i = kAsObjIds[i]
// dirtying causes (docs/122 §14.19, counters markBy/rowWhy)
enum { AS_MC_FIRST, AS_MC_NEW, AS_MC_LOT, AS_MC_ROOM, AS_MC_GONE, AS_MC_LOST, AS_MC_LITE, AS_MC_KID, AS_MC_N };
static const char* const kAsMcName[AS_MC_N] = {"first", "new", "lot", "room", "gone", "lost", "lite", "kid"};
struct AsMem {
    AsTab m, v[2];           // master (sim thread), published versions (worker)
    DWORD poolTmp[AS_POOL];  // rooted too (holds fresh array payloads during a cut)
    DWORD keep[4];           // rooted: empty string, F2 exception
    AsSet dirty, always, work;
    AsSet sims, learnSims;   // part 2: Sims the request cannot reach (stale unless refreshed), Sims a worker read promoted
    AsSet t2skip, t2ref;     // TierMs: tier-2 Sims left unrefreshed at this cut, tier-2 Sims refreshed at it
    AsSet lotDirty, lotRoom, t1Lots;  // CutLean=2, per cut: lots whose membership / some room changed; tier-1 lots
    // docs/122 §14.18: the same marks from Sim moves (only list rows whose type can hold a Sim read them); every Sim id a
    // request listed (kept until the epoch); Sims with a child in our parent rows (their moves mark both); lots with list rows
    AsSet lotDirtySim, lotRoomSim, simIds, kids, listLots, simPtrs;  // simPtrs: per cut, the object pointers of known Sims
    // docs/122 §14.19: lots whose objects are in the map (until the epoch); pointers of unplaceable objects destroyed or
    // created since the last cut (window: event side, under g_asDirtyLock; cut: what the lot-vector prints test)
    AsSet seeded, suspectW, suspect;
    AsSet readCold;  // docs/122 §14.21, per cut: owners of cold object rows the worker read (their runs are walked)
    AsOl ol[AS_OL];
    LONG olN, touchN;        // map rows used; touched rows since the last cut (under g_asDirtyLock)
    int touch[AS_TOUCH];
    bool olOver;             // a touch was lost (touch list or map full): the next cut resyncs
    bool olFull;             // of those, the map was full
};
static AsMem* g_as;  // ~3.9 MB (docs/122 §14.14), allocated only with [AutShadow] Enabled=1
static bool AsAlloc() { return g_as || (g_as = (AsMem*)VirtualAlloc(NULL, sizeof(AsMem), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)) != NULL; }
static volatile LONG g_asPub = -1, g_asHeld = -1;
static LONG g_asCut;              // cuts so far (sim)
static AsU64 g_asActor;           // the current request's actor (TierMs: always tier 1); cleared by Begin
static AsU64 g_asActors[8];       // docs/122 §14.18: every actor of a shared cut (each request's first reachable Sim)
static int g_asActorN;
static bool g_asNextActor;        // Begin of a joining request: its first reachable Sim is an actor too
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
static LONG g_asRowsGen;  // docs/122 §14.21: moves whenever master rows move (eviction, epoch filter, aging): the runs are rebuilt
static bool g_asReadColdOver;  // the readCold set overflowed this cut: runs with cold rows are walked
static void AsReindex(AsTab& t) {
    if (g_as && &t == &g_as->m) g_asRowsGen++;
    memset(t.idx, 0, sizeof(t.idx));
    for (int i = 0; i < t.n; i++) AsIndexAdd(t, i);
}

// EA's MonoString {vtable, int length at +4, UTF-16 chars at +8} [verified: mono_string_new_size 0xEA3FB3 stores the
// vtable at +0 and the length at +4, the terminator at +8 + 2*len; the 0.36.106 crash, docs/67 §24, read +8 as
// the length]. Keyed by content, not identity. A length past kAsStrMax is no string of ours: returns 0, the caller does
// not key the call (a memo key of 0 would answer another option).
// shortcut: a 32-bit content hash stands for the string; a collision would answer a different option's memo (none seen
// offline), upgrade to the full content if the memo ever holds long keys
static const LONG kAsStrMax = 4096;
static volatile LONG g_asBadStrLogged;
static DWORD AsStrKey(DWORD s) {
    if (!s) return 1;  // null string: its own key, never 0
    const LONG len = *(const LONG*)(uintptr_t)(s + 4);
    if (len < 0 || len > kAsStrMax) {
        if (!InterlockedExchange(&g_asBadStrLogged, 1)) Log("AutShadow: string argument 0x%08X with length %ld not keyed (once)", s, len);
        return 0;
    }
    const u8* c = (const u8*)(uintptr_t)(s + 8);
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
        if (j == d.nOut) {
            if (!((d.strMask >> i) & 1)) key[n++] = a[i];
            else if (!(key[n++] = AsStrKey(a[i]))) return -1;  // not a string we can key: no memo row, no answer
        }
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
    e.flags &= ~(AE_FRESH | AE_NOPAY | AE_DISTRUST);
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

// LRU eviction (docs/122 §14.14): drops the eighth of the rows least recently used by the sim or the worker (memo rows
// only, when the memo quota is hit), so a full table keeps the keys the allow-listed definitions use instead of refusing
// every new key. Zero-argument rows (world constants, re-added at every cut) stay. Sim thread only (the worker reads the
// published versions); one O(n) pass plus a reindex per eighth of the table.
static void AsEvict(bool memoOnly) {
    AsTab& m = g_as->m;
    Ts3nAutShadow& k = ts3native_autshadow;
    LONG hist[64] = {0}, cand = 0;
    auto age = [](const AsEnt& e) { const LONG a = g_asCut - e.lastUsed; return a < 0 ? 0 : a > 63 ? 63 : a; };
    auto isCand = [memoOnly](const AsEnt& e) { return e.nKey && (!memoOnly || g_asDescOf[e.ic]->owner == AO_MEMO); };
    for (int i = 0; i < m.n; i++) if (isCand(m.e[i])) hist[age(m.e[i])]++, cand++;
    if (!cand) return;
    const LONG want = max(cand / 8, 1L);
    LONG T = 63, got = 0;
    while (T > 0 && got + hist[T] < want) got += hist[T--];
    LONG atT = want - got;  // rows of age T still to drop (the newest of the dropped ones)
    int w = 0;
    for (int i = 0; i < m.n; i++) {
        const AsEnt& e = m.e[i];
        if (isCand(e)) {
            const LONG a = age(e);
            if (a > T || (a == T && atT-- > 0)) {
                k.evicted++;
                if (g_asDescOf[e.ic]->owner == AO_MEMO) k.evictedMemo++, g_asMemoN--;
                continue;
            }
        }
        m.refs[w] = m.refs[i], m.e[w++] = e;
    }
    memset(m.refs + w, 0, (m.n - w) * 4);
    m.n = w;
    AsReindex(m);
}

// ---- list kinds (docs/122 §14.18) ----
// A list row's answer is the ids' managed objects of its type (key[0], the System.Type the closure passed; host +0xC0/+0xC4
// after the walker), so the rows of a type no Sim can be keep their answer whatever Sims do. ts3fixes classifies each type
// once (ListTypeNext/ListTypeSet: Type.IsAssignableFrom with Sim, both ways) [inferred: the host's type filter is an
// isinst-style check; ListAudit and Mode R A1-A3 would show a Sim in a Sim-free answer]. Unknown = can hold a Sim.
// shortcut: the System.Type pointer is the key (typeof objects live as long as the domain, the Boehm heap does not move
// them); a type object freed and its address reused by another type would keep the old class
struct AsLType { DWORD type; u8 cls, asked; };  // cls 0 unknown, 1 Sim-free, 2 can hold a Sim
static AsLType g_asLTypes[256];
static int g_asLTypeN;
static AsLType* AsLTypeOf(DWORD type, bool add) {
    for (int i = 0; i < g_asLTypeN; i++) if (g_asLTypes[i].type == type) return &g_asLTypes[i];
    if (!add || g_asLTypeN >= 256) return NULL;
    AsLType& t = g_asLTypes[g_asLTypeN++];
    t.type = type, t.cls = 0, t.asked = 0;
    return &t;
}
static bool AsLTypeSimFree(DWORD type) { const AsLType* t = AsLTypeOf(type, false); return t && t->cls == 1; }

static int AsInsert(int idx, const DWORD* key, int n, int flags) {
    AsTab& m = g_as->m;
    const bool memo = g_asDescOf[idx]->owner == AO_MEMO;
    if (memo && g_asMemoN >= AS_MEMO) AsEvict(true);
    if (m.n >= AS_CAP) AsEvict(false);
    if (m.n >= AS_CAP) { ts3native_autshadow.full++; return -1; }
    if (memo) g_asMemoN++;
    if (m.n + 1 > ts3native_autshadow.entriesMax) ts3native_autshadow.entriesMax = m.n + 1;
    AsEnt& e = m.e[m.n];
    memset(&e, 0, sizeof(e));
    e.ic = (WORD)idx, e.nKey = (u8)n, e.flags = (u8)(flags | AE_FRESH);
    memcpy(e.key, key, n * 4);
    const AsDesc& d = *g_asDescOf[idx];
    if ((d.owner == AO_OBJ || d.owner == AO_LIST) && d.at + 1 < n) e.owner = key[d.at] | (AsU64)key[d.at + 1] << 32;
    if (d.owner == AO_LIST && n >= 4) AsLTypeOf(key[0], true), AsSetAdd(g_as->listLots, e.owner);  // §14.18 kinds, lot prints
    e.lastUsed = g_asCut;
    if (d.owner == AO_OBJ) e.lastRead = g_asCut;  // RowHot: a new row is hot for RowHot cuts
    AsIndexAdd(m, m.n);
    return m.n++;
}

// ---- change tracking ----
static bool AsSetAdd(AsSet& s, AsU64 id) {
    if (!id) return true;
    for (DWORD h = (DWORD)(id ^ id >> 29) * 0x9E3779B1u, i = 0; i < 4096; i++) {
        AsU64& v = s.k[(h + i) & 4095];
        if (v == id) return true;
        if (!v) { if (s.n >= 3072) return false; v = id; s.at[s.n++] = (WORD)((h + i) & 4095); return true; }
    }
    return false;
}
static void AsSetClear(AsSet& s) {
    for (LONG i = 0; i < s.n; i++) s.k[s.at[i]] = 0;
    s.n = 0;
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
static AsU64 AsObjId(const void* obj) { return obj ? *(const AsU64*)((const u8*)obj + 8) : 0; }  // object id at +0x08 (docs/45 §2)

// ---- CutLean=2: dirty lots from the object events (docs/122 §14.17) ----
// Every object event of ours (added 4, destroy 5/6, scene 0x0B/0x0C, transform 0x0E, child 0x12, visual 0x18, parent 0x21)
// notes the object in the object-lot map. At the cut the touched objects are resolved: their world component's lot (+8)
// and room (+0x20), the reads the list walker makes (AsLotPrintGame); a new lot marks the old and the new lot's membership
// dirty, a new room marks the lot's room rows dirty, a destroy marks the last lot, a first sight marks its lot. A list row
// is re-checked only when its lot is dirty. [inferred: every lot or room change of an object comes with one of those
// events between two cuts (SetPosition publishes the transform, 0x0E, before world event 0x11, docs/76 §3); an object in a
// lot's vector has that lot in its component.] The safety net is ListAudit=N rows per cut against a fresh body call: a miss
// distrusts every list row for one cut (worker reads abort `list`) and switches the session back to CutLean=1 prints.
static int g_asLean;
static bool AsObjLotGame(const void* obj, AsU64* lot, DWORD* room);
static bool (*g_asObjLot)(const void*, AsU64*, DWORD*) = AsObjLotGame;  // selftest: fake objects
static AsOl* AsOlSlot(AsU64 id, bool add) {
    for (DWORD h = (DWORD)(id ^ id >> 29) * 0x9E3779B1u, i = 0; i < (DWORD)AS_OL; i++) {
        AsOl& o = g_as->ol[(h + i) & (AS_OL - 1)];
        if (o.id == id) return &o;
        if (!o.id) {
            if (!add || g_as->olN >= AS_OL_MAX) return NULL;
            g_as->olN++;
            memset(&o, 0, sizeof(o));
            o.id = id;
            return &o;
        }
    }
    return NULL;
}
// ---- typed marks (docs/122 §14.20): which list types an object can be ----
// mono-cost11 BP: every list body (6.9 a cut, 0.43 ms) answered the same; the lots were marked by short-lived props
// (created, parented to a Sim, destroyed: the `new`/`gone` causes) and by lot-vector prints re-marking those lots (`lite`).
// A list answer is the ids' managed objects of the row's type (§14.18 kinds, I-391-1), so an object that is none of the
// row's type cannot enter or leave it, and its add or removal leaves the order of the others [inferred, I-396-1: the same
// rule as the Sim-free kinds, which ListAudit and Mode R kept exact over mono-cost10/11]. ts3fixes classifies an object
// once against the list types (ObjClassTake/ObjClassSet at each request, sim thread: Type.IsInstanceOfType on the
// object's script target); a mark by a classified object dirties only the rows of its types. Queued: new objects (event 4)
// and every object whose mark found no class. Unknown = every type (the old mark).
static AsU64 g_asClsQ[512];
static LONG g_asClsQN;
static void AsClsWant(AsOl& o) {  // under g_asDirtyLock
    if (o.clsN || o.clsQ || o.gone || g_asClsQN >= 512) return;
    o.clsQ = 1;
    g_asClsQ[g_asClsQN++] = o.id;
}
static void AsClsWantLocked(AsOl& o) {
    while (InterlockedExchange(&g_asDirtyLock, 1)) YieldProcessor();
    AsClsWant(o);
    InterlockedExchange(&g_asDirtyLock, 0);
}
// the types this object can be: bit i for list type i; types it was not classified against (i >= clsN) count as possible
static DWORD AsOlMask(const AsOl& o) { return !o.clsN ? 0xFFFFFFFFu : o.cls | (o.clsN >= 32 ? 0 : ~((1u << o.clsN) - 1)); }
// under g_asDirtyLock; gone = destroy (5 before, 6 after): the pointer is never read again; ev = AS_EV_* bit of the event
static void AsObjNote(const void* obj, bool gone, WORD ev) {
    if (g_asLean != 2 || !obj) return;
    const AsU64 id = AsObjId(obj);
    if (!id) return;
    AsOl* o = AsOlSlot(id, true);
    if (!o) { g_as->olOver = true, g_as->olFull = true; return; }
    if (gone) {
        if (!o->gone && !o->seen) {  // destroyed before any cut placed it: its lot now (event 5: the object is still whole)
            AsU64 lot; DWORD room;
            // no lot: no component, so no lot's world-vector part held it; a lot vector that did is caught by its
            // lot-vector print at the cut (docs/122 §14.18), no resync; its pointer is a suspect there (§14.19: the prints
            // hash pointers, and the memory may come back as another object in the same vector slot)
            if (g_asObjLot(obj, &lot, &room) && lot) o->lot = lot, o->seen = 1;
            else ts3native_autshadow.unplaced++, AsSetAdd(g_as->suspectW, (AsU64)(uintptr_t)obj);
        }
        o->gone = 1, o->obj = NULL;
    } else if (!o->gone) {
        o->obj = obj;
        if (ev & AS_EV_ADD) AsClsWant(*o);  // classified at the next request, before the cut that would mark it `new`
    }
    if (!o->touched) o->ev = 0;
    o->ev |= ev;
    if (!o->touched) {
        if (g_as->touchN >= AS_TOUCH) { g_as->olOver = true; return; }
        o->touched = 1;
        g_as->touch[g_as->touchN++] = (int)(o - g_as->ol);
    }
}
static void AsDirtyObj(const void* obj, bool gone, WORD ev) {
    const AsU64 id = AsObjId(obj);
    while (InterlockedExchange(&g_asDirtyLock, 1)) YieldProcessor();
    const bool ok = AsSetAdd(g_as->dirty, id);
    AsObjNote(obj, gone, ev);
    InterlockedExchange(&g_asDirtyLock, 0);
    if (!ok) { ts3native_autshadow.dirtyOverflow++; AsEpochBump(); }
}
// unseen.cpp AsTierOf: a Sim found for classification gets its lot placed at the next cut (tier-1 lots, docs/122 §14.17)
static void AsObjSeen(const void* obj) {
    if (g_asLean != 2 || !g_as || !obj) return;
    while (InterlockedExchange(&g_asDirtyLock, 1)) YieldProcessor();
    AsObjNote(obj, false, 0);
    InterlockedExchange(&g_asDirtyLock, 0);
}
// object-event handlers (docs/76 §3.3): fn(object, data), return 1 so the chain goes on. One handler per event id (B = its
// index in kAsObjIds) so a touch knows which events it carries (docs/122 §14.19 attribution, new-object suspects).
static const DWORD kAsObjIds[] = {4, 5, 6, 0x0B, 0x0C, 0x0E, 0x12, 0x18, 0x21};  // docs/122 §2.2 object rows, docs/79 0x12, §14.17 5
template <int B> static char __cdecl AsObjEv(void* obj, void*) {
    InterlockedIncrement(&ts3native_autshadow.objEvents);
    const bool add = B == 0, gone = B == 1 || B == 2;  // ids 4; 5, 6
    InterlockedExchangeAdd(&g_asListGen, add || gone ? 1 : 0);
    AsDirtyObj(obj, gone, (WORD)(1u << B));
    return 1;
}

static void AsObjRegister() {
    Ts3nAutShadow& k = ts3native_autshadow;
    static void* const fns[] = {(void*)AsObjEv<0>, (void*)AsObjEv<1>, (void*)AsObjEv<2>, (void*)AsObjEv<3>, (void*)AsObjEv<4>,
                                (void*)AsObjEv<5>, (void*)AsObjEv<6>, (void*)AsObjEv<7>, (void*)AsObjEv<8>};
    static_assert(sizeof(fns) / sizeof(fns[0]) == sizeof(kAsObjIds) / sizeof(kAsObjIds[0]), "one handler per event id");
    for (int b = 0; b < (int)(sizeof(kAsObjIds) / sizeof(kAsObjIds[0])); b++) {
        const DWORD id = kAsObjIds[b];
        if (!SnObjListed(id, fns[b]) && !g_snObjReg(id, fns[b], 0x7FFFFFFF)) { Log("FAIL autshadow object event 0x%lX node", id); continue; }
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
// 1,024 records: mono-cost5 BP worker runs missed ~944 keys each and the 64-deep ring dropped 672,605 (docs/122 §14.14);
// the cut takes at most AS_LEARN_CUT per cut (each new key is one body call), the rest waits in the ring
static const LONG AS_RING = 1024, AS_LEARN_CUT = 256;
static AsLearnRec g_asRing[AS_RING];
static volatile LONG g_asRingHead, g_asRingTail;  // head: worker, tail: sim

static void AsLearn(DWORD idx, const DWORD* key, int n) {
    const LONG h = g_asRingHead;
    if (h - g_asRingTail >= AS_RING) {
        InterlockedIncrement(&ts3native_autshadow.learnDropped);
        InterlockedIncrement(&ts3native_autshadow.ringDropped);
        return;
    }
    AsLearnRec& r = g_asRing[h & (AS_RING - 1)];
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
    if (!d) { AsAbort(g_asNoDesc[idx], idx); return fp; }  // never the body: H12 route ICs would answer for handle 0
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
    if (n < 0) { AsAbort(d->reason, idx); return fp; }
    const int i = AsFind(t, (int)idx, key, n);
    if (i < 0 || (t.e[i].flags & AE_NOPAY)) {
        InterlockedIncrement(&k.workerMisses);
        if (i < 0 && d->owner != AO_MEMO) AsLearn(idx, key, n);  // memo keys can only come from the sim's own calls
        static volatile LONG s_missLogged;  // which keys miss (docs/122 §14.15: mono-cost6 `row` = Slots_GetParentIdImpl)
        if (InterlockedIncrement(&s_missLogged) <= 48)
            Log("autshadow miss %s key %08lX %08lX %08lX %08lX%s", g_asName[idx] ? g_asName[idx] : "-", n > 0 ? key[0] : 0, n > 1 ? key[1] : 0,
                n > 2 ? key[2] : 0, n > 3 ? key[3] : 0, i >= 0 ? " (no payload)" : "");
        AsAbort(d->reason, idx);
        return fp;
    }
    const AsEnt& e = t.e[i];
    t.used[i] = 1;
    if (e.flags & AE_COLD) {  // not refreshed at this cut (ColdIc): no answer; the IC is hot from the next cut on
        InterlockedIncrement(&k.workerMisses);
        InterlockedIncrement(&k.coldReads);
        AsLearn(idx | AS_LEARN_HEAT, key, 0);
        AsAbort(d->reason, idx);
        return fp;
    }
    if (e.flags & AE_DISTRUST) {  // CutLean=2: a list row not vouched for at this cut; its lot is tier 1 from the next cut
        const DWORD owner[2] = {(DWORD)e.owner, (DWORD)(e.owner >> 32)};
        InterlockedIncrement(&k.workerMisses);
        AsLearn(idx | AS_LEARN_LOT, owner, 2);
        AsAbort(d->reason, idx);
        return fp;
    }
    if (e.flags & AE_STALE) {  // Sim the request could not reach: not refreshed at this cut (docs/122 §2.3 simstale)
        const DWORD owner[2] = {(DWORD)e.owner, (DWORD)(e.owner >> 32)};
        AsLearn(idx | AS_LEARN_SIM, owner, 2);
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
    if (n < 0) return fp;  // unkeyable string: the game's answer, nothing recorded
    int i = AsFind(g_as->m, (int)idx, key, n);
    if (i < 0 && (i = AsInsert((int)idx, key, n, 0)) >= 0) ts3native_autshadow.memoRecords++;
    if (i >= 0) {
        AsEnt& e = g_as->m.e[i];
        e.lastUsed = g_asCut;  // LRU: the sim's own use keeps a memo row
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
// [AutShadow] Satb (satb.cpp, docs/206 §7.3): while a barrier span is on, every other thread's IC call is checked first
// for pointer arguments into shared memory (the body may write there)
static volatile LONG g_asSatbIc;
static void(__cdecl* g_asSatbIcFn)(DWORD idx, const DWORD* args);
static __declspec(naked) void AsGuardEntry() {
    __asm {
        push eax
        mov eax, dword ptr fs:[0x24]
        cmp eax, dword ptr [g_asWorkerTid]
        je worker
        add dword ptr [g_asSimCalls], 1
        cmp dword ptr [g_asSatbIc], 0
        jne satb
    pass:
        mov eax, [esp + 4]
        mov eax, dword ptr [g_asOrig + eax * 4]
        mov [esp + 4], eax
        pop eax
        ret
    satb:  // [esp] eax, [esp+4] index, [esp+8] return, [esp+12] first argument
        pushad
        pushfd
        lea ecx, [esp + 36 + 12]
        push ecx
        push dword ptr [esp + 36 + 4 + 4]
        call dword ptr [g_asSatbIcFn]
        add esp, 8
        popfd
        popad
        jmp pass
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
static int g_asIcParent = -1;  // guard index of Slots_GetParentIdImpl
struct AsPar { AsU64 owner, parent; u8 flags; };
static AsPar g_asPar[AS_CAP];  // the parent rows as the last refresh loop left them (docs/122 §14.19)
static int g_asParN;
static void AsTouchParent(AsU64 id) {
    const DWORD key[2] = {(DWORD)id, (DWORD)(id >> 32)};
    const int p = AsFind(g_as->m, g_asIcParent, key, 2);
    if (p >= 0) g_as->m.e[p].lastUsed = g_asCut; else AsInsert(g_asIcParent, key, 2, 0);
}
static void AsFoldUsed(AsTab& t, bool clear) {
    for (int i = 0; i < t.n; i++) {
        if (!t.used[i]) continue;
        const int m = AsFind(g_as->m, t.e[i].ic, t.e[i].key, t.e[i].nKey);
        const bool icHot = g_asIcHit[t.e[i].ic] && g_asCut - g_asIcHit[t.e[i].ic] <= AS_HOT;
        g_asIcHit[t.e[i].ic] = g_asCut;  // ColdIc: the IC stays hot
        if (m >= 0) {
            AsEnt& me = g_as->m.e[m];
            if (g_asDescOf[me.ic]->owner == AO_OBJ) {  // RowHot (docs/122 §14.19): the row is hot from this cut
                if ((t.e[i].flags & AE_COLD) && (icHot || !g_asColdIc) && g_asRowHot) ts3native_autshadow.rowColdReads++;
                if (((t.e[i].flags | me.flags) & AE_COLD) && !AsSetAdd(g_as->readCold, me.owner)) g_asReadColdOver = true;  // its run's outcome may change (RunSkip)
                me.lastRead = g_asCut;
            }
            g_as->m.e[m].lastUsed = g_asCut;
            // LRU: an object row in use keeps its parent row (the dirty propagation needs it; re-added if evicted)
            if (t.e[i].owner && g_asDescOf[t.e[i].ic]->owner == AO_OBJ && g_asIcParent >= 0 && t.e[i].ic != g_asIcParent) AsTouchParent(t.e[i].owner);
        }
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

static int g_asIcRng = -1;     // guard index of Random_NextDoubleImpl (the reference's draws, docs/125 §2)
// a learned key names an object: add its parent entry (the dirty propagation needs it) and, as learned rows, its other
// (ulong) object rows, so a new object costs one worker abort instead of one per IC it reads (docs/122 §14.15: mono-cost6
// `row` aborts were all first reads of an object, Slots_GetParentIdImpl first)
static void AsLearnObject(AsU64 id) {
    const DWORD key[3] = {(DWORD)id, (DWORD)(id >> 32), 0};
    for (int i = 0; i < g_asIcN; i++) {
        const AsDesc* d = g_asDescOf[i];
        if (!d || d->owner != AO_OBJ || (d->nArg != 2 && d != &kAsDesc[7])) continue;  // as AsAddRowKind
        for (DWORD b = 0; b < (d->nArg == 3 ? 2u : 1u); b++) {
            const DWORD k3[3] = {key[0], key[1], b};
            if (AsFind(g_as->m, i, k3, d->nArg) < 0) AsInsert(i, k3, d->nArg, i == g_asIcParent ? 0 : AE_LEARNED);
        }
    }
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
            const int f = AsFind(g_as->m, i, k3, d->nArg);
            if (f >= 0) g_as->m.e[f].lastUsed = g_asCut;  // LRU: a request's own rows stay
            else AsInsert(i, k3, d->nArg, 0);
        }
    }
    if (sim) AsSetAdd(g_as->always, id);
    if (kind) AsSetAdd(g_as->simIds, id);  // docs/122 §14.18: its moves mark the Sim sets
    // the request's first reachable Sim is its actor (ts3fixes adds it first); a shared cut has one per request
    if (sim && (!g_asActor || g_asNextActor)) {
        if (!g_asActor) g_asActor = id, g_asActorN = 0;  // a new batch
        if (g_asActorN < 8) g_asActors[g_asActorN++] = id;
        g_asNextActor = false;
    }
}
static bool AsIsActor(AsU64 id) {
    for (int i = 0; i < g_asActorN; i++) if (g_asActors[i] == id) return true;
    return id && id == g_asActor;
}

// ---- CutLean=1: change-only list refresh (docs/122 §14.16) ----
// Query_CountObjectsOnLot 0x749BC0 / Query_GetObjectsOnLot 0x749D30 (type, lot, room) = walker 0x7499B0 (ids), then host
// +0xC4 / +0xC0 (count / array of the ids' managed objects of `type`) [verified disassembly]. The walker reads: the lot
// found by id (0xC62D40, thiscall(mgr [0x11ECBC4]; lo, hi, 0), ret 0xC), its object vector {begin, end} at lot +0x14 (ids at
// object +8/+0xC; with room != -1 only those whose world component (0x935770, thiscall(obj; [0x11ECF08]), ret 4) has
// room +0x20 == room), then the world lot's vector ([mgr +0xB0] +0x14): objects whose component lot +8/+0xC is the lot
// (room != -1: EA's 0/1 flag compare against the room, docs/127 §5). The print hashes exactly those reads in walk order
// (ids, plus component rooms for room rows), so an equal print = an equal id list = the body's answer again, as long as
// an id keeps its managed object and class [inferred: host +0xC0's id -> object map; no rebinding of a listed id is
// known; the audit (Audit=N) and Mode R A1-A3 would show one]. World event 0x11 (~1,000/s on BP, mostly room changes of
// walking Sims) moves the global generation at almost every cut; room changes leave the room -1 print alone.
// shortcut: one walk per lot and cut (the world lot's vector walked once per lot, as each body call does); share one world
// walk across lots if listPrintUs shows it
typedef void*(__fastcall* AsFindLotFn)(void* mgr, void* edx, DWORD lo, DWORD hi, DWORD z);
typedef const u8*(__fastcall* AsCompFn)(const void* obj, void* edx, DWORD bit);
static void* const* g_asWmPP = (void* const*)(uintptr_t)0x011ECBC4;
static AsFindLotFn g_asFindLot = (AsFindLotFn)(uintptr_t)0x00C62D40;
static AsCompFn g_asComp = (AsCompFn)(uintptr_t)0x00935770;
static const DWORD* g_asCompBit = (const DWORD*)(uintptr_t)0x011ECF08;
static const LONG AS_AGE_EVERY = 8;  // CutLean=1: aging and its reindex every 8th cut (unused rows live up to 7 cuts longer)
static void AsMix(AsU64& h, DWORD v) { h ^= v; h *= 0x100000001B3ull; }
static void AsMixVec(const u8* owner, AsU64 lot, bool world, AsU64* p) {
    const void* const* b = *(const void* const* const*)(owner + 0x14);
    const void* const* e = *(const void* const* const*)(owner + 0x18);
    for (; b && b < e; b++) {
        const u8* o = (const u8*)*b;
        if (!o) { AsMix(p[0], 0), AsMix(p[1], 0); continue; }
        const u8* c = g_asComp(o, NULL, *g_asCompBit);
        if (world && (!c || *(const AsU64*)(c + 8) != lot)) continue;
        const DWORD lo = *(const DWORD*)(o + 8), hi = *(const DWORD*)(o + 0xC);
        AsMix(p[0], lo), AsMix(p[0], hi), AsMix(p[1], lo), AsMix(p[1], hi);
        AsMix(p[1], c ? *(const DWORD*)(c + 0x20) : 0xDEADu);
    }
}
// p[0] = print of a room -1 call, p[1] = of a room call; false = could not read (refresh)
static bool AsLotPrintGame(AsU64 lot, AsU64* p) {
    p[0] = p[1] = 0xCBF29CE484222325ull;
    __try {
        void* mgr = *g_asWmPP;
        if (!mgr) return true;
        const u8* L = (const u8*)g_asFindLot(mgr, NULL, (DWORD)lot, (DWORD)(lot >> 32), 0);
        AsMix(p[0], L ? 1 : 2), AsMix(p[1], L ? 1 : 2);
        if (!L) return true;  // no lot: the walker returns no ids
        AsMixVec(L, lot, false, p);
        AsMix(p[0], 0xFFFFFFFFu), AsMix(p[1], 0xFFFFFFFFu);
        const u8* W = *(const u8* const*)((const u8*)mgr + 0xB0);
        if (W) AsMixVec(W, lot, true, p);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static bool (*g_asLotPrint)(AsU64, AsU64*) = AsLotPrintGame;  // selftest: a fake world
static struct { AsU64 lot, p[2]; bool ok; } g_asPrint[64];
static int g_asPrintN;
static double g_asPrintUsPerTick;
static bool AsPrintOf(AsU64 lot, int room, AsU64* out) {
    int i = 0;
    while (i < g_asPrintN && g_asPrint[i].lot != lot) i++;
    if (i == g_asPrintN) {
        AsU64 p[2];
        LARGE_INTEGER a, b;
        QueryPerformanceCounter(&a);
        const bool ok = g_asLotPrint(lot, p);
        QueryPerformanceCounter(&b);
        ts3native_autshadow.listLots++;
        ts3native_autshadow.listPrintUs += (LONG)((b.QuadPart - a.QuadPart) * g_asPrintUsPerTick);
        if (i == 64) { *out = p[room != -1]; return ok; }  // cache full: not cached
        g_asPrint[i].lot = lot, g_asPrint[i].p[0] = p[0], g_asPrint[i].p[1] = p[1], g_asPrint[i].ok = ok;
        g_asPrintN++;
    }
    *out = g_asPrint[i].p[room != -1];
    return g_asPrint[i].ok;
}

// CutLean=2: the walker's per-object reads (component lot +8, room +0x20); no component = not placeable (resync)
static bool AsObjLotGame(const void* obj, AsU64* lot, DWORD* room) {
    __try {
        const u8* c = g_asComp(obj, NULL, *g_asCompBit);
        if (!c) return false;
        *lot = *(const AsU64*)(c + 8), *room = *(const DWORD*)(c + 0x20);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static bool g_asOlUnknown;  // this cut: touches were lost (touch list or map full); every list row goes through the print and the map resyncs
static bool g_asDirtyTrip;  // a ListAudit miss: dirty mode off for the session
static int g_asListAudit = 1;
// lot-vector print baselines (below): per lot, the print of each part (0 objects, 1 Sims) and the cut it was taken at;
// -1 = no baseline (a membership mark since then: the rows were refreshed against a state the print never saw)
struct AsLite { AsU64 lot, p[2]; LONG cut[2]; };
static AsLite g_asLite[1024];
static AsLite* AsLiteSlot(AsU64 lot, bool add) {
    for (DWORD h = (DWORD)(lot ^ lot >> 29) * 0x9E3779B1u, j = 0; j < 1024; j++) {
        AsLite& s = g_asLite[(h + j) & 1023];
        if (s.lot == lot) return &s;
        if (!s.lot) {
            if (!add) return NULL;
            memset(&s, 0, sizeof(s));
            s.lot = lot, s.cut[0] = s.cut[1] = -1;
            return &s;
        }
    }
    return NULL;  // full: no baseline (the lot is marked whenever prints run)
}
static bool g_asLiteDue;  // this cut placed a touch that could not be placed: the lot-vector prints run
// docs/122 §14.19 attribution: per lot marked at this cut, the causes (AS_MC_* bits); rows refreshed for the lot count them
static struct { AsU64 lot; DWORD why; } g_asLotWhy[256];
static int g_asLotWhyN;
static void AsLotWhy(AsU64 lot, int cause) {
    int i = 0;
    while (i < g_asLotWhyN && g_asLotWhy[i].lot != lot) i++;
    if (i == g_asLotWhyN) { if (i == 256) return; g_asLotWhy[g_asLotWhyN++] = {lot, 0}; }  // shortcut: over 256 lots a cut, the rest unattributed
    g_asLotWhy[i].why |= 1u << cause;
}
static DWORD AsLotWhyOf(AsU64 lot) { for (int i = 0; i < g_asLotWhyN; i++) if (g_asLotWhy[i].lot == lot) return g_asLotWhy[i].why; return 0; }
// typed marks of this cut (docs/122 §14.20): per lot, the list types (bits) whose rows a classified object's change can
// touch, for membership (mem) and room rows (room); any* = some typed mark (types past 32 are never classified: possible)
struct AsLotTy { AsU64 lot; DWORD mem, room; u8 memAny, roomAny; };
static AsLotTy g_asLotTy[256];
static int g_asLotTyN;
static AsLotTy* AsLotTyOf(AsU64 lot, bool add) {
    for (int i = 0; i < g_asLotTyN; i++) if (g_asLotTy[i].lot == lot) return &g_asLotTy[i];
    if (!add || g_asLotTyN >= 256) return NULL;
    AsLotTy& t = g_asLotTy[g_asLotTyN++];
    memset(&t, 0, sizeof(t));
    t.lot = lot;
    return &t;
}
static int AsLTypeIdx(DWORD type) { for (int i = 0; i < g_asLTypeN; i++) if (g_asLTypes[i].type == type) return i; return -1; }
static void AsMarkLog(int cause, AsU64 lot, bool member, WORD ev, const AsOl* o, DWORD mask) {
    static int s_logged;  // which objects dirty lots: id, cause, events, the object's vtable (its native kind), typed mask
    if (!o || s_logged >= 32) return;
    DWORD vt = 0;
    if (o->obj) __try { vt = *(const DWORD*)o->obj; } __except (EXCEPTION_EXECUTE_HANDLER) { vt = 0; }
    s_logged++;
    Log("autshadow mark %s lot 0x%08lX%08lX%s by 0x%08lX%08lX events 0x%03X vtable 0x%08lX types 0x%08lX", kAsMcName[cause], (DWORD)(lot >> 32),
        (DWORD)lot, member ? "" : " (room)", (DWORD)(o->id >> 32), (DWORD)o->id, ev, vt, mask);
}
// mask = the list types the change can touch (0xFFFFFFFF = all: the plain sets); sim = the Sim sets (mask ignored)
static void AsLotMark(AsU64 lot, bool member, bool sim = false, int cause = AS_MC_LITE, WORD ev = 0, const AsOl* o = NULL, DWORD mask = 0xFFFFFFFFu) {
    if (!lot) return;
    Ts3nAutShadow& k = ts3native_autshadow;
    AsSet& s = sim ? (member ? g_as->lotDirtySim : g_as->lotRoomSim) : (member ? g_as->lotDirty : g_as->lotRoom);
    if (member) { AsLite* t = AsLiteSlot(lot, false); if (t) t->cut[sim] = -1; }
    if (!sim) AsLotWhy(lot, cause);
    if (AsSetHas(s, lot)) return;
    if (!sim && mask != 0xFFFFFFFFu) {  // typed (docs/122 §14.20); the lot table full: the plain mark below
        const bool had = AsLotTyOf(lot, false) != NULL;
        if (AsLotTy* t = AsLotTyOf(lot, true)) {
            (member ? t->mem : t->room) |= mask;
            (member ? t->memAny : t->roomAny) = 1;
            k.typedMarks++;
            if (!had) {
                k.dirtyLots++, k.markBy[cause]++;
                for (int b = 0; b < 9; b++) if (ev >> b & 1) k.markEv[b]++;
            }
            AsMarkLog(cause, lot, member, ev, o, mask);
            return;
        }
    }
    if (!AsSetAdd(s, lot)) { g_asOlUnknown = true; return; }
    if (sim) { k.simMarks++; return; }
    if (!AsLotTyOf(lot, false)) {  // a lot typed-marked earlier this cut was counted then
        k.dirtyLots++, k.markBy[cause]++;
        for (int b = 0; b < 9; b++) if (ev >> b & 1) k.markEv[b]++;
    }
    AsMarkLog(cause, lot, member, ev, o, mask);
}
// docs/122 §14.18 list kinds: a Sim's change goes to the Sim sets (read only by list rows whose type can hold a Sim), an
// object's to the plain sets (read by every list row). A Sim with a child in our parent rows marks both: the child moves
// with it [inferred: no own event for a carried child's lot or room is known; its lot-vector membership is covered by the
// lot-vector print, its room by this rule only for children we have rows for; ListAudit is the net for the rest].
// docs/122 §14.20: an object's mark is typed by its class; an unclassified object marks every type and is queued.
static void AsObjMark(AsOl& o, AsU64 lot, bool member, int cause) {
    const bool sim = AsSetHas(g_as->simIds, o.id), kid = sim && AsSetHas(g_as->kids, o.id);
    if (!sim && !o.clsN) AsClsWantLocked(o);
    if (!sim || kid) AsLotMark(lot, member, false, kid ? AS_MC_KID : cause, o.ev, &o, kid ? 0xFFFFFFFFu : AsOlMask(o));
    if (sim) AsLotMark(lot, member, true, cause, o.ev, &o);
}
// the cut's unplaceable objects (docs/122 §14.20): a lot-vector print change is theirs, so its mark takes their classes
static DWORD g_asLiteMask;
static bool g_asLiteAll;
static void AsLiteObj(AsOl& o) {
    if (!o.clsN && !AsSetHas(g_as->simIds, o.id)) AsClsWantLocked(o);
    if (!o.clsN || AsSetHas(g_as->simIds, o.id)) g_asLiteAll = true;
    else g_asLiteMask |= AsOlMask(o);
}
// one map row against the object now; mark = what changed (false during a resync or a seeding: no marks)
static void AsOlPlace(AsOl& o, const void* obj, bool mark) {
    AsU64 lot = 0;
    DWORD room = 0;
    bool ok = false;
    if (obj) __try { ok = AsObjId(obj) == o.id; } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    ok = ok && g_asObjLot(obj, &lot, &room);
    if (mark) o.placedCut = g_asCut;
    if (!ok) {  // destroyed, reused or no component: the last known lot loses it; never placed = only a lot vector can hold
        // it (no component, no world-vector part), and the lot-vector prints below catch that (docs/122 §14.18: no resync)
        if (mark) {
            if (o.seen) AsObjMark(o, o.lot, true, AS_MC_LOST); else ts3native_autshadow.unplaced++;
            g_asLiteDue = true;
            AsLiteObj(o);
            // a new object with no component may sit in a vector slot whose memory held another object (the prints hash
            // pointers, docs/122 §14.19): its pointer is a suspect this cut
            if (obj && (o.ev & AS_EV_ADD)) AsSetAdd(g_as->suspect, (AsU64)(uintptr_t)obj);
        }
        o.seen = 0, o.unpl = 1;
        return;
    }
    // placed again after an unplaceable stretch: where it sat meanwhile only a lot vector knows (the prints run this cut)
    if (o.unpl && mark) g_asLiteDue = true, AsLiteObj(o);
    o.unpl = 0;
    if (mark) {
        // docs/122 §14.19: with seeding, an object of a list lot already has its row, so a first sight here is an object
        // that was on no list lot (or a new one): only its new lot can have changed
        if (!o.seen) AsObjMark(o, lot, true, o.ev & AS_EV_ADD ? AS_MC_NEW : AS_MC_FIRST);
        else if (lot != o.lot) AsObjMark(o, o.lot, true, AS_MC_LOT), AsObjMark(o, lot, true, AS_MC_LOT);
        else if (room != o.room) AsObjMark(o, lot, false, AS_MC_ROOM);
    }
    o.lot = lot, o.room = room, o.seen = 1;
}

// ---- seeding (docs/122 §14.19) ----
// Before 0.36.119 an object's first touch since the epoch marked its lot, because its earlier lot was unknown: on BP the map
// grew by ~28 rows a cut (17,655 rows over 631 cuts), mostly objects that never moved (visual or state events), and every
// list row of their lots was refreshed for nothing (mono-cost10: 7.3 list bodies a cut, every one with the same answer).
// Seeding puts every object of a lot with list rows into the map (lot and room as the walker reads them) at the cut the lot
// gets its first list row or after an epoch, i.e. the cut those rows are refreshed in, so a later touch compares with the
// state the rows hold: no mark unless it moved. An object with no row then was on no list lot, and its first touch marks
// only its new lot (also exact for the old lot, which the old rule never marked) [inferred: the event coverage of §14.17;
// an object in a lot's vector has that lot in its component].
static void AsSeedObj(const void* obj) {
    const AsU64 id = obj ? AsObjId(obj) : 0;
    if (!id || AsOlSlot(id, false)) return;  // a row: current (every change since came with a touch)
    if (g_as->olN >= AS_OL_MAX - 1024) { g_as->olFull = true; return; }  // headroom for touches; the lot is counted in part
    AsOl* o = AsOlSlot(id, true);
    if (!o) return;
    o->obj = obj;
    AsOlPlace(*o, obj, false);
    ts3native_autshadow.seedObjs++;
}
// the lot's own vector (lot +0x14) and the world lot's objects whose component names one of the lots (AsLotPrintGame)
static bool AsSeedGame(const AsSet& lots) {
    __try {
        void* mgr = *g_asWmPP;
        if (!mgr) return true;
        for (LONG i = 0; i < lots.n; i++) {
            const AsU64 lot = lots.k[lots.at[i]];
            const u8* L = (const u8*)g_asFindLot(mgr, NULL, (DWORD)lot, (DWORD)(lot >> 32), 0);
            if (!L) continue;
            const void* const* b = *(const void* const* const*)(L + 0x14);
            const void* const* e = *(const void* const* const*)(L + 0x18);
            for (; b && b < e; b++) AsSeedObj(*b);
        }
        const u8* W = *(const u8* const*)((const u8*)mgr + 0xB0);
        if (!W) return true;
        const void* const* b = *(const void* const* const*)(W + 0x14);
        const void* const* e = *(const void* const* const*)(W + 0x18);
        for (; b && b < e; b++) {
            const u8* c = *b ? g_asComp(*b, NULL, *g_asCompBit) : NULL;
            if (c && AsSetHas(lots, *(const AsU64*)(c + 8))) AsSeedObj(*b);
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static bool (*g_asSeed)(const AsSet&) = AsSeedGame;  // selftest: a fake world
static void AsSeedLots() {  // under g_asDirtyLock (rows are added)
    Ts3nAutShadow& k = ts3native_autshadow;
    static AsSet s_need;
    AsSetClear(s_need);
    for (LONG i = 0; i < g_as->listLots.n; i++) {
        const AsU64 lot = g_as->listLots.k[g_as->listLots.at[i]];
        if (!AsSetHas(g_as->seeded, lot)) AsSetAdd(s_need, lot);
    }
    if (!s_need.n) return;
    LARGE_INTEGER a, b;
    QueryPerformanceCounter(&a);
    const bool full0 = g_as->olFull;
    g_as->olFull = false;
    // a walk that faulted or a map near full leaves some objects unseeded: they keep the first-sight mark (the pre-0.36.119
    // rule); the lots count as seeded either way, so a fault is not retried (and paid) at every cut
    const bool ok = g_asSeed(s_need);
    for (LONG i = 0; i < s_need.n; i++) AsSetAdd(g_as->seeded, s_need.k[s_need.at[i]]), k.seedLots++;
    if (!ok || g_as->olFull) k.seedFull += s_need.n;
    g_as->olFull = full0;
    QueryPerformanceCounter(&b);
    k.seedUs += (LONG)((b.QuadPart - a.QuadPart) * g_asPrintUsPerTick);
}

// ---- lot-vector prints (docs/122 §14.18) ----
// At a CutLean=2 cut that placed a touch it could not place (no world component, reused pointer, destroyed unplaced), per
// lot with list rows: a hash of the lot's own object vector (lot +0x14: pointers and ids at +8, in order; no component
// reads), known Sims apart. A print that differs from the lot's baseline marks the lot (Sim part: the Sim sets) as an event
// would, so such a touch needs no resync cut: an object with no world component can only be in a lot's own vector (the
// walker's world-vector part keeps objects by their component's lot) [inferred from the walker reads, AsLotPrintGame;
// verified offline only]. A baseline stays valid until a membership mark (the rows then refresh against a state the print
// did not see): between its cut and now every lot-vector change was either marked by an event or made by an unplaceable
// touch, and every cut with one of those runs the prints. A lot with no baseline is marked once.
// docs/122 §14.19: pointers only (mono-cost10 BP: 6.5 prints a cut cost 123 us, one id read per object). The ids caught a
// pointer reused by another object in the same slot; that needs a destroy and an add, and the reuse is caught without
// them: a destroyed object that was placed marks its lot (no baseline survives there), and the pointer of a destroyed
// unplaced object or of a new unplaceable one is a suspect at the cut (g_as->suspect): a vector holding it moves its print
// (a cut nonce) [inferred: an object's id at +8 never changes while it lives (AsOlPlace's reuse test relies on the same)].
static bool AsLotLiteGame(AsU64 lot, AsU64* p) {
    p[0] = p[1] = 0xCBF29CE484222325ull;
    __try {
        void* mgr = *g_asWmPP;
        if (!mgr) return true;
        const u8* L = (const u8*)g_asFindLot(mgr, NULL, (DWORD)lot, (DWORD)(lot >> 32), 0);
        if (!L) { AsMix(p[0], 2); return true; }
        const void* const* b = *(const void* const* const*)(L + 0x14);
        const void* const* e = *(const void* const* const*)(L + 0x18);
        const bool sus = g_as->suspect.n > 0;
        for (; b && b < e; b++) {
            const AsU64 o = (AsU64)(uintptr_t)*b;
            AsU64& h = p[o && AsSetHas(g_as->simPtrs, o)];
            AsMix(h, (DWORD)o);
            if (sus && AsSetHas(g_as->suspect, o)) AsMix(p[0], 0x5EED0000u ^ (DWORD)g_asCut), AsMix(p[1], 0x5EED0000u ^ (DWORD)g_asCut), ts3native_autshadow.liteSuspect++;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static bool (*g_asLotLite)(AsU64, AsU64*) = AsLotLiteGame;  // selftest: a fake world
static void AsLotLites() {
    Ts3nAutShadow& k = ts3native_autshadow;
    LARGE_INTEGER a, b;
    QueryPerformanceCounter(&a);
    AsSetClear(g_as->simPtrs);
    for (LONG i = 0; i < g_as->simIds.n; i++) {
        const AsOl* o = AsOlSlot(g_as->simIds.k[g_as->simIds.at[i]], false);
        if (o && o->obj && !o->gone) AsSetAdd(g_as->simPtrs, (AsU64)(uintptr_t)o->obj);
    }
    for (LONG i = 0; i < g_as->listLots.n; i++) {
        const AsU64 lot = g_as->listLots.k[g_as->listLots.at[i]];
        AsLite* t = AsLiteSlot(lot, true);
        AsU64 p[2];
        const bool ok = g_asLotLite(lot, p);
        k.liteLots++;
        for (int s = 0; s < 2; s++) {
            const bool base = t && ok && t->cut[s] >= 0;
            if (!base || p[s] != t->p[s]) {
                const bool had = AsSetHas(s ? g_as->lotDirtySim : g_as->lotDirty, lot);
                // docs/122 §14.20: the object part changed (or has no baseline: a mark since the last print) through this
                // cut's unplaceable objects only: every other lot-vector change came with an event and was marked at its own
                // cut (typed by its object), and every earlier unplaceable one ran the prints at its cut. So the mark takes
                // their classes; none of a list type = no mark (rows of types past 32 still refresh: memAny)
                if (s == 0 && t && ok && !g_asLiteAll && !g_asOlUnknown) {
                    k.liteTyped++;
                    if (g_asLiteMask) AsLotMark(lot, true, false, AS_MC_LITE, 0, NULL, g_asLiteMask);
                    else if (AsLotTy* ty = AsLotTyOf(lot, true)) ty->memAny = 1, k.liteTypedNone++;  // a typed-kept candidate for the audit
                    else AsLotMark(lot, true, false);
                } else AsLotMark(lot, true, s == 1);
                if (!had && base) k.liteMarks++;
            }
            if (t) t->p[s] = p[s], t->cut[s] = ok ? g_asCut : -1;  // after the mark: this print is the new baseline
        }
    }
    QueryPerformanceCounter(&b);
    k.liteUs += (LONG)((b.QuadPart - a.QuadPart) * g_asPrintUsPerTick);
}

// drops destroyed rows no pending touch names (under g_asDirtyLock); a full map otherwise forces resync cuts
static void AsOlCompact() {
    AsOl* tmp = (AsOl*)VirtualAlloc(NULL, sizeof(AsOl) * AS_OL, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!tmp) return;
    int n = 0;
    for (int i = 0; i < AS_OL; i++) { const AsOl& o = g_as->ol[i]; if (o.id && (!o.gone || o.touched)) tmp[n++] = o; }
    memset(g_as->ol, 0, sizeof(g_as->ol));
    g_as->olN = 0, g_as->touchN = 0;
    for (int i = 0; i < n; i++) {
        AsOl* o = AsOlSlot(tmp[i].id, true);  // fits: n <= the old row count
        *o = tmp[i];
        if (o->touched) g_as->touch[g_as->touchN++] = (int)(o - g_as->ol);
    }
    VirtualFree(tmp, 0, MEM_RELEASE);
    ts3native_autshadow.olCompact++;
}
static void AsOlResolve(bool clear) {
    Ts3nAutShadow& k = ts3native_autshadow;
    static struct { int slot; const void* obj; } s_t[AS_TOUCH];
    static LONG s_compactCut = -1000;
    AsSetClear(g_as->lotDirty), AsSetClear(g_as->lotRoom), AsSetClear(g_as->lotDirtySim), AsSetClear(g_as->lotRoomSim);
    AsSetClear(g_as->suspect);
    g_asOlUnknown = false;
    g_asLotWhyN = 0;
    g_asLotTyN = 0, g_asLiteMask = 0, g_asLiteAll = false;
    if (g_asLean != 2) return;
    LARGE_INTEGER a, b;
    QueryPerformanceCounter(&a);
    while (InterlockedExchange(&g_asDirtyLock, 1)) YieldProcessor();
    if (clear) {  // epoch: every row is refreshed anyway; the lots are seeded again below
        memset(g_as->ol, 0, sizeof(g_as->ol)), g_as->olN = 0, g_as->touchN = 0, memset(g_asLite, 0, sizeof(g_asLite));
        AsSetClear(g_as->seeded), AsSetClear(g_as->suspectW);
    } else if (g_as->olN > AS_OL_MAX - 4096 && g_asCut - s_compactCut >= 64) AsOlCompact(), s_compactCut = g_asCut;
    const int n = g_as->touchN;
    for (int i = 0; i < n; i++) {
        AsOl& o = g_as->ol[g_as->touch[i]];
        s_t[i].slot = g_as->touch[i], s_t[i].obj = o.obj, o.touched = 0;
    }
    g_as->touchN = 0;
    const bool over = g_as->olOver, mapFull = g_as->olFull;
    g_as->olOver = g_as->olFull = false;
    for (LONG i = 0; i < g_as->suspectW.n; i++) AsSetAdd(g_as->suspect, g_as->suspectW.k[g_as->suspectW.at[i]]);
    AsSetClear(g_as->suspectW);
    InterlockedExchange(&g_asDirtyLock, 0);
    // shortcut: map rows are written here and by destroy notes without the lock (both on the sim thread, docs/76 §3.3);
    // a destroy note racing this pass from another thread could lose its lot mark (the ListAudit net catches it)
    for (int i = 0; i < n; i++) {
        AsOl& o = g_as->ol[s_t[i].slot];
        if (o.gone) { if (o.seen) AsObjMark(o, o.lot, true, AS_MC_GONE); else g_asLiteDue = true, AsLiteObj(o); o.seen = 0; continue; }
        AsOlPlace(o, s_t[i].obj, true);
    }
    k.objResolved += n;
    // docs/122 §14.19: objects of lots that got list rows (or all, after an epoch) into the map, before the rows refresh
    while (InterlockedExchange(&g_asDirtyLock, 1)) YieldProcessor();
    AsSeedLots();
    InterlockedExchange(&g_asDirtyLock, 0);
    if (over) {  // lost touches only (touch list or map full): place every live row now; this cut's list rows go by print
        g_asOlUnknown = true;
        k.dirtyUnknown++;
        (mapFull ? k.resyncMap : k.resyncTouch)++;
        memset(g_asLite, 0, sizeof(g_asLite));  // rows refresh by print this cut, no marks: no lot-vector baseline stays valid
        for (int i = 0; i < AS_OL; i++) {
            AsOl& o = g_as->ol[i];
            if (o.id && !o.gone) AsOlPlace(o, o.obj, false);
        }
    }
    k.olRows = g_as->olN;
    QueryPerformanceCounter(&b);
    k.objResolveUs += (LONG)((b.QuadPart - a.QuadPart) * g_asPrintUsPerTick);
    if (g_asLiteDue && !g_asDirtyTrip) AsLotLites();
    g_asLiteDue = false;
}

// ---- TierMs > 0: Sim tiers (docs/122 §14.16) ----
// Tier 2 = a far unseen Sim by the [UnseenThrottle] rule (unseen.cpp AsTierOf: not on screen, beyond the camera reach,
// not within 80 m of an active-household Sim from ts3fixes' viewer push, a fresh viewer table); everything else tier 1.
// A tier-2 Sim's rows are refreshed at most every TierMs; at the cuts in between they are published stale, so a worker
// read aborts (simstale) and the game scores that pair itself: never an old answer. The request's actor stays tier 1.
// shortcut: native rule only (no household, seen-lot or social-target test of ts3fixes UfWhy, no "near a visible Sim");
// the camera reach and the household viewers cover most of it, add the rest over GetOption2 if tier-2 aborts show it
static int AsTierOf(AsU64 id);  // unseen.cpp; id 0 = start of a cut (camera refresh)
static int (*g_asTierFn)(AsU64) = AsTierOf;  // selftest: a fake rule
static DWORD(WINAPI* g_asNowMs)() = GetTickCount;
static LONG g_asTierMs;
// tier cache (docs/122 §14.17): a Sim's tier is kept TierClassMs (default 1,000) before it is classified again, or until the
// camera moves (AsTierOf(0) = 1); a worker read of a waiting Sim or lot holds it at tier 1 for AS_TIER_HOLD cuts
static LONG g_asTierClassMs = 1000;
static const LONG AS_TIER_HOLD = 32;
struct AsTierEnt { AsU64 id; DWORD refMs, clsMs; LONG visitCut, holdCut, clsCut; u8 tier; };
static AsTierEnt g_asTierTab[1024];
static AsTierEnt* AsTierSlot(AsU64 id) {
    for (DWORD h = (DWORD)(id ^ id >> 29) * 0x9E3779B1u, i = 0; i < 1024; i++) {
        AsTierEnt& t = g_asTierTab[(h + i) & 1023];
        if (t.id == id) return &t;
        if (!t.id) { memset(&t, 0, sizeof(t)); t.id = id; return &t; }
    }
    return NULL;  // full: tier 1
}
static void AsT1Lot(AsU64 id) {  // CutLean=2 + TierMs: the lot a tier-1 Sim was last placed on is a tier-1 lot
    if (g_asLean != 2) return;
    const AsOl* o = AsOlSlot(id, false);
    if (o && o->seen) AsSetAdd(g_as->t1Lots, o->lot);
}
// list tiers: a dirty row of a lot with no tier-1 Sim (and no hold) is refreshed at most every TierMs
struct AsLotTier { AsU64 lot; DWORD refMs; LONG refCut, holdCut; };
static AsLotTier g_asLotTier[512];
static AsLotTier* AsLotTierSlot(AsU64 lot) {
    for (DWORD h = (DWORD)(lot ^ lot >> 29) * 0x9E3779B1u, i = 0; i < 512; i++) {
        AsLotTier& t = g_asLotTier[(h + i) & 511];
        if (t.lot == lot) return &t;
        if (!t.lot) { memset(&t, 0, sizeof(t)); t.lot = lot; return &t; }
    }
    return NULL;  // full: tier 1
}
static bool AsLotDue(AsU64 lot, DWORD now) {
    if (AsSetHas(g_as->t1Lots, lot)) return true;
    AsLotTier* t = AsLotTierSlot(lot);
    if (!t || t->holdCut >= g_asCut || t->refCut == g_asCut) return true;
    if (t->refMs && now - t->refMs < (DWORD)g_asTierMs) return false;
    t->refMs = now ? now : 1, t->refCut = g_asCut;
    ts3native_autshadow.listT2Ref++;
    return true;
}
// docs/122 §14.18 tier triggers: a cached tier is classified again only when the camera moved (all), the Sim was placed
// by a touch since its last classification and TierClassMs passed (move), or 4 x TierClassMs passed (the viewer table can
// still move; mono-cost9 BP re-classified ~85 Sims per cut on the timer alone). Without the object-lot map (CutLean < 2)
// every Sim counts as moved (the 0.36.115 timer). Sims of the unreachable set are classified only when their rows are
// refreshed this cut (in the work set); the others keep their cached tier (a tier only gates refreshes, never an answer).
static void AsTierSet(const AsSet& s, DWORD now, bool camMoved, bool workOnly) {
    Ts3nAutShadow& k = ts3native_autshadow;
    for (LONG i = 0; i < s.n; i++) {
        const AsU64 id = s.k[s.at[i]];
        if (!id || AsIsActor(id)) continue;
        AsTierEnt* t = AsTierSlot(id);
        if (!t) { k.tier1Sims++; AsT1Lot(id); continue; }
        if (t->visitCut == g_asCut) continue;  // in another set too: once per cut
        t->visitCut = g_asCut;
        if (workOnly && t->clsMs && !AsSetHas(g_as->work, id)) { k.tierKept++; if (!t->tier) AsT1Lot(id); continue; }
        const AsOl* o = g_asLean == 2 ? AsOlSlot(id, false) : NULL;
        const bool moved = !o || o->placedCut > t->clsCut;
        const DWORD age = now - t->clsMs;
        if (!t->clsMs || camMoved || age >= (DWORD)g_asTierClassMs * (moved ? 1 : 4))
            t->tier = (u8)(g_asTierFn(id) == 1), t->clsMs = now ? now : 1, t->clsCut = g_asCut, k.tierClassN++;
        else k.tierKept++;
        if (!t->tier || t->holdCut >= g_asCut) { k.tier1Sims++; t->refMs = now; AsT1Lot(id); continue; }
        k.tier2Sims++;
        if (!t->refMs || now - t->refMs >= (DWORD)g_asTierMs) { t->refMs = now ? now : 1; AsSetAdd(g_as->t2ref, id); }
        else { AsSetAdd(g_as->t2skip, id); k.tier2Stale++; }
    }
}
static void AsTierCut() {
    AsSetClear(g_as->t2skip), AsSetClear(g_as->t2ref), AsSetClear(g_as->t1Lots);
    if (g_asTierMs <= 0) return;
    LARGE_INTEGER a, b;
    QueryPerformanceCounter(&a);
    const bool cam = g_asTierFn(0) == 1;  // the camera moved: every cached tier is classified again
    ts3native_autshadow.tierCamInval += cam;
    const DWORD now = g_asNowMs();
    AsT1Lot(g_asActor);
    for (int i = 0; i < g_asActorN; i++) AsT1Lot(g_asActors[i]);
    AsTierSet(g_as->always, now, cam, false), AsTierSet(g_as->learnSims, now, cam, false), AsTierSet(g_as->sims, now, cam, true);
    ts3native_autshadow.t1Lots = g_as->t1Lots.n;
    QueryPerformanceCounter(&b);
    ts3native_autshadow.tierClassUs += (LONG)((b.QuadPart - a.QuadPart) * g_asPrintUsPerTick);
}

// CutLean=2 safety net (docs/122 §14.17): ListAudit list rows per cut (rotating) against a fresh body call. A difference is
// a missed event: every list row is distrusted for this cut (worker reads abort `list`), their generation is moved off so
// the CutLean=1 print path re-checks each one from the next cut on, and dirty mode stays off for the session.
// docs/122 §14.20: list rows kept this cut because typed marks excluded their type (refresh-loop indexes, first 8)
static int g_asTyKept[8];
static LONG g_asTyKeptN;
static void AsListAudit(LONG gen) {
    Ts3nAutShadow& k = ts3native_autshadow;
    AsTab& m = g_as->m;
    static int s_at;
    LARGE_INTEGER a, b;
    QueryPerformanceCounter(&a);
    int done = 0;
    bool miss = false;
    // every other cut with a typed-kept row, one audit goes to such a row (the new rule's own check, same cost)
    const int ty = g_asTyKeptN && (g_asCut & 1) ? g_asTyKept[(g_asCut >> 1) % min(g_asTyKeptN, 8L)] : -1;
    for (int j = 0; j < m.n && done < g_asListAudit; j++) {
        const bool typed = j == 0 && ty >= 0;
        if (!typed && s_at >= m.n) s_at = 0;
        AsEnt& e = m.e[typed ? ty : s_at++];
        const AsDesc& d = *g_asDescOf[e.ic];
        if (d.owner != AO_LIST || (e.flags & (AE_FRESH | AE_COLD | AE_DISTRUST | AE_NOPAY))) continue;
        AsEnt x = e;
        LONG xn = 0;
        AsRefresh(e.ic, x, g_as->poolTmp, &xn);
        k.refreshed--, k.listAuditN++, done++;
        if (typed) k.typedAuditN++;
        if (!AsSame(d, e, m.pool, x, g_as->poolTmp)) {
            miss = true;
            k.listMissed++;
            if (typed) k.typedAuditMis++;
            if (k.listMissed <= 16) Log("autshadow list audit miss%s: %s lot 0x%08lX%08lX room %ld (dirty mode off)", typed ? " (typed)" : "", g_asName[e.ic], (DWORD)(e.owner >> 32), (DWORD)e.owner, (LONG)e.key[3]);
        }
    }
    if (miss) {
        g_asDirtyTrip = true, k.dirtyTripped = 1;
        for (int i = 0; i < m.n; i++)
            if (g_asDescOf[m.e[i].ic]->owner == AO_LIST) m.e[i].flags |= AE_DISTRUST, m.e[i].gen = gen ^ 0x40000000, m.e[i].lp = 0;
    }
    QueryPerformanceCounter(&b);
    k.listAuditUs += (LONG)((b.QuadPart - a.QuadPart) * g_asPrintUsPerTick);
}

// ---- RunSkip (docs/122 §14.21): runs of rows, skipped whole when nothing can change their outcome ----
// mono-cost11 BP: the refresh loop itself (bodies and the audit aside) was ~0.24 ms a cut for ~5,000 rows of 128 bytes,
// most of them object rows of clean objects that only get their flags rewritten. A run = one object's consecutive rows
// (AsAddRowKind and AsLearnObject insert them together), or consecutive rows of other kinds. A walked run keeps a summary;
// at a later cut a "plain" run (object rows only, no payload, none fresh or stale) is skipped whole when its object is in
// none of the per-cut sets the loop reads (work, t2skip, sims), the cut is not full, and, if it has cold rows, neither the
// IC hot states (with ColdIc and RowHot) nor a worker read of one of its cold rows changed since it was walked. Then
// every row would take the path it took last time with nothing to write: why -1 for a hot row (flags unchanged), cold
// again for a cold one (the counters it added then are added again). [verified by reading the loop; selftest T13 runs
// every random test with RunSkip on and compares against RunSkip off]
// Delta publish: a version gets only the runs walked since that version was last published (plus rows appended since);
// a skipped run's rows are bitwise what that version already holds. Fields the worker never reads (lastUsed, lastRead)
// may lag there.
struct AsRunRec { int start, end; AsU64 owner; LONG coldN, rowColdN, stamp, walked; int par; u8 plain, stale; };
static AsRunRec g_asRuns[AS_CAP];
static int g_asRunN, g_asRunEnd;
static LONG g_asRunGen = -1, g_asIcStamp;
static int g_asRunSkip;  // [AutShadow] RunSkip=1
static AsU64 g_asIcBitsHash;
static bool g_asRunSkipCheck;  // selftest: walk the runs RunSkip would skip and count any change (runCheckMis)
static AsEnt g_asRunCheckE[256];
static DWORD g_asRunCheckR[256];
static LONG g_asPubCut[2] = {-1, -1}, g_asPubGen[2] = {-1, -1}, g_asPubN[2];
static void AsRunsSync() {
    AsTab& m = g_as->m;
    if (g_asRunGen != g_asRowsGen || m.n < g_asRunEnd) g_asRunN = 0, g_asRunEnd = 0, g_asRunGen = g_asRowsGen, ts3native_autshadow.runBuilds++;
    for (int i = g_asRunEnd; i < m.n;) {
        const bool obj = g_asDescOf[m.e[i].ic]->owner == AO_OBJ;
        const AsU64 o = m.e[i].owner;
        int j = i + 1;
        if (obj) while (j < m.n && m.e[j].owner == o && g_asDescOf[m.e[j].ic]->owner == AO_OBJ) j++;
        else while (j < m.n && g_asDescOf[m.e[j].ic]->owner != AO_OBJ) j++;
        AsRunRec& r = g_asRuns[g_asRunN++];
        r.start = i, r.end = j, r.owner = obj ? o : 0, r.coldN = r.rowColdN = 0, r.stamp = -1, r.walked = -1, r.par = -1, r.plain = 0, r.stale = 0;
        i = j;
    }
    g_asRunEnd = m.n;
    ts3native_autshadow.runN = g_asRunN;
}
// the IC hot states the cold test reads, with the switches it reads: a change walks every run with cold rows again
static void AsIcStampCut() {
    AsU64 h = 0xCBF29CE484222325ull;
    AsMix(h, (DWORD)g_asColdIc), AsMix(h, (DWORD)g_asRowHot), AsMix(h, (DWORD)g_asIcN);
    DWORD bits = 0;
    for (int i = 0; i < g_asIcN; i++) {
        if (g_asIcHit[i] && g_asCut - g_asIcHit[i] <= AS_HOT) bits |= 1u << (i & 31);
        if ((i & 31) == 31 || i == g_asIcN - 1) AsMix(h, bits), bits = 0;
    }
    if (h != g_asIcBitsHash) g_asIcBitsHash = h, g_asIcStamp++, ts3native_autshadow.icStampMoves++;
}

LONG AsCut() {
    if (!g_as) return 0;
    Ts3nAutShadow& k = ts3native_autshadow;
    LARGE_INTEGER t0, t1, f;
    QueryPerformanceCounter(&t0);
    QueryPerformanceFrequency(&f);
    const double usPerTick = 1e6 / (double)f.QuadPart;
    auto us = [usPerTick](const LARGE_INTEGER& a, const LARGE_INTEGER& b) { return (LONG)((b.QuadPart - a.QuadPart) * usPerTick); };
    AsTab& m = g_as->m;
    g_asCut++;
    k.guardSim = g_asSimCalls;
    // worker hits since the last cut keep learned keys alive
    for (int v = 0; v < 2; v++) AsFoldUsed(g_as->v[v], v != g_asHeld);
    // learned keys from the worker's misses
    LONG want = 0;
    const LONG head = g_asRingHead, tEnd = head - g_asRingTail > AS_LEARN_CUT ? g_asRingTail + AS_LEARN_CUT : head;
    for (LONG t = g_asRingTail; t != tEnd; t++) {
        const AsLearnRec& r = g_asRing[t & (AS_RING - 1)];
        if (r.idx & AS_LEARN_SIM) {  // a worker read of a stale Sim row: refresh that Sim from now on
            const AsU64 id = r.key[0] | (AsU64)r.key[1] << 32;
            AsSetAdd(g_as->learnSims, id);
            AsTierEnt* te = g_asTierMs > 0 ? AsTierSlot(id) : NULL;  // TierMs: tier 1 for AS_TIER_HOLD cuts (docs/122 §14.17)
            if (te) te->holdCut = g_asCut + AS_TIER_HOLD, k.tierHold++;
        } else if (r.idx & AS_LEARN_LOT) {  // a worker read of a distrusted list row: its lot is tier 1 for a while
            AsLotTier* lt = AsLotTierSlot(r.key[0] | (AsU64)r.key[1] << 32);
            if (lt) lt->holdCut = g_asCut + AS_TIER_HOLD, k.tierHold++;
        } else if (r.idx & AS_LEARN_HEAT) {  // a worker read of a cold row: its IC is refreshed from this cut on
            g_asIcHit[r.idx & (AS_MAXIC - 1)] = g_asCut;
        } else {
            g_asIcHit[r.idx] = g_asCut;  // a miss: the IC is read, so hot
            int at = -1;
            if (AsFind(m, (int)r.idx, r.key, r.n) < 0 && (++want, (at = AsInsert((int)r.idx, r.key, r.n, AE_LEARNED)) >= 0)) {
                k.learned++;
                const AsDesc& d = *g_asDescOf[r.idx];
                if (d.owner == AO_OBJ && m.e[at].owner) AsLearnObject(m.e[at].owner);  // `at`, not m.n - 1: the insert may evict
                // shortcut: the System.Type pointer, not its name (no class-name call wired); match it in a dump if needed
                if (d.reason == AS_AB_QUERY && k.learned <= 64) Log("autshadow learned %s key 0x%08lX", g_asName[r.idx], r.key[0]);
            }
        }
        MemoryBarrier();
        g_asRingTail = t + 1;
    }
    if (want > k.learnWantMax) k.learnWantMax = want;
    k.learnBacklog = head - g_asRingTail;
    LARGE_INTEGER tp[6];  // phase ends: learn, prep, refresh, age, audit (publish = t1)
    QueryPerformanceCounter(&tp[0]);
    // what changed: the epoch (or no tracking) refreshes everything; else dirty objects, their children, list generation
    const LONG epoch = g_asEpoch;
    const bool full = epoch != g_asEpochSeen || !g_asTracked;
    const bool newEpoch = epoch != g_asEpochSeen;
    if (epoch != g_asEpochSeen) {
        k.epochs++;
        int w = 0;  // lot names may change with lot events: forget them
        for (int i = 0; i < m.n; i++) if (!g_asDescOf[m.e[i].ic]->lotClear) m.refs[w] = m.refs[i], m.e[w++] = m.e[i];
        if (w != m.n) {
            m.n = w;
            AsReindex(m);
            g_asMemoN = 0;
            for (int i = 0; i < m.n; i++) g_asMemoN += g_asDescOf[m.e[i].ic]->owner == AO_MEMO;
        }
    }
    g_asEpochSeen = epoch;
    static const DWORD none[1] = {0};
    for (int i = 0; i < g_asIcN; i++) {  // world constants (docs/122 §2.2): the zero-argument snapshot ICs
        const AsDesc* d = g_asDescOf[i];
        if (d && !d->nArg && (d->owner == AO_CUT || d->owner == AO_WORLD) && AsFind(m, i, none, 0) < 0) AsInsert(i, none, 0, 0);
    }
    while (InterlockedExchange(&g_asDirtyLock, 1)) YieldProcessor();
    AsSetClear(g_as->work);  // set walks by the used-slot list (docs/122 §14.18), not 4,096 slots or 32 KB copies
    for (LONG i = 0; i < g_as->dirty.n; i++) AsSetAdd(g_as->work, g_as->dirty.k[g_as->dirty.at[i]]);
    AsSetClear(g_as->dirty);
    InterlockedExchange(&g_asDirtyLock, 0);
    for (LONG i = 0; i < g_as->always.n; i++) AsSetAdd(g_as->work, g_as->always.k[g_as->always.at[i]]);
    for (LONG i = 0; i < g_as->learnSims.n; i++) AsSetAdd(g_as->work, g_as->learnSims.k[g_as->learnSims.at[i]]);
    // parent rows in a compact array (docs/122 §14.16), filled by the previous cut's refresh loop (docs/122 §14.19: a
    // separate scan of the ~600 KB table cost tens of us). Rows added since are fresh (parent 0, skipped below as before);
    // rows dropped since stay one cut longer, which only refreshes a child more or marks a Sim's lots for both kinds.
    const int nPar = g_asParN;
    const AsPar* const s_par = g_asPar;
    for (int pass = 0, added = 1; added && pass < 64; pass++) {  // children and grandchildren via our parent entries (docs/79)
        added = 0;
        for (int j = 0; j < nPar; j++)
            if (AsSetHas(g_as->work, s_par[j].parent) && !AsSetHas(g_as->work, s_par[j].owner)) { AsSetAdd(g_as->work, s_par[j].owner); added = 1; }
    }
    // docs/122 §14.15: what a Sim is parented to (seat, bed, vehicle, carrier) gets object rows before the worker asks; the
    // previous cut's parent answers pick them (only which rows exist, never an answer: new rows are refreshed below)
    if (g_asIcParent >= 0) {
        AsU64 par[32];
        int nw = 0;
        for (int j = 0; j < nPar && nw < 32; j++) {
            if ((s_par[j].flags & (AE_FRESH | AE_COLD)) || !(AsSetHas(g_as->always, s_par[j].owner) || AsSetHas(g_as->sims, s_par[j].owner))) continue;
            const AsU64 p = s_par[j].parent;
            const DWORD pk[2] = {(DWORD)p, (DWORD)(p >> 32)};
            bool dup = false;
            for (int q = 0; q < nw; q++) dup |= par[q] == p;
            if (p && !dup && AsFind(m, g_asIcParent, pk, 2) < 0) par[nw++] = p;
        }
        for (int j = 0; j < nw; j++) AsLearnObject(par[j]), k.parentRows++;  // a full table evicts (LRU), as learning does
    }
    AsSetClear(g_as->kids);  // Sims with a child in our parent rows: their moves mark every list kind (docs/122 §14.18)
    for (int j = 0; j < nPar; j++) if (s_par[j].parent && AsSetHas(g_as->simIds, s_par[j].parent)) AsSetAdd(g_as->kids, s_par[j].parent);
    AsOlResolve(newEpoch);  // CutLean=2: touched objects placed, dirty lots (before the tiers: tier-1 lots read the map)
    AsTierCut();  // TierMs: which Sims of the work set wait (t2skip) and which tier-2 Sims are due (t2ref)
    g_asPrintN = 0;
    QueryPerformanceCounter(&tp[1]);
    const LONG gen = g_asListGen, refreshed0 = k.refreshed;
    const bool tiers = g_asTierMs > 0;
    // CutLean=2 (docs/122 §14.17): list rows by dirty lot; a resync cut (or after a trip) judges every list row by print
    const bool dirtyMode = g_asLean == 2 && !g_asDirtyTrip && !g_asOlUnknown && !full;
    const bool printAll = g_asLean == 2 && !g_asDirtyTrip && g_asOlUnknown;
    const DWORD nowMs = tiers ? g_asNowMs() : 0;
    LONG poolN = 0, stale = 0;
    g_asParN = 0, g_asTyKeptN = 0;
    // docs/122 §14.19: an object's rows sit together (AsAddRowKind, AsLearnObject insert them in a run), so the set lookups
    // are made once per owner run instead of three per row (~4,650 rows a cut on BP)
    AsU64 cOwner = 0;
    bool cWork = false, cT2 = false, cSims = false, cActor = false;
    AsRunsSync();
    AsIcStampCut();
    const bool skipOk = g_asRunSkip && !full;
    for (int ri = 0; ri < g_asRunN; ri++) {
    AsRunRec& run = g_asRuns[ri];
    if (ri + 1 < g_asRunN) _mm_prefetch((const char*)&m.e[g_asRuns[ri + 1].start], _MM_HINT_T0);
    bool checking = false;  // selftest (g_asRunSkipCheck): a run judged skippable is walked anyway and must come out unchanged
    const AsU64 o = run.owner;
    const bool skip = skipOk && run.plain && !run.stale && run.walked >= 0 && (!run.coldN || (run.stamp == g_asIcStamp && !g_asReadColdOver)) &&
                      !AsSetHas(g_as->work, o) && !(tiers && AsSetHas(g_as->t2skip, o)) && !AsSetHas(g_as->sims, o) &&
                      !(run.coldN && (AsSetHas(g_as->readCold, o) || AsIsActor(o)));
    if (skip) {
        k.runSkips++, k.rowSkips += run.end - run.start;
        if (g_asRunSkipCheck && run.end - run.start <= 256) {
            checking = true;
            memcpy(g_asRunCheckE, m.e + run.start, (run.end - run.start) * sizeof(AsEnt));
            memcpy(g_asRunCheckR, m.refs + run.start, (run.end - run.start) * 4);
        } else {
            k.coldSkips += run.coldN, k.rowColdSkips += run.rowColdN;
            if (run.par >= 0) {
                const AsEnt& p = m.e[run.par];
                g_asPar[g_asParN].owner = p.owner, g_asPar[g_asParN].parent = p.r[0] | (AsU64)p.r[1] << 32, g_asPar[g_asParN++].flags = p.flags;
            }
            continue;
        }
    }
    const LONG cold0 = k.coldSkips, rowCold0 = k.rowColdSkips;
    bool plain = run.owner != 0, anyStale = false;
    run.par = -1;
    for (int i = run.start; i < run.end; i++) {
        AsEnt& e = m.e[i];
        const AsDesc& d = *g_asDescOf[e.ic];
        if (d.owner == AO_MEMO) { m.refs[i] = d.kind == AK_REF ? e.r[0] : 0; plain = false; continue; }
        const bool obj = d.owner == AO_OBJ;
        if (obj && e.owner != cOwner) {
            cOwner = e.owner;
            cWork = AsSetHas(g_as->work, cOwner), cT2 = tiers && AsSetHas(g_as->t2skip, cOwner), cSims = AsSetHas(g_as->sims, cOwner);
            cActor = AsIsActor(cOwner);
        }
        const bool inWork = obj && cWork;
        const bool t2 = obj && cT2;
        // cause: 0 new row or full rebuild (or cold until now), 1 dirty object / reachable Sim, 2 list generation, 3 per-cut row
        int why = full || (e.flags & (AE_FRESH | AE_COLD)) ? 0 : d.owner == AO_CUT ? 3 :
                  inWork && !t2 ? 1 : d.owner == AO_LIST && e.gen != gen ? 2 : -1;
        bool t2wait = false;
        if (d.owner == AO_LIST && why != 0) {
            if (dirtyMode) {
                const bool room = e.nKey >= 4 && e.key[3] != 0xFFFFFFFFu;
                bool dl = (e.flags & AE_DISTRUST) || AsSetHas(g_as->lotDirty, e.owner) || (room && AsSetHas(g_as->lotRoom, e.owner));
                bool tyKept = false;
                if (!dl && g_asLotTyN) {  // docs/122 §14.20: typed marks dirty only the rows of the marking objects' types
                    if (const AsLotTy* ty = AsLotTyOf(e.owner, false)) {
                        const int ti = AsLTypeIdx(e.key[0]);
                        const DWORD bits = ty->mem | (room ? ty->room : 0);
                        dl = ti < 0 || ti >= 32 ? (ty->memAny || (room && ty->roomAny)) : (bits >> ti & 1) != 0;
                        tyKept = !dl;
                    }
                }
                if (!dl && (AsSetHas(g_as->lotDirtySim, e.owner) || (room && AsSetHas(g_as->lotRoomSim, e.owner)))) {
                    if (AsLTypeSimFree(e.key[0])) k.simFreeKept++;  // docs/122 §14.18: no Sim is of this type
                    else dl = true;
                }
                why = dl ? 2 : -1;
                if (!dl) k.dirtyKept++;
                else if (tiers && !AsLotDue(e.owner, nowMs)) why = -1, t2wait = true;
                if (!dl && tyKept) {
                    k.typedKept++;
                    if (g_asTyKeptN < 8) g_asTyKept[g_asTyKeptN] = i;
                    g_asTyKeptN++;
                }
            } else if (printAll) why = 2;
        }
        AsU64 lp = 0;
        bool lpOk = false;
        if (g_asLean && !dirtyMode && d.owner == AO_LIST && why >= 0 && e.nKey >= 4) {
            lpOk = AsPrintOf(e.owner, (int)e.key[3], &lp);
            if (why == 2 && lpOk && lp == e.lp) { why = -1, e.gen = gen; k.listPrintSkips++; }  // same walker input: same answer
        }
        bool cold = why >= 0 && g_asColdIc && e.nKey && !(g_asIcHit[e.ic] && g_asCut - g_asIcHit[e.ic] <= AS_HOT);
        // RowHot (docs/122 §14.19): an object row the worker has not read for RowHot cuts waits cold (never answered)
        if (!cold && why >= 0 && g_asRowHot && obj && e.nKey && !full && e.ic != g_asIcParent && !cActor && g_asCut - e.lastRead > g_asRowHot)
            cold = true, k.rowColdSkips++;
        if (cold) e.flags |= AE_COLD, k.coldSkips++;
        if (why >= 0 && !cold) {
            const AsEnt old = e;
            LARGE_INTEGER r0, r1;
            QueryPerformanceCounter(&r0);
            AsRefresh(e.ic, e, g_as->poolTmp, &poolN);
            QueryPerformanceCounter(&r1);
            e.flags &= ~AE_COLD;
            if (d.owner == AO_LIST) e.lp = lpOk ? lp : 0;  // object rows keep lastRead in the union
            k.bodyN[why]++;
            k.bodyUs[why] += us(r0, r1);
            if (dirtyMode && why == 2) {
                k.dirtyRows++;
                const DWORD w = AsLotWhyOf(e.owner);
                for (int c = 0; c < AS_MC_N; c++) if (w >> c & 1) k.rowWhy[c]++;
            }
            if (why == 2 && g_asLean && AsSame(d, old, m.pool, e, g_as->poolTmp)) k.listSame++;
            if (obj && tiers && why == 1) {
                const int tier = AsSetHas(g_as->t2ref, e.owner);
                if (tier || AsSetHas(g_as->always, e.owner) || AsSetHas(g_as->learnSims, e.owner) || cSims)
                    k.tierBodyN[tier]++, k.tierBodyUs[tier] += us(r0, r1);
            }
        } else if (e.payN) {  // keep the payload: move it to the new pool (a cold row's is never answered)
            memcpy(g_as->poolTmp + poolN, m.pool + e.pay, e.payN * 4);
            e.pay = poolN, poolN += e.payN;
        }
        if (t2wait) e.flags |= AE_DISTRUST, k.listT2Wait++;  // dirty, not refreshed: no answer until its lot is due
        // Sim rows not refreshed this cut are published stale (docs/122 §2.3; TierMs: the waiting tier-2 Sims too)
        const bool st = obj && !full && (t2 ? why < 0 : cSims && !inWork);
        e.flags = (u8)(st ? e.flags | AE_STALE : e.flags & ~AE_STALE);
        stale += st;
        m.refs[i] = d.kind == AK_REF ? e.r[0] : 0;  // the rooted column matches the entries
        if (e.ic == g_asIcParent) g_asPar[g_asParN].owner = e.owner, g_asPar[g_asParN].parent = e.r[0] | (AsU64)e.r[1] << 32, g_asPar[g_asParN++].flags = e.flags, run.par = i;
        if (!obj || e.payN || (e.flags & AE_FRESH)) plain = false;
        anyStale |= st;
    }
    if (checking) {  // the walk of a run judged skippable: rows, rooted column and cold counts as the skip would leave them
        const int n = run.end - run.start;
        if (memcmp(g_asRunCheckE, m.e + run.start, n * sizeof(AsEnt)) || memcmp(g_asRunCheckR, m.refs + run.start, n * 4) ||
            k.coldSkips - cold0 != run.coldN || k.rowColdSkips - rowCold0 != run.rowColdN || !plain || anyStale) {
            k.runCheckMis++;
            if (k.runCheckMis <= 8) Log("autshadow run check: owner 0x%08lX%08lX rows %d changed by a walk (cut %ld)", (DWORD)(o >> 32), (DWORD)o, n, g_asCut);
        }
        continue;  // the summary stays: the skip would not have walked it (delta publish sees it as skipped)
    }
    run.plain = plain, run.stale = anyStale, run.walked = g_asCut, run.stamp = g_asIcStamp;
    run.coldN = k.coldSkips - cold0, run.rowColdN = k.rowColdSkips - rowCold0;
    }
    AsSetClear(g_as->readCold), g_asReadColdOver = false;
    k.stale = stale;
    memcpy(m.pool, g_as->poolTmp, poolN * 4);
    m.poolN = poolN;
    if (dirtyMode && g_asListAudit > 0) AsListAudit(gen);
    if (k.refreshed - refreshed0 > k.refreshMax) k.refreshMax = k.refreshed - refreshed0;
    QueryPerformanceCounter(&tp[2]);
    // learned keys and memo rows unused for AS_AGE cuts (CutLean=1: every AS_AGE_EVERY-th cut, so the reindex is rare)
    if (!g_asLean || g_asCut % AS_AGE_EVERY == 0) {
        k.ageRuns++;
        AsSetClear(g_as->listLots);  // lots with list rows, rebuilt with the kept rows (lot-vector prints walk only these)
        int w = 0;
        for (int i = 0; i < m.n; i++) {
            const bool memo = g_asDescOf[m.e[i].ic]->owner == AO_MEMO;
            if (((m.e[i].flags & AE_LEARNED) || (memo && m.e[i].nKey)) && g_asCut - m.e[i].lastUsed > AS_AGE) {
                if (memo) k.memoAged++, g_asMemoN--; else k.learnDropped++;
                continue;
            }
            if (w != i) m.refs[w] = m.refs[i], m.e[w] = m.e[i];  // no self-copies: the table is ~1 MB at 8,192 rows
            if (g_asDescOf[m.e[w].ic]->owner == AO_LIST && m.e[w].nKey >= 4) AsSetAdd(g_as->listLots, m.e[w].owner);
            w++;
        }
        memset(m.refs + w, 0, (AS_CAP - w) * 4);
        if (w != m.n) {  // payload offsets stay valid: the pool is not compacted again until the next cut
            m.n = w;
            AsReindex(m);
        }
    } else memset(m.refs + m.n, 0, (AS_CAP - m.n) * 4);
    k.memoRows = g_asMemoN;
    QueryPerformanceCounter(&tp[3]);
    // audit (invariant S-A, docs/122 §2.4): every Nth cut, the incremental answers against a fresh body call
    if (g_asAudit && g_asCut % g_asAudit == 0) {
        k.auditN++;
        // a rotating slice of at most 1,024 rows per audit cut: the cost stays bounded with the 8,192-row table
        static int s_auditAt;
        const int nAudit = min(m.n, 1024);
        if (s_auditAt >= m.n) s_auditAt = 0;
        for (int j = 0; j < nAudit; j++) {
            const int i = (s_auditAt + j) % m.n;
            const AsEnt& e = m.e[i];
            const AsDesc& d = *g_asDescOf[e.ic];
            if (d.owner == AO_MEMO || (e.flags & (AE_COLD | AE_STALE | AE_DISTRUST))) continue;  // never answered
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
        s_auditAt += nAudit;
    }
    QueryPerformanceCounter(&tp[4]);
    // publish into the version the worker does not hold
    const LONG pub = g_asPub;
    const LONG v = pub < 0 ? 0 : (g_asHeld == (pub ^ 1) ? pub : pub ^ 1);
    AsTab& t = g_as->v[v];
    // RunSkip delta publish (docs/122 §14.21): the same rows in the same places as this version's last publish (no row
    // moved since, only appended): copy the runs walked since then and the appended rows; else everything
    const bool delta = g_asRunSkip && g_asPubGen[v] == g_asRowsGen && g_asRunGen == g_asRowsGen && g_asPubN[v] <= m.n && g_asRunEnd == m.n;
    if (delta) {
        LONG rows = 0;
        for (int ri = 0; ri < g_asRunN; ri++) {
            const AsRunRec& r = g_asRuns[ri];
            if (r.walked <= g_asPubCut[v] && r.end <= g_asPubN[v]) continue;
            memcpy(t.e + r.start, m.e + r.start, (r.end - r.start) * sizeof(AsEnt));
            memcpy(t.refs + r.start, m.refs + r.start, (r.end - r.start) * 4);
            rows += r.end - r.start;
        }
        memset(t.refs + m.n, 0, (AS_CAP - m.n) * 4);
        k.pubDelta++, k.pubRows += rows;
    } else {
        memcpy(t.e, m.e, m.n * sizeof(AsEnt));
        memcpy(t.refs, m.refs, sizeof(t.refs));
        k.pubRows += m.n;
    }
    g_asPubCut[v] = g_asCut, g_asPubGen[v] = g_asRowsGen, g_asPubN[v] = m.n;
    t.n = m.n, t.poolN = m.poolN;
    memcpy(t.idx, m.idx, sizeof(t.idx));
    memcpy(t.pool, m.pool, m.poolN * 4);
    memset(t.used, 0, sizeof(t.used));
    InterlockedExchange(&g_asPub, v);
    k.entries = m.n;
    k.snapBytes = (LONG)(m.n * sizeof(AsEnt) + sizeof(t.idx) + m.poolN * 4);
    k.listGen = gen;
    k.cuts++;
    QueryPerformanceCounter(&t1);
    k.cutLearnUs += us(t0, tp[0]), k.cutPrepUs += us(tp[0], tp[1]), k.cutRefreshUs += us(tp[1], tp[2]);
    k.cutAgeUs += us(tp[2], tp[3]), k.cutAuditUs += us(tp[3], tp[4]), k.cutPublishUs += us(tp[4], t1);
    LONG cold = 0;
    for (int i = 0; i < g_asIcN; i++) cold += g_asDescOf[i] && !(g_asIcHit[i] && g_asCut - g_asIcHit[i] <= AS_HOT);
    k.coldIcs = g_asColdIc ? cold : 0;
    const LONG total = us(t0, t1);
    k.cutUs += total;
    if (total > k.cutMaxUs) k.cutMaxUs = total;
    return total;
}

#include "satb.cpp"           // [AutShadow] Satb: copy on first write while the worker runs (docs/206 §7)
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
    for (int i = 0; i < 7; i++) if (fn == (DWORD)(uintptr_t)kHxShims[i]) return true;  // [RouteAsync] H2Ic slot shims (locowin.cpp)
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
        const bool route = !strncmp(s, "ScriptCore.Route::", 18);  // H12: guarded with Guard=closure too
        if ((!all && !d && !route) || g_asIcN >= AS_MAXIC) { k.guardSkipped++; continue; }
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
        g_asNoDesc[idx] = (u8)(route ? AS_AB_ROUTE : AS_AB_IC);
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
