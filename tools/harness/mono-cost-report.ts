// Mono cost report (PLAN.md "Mono goal 2026-10-29"; queue tools/harness/queues/mono-cost.json): per run, what the Mono script
// engine costs the sim thread, and with [MonoThread] / [AutShadow] on, what the worker and the shadow scorer add. Reads only.
//   node tools/harness/mono-cost-report.ts [probe-out/queue/<queue>-<date>.jsonl | <tag> ...] [--tag <family>] [--minutes 10]
//     default: every probe-out/queue/mono-cost*-<date>.jsonl family (mono-cost, mono-cost2, ... mono-cost5, later ones), the
//     newest row per job id (a rerun counts for its job); --tag mono-cost5 keeps one family. A bare tag reads
//     probe-out/queue/<tag>-native.json (no ts3fixes counters) and assumes --minutes for the window
//   node tools/harness/mono-cost-report.ts --self-check
// Sources: sim-budget-report.ts analyze() on the profile files (Mono bucket, managed-sample owners, GC); the queue's /native
// dump {before, after}: ts3native_mtprobe (v4) and ts3native_autshadow (v5) deltas over the play window; ts3fixes autshadow_a0..a5
// (Mode R classes, docs/122 §5) from the queue row's fixActivity. Counter rates use the profile length as the window [inferred
// window]: /native has no clock. Set TS3_REPO=<main checkout> when running from a git worktree.
import assert from 'node:assert/strict';
import { existsSync, readdirSync, readFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { analyze, load, type Run } from './sim-budget-report.ts';

const ROOT = process.env.TS3_REPO ?? join(import.meta.dirname, '..', '..');
const QUEUE = join(ROOT, 'probe-out', 'queue');
export const STOP_MS = 5; // PLAN.md stop rule: the shadow worker alone adds >= ~5 ms/s of sim cost -> no apply step
// [measured, selftest] docs/30 §15: uncontended P3 lock entry, gate on minus gate off (runs: 68.4, 54.9, 69.8, 68.3 ns);
// per entry in game [inferred]: the same order, not measured
export const LOCK_ENTRY_NS = 68;
// [measured, selftest] TestAutShadow: sim-thread IC call through a guard thunk minus the direct call (+17.1 ns); memo ICs
// (AsCallEntry) cost more and are counted the same [inferred lower bound]
export const GUARD_NS = 17;
// docs/206 §7 Satb barrier on the sim thread: per store through the stub, extra per first-written word, per load through a
// load stub (thread test, worker path not taken). [measured, monohost AS satb cost line, best of 20: 4.9-6.0 ns per store
// at 18% first writes, 9.1 ns per load after its own stores; split 4 + 6 per word: docs/206 §7.6]
export const SATB_STORE_NS = 4, SATB_WORD_NS = 6, SATB_LOAD_NS = 9;
// docs/124 §15: Test share of meta pair scoring (docs/75 meta samples: base path + Test frames vs score overrides), TopN, per-search fixed ms
export const TAU = 0.7, TOPN = 10, CONSUME_FIXED_MS = 0.15;

type Budget = ReturnType<typeof analyze>;
export type Input = { tag: string; minutes: number; budget?: Budget; budgetWhy?: string; native?: { before?: any; after?: any };
  fix?: Record<string, { before?: number; after?: number }> };

const NA = 'n/a';
const f1 = (x: number) => x.toFixed(1);
// delta of one counter in a /native block; undefined when the block or the field is absent
function d(nat: Input['native'], block: string, k: string): number | undefined {
  const a = nat?.after?.[block]?.[k], b = nat?.before?.[block]?.[k];
  return typeof a === 'number' ? a - (typeof b === 'number' ? b : 0) : undefined;
}
const fx = (i: Input, k: string) => { const c = i.fix?.[k]; return typeof c?.after === 'number' ? c.after - (c.before ?? 0) : undefined; };

export function costLines(i: Input): string[] {
  const secs = i.minutes * 60, perS = (us: number | undefined) => us === undefined ? NA : `${f1(us / 1000 / secs)} ms/s`;
  const L: string[] = [`### ${i.tag}`, ''];
  // 1. Mono interpreter and GC on the sim thread (sim-budget)
  const b = i.budget?.secs ? i.budget : undefined;
  if (b) {
    L.push(`- Mono interpreter (sim thread): ${f1(b.ms['Mono script'])} ms/s [inferred split of measured stage timers, ${b.samples.monoN} managed samples]; top owners: ${b.contrib['Mono script']}`,
      `- ICs under script: ${f1(b.ms['ICs (native under script)'])} ms/s [inferred split]; top: ${b.contrib['ICs (native under script)']}`,
      `- GC (mark + finalizers): ${f1(b.ms['GC + finalizers'])} ms/s [measured]; ${b.contrib['GC + finalizers']}`,
      `- run: ${b.secs} s kept, ${f1(b.its)} it/s, ${b.hitches} frames >= 300 ms`);
  } else L.push(`- Mono interpreter: ${NA}; GC: ${NA} (${i.budgetWhy ?? (i.budget ? 'no second rows kept: profile without stage columns or under 61 s' : 'no profile files')})`);
  const N = i.native;
  // 2. ledger probe (autledger.cpp): ticks and meta-scope wall
  const ticks = d(N, 'autshadow', 'lgTicks') ?? (N?.after?.autshadow?.ledger ? N.after.autshadow.ledger.ticks - (N.before?.autshadow?.ledger?.ticks ?? 0) : undefined);
  const scopeUs = N?.after?.autshadow?.ledger ? N.after.autshadow.ledger.scopeUs - (N.before?.autshadow?.ledger?.scopeUs ?? 0) : undefined;
  if (N?.after?.autshadow?.ledger?.on & 1) L.push(`- ledger: ${ticks ?? NA} Simulate ticks [measured]${ticks === 0 ? ' (0 = loop-back hook did not fire: FAIL)' : ''}, meta scopes wall ${perS(scopeUs)} [measured, includes yielded B13 sleep]`);
  // 3. [MonoThread] worker + GC lock
  const mt = N?.after?.mtprobe;
  const starts = d(N, 'mtprobe', 'workerStarts'), runUs = d(N, 'mtprobe', 'workerRunUs'), waitUs = d(N, 'mtprobe', 'lockWaitUs');
  const calls = d(N, 'mtprobe', 'lockedCalls'), callsW = d(N, 'mtprobe', 'lockedWorker');
  const worker = !!mt && (mt.workerStarts > 0 || mt.workerState > 0);
  if (!mt) L.push(`- [MonoThread]: ${NA} (no mtprobe block)`);
  else {
    L.push(`- [MonoThread] p3Sites ${mt.p3Sites}, p4Ready ${mt.p4Ready}, locking ${mt.locking}, workerState ${mt.workerState}; tripwires lockOverflow ${mt.lockOverflow}, workerCtxSet ${mt.workerCtxSet}, n2mOverflow ${mt.n2mOverflow}, lockStale ${mt.lockStale ?? NA}`);
    L.push(`- worker busy: ${worker ? perS(runUs) : NA} [measured], ${d(N, 'mtprobe', 'workerJobs') ?? NA} jobs, ${d(N, 'mtprobe', 'workerExceptions') ?? NA} exceptions`);
    L.push(`- GC lock (covers the hooked GC/alloc entries): waits ${perS(waitUs)} [measured, every thread], ${d(N, 'mtprobe', 'lockWaits') ?? NA} waits, max ${mt.lockMaxWaitUs} us; entries ${calls ?? NA} (worker ${callsW ?? NA})`);
    if (calls !== undefined) L.push(`- GC lock uncontended entries: ${f1(calls / secs / 1000)}k/s x ${LOCK_ENTRY_NS} ns = ${perS(calls * LOCK_ENTRY_NS / 1000)} [inferred: measured entries x selftest ns, docs/30 §15]${mt.version >= 5 ? `; lazy gate ${mt.lazyGate ? 'on' : 'off'}, on ${d(N, 'mtprobe', 'lazyOn') ?? NA}, off ${d(N, 'mtprobe', 'lazyOff') ?? NA}` : ''}`);
  }
  // 4. [AutShadow]
  const as = N?.after?.autshadow;
  const req = d(N, 'autshadow', 'req'), cutUs = d(N, 'autshadow', 'cutUs'), cutReq = d(N, 'autshadow', 'cutReq');
  const guard = d(N, 'autshadow', 'guardSim');
  if (!as || !(as.mode >= 0 && (req ?? 0) + (d(N, 'autshadow', 'cuts') ?? 0) > 0)) L.push(`- [AutShadow]: ${NA} (${!as ? 'no autshadow block' : as.mode < 0 ? 'mode -1: no Simulate slot stub' : 'no requests or cuts'})`);
  else {
    const runs = d(N, 'autshadow', 'workRuns');
    L.push(`- [AutShadow] mode ${as.mode}, cut site ${as.cutSite}: cut + snapshot ${perS(cutUs)} [measured], ${cutReq ?? NA} request cuts, avg ${cutReq ? f1((cutUs ?? 0) / cutReq) : NA} us, max ${as.cutMaxUs} us, last snapshot ${as.snapBytes} B`,
      `- reference re-score on the sim (Mode R test cost, not paid by an apply step): ${perS(d(N, 'autshadow', 'refUs'))} [measured]; worker scoring ${perS(d(N, 'autshadow', 'workUs'))} [measured]${as.version >= 6 ? `; window waits ${d(N, 'autshadow', 'windowWaits') ?? NA}, ${perS(d(N, 'autshadow', 'windowWaitUs'))} (WindowWaitUs test cost) [measured]` : ''}`,
      `- guard thunks on the sim: ${guard === undefined ? NA : `${f1(guard / secs / 1000)}k calls/s x ${GUARD_NS} ns = ${perS(guard * GUARD_NS / 1000)}`} [inferred: measured calls x selftest ns]`);
    const a = [0, 1, 2, 3, 4, 5].map(k => fx(i, `autshadow_a${k}`));
    const cmp = a.slice(0, 4).every(x => x !== undefined) ? a.slice(0, 4).reduce((s, x) => s! + x!, 0)! : undefined;
    const fell = a[4] !== undefined && a[5] !== undefined ? a[4]! + a[5]! : undefined;
    L.push(`- jobs: posted ${req ?? NA}, Begin busy ${d(N, 'autshadow', 'busy') ?? NA}, worker runs ${runs ?? NA}, compared ${cmp ?? NA}, fell back ${fell ?? NA} (aborted/skipped ${a[4] ?? NA}, overrun/epoch ${a[5] ?? NA}; native abortTotal ${d(N, 'autshadow', 'abortTotal') ?? NA}, overrun ${d(N, 'autshadow', 'overrun') ?? NA}, noWorker ${d(N, 'autshadow', 'noWorker') ?? NA}) [measured]`);
    L.push(`- Mode R match rate: ${as.mode === 1 && cmp ? `${(a[0]! * 100 / cmp).toFixed(1)}% bit-equal (A0 ${a[0]} of ${cmp}; A1 ${a[1]}, A2 ${a[2]}, A3 ${a[3]}; A2/A3 must be 0)` : NA} [measured]`);
    // worker aborts by reason (abort[] + abort2 + abortRoute, read by agent.js into one map) and the answer table (v7, docs/122 §14.14)
    const ab0 = N?.before?.autshadow?.abort ?? {}, ab = Object.entries(as.abort ?? {}).map(([r, n]) => [r, (n as number) - (ab0[r] ?? 0)] as [string, number])
      .filter(([, n]) => n > 0).sort((x, y) => y[1] - x[1]);
    const abSum = ab.reduce((t, [, n]) => t + n, 0);
    L.push(`- worker aborts by reason: ${ab.length ? ab.slice(0, 5).map(([r, n]) => `${r} ${n} (${(n * 100 / abSum).toFixed(1)}%)`).join(', ') : NA} [measured]`);
    // docs/122 §5.1: A4 target < 20% of allow-listed pairs (ts3fixes from docs/122 §14.15 leaves off-allow-list skips out of A0-A5)
    const all = a.every(x => x !== undefined) ? a.reduce((s, x) => s! + x!, 0)! : undefined, skips = fx(i, 'autshadow_skips');
    L.push(`- A4 share: ${all ? `${(a[4]! * 100 / all).toFixed(1)}% of ${all} classified pairs (target < 20%)` : NA}; skipped pairs ${skips ?? NA} [measured]`);
    L.push(`- answer table: ${as.entries ?? NA} rows after the last cut, learned ${d(N, 'autshadow', 'learned') ?? NA}, learnDropped ${d(N, 'autshadow', 'learnDropped') ?? NA}, inserts refused ${d(N, 'autshadow', 'full') ?? NA}` +
      (as.version >= 7 ? `; max ${as.entriesMax} rows, memo ${as.memoRows}, evicted ${d(N, 'autshadow', 'evicted') ?? NA} (memo ${d(N, 'autshadow', 'evictedMemo') ?? NA}), ring dropped ${d(N, 'autshadow', 'ringDropped') ?? NA}, most new keys at one cut ${as.learnWantMax}, most body calls at one cut ${as.refreshMax}` : '') + ' [measured]');
    if (as.version >= 8) {  // docs/122 §14.15: where the cut time goes
      const cuts = d(N, 'autshadow', 'cuts'), per = (k: string) => { const x = d(N, 'autshadow', k); return x === undefined || !cuts ? NA : f1(x / cuts); };
      L.push(`- cut per cut (us): learn ${per('cutLearnUs')}, prep ${per('cutPrepUs')}, refresh ${per('cutRefreshUs')}, age ${per('cutAgeUs')}, audit ${per('cutAuditUs')}, publish ${per('cutPublishUs')}; ` +
        `body calls per cut (us per cut): new ${per('bodyNew')} (${per('bodyNewUs')}), object ${per('bodyObj')} (${per('bodyObjUs')}), list ${per('bodyList')} (${per('bodyListUs')}), per-cut ${per('bodyCut')} (${per('bodyCutUs')}); ` +
        `Sim parent objects learned ${d(N, 'autshadow', 'parentRows') ?? NA}; cold: ${d(N, 'autshadow', 'coldSkips') ?? NA} rows skipped, ${d(N, 'autshadow', 'coldReads') ?? NA} reads, ${as.coldIcs} ICs; x87 sim 0x${(as.simX87 >>> 0).toString(16)} worker 0x${(as.workX87 >>> 0).toString(16)}, MXCSR sim 0x${(as.simCsr >>> 0).toString(16)} worker 0x${(as.workCsr >>> 0).toString(16)} [measured]`);
      if (as.version >= 9)  // docs/122 §14.16: change-only lists (CutLean) and Sim tiers (TierMs)
        L.push(`- lists (CutLean) per cut: kept by print ${per('listPrintSkips')}, prints ${per('listLots')} (${per('listPrintUs')} us), refreshed same ${per('listSame')}; ` +
          `Sim tiers (TierMs ${as.tierMs}) per cut: tier 1 ${per('tier1Sims')} Sims, body ${per('tier1Body')} (${per('tier1BodyUs')} us); tier 2 ${per('tier2Sims')} Sims, ` +
          `${per('tier2Stale')} waiting (stale), body ${per('tier2Body')} (${per('tier2BodyUs')} us); classify ${per('tierClassUs')} us; aging runs ${d(N, 'autshadow', 'ageRuns') ?? NA} [measured]`);
      if (as.version >= 10)  // docs/122 §14.17: dirty lots (CutLean=2), ListAudit net, tier cache, list tiers
        L.push(`- dirty lots (CutLean=2) per cut: rows kept ${per('dirtyKept')}, refreshed ${per('dirtyRows')}, lots marked ${per('dirtyLots')}, objects placed ${per('objResolved')} (${per('objResolveUs')} us); ` +
          `resync cuts ${d(N, 'autshadow', 'dirtyUnknown') ?? NA}; list audit ${per('listAuditN')} rows (${per('listAuditUs')} us), misses ${d(N, 'autshadow', 'listMissed') ?? NA}, tripped ${as.dirtyTripped ?? NA}; ` +
          `tier cache: classified ${per('tierClassN')} Sims, camera moves ${d(N, 'autshadow', 'tierCamInval') ?? NA}, holds ${d(N, 'autshadow', 'tierHold') ?? NA}; ` +
          `list tiers: ${per('listT2Wait')} rows waiting, ${per('listT2Ref')} lot refreshes, tier-1 lots ${as.t1Lots ?? NA} [measured]`);
      if (as.version >= 11)  // docs/122 §14.18: list kinds, lot-vector prints, tier triggers, shared cuts
        L.push(`- list kinds per cut: Sim-free rows kept ${per('simFreeKept')}, Sim marks ${per('simMarks')} (types ${as.listTypesSimFree ?? NA} Sim-free of ${as.listTypes ?? NA}); ` +
          `unplaced touches ${per('unplaced')}, lot-vector prints ${per('liteLots')} (${per('liteUs')} us), their marks ${per('liteMarks')}; ` +
          `resyncs ${d(N, 'autshadow', 'resyncTouch') ?? NA} touch list / ${d(N, 'autshadow', 'resyncMap') ?? NA} map full, map rows ${as.olRows ?? NA}, compactions ${d(N, 'autshadow', 'olCompact') ?? NA}; ` +
          `tiers kept ${per('tierKept')} Sims; shared cuts ${d(N, 'autshadow', 'batchCuts') ?? NA}, ${per('batchReqs')} requests per cut (max ${as.batchMax ?? NA}) [measured]`);
      if (as.version >= 12) {  // docs/122 §14.19: what dirties lots, seeding, pointer lot-vector prints, RowHot
        const mc = ['First', 'New', 'Lot', 'Room', 'Gone', 'Lost', 'Lite', 'Kid'], ev = ['4', '5', '6', '0B', '0C', '0E', '12', '18', '21'];
        L.push(`- dirtying per cut, lots marked / list rows refreshed by cause: ${mc.map(n => `${n.toLowerCase()} ${per('mark' + n)}/${per('row' + n)}`).join(', ')}; ` +
          `marks by event: ${ev.map(n => `0x${n} ${per('markEv' + n)}`).join(', ')}; seeding: ${d(N, 'autshadow', 'seedLots') ?? NA} lots, ` +
          `${d(N, 'autshadow', 'seedObjs') ?? NA} objects (${per('seedUs')} us per cut), ${d(N, 'autshadow', 'seedFull') ?? NA} in part; ` +
          `lot-vector suspects ${d(N, 'autshadow', 'liteSuspect') ?? NA}; RowHot ${as.rowHot ?? NA}: ${per('rowColdSkips')} rows cold per cut, ` +
          `${d(N, 'autshadow', 'rowColdReads') ?? NA} cold reads [measured]`);
      }
      if (as.version >= 13) {  // docs/122 §14.20: typed marks (classified objects dirty only the rows of their types)
        const g = (n: string) => d(N, 'autshadow', n) ?? NA;
        L.push(`- typed marks per cut: ${per('typedMarks')} marks, ${per('typedKept')} list rows kept; classified ${g('clsSet')} of ${g('clsAsked')} asked ` +
          `(${g('clsNone')} no object, ${per('clsUs')} us per request on the sim thread); typed audits ${g('typedAuditN')}, misses ${g('typedAuditMis')} (must be 0); lot-vector prints typed ` +
          `${per('liteTyped')} (${per('liteTypedNone')} no list type) [measured]`);
      }
      if (as.version >= 14) {  // docs/122 §14.21: RunSkip (clean runs skipped whole) and the delta publish
        const g = (n: string) => d(N, 'autshadow', n) ?? NA;
        L.push(`- run skip (RunSkip ${as.runSkip ?? NA}) per cut: ${per('runSkips')} runs / ${per('rowSkips')} rows skipped of ${as.runN ?? NA} runs; ` +
          `rebuilds ${g('runBuilds')}, IC stamp moves ${g('icStampMoves')}; delta publishes ${g('pubDelta')}, ${per('pubRows')} rows copied [measured]`);
        // docs/124 §15: the pass share p, and the stale-score check (a fresh re-score >= 1 tick after the cut vs the worker's)
        const pp = d(N, 'autshadow', 'passPairs'), cp = d(N, 'autshadow', 'classPairs'), fr = d(N, 'autshadow', 'freshReqs') ?? 0;
        const fp = d(N, 'autshadow', 'freshPairs') ?? 0, pct = (x: number | undefined, y: number) => y > 0 && x !== undefined ? `${(x * 100 / y).toFixed(1)}%` : NA;
        L.push(`- consume side [measured]: pass share ${cp ? pct(pp, cp) : NA} (${pp ?? NA} of ${cp ?? NA} classified pairs pass Test); fresh re-score ` +
          `(FreshEvery ${as.freshEvery ?? NA}): ${fr} searches, ${fp} pairs, ${pct(d(N, 'autshadow', 'freshDiff'), fp)} differ, top pick changes ` +
          `${pct(d(N, 'autshadow', 'freshTop1'), fr)}, top-10 changes ${pct(d(N, 'autshadow', 'freshTop10'), fr)}, ${fr ? f1((d(N, 'autshadow', 'freshUs') ?? 0) / fr / 1000) : NA} ms each (test cost); ` +
          `KeepPartial ${as.keepPartial ?? NA}: ${d(N, 'autshadow', 'partialKept') ?? NA} overrun searches kept their finished objects`);
      }
    }
    if (as.version >= 15 && as.satbMode) L.push(...satbLines(N, secs));
    // docs/122 §14.18 go-live estimate [inferred]: the scoring a worker would take off the sim (reference us per request x
    // every B13 call x the answered share) minus what it costs the sim (one shared cut per frame with a B13 call, guard thunks)
    const calls13 = d(N, 'autshadow', 'metaCalls'), ticks13 = d(N, 'autshadow', 'metaTicks'), breq = d(N, 'autshadow', 'batchReqs'), refUs = d(N, 'autshadow', 'refUs');
    const allC = a.every(x => x !== undefined) ? a.reduce((t, x) => t! + x!, 0)! : 0, cutsN = d(N, 'autshadow', 'cuts');
    if (as.version >= 11 && calls13 && breq && refUs !== undefined && cutsN && allC) {
      const moved = refUs / 1000 / breq * (calls13 / secs) * (1 - a[4]! / allC), cutMs = (cutUs ?? 0) / 1000 / cutsN * (ticks13 ?? 0) / secs;
      const g = (guard ?? 0) * GUARD_NS / 1e6 / secs, net = moved - cutMs - g, share = moved > 0 ? cutMs * 100 / moved : Infinity;
      L.push(`- go-live estimate [inferred]: scoring moved ${f1(moved)} ms/s (${f1(refUs / 1000 / breq)} ms per request x ${f1(calls13 / secs)} B13 calls/s x ${(100 - a[4]! * 100 / allC).toFixed(1)}% answered) - cut ${f1(cutMs)} ms/s ` +
        `(${f1((ticks13 ?? 0) / secs)} cuts/s x ${f1((cutUs ?? 0) / cutsN / 1000)} ms) - guard ${f1(g)} ms/s = net ${f1(net)} ms/s; cut = ${share.toFixed(0)}% of the scoring moved ` +
        `(gate <= 25%: ${share <= 25 ? 'PASS' : 'FAIL'}); GC lock entries are [MonoThread]'s, in the stop-rule line, not here`);
      // docs/122 §14.20: the worker only runs in the window (cut end to the next Simulate entry); when that is far shorter
      // than a job, Mode R makes the sim wait (WindowWaitUs). Go-live with the same window either waits too or falls back
      const winUs = d(N, 'autshadow', 'windowUs'), waits = d(N, 'autshadow', 'windowWaits'), waitUs = d(N, 'autshadow', 'windowWaitUs');
      const runs = d(N, 'autshadow', 'workRuns'), workUs = d(N, 'autshadow', 'workUs'), over = d(N, 'autshadow', 'overrun');
      if (winUs !== undefined && runs && workUs !== undefined && waits && waitUs !== undefined) {
        const waitMs = waitUs / 1000 / waits * (calls13 / secs), ticks = d(N, 'autshadow', 'simTicks');
        L.push(`- worker window [measured]: quiescent gap (cut end to the next Simulate entry, docs/124 §2/§3.4) ${f1(winUs / runs)} us vs ${f1(workUs / runs / 1000)} ms a job` +
          `${ticks ? `; post to resume ~1 tick = ${f1(secs * 1000 / ticks)} ms [inferred]` : ''}; overruns ${over ?? NA} of ${runs} ` +
          `(${f1((over ?? 0) * 100 / runs)}%) at the test wait; go-live with in-place reads [inferred]: no wait = nearly every job falls back, ` +
          `or the sim waits ${f1(waitUs / 1000 / waits)} ms a request x ${f1(calls13 / secs)}/s = ${f1(waitMs)} ms/s: net ${f1(net - waitMs)} ms/s ` +
          `(cut-time reads or a copied heap would let the job use the tick, docs/122 §14.20)`);
      }
      // docs/124 §15-16 consume cost [inferred]: go-live re-runs the game's C1 (live Test + score) for band 1 = the top TOPN passing
      // answered pairs of each search, and for further bands only when §16.1's rule fires (bounds: band 1 only / every passing
      // pair). A selected pair costs R/P x (TAU + (1 - TAU) / p): its Test (share TAU of the pair scoring, docs/75) plus the score
      // share the passing pairs carry. Off-allow-list ads of mixed objects go through C1 as in vanilla (§16.2): cost-neutral, so
      // "moved" stays the allow-listed answered scoring. Pass share p from the Mode R pass counter when present, else the range.
      // Overrun searches (native overrun / req) move nothing but pay the cut; CONSUME_FIXED_MS = post, Sleep(0), resume, selection.
      // shortcut: pass counter name assumed (native autshadow passPairs or ts3fixes autshadow_pass); align when it lands
      const P = (allC - a[5]!) / breq, ov = (d(N, 'autshadow', 'overrun') ?? 0) / (req || breq), rate = calls13 / secs;
      const passN = d(N, 'autshadow', 'passPairs') ?? fx(i, 'autshadow_pass'), ans = 1 - a[4]! / allC;
      const base = (1 - ov) * moved - cutMs - g - rate * CONSUME_FIXED_MS, ps = passN !== undefined ? [passN / (allC - a[5]!)] : [0.25, 0.5, 0.75, 1];
      const at = ps.map(p => { const c = refUs / 1000 / breq / P * (TAU + (1 - TAU) / p), cost = (n: number) => base - rate * (1 - ov) * n * c;
        return { b1: cost(Math.min(TOPN, p * P * ans)), all: cost(p * P * ans), nb: base / (rate * (1 - ov) * c) }; });
      if (P > 0) L.push(`- go-live after consume [inferred]: overrun ${(ov * 100).toFixed(1)}% of searches, before consume ${f1(base)} ms/s; live C1, top ${TOPN} + bands, ` +
        `pass share ${passN !== undefined ? `${ps[0].toFixed(2)} [measured]` : `${ps.join('/')} (no pass counter)`}: net ${at.map(x => f1(x.b1)).join('/')} ms/s with band 1 only, ` +
        `${at.map(x => f1(x.all)).join('/')} with every band; break-even N ${at.map(x => f1(x.nb)).join('/')} (${f1(P)} allow-listed pairs per search)`);
    }
  }
  // stop rule: sim-side cost of the worker path = cut + snapshot + GC lock waits (all threads: conservative) [measured] +
  // uncontended lock entries x LOCK_ENTRY_NS + sim guard calls x GUARD_NS [inferred]; the Mode R reference and the
  // WindowWaitUs wait are test costs and stay out
  if (worker || as?.mode >= 1) {
    const meas = (cutUs ?? 0) / 1000 / secs + (waitUs ?? 0) / 1000 / secs;
    const inf = ((calls ?? 0) * LOCK_ENTRY_NS + (guard ?? 0) * GUARD_NS) / 1e6 / secs, x = meas + inf;
    L.push(`- worker adds ${f1(x)} ms/s to the sim (stop rule: >= ${STOP_MS}): ${f1(meas)} [measured: cut ${cutUs === undefined ? NA : 'yes'} + GC lock waits ${waitUs === undefined ? NA : 'yes'}] + ${f1(inf)} [inferred: lock entries ${calls === undefined ? NA : `x ${LOCK_ENTRY_NS} ns`} + guard calls ${guard === undefined ? NA : `x ${GUARD_NS} ns`}]; Mode R reference and window waits left out${x >= STOP_MS ? ' STOP' : ''}`);
  }
  return L;
}

// docs/206 §7 S0 gate [inferred model from measured counts]: per job at go-live (one shared cut per frame with a B13 call,
// Sample=1) the sim pays every store in the job span through the barrier, the first write of each word again, and the
// thread test on every load stub; unlogged events (an IC with a shared pointer argument, a runtime writer we cannot log)
// cost a wait for the worker. Gate: <= SATB_GATE_MS ms/s and <= 20k words per job; stop above SATB_STOP_MS or a 4 MB log.
export const SATB_GATE_MS = 3, SATB_STOP_MS = 5;
export function satbLines(N: Input['native'], secs: number): string[] {
  const g = (k: string) => d(N, 'autshadow', k), as = N?.after?.autshadow;
  const spans = g('satbJobSpans'), ticks13 = g('metaTicks');
  if (!spans) return [`- Satb (mode ${as?.satbMode}): no job spans timed (satbJobSpans 0)`];
  const per = (k: string) => (g(k) ?? 0) / spans;
  const st = per('satbJobStores'), w = per('satbJobWords'), ld = per('satbJobLoads'), ev = per('satbJobIc'), us = per('satbJobUs');
  const jobsS = ticks13 !== undefined ? ticks13 / secs : undefined;
  const nsJob = st * SATB_STORE_NS + w * SATB_WORD_NS + ld * SATB_LOAD_NS, waitMs = ev * us / 2 / 1000;  // a wait ~ half a job [inferred]
  const ms = jobsS === undefined ? undefined : jobsS * (nsJob / 1e6 + waitMs);
  const maxMB = (as?.satbJobMaxWords ?? 0) * 8 / 1048576;
  const verdict = ms === undefined ? NA : ms > SATB_STOP_MS || maxMB > 4 ? 'STOP (docs/206 §5 stop rule)' : ms <= SATB_GATE_MS && w <= 20_000 ? 'PASS' : 'between gate and stop: report';
  return [`- Satb probe per job span (${f1(us / 1000)} ms, ${spans} spans) [measured]: ${f1(st)} sim stores, ${f1(w)} words first written (max ${as?.satbJobMaxWords ?? NA}, ` +
    `${f1(maxMB)} MB log), ${f1(per('satbJobPages'))} pages, ${f1(ld)} sim loads, ${f1(ev)} unlogged events (IC shared pointer / runtime writer); whole tick: ` +
    `${f1((g('satbStores') ?? 0) / (g('satbSpans') || 1))} stores, ${f1((g('satbWords') ?? 0) / (g('satbSpans') || 1))} words; IC calls ${g('satbIcCalls') ?? NA} ` +
    `(byref/array ICs ${g('satbIcCallsAll') ?? NA}, shared ${g('satbIcShared') ?? NA}), runtime writers ${g('satbRtCalls') ?? NA} (unlogged ${g('satbRtWaits') ?? NA}), big ${g('satbBig') ?? NA}, overflow ${g('satbOverflow') ?? NA}`,
  `- Satb barrier at go-live [inferred]: ${jobsS === undefined ? NA : f1(jobsS)} jobs/s x (${f1(nsJob / 1000)} us barrier: stores x ${SATB_STORE_NS} + words x ${SATB_WORD_NS} + loads x ${SATB_LOAD_NS} ns` +
    ` + waits ${f1(waitMs)} ms) = ${ms === undefined ? NA : `${f1(ms)} ms/s`}: ${verdict} (gate <= ${SATB_GATE_MS} ms/s and <= 20k words a job; stop > ${SATB_STOP_MS} ms/s or > 4 MB)`];
}

const jsonl = (f: string) => readFileSync(f, 'utf8').split('\n').filter(Boolean).flatMap(l => { try { return [JSON.parse(l)]; } catch { return []; } });
function withBudget(i: Input): Input {
  try { return { ...i, budget: analyze(load(i.tag)) }; } catch (e) { return { ...i, budgetWhy: String((e as Error).message ?? e) }; }
}
const readJson = (f?: string) => f && existsSync(f) ? JSON.parse(readFileSync(f, 'utf8')) : undefined;
const rowInput = (r: any, minutes: number) => withBudget({ tag: r.profileTag, minutes: r.run?.profile?.minutes ?? minutes,
  native: readJson(r.nativeDump) ?? readJson(join(QUEUE, `${r.profileTag}-native.json`)), fix: r.fixActivity?.counters });
export function inputs(arg: string, minutes: number): Input[] {
  if (arg.endsWith('.jsonl')) return jsonl(arg).filter(r => r.profileTag).map(r => rowInput(r, minutes));
  return [withBudget({ tag: arg, minutes, native: readJson(join(QUEUE, `${arg}-native.json`)) })];
}
// queue files of every mono-cost family (mono-cost-<date>.jsonl, mono-cost2-..., later ones); family = the part before the date
export const FAMILY_RE = /^(mono-cost\d*|satb-s\d+)-(\d{4}-\d\d-\d\d)\.jsonl$/;
// newest row per job id (a rerun counts for the job it reruns), optionally one family; rows sorted by job start time
export function newestPerJob(rows: any[], family?: string): any[] {
  const best = new Map<string, any>();
  for (const r of rows) {
    if (!r?.profileTag || (family && (r.queue ?? '') !== family)) continue;
    const id = String(r.rerunOf ?? r.job), o = best.get(id);
    if (!o || String(r.time) > String(o.time)) best.set(id, r);
  }
  return [...best.values()].sort((a, b) => String(a.time) < String(b.time) ? -1 : 1);
}

function selfCheck() {
  const row = { iters: 30, script: 10, taskObjects: 80, oneShot: 0, threaded: 600, route: 100, rtPlanWait: 10, rtLock: 50, rtOtherLock: 0, rtJob: 0,
    gcMark: 4, gcForcedMs: 1, fin: 2, animSacs: 20, animPose: 30, locomotion: 40, publish: 10 };
  const run: Run = { tag: 'mono-cost-fx-bp-autshadow-r', ics: new Set(['ScriptCore.World.World_IsOnRoofImpl']),
    rows: [{ t: 0, iters: 1 }, { t: 61_000, ...row }, { t: 62_000, ...row }, { t: 63_000, ...row }], hitches: [],
    managed: [['ScriptCore.World.World_IsOnRoofImpl', 'X.Y'], ['A.B', 'Sims3.Gameplay.Autonomy.AutonomyManager.Simulate', 'ScriptCore.ScriptProxy.Simulate']],
    simstack: [], native: {} };
  const mt0 = { version: 4, workerStarts: 0, workerRunUs: 0, lockWaitUs: 0, lockWaits: 0, lockedCalls: 0, lockedWorker: 0, workerJobs: 0, workerExceptions: 0 };
  const as0 = { version: 5, mode: 1, req: 0, cuts: 0, cutUs: 0, cutReq: 0, refUs: 0, workUs: 0, workRuns: 0, busy: 0, abortTotal: 0, overrun: 0, noWorker: 0 };
  // 10 min window = 600 s: cut 600 ms -> 1.0 ms/s; lock waits 2400 ms -> 4.0 ms/s; total 5.0 = STOP
  const native = { before: { mtprobe: mt0, autshadow: as0 },
    after: { mtprobe: { ...mt0, p3Sites: 19, p4Ready: 15, locking: 1, workerState: 2, lockOverflow: 0, workerCtxSet: 0, n2mOverflow: 0, lockStale: 0, lockMaxWaitUs: 900,
      workerStarts: 1, workerRunUs: 1_200_000, lockWaitUs: 2_400_000, lockWaits: 50, lockedCalls: 1000, lockedWorker: 300, workerJobs: 400, workerExceptions: 0 },
      autshadow: { ...as0, cutSite: 2, cutMaxUs: 4000, snapBytes: 9000, req: 500, cuts: 500, cutUs: 600_000, cutReq: 500, refUs: 3_000_000, workUs: 1_200_000, workRuns: 400, busy: 3, abortTotal: 60, overrun: 40, noWorker: 0 } } };
  const fix = { autshadow_a0: { before: 0, after: 297 }, autshadow_a1: { before: 0, after: 3 }, autshadow_a2: { before: 0, after: 0 }, autshadow_a3: { before: 0, after: 0 },
    autshadow_a4: { before: 0, after: 60 }, autshadow_a5: { before: 0, after: 40 }, autshadow_skips: { before: 0, after: 900 } };
  const out = costLines({ tag: run.tag, minutes: 10, budget: analyze(run), native, fix }).join('\n');
  assert.match(out, /Mono interpreter \(sim thread\): 269\.5 ms\/s .*autonomy search/); // script 690 - route 90 - waits 60 - forced GC 1 = 539, 1 of 2 samples Mono
  assert.match(out, /GC \(mark \+ finalizers\): 6\.0 ms\/s \[measured\]/);
  assert.match(out, /worker busy: 2\.0 ms\/s/);
  assert.match(out, /cut \+ snapshot 1\.0 ms\/s .*avg 1200\.0 us/);
  assert.match(out, /reference re-score .*: 5\.0 ms\/s/);
  assert.match(out, /posted 500, .*compared 300, fell back 100/);
  assert.match(out, /Mode R match rate: 99\.0% bit-equal/);
  assert.match(out, /A4 share: 15\.0% of 400 classified pairs \(target < 20%\); skipped pairs 900/);
  // 1000 entries x 68 ns, no guard calls: inferred 0.0; measured 5.0 -> STOP
  assert.match(out, /worker adds 5\.0 ms\/s to the sim \(stop rule: >= 5\): 5\.0 \[measured.* \+ 0\.0 \[inferred.* STOP$/m);
  // 580k entries/s x 68 ns = 39.4 ms/s, 100k guard calls/s x 17 ns = 1.7 ms/s (inferred); window waits 120 ms / 600 s
  const busy = { before: native.before, after: { ...native.after, mtprobe: { ...native.after.mtprobe, version: 5, lazyGate: 0, lazyOn: 0, lazyOff: 0, lockedCalls: 580_000 * 600 },
    autshadow: { ...native.after.autshadow, version: 7, guardSim: 100_000 * 600, windowWaits: 40, windowWaitUs: 120_000,
      abort: { row: 900, other: 100 }, entries: 3000, entriesMax: 8192, memoRows: 2048, evicted: 500, evictedMemo: 100, ringDropped: 0, learnWantMax: 256, refreshMax: 700 } } };
  const v8 = { before: busy.before, after: { ...busy.after, autshadow: { ...busy.after.autshadow, version: 8, cutLearnUs: 5_000, cutPrepUs: 10_000, cutRefreshUs: 400_000,
    cutAgeUs: 5_000, cutAuditUs: 20_000, cutPublishUs: 60_000, bodyNew: 1000, bodyObj: 50_000, bodyList: 10_000, bodyCut: 5_000, bodyNewUs: 3_000, bodyObjUs: 150_000,
    bodyListUs: 200_000, bodyCutUs: 40_000, coldSkips: 7000, coldReads: 3, coldIcs: 30, simX87: 0x7f, workX87: 0x27f, simCsr: 0x1f80, workCsr: 0x1f80, parentRows: 4 } } };
  const out3 = costLines({ tag: 'v8', minutes: 10, native: v8, fix }).join('\n');
  assert.match(out3, /cut per cut \(us\): learn 10\.0, prep 20\.0, refresh 800\.0, age 10\.0, audit 40\.0, publish 120\.0; body calls per cut \(us per cut\): new 2\.0 \(6\.0\), object 100\.0 \(300\.0\), list 20\.0 \(400\.0\), per-cut 10\.0 \(80\.0\); Sim parent objects learned 4; cold: 7000 rows skipped, 3 reads, 30 ICs; x87 sim 0x7f worker 0x27f/);
  const v9 = { before: v8.before, after: { ...v8.after, autshadow: { ...v8.after.autshadow, version: 9, listPrintSkips: 4000, listPrintUs: 10_000, listLots: 2000, listSame: 100,
    tierMs: 1500, tier1Sims: 5000, tier2Sims: 3000, tier2Stale: 2000, tier1Body: 20_000, tier2Body: 4000, tier1BodyUs: 60_000, tier2BodyUs: 12_000, tierClassUs: 8000, ageRuns: 62 } } };
  const out4 = costLines({ tag: 'v9', minutes: 10, native: v9, fix }).join('\n');
  const v10 = { before: v9.before, after: { ...v9.after, autshadow: { ...v9.after.autshadow, version: 10, dirtyLots: 1000, dirtyRows: 2000, dirtyKept: 8000,
    objResolved: 40_000, objResolveUs: 20_000, dirtyUnknown: 3, listAuditN: 500, listAuditUs: 20_000, listMissed: 0, dirtyTripped: 0, tierClassN: 50_000,
    tierCamInval: 7, tierHold: 12, listT2Wait: 600, listT2Ref: 100, t1Lots: 5 } } };
  const out5 = costLines({ tag: 'v10', minutes: 10, native: v10, fix }).join('\n');
  // v11: 600 s, 500 cuts, refUs 3 s over 500 batch requests = 6 ms each; 3000 B13 calls = 5/s; A4 60 of 400 = 15%; moved 6 x 5 x 0.85 =
  // 25.5 ms/s; cut 1.2 ms x 1000 ticks / 600 s = 2.0 ms/s; guard 1.7 ms/s; net 21.8, cut 8% of moved
  const v11 = { before: v10.before, after: { ...v10.after, autshadow: { ...v10.after.autshadow, version: 11, simMarks: 5000, simFreeKept: 3000, listTypes: 12, listTypesSimFree: 9,
    unplaced: 400, liteLots: 10_000, liteUs: 5_000, liteMarks: 50, resyncTouch: 1, resyncMap: 0, olCompact: 0, olRows: 9000, tierKept: 30_000, batchCuts: 20, batchReqs: 500,
    batchMax: 3, metaCalls: 3000, metaTicks: 1000, simTicks: 20_000 } } };
  const out6 = costLines({ tag: 'v11', minutes: 10, native: v11, fix }).join('\n');
  assert.match(out6, /list kinds per cut: Sim-free rows kept 6\.0, Sim marks 10\.0 \(types 9 Sim-free of 12\); unplaced touches 0\.8, lot-vector prints 20\.0 \(10\.0 us\), their marks 0\.1; resyncs 1 touch list \/ 0 map full, map rows 9000, compactions 0; tiers kept 60\.0 Sims; shared cuts 20, 1\.0 requests per cut \(max 3\)/);
  const z8 = Object.fromEntries(['First', 'New', 'Lot', 'Room', 'Gone', 'Lost', 'Lite', 'Kid'].flatMap(n => [['mark' + n, 0], ['row' + n, 0]]));
  const z9 = Object.fromEntries(['4', '5', '6', '0B', '0C', '0E', '12', '18', '21'].map(n => ['markEv' + n, 0]));
  const v12 = { before: v11.before, after: { ...v11.after, autshadow: { ...v11.after.autshadow, version: 12, ...z8, ...z9, markFirst: 1000, rowFirst: 500,
    markEv0E: 1000, seedLots: 30, seedObjs: 4000, seedUs: 2500, seedFull: 0, liteSuspect: 3, rowHot: 200, rowColdSkips: 50_000, rowColdReads: 40 } } };
  const out7 = costLines({ tag: 'v12', minutes: 10, native: v12, fix }).join('\n');
  const v13 = { before: v12.before, after: { ...v12.after, autshadow: { ...v12.after.autshadow, version: 13, clsAsked: 50, clsSet: 45, clsNone: 5,
    typedMarks: 3000, typedKept: 2500, typedAuditN: 200, typedAuditMis: 0, liteTyped: 400, liteTypedNone: 300, clsUs: 10_000 } } };
  const out8 = costLines({ tag: 'v13', minutes: 10, native: v13, fix }).join('\n');
  const v14 = { before: v13.before, after: { ...v13.after, autshadow: { ...v13.after.autshadow, version: 14, runSkip: 1, runN: 1200, runSkips: 400_000,
    rowSkips: 1_500_000, runBuilds: 70, icStampMoves: 9, pubDelta: 430, pubRows: 1_000_000, runCheckMis: 0,
    passPairs: 5000, classPairs: 20_000, freshReqs: 50, freshPairs: 1000, freshDiff: 30, freshTop1: 2, freshTop10: 5, freshUs: 250_000, freshEvery: 8, partialKept: 12, keepPartial: 1 } } };
  assert.match(costLines({ tag: 'v14', minutes: 10, native: v14, fix }).join('\n'),
    /run skip \(RunSkip 1\) per cut: 800\.0 runs \/ 3000\.0 rows skipped of 1200 runs; rebuilds 70, IC stamp moves 9; delta publishes 430, 2000\.0 rows copied/);
  assert.match(costLines({ tag: 'v14', minutes: 10, native: v14, fix }).join('\n'),
    /consume side \[measured\]: pass share 25\.0% \(5000 of 20000 classified pairs pass Test\); fresh re-score \(FreshEvery 8\): 50 searches, 1000 pairs, 3\.0% differ, top pick changes 4\.0%, top-10 changes 10\.0%, 5\.0 ms each \(test cost\); KeepPartial 1: 12 overrun searches kept/);
  // docs/206 §7 Satb probe: 100 spans of 4 ms, 300 stores / 100 words / 2,000 loads / 0.1 events a span, 5 cuts/s
  const v15 = { before: v14.before, after: { ...v14.after, autshadow: { ...v14.after.autshadow, version: 15, satbMode: 1, satbJobSpans: 100,
    satbJobStores: 30_000, satbJobWords: 10_000, satbJobPages: 2_000, satbJobLoads: 200_000, satbJobIc: 10, satbJobUs: 400_000, satbJobMaxWords: 900,
    satbSpans: 100, satbStores: 90_000, satbWords: 25_000, satbIcCalls: 5000, satbIcCallsAll: 300, satbIcShared: 10, satbRtCalls: 40, satbRtWaits: 0,
    satbBig: 0, satbOverflow: 0, metaTicks: 3000 } } };
  const out15 = costLines({ tag: 'v15', minutes: 10, native: v15, fix }).join('\n');
  assert.match(out15, /Satb probe per job span \(4\.0 ms, 100 spans\) \[measured\]: 300\.0 sim stores, 100\.0 words first written \(max 900, 0\.0 MB log\), 20\.0 pages, 2000\.0 sim loads, 0\.1 unlogged events/);
  assert.match(out15, /Satb barrier at go-live \[inferred\]: 5\.0 jobs\/s x \(19\.8 us barrier: .* \+ waits 0\.2 ms\) = 1\.1 ms\/s: PASS/);
  const v13w = { before: v13.before, after: { ...v13.after, autshadow: { ...v13.after.autshadow, windowUs: 4_000 } } };
  assert.match(costLines({ tag: 'v13w', minutes: 10, native: v13w, fix }).join('\n'),
    /worker window \[measured\]: quiescent gap .* 10\.0 us vs 3\.0 ms a job.*overruns 40 of 400 \(10\.0%\) at the test wait; .*sim waits 3\.0 ms a request x 5\.0\/s = 15\.0 ms\/s: net /);
  assert.match(out8, /typed marks per cut: 6\.0 marks, 5\.0 list rows kept; classified 45 of 50 asked \(5 no object, 20\.0 us per request on the sim thread\); typed audits 200, misses 0 \(must be 0\); lot-vector prints typed 0\.8 \(0\.6 no list type\)/);
  assert.match(out7, /dirtying per cut, lots marked \/ list rows refreshed by cause: first 2\.0\/1\.0, new 0\.0\/0\.0, lot 0\.0\/0\.0, .*kid 0\.0\/0\.0; marks by event: 0x4 0\.0, .*0x0E 2\.0, .*; seeding: 30 lots, 4000 objects \(5\.0 us per cut\), 0 in part; lot-vector suspects 3; RowHot 200: 100\.0 rows cold per cut, 40 cold reads/);
  assert.match(out6, /go-live estimate \[inferred\]: scoring moved 25\.5 ms\/s \(6\.0 ms per request x 5\.0 B13 calls\/s x 85\.0% answered\) - cut 2\.0 ms\/s \(1\.7 cuts\/s x 1\.2 ms\) - guard 1\.7 ms\/s = net 21\.8 ms\/s; cut = 8% of the scoring moved \(gate <= 25%: PASS\)/);
  // consume: P = 360 / 500 = 0.72, overrun 40 / 500 = 8%; base 0.92 x 25.5 - 2.0 - 1.7 - 5 x 0.15 = 19.0; p = 0.25: a pair 6 / 0.72 x 1.9 =
  // 15.83 ms, 0.153 selected, consume 5 x 0.92 x 0.153 x 15.83 = 11.1, net 7.9; break-even N 19.0 / (5 x 0.92 x 15.83) = 0.3
  assert.match(out6, /go-live after consume \[inferred\]: overrun 8\.0% of searches, before consume 19\.0 ms\/s; live C1, top 10 \+ bands, pass share 0\.25\/0\.5\/0\.75\/1 \(no pass counter\): net 7\.9\/.* ms\/s with band 1 only, 7\.9\/.* with every band; break-even N 0\.3\/.* \(0\.7 allow-listed pairs per search\)/);
  // pass counter 180 of 360 = 0.5: a pair 6 / 0.72 x 1.3 = 10.83 ms, 0.306 selected, consume 15.25, net 3.8; break-even 19.0 / 49.8 = 0.4
  const outP = costLines({ tag: 'v11p', minutes: 10, native: v11, fix: { ...fix, autshadow_pass: { before: 0, after: 180 } } }).join('\n');
  assert.match(outP, /pass share 0\.50 \[measured\]: net 3\.8 ms\/s with band 1 only, 3\.8 with every band; break-even N 0\.4 /);
  assert.match(out5, /dirty lots \(CutLean=2\) per cut: rows kept 16\.0, refreshed 4\.0, lots marked 2\.0, objects placed 80\.0 \(40\.0 us\); resync cuts 3; list audit 1\.0 rows \(40\.0 us\), misses 0, tripped 0; tier cache: classified 100\.0 Sims, camera moves 7, holds 12; list tiers: 1\.2 rows waiting, 0\.2 lot refreshes, tier-1 lots 5/);
  assert.match(out4, /lists \(CutLean\) per cut: kept by print 8\.0, prints 4\.0 \(20\.0 us\), refreshed same 0\.2; Sim tiers \(TierMs 1500\) per cut: tier 1 10\.0 Sims, body 40\.0 \(120\.0 us\); tier 2 6\.0 Sims, 4\.0 waiting \(stale\), body 8\.0 \(24\.0 us\); classify 16\.0 us; aging runs 62/);
  const out2 = costLines({ tag: 'z', minutes: 10, native: busy, fix }).join('\n');
  assert.match(out2, /GC lock uncontended entries: 580\.0k\/s x 68 ns = 39\.4 ms\/s \[inferred.*lazy gate off/);
  assert.match(out2, /guard thunks on the sim: 100\.0k calls\/s x 17 ns = 1\.7 ms\/s \[inferred/);
  assert.match(out2, /window waits 40, 0\.2 ms\/s/);
  assert.match(out2, /worker aborts by reason: row 900 \(90\.0%\), other 100 \(10\.0%\)/);
  assert.match(out2, /answer table: 3000 rows .*max 8192 rows, memo 2048, evicted 500 \(memo 100\)/);
  assert.match(out2, /worker adds 46\.1 ms\/s .*: 5\.0 \[measured.* \+ 41\.1 \[inferred/);
  // newest row per job id across families; a rerun replaces its job; --tag keeps one family; rows without a profile skipped
  const rows = [{ queue: 'mono-cost3', job: 'ip-r', profileTag: 'a', time: '2026-10-29T01:00:00Z' }, { queue: 'mono-cost4', job: 'ip-r', profileTag: 'b', time: '2026-10-29T02:00:00Z' },
    { queue: 'mono-cost4', job: 'ip-r-rerun', rerunOf: 'ip-r', profileTag: 'c', time: '2026-10-29T03:00:00Z' }, { queue: 'mono-cost', job: 'bp-gates', profileTag: 'd', time: '2026-10-29T00:00:00Z' },
    { queue: 'mono-cost4', job: 'x', time: '2026-10-29T04:00:00Z' }];
  assert.deepEqual(newestPerJob(rows).map(r => r.profileTag), ['d', 'c']);
  assert.deepEqual(newestPerJob(rows, 'mono-cost3').map(r => r.profileTag), ['a']);
  assert.ok(FAMILY_RE.test('mono-cost-2026-10-29.jsonl') && FAMILY_RE.test('mono-cost12-2026-11-01.jsonl') && !FAMILY_RE.test('mono-cost4-2026-10-29-ip-r-native.json') && !FAMILY_RE.test('mono-ledger-2026-10-29.jsonl'));
  // absent counters print n/a, never a number; no worker = no stop-rule line
  const bare = costLines({ tag: 'x', minutes: 10, budgetWhy: 'no x-<secs>-seconds.csv' }).join('\n');
  assert.match(bare, /Mono interpreter: n\/a; GC: n\/a/); assert.match(bare, /\[MonoThread\]: n\/a/); assert.match(bare, /\[AutShadow\]: n\/a/);
  assert.doesNotMatch(bare, /worker adds/);
  const count = costLines({ tag: 'y', minutes: 10, native: { before: native.before, after: { autshadow: { ...native.after.autshadow, mode: 0 } } } }).join('\n');
  assert.match(count, /Mode R match rate: n\/a/); assert.match(count, /compared n\/a/); assert.doesNotMatch(count, /worker adds/);
  console.log(out);
  console.log('self-check ok');
}

if (resolve(process.argv[1] ?? '') === fileURLToPath(import.meta.url)) {
  const args = process.argv.slice(2);
  if (args.includes('--self-check')) selfCheck();
  else {
    const mi = args.indexOf('--minutes'), minutes = mi >= 0 ? Number(args[mi + 1]) : 10;
    const ti = args.indexOf('--tag'), family = ti >= 0 ? args[ti + 1] : undefined;
    const files = args.filter((a, k) => !a.startsWith('--') && !(mi >= 0 && k === mi + 1) && !(ti >= 0 && k === ti + 1));
    let ins: Input[];
    if (files.length) ins = files.flatMap(a => inputs(a, minutes));
    else {
      const q = existsSync(QUEUE) ? readdirSync(QUEUE).filter(f => FAMILY_RE.test(f) && (!family || FAMILY_RE.exec(f)![1] === family)).map(f => join(QUEUE, f)) : [];
      if (!q.length) { console.error(`no ${family ?? 'mono-cost*'}-<date>.jsonl in ${QUEUE} (run a tools/harness/queues/mono-cost*.json, or pass a jsonl or tags)`); process.exit(2); }
      ins = newestPerJob(q.flatMap(jsonl), family).map(r => rowInput(r, minutes));
    }
    console.log(ins.map(i => costLines(i).join('\n')).join('\n\n'));
  }
}
