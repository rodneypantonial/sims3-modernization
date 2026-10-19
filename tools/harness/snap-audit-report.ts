// Report of a [RouteSnap] Enabled=1 Mode=0 run (docs/94 §5 phase P3, ts3native 0.26.0, counters v47): images audited and
// mismatches per input, cut and audit cost, image memory, door channel, and the gate G3 verdict for this world. With
// Lots=replay (P3-B, 0.28.0, counters v49, docs/94 §16) also the replay clone: records, rebases, consumer, lot audits and
// the P3-B verdict.
//   node tools/harness/snap-audit-report.ts probe-out/queue/<queue>-<date>-<job>-native.json [--log <ts3native.log>] [--minutes 10]
//   node tools/harness/snap-audit-report.ts --self-check
// Counts are after - before (the profile window); memory and the ready bits are taken from `after`. With --log the first
// "snap audit MISMATCH" lines (input, key, offset, live object) are listed per input.
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import assert from 'node:assert/strict';

type Counters = Record<string, number>;
export const INPUTS = ['R', 'Gcore', 'Gtiles', 'bnm', 'grid', 'blend', 'tune', 'lots'];
const SKIPS = ['async job busy', 'no world / planner', 'lock failed'];
const DOOR = ['messages', 'lots filled', 'doors', 'invalidations', 'protocol errors'];
export type Gate = { name: string; value: string; verdict: 'PASS' | 'FAIL' | 'OPEN' };

export const REC = ['', 'INS', 'REM', 'STALE', 'LOTS', 'EPOCH', 'AUDIT', 'SYNC'];
const RP_SKIP = ['Q not in the clone', 'applied to a stale lot', 'no footprint copy', 'no clone'];
export function replayLines(log: string) {
  return log.split(/\r?\n/).filter(l => /snap replay (audit MISMATCH|: clone diverged|: clone build failed|: fault)/.test(l))
    .map(l => l.replace(/^.*?snap replay/, 'snap replay'));
}

// P3-B (docs/94 §16): the clone built, every lot audit equal, no divergence / fault / dropped record, sim cost <= 1 ms/s
export function replayReport(before: Counters, after: Counters, minutes: number, log: string, out: (s: string) => void) {
  const d = (k: string) => (after[k] ?? 0) - (before[k] ?? 0);
  const a = (k: string) => after[k] ?? 0;
  const secs = minutes * 60, ready = a('rpReady');
  out(`\nReplay (Lots=replay): rpReady ${ready} (on ${ready & 1 ? 'y' : 'n'}, clone ${ready & 2 ? 'y' : 'n'}, thread ${ready & 4 ? 'y' : 'n'}); ` +
    `clone ${a('rpLots')} lots, ${a('rpFps')} footprint blocks, ${(a('rpKB') / 1024).toFixed(1)} MB now, max ${(a('rpKBMax') / 1024).toFixed(1)} MB; cap switch-off ${a('rpCapOff')}`);
  out(`  builds ${d('rpEpochs')} (failed ${d('rpEpochFails')}, max ${(a('rpEpochMaxUs') / 1000).toFixed(1)} ms); records ` +
    REC.map((n, i) => (i ? `${n} ${d(`rpRec_${i}`)}` : '')).filter(Boolean).join(', ') + `; dropped ${d('rpDrop')}, ring max ${a('rpRingMax')}`);
  if (a('rpPrebuilt') || a('rpPrebuildFail'))
    out(`  prebuild (Prebuild=1): ${a('rpPrebuilt')} lot graphs at load (${a('rpPrebuildFail')} failed), ${(a('rpPrebuildUs') / 1000).toFixed(0)} ms, ` +
      `process private +${(a('rpPrebuildKB') / 1024).toFixed(1)} MB; rebased lots in the window ${d('rpRebaseLots')} (~0 expected outside build/buy and lot add/remove)`);
  out(`  applied ${REC.map((n, i) => (i ? `${n} ${d(`rpApplied_${i}`)}` : '')).filter(Boolean).join(', ')}; ` +
    `not applied: ${RP_SKIP.map((n, i) => `${n} ${d(`rpSkip_${i}`)}`).join(', ')}`);
  const copyMs = d('rpCopyUs') / 1000 / secs, rebaseMs = d('rpRebaseUs') / 1000 / secs, hashMs = d('rpAuditUs') / 1000 / secs;
  const applyMs = d('rpApplyUs') / 1000 / secs, waitMs = d('rpLockWaitUs') / 1000 / secs;
  out(`  sim: footprint copies ${copyMs.toFixed(3)} ms/s (${d('rpCopyFail')} refused, last unknown vtable 0x${(a('rpUnknownVt') >>> 0).toString(16)}), ` +
    `rebases ${d('rpRebaseLots')} lots ${rebaseMs.toFixed(3)} ms/s (max ${(a('rpRebaseMaxUs') / 1000).toFixed(1)} ms), live lot hashing ${hashMs.toFixed(3)} ms/s`);
  out(`  consumer: applying ${applyMs.toFixed(3)} ms/s under +0x918 (longest hold ${(a('rpApplyMaxUs') / 1000).toFixed(2)} ms), waiting ${waitMs.toFixed(3)} ms/s, ` +
    `lag max ${a('rpLag')}, footprints freed ${d('rpGcFreed')}, orphan FMEs ${d('rpOrphanFme')}`);
  const audits = d('rpAudits'), mis = d('rpMis');
  out(`  lot audits ${audits}, mismatches ${mis}; syncs ${d('rpSyncs')}, divergences ${d('rpDiverge')}; faults ${d('rpFaults')}`);
  for (const l of replayLines(log).slice(0, 8)) out(`      ${l}`);
  const gates: Gate[] = [];
  gates.push({ name: 'clone', value: ready & 2 ? `built ${d('rpEpochs')}x` : a('rpCapOff') ? 'switched off at ReplayCapMB' : 'none',
    verdict: ready & 2 && !d('rpEpochFails') ? 'PASS' : 'FAIL' });
  gates.push({ name: 'lot mismatches', value: `${mis} in ${audits} lot audits (>= 200 needed)`, verdict: mis ? 'FAIL' : audits >= 200 ? 'PASS' : 'OPEN' });
  gates.push({ name: 'divergence / faults / drops', value: `${d('rpDiverge')} / ${d('rpFaults')} / ${d('rpDrop')}`,
    verdict: d('rpFaults') ? 'FAIL' : d('rpDiverge') || d('rpDrop') ? 'OPEN' : 'PASS' });
  gates.push({ name: 'sim cost', value: `${(copyMs + rebaseMs).toFixed(3)} ms/s copies + rebases (<= 1)`, verdict: copyMs + rebaseMs <= 1 ? 'PASS' : 'FAIL' });
  gates.push({ name: 'memory', value: `${(a('rpKBMax') / 1024).toFixed(1)} MB (<= 128)`, verdict: a('rpKBMax') <= 128 * 1024 ? 'PASS' : 'FAIL' });
  // docs/94 §16.6-§16.7: the clone on private pools (v53); an older asi shares the game's unlocked pools, so its audits are not trusted
  gates.push(a('version') >= 53
    ? { name: 'private pools', value: `sites ${a('plReady') & 1 ? 'in' : 'NOT in'} place, rpLivePool ${a('rpLivePool')}, plForeign ${a('plForeign')}, ` +
        `plOverlap ${a('plOverlap')}, pool epochs ${d('plEpochs')}, region ${(a('plKB') / 1024).toFixed(1)} MB`,
        verdict: a('plReady') & 1 && !a('rpLivePool') && !a('plForeign') && !a('plOverlap') ? 'PASS' : 'FAIL' }
    : { name: 'private pools', value: 'none (pre-v53 asi: the clone shares the unlocked game pools)', verdict: 'OPEN' });
  const v = gates.some(g => g.verdict === 'FAIL') ? 'FAIL' : gates.some(g => g.verdict === 'OPEN') ? 'OPEN' : 'PASS';
  out(`P3-B (this world): ${v}`);
  for (const g of gates) out(`  ${g.verdict.padEnd(4)} ${g.name}: ${g.value}`);
  return { gates, verdict: v };
}

// P5 (docs/94 §22, counters v61): [RouteSnap] Mode=2 takes the clone run's answer for ReplanFromPosition jobs
export const AB = ['clone', 'nover', 'epoch', 'cold', 'unsupported', 'capture', 'nrbusy', 'mat', 'door-miss', 'door-over', 'managed',
  'event', 'livelock', 'lazy', 'fault', 'other'];
const M2_OFF = ['', 'site owned by an unknown hook (M2-1)', '3 clone-run tripwires (M2-2)', 'P4 fault'];
const ASKS = ['0', '1-2', '3-5', '6-10', '11+'];
export function p5Report(before: Counters, after: Counters, out: (s: string) => void) {
  const d = (k: string) => (after[k] ?? 0) - (before[k] ?? 0);
  const a = (k: string) => after[k] ?? 0;
  const ready = a('m2Ready'), off = a('m2Off'), plans = d('m2Plans'), taken = d('m2Taken');
  out(`\nP5 (Mode=2): m2Ready ${ready} (on ${ready & 1 ? 'y' : 'n'}, M2-1 sites ${a('m2Sites')}, L2 read ${ready & 4 ? 'y' : 'n'}), take switch ` +
    `${a('m2Take')}; ${off ? `OFF: ${M2_OFF[off] ?? off}${a('m2SiteAt') ? ` at 0x${(a('m2SiteAt') >>> 0).toString(16)}` : ''}` : 'still on'}`);
  const fb = AB.map((n, i) => [n, d(`m2Fb_${i}`)] as const).filter(([, c]) => c);
  out(`  armed ${d('m2Armed')}, reached the plan ${plans}, taken ${taken} (${plans ? (100 * taken / plans).toFixed(1) : '0.0'}%); the game's plan: ` +
    `${fb.map(([n, c]) => `${n} ${c}`).join(', ') || 'none'}; of capture: list not empty ${d('m2FbList')}; nr skew ${d('m2FbNr')}`);
  out(`  verify ${d('m2Verify')}: ${[0, 1, 2, 3, 4, 5].map(i => `M${i} ${d(`m2VerM_${i}`)}`).join(', ')}; would-be-wrong (M2/M3) ${d('m2VerBad')} ` +
    `(G4 classes: shadow-compare-report.ts on the "m2":1 records)`);
  const wait = d('m2DoorWaitUs'), job = d('m2JobUs');
  out(`  door asks ${d('m2DoorAsk')}; clone runs by asks ${ASKS.map((n, i) => `${n}: ${d(`m2DoorHist_${i}`)}`).join(', ')}; ask wait ` +
    `${(wait / 1000).toFixed(1)} ms of ${(job / 1000).toFixed(1)} ms clone run time (${job ? (wait / job).toFixed(2) : '0.00'}; > 0.50 = build the ` +
    `evaluator (a), docs/94 §19.1)`);
  out(`  cuts with replay records after them ${d('m2Stale')} (${d('m2StaleRecs')} records); tripwires ${d('m2Trips')}; ModGuard L2 bits ${a('m2Guard')}`);
  const gates: Gate[] = [];
  gates.push({ name: 'Mode=2 on at the end', value: off ? M2_OFF[off] ?? String(off) : 'on', verdict: !off && ready & 1 ? 'PASS' : 'FAIL' });
  gates.push({ name: 'answers taken', value: `${taken} of ${plans}`, verdict: taken ? 'PASS' : 'OPEN' });
  gates.push({ name: 'verify M2 / M3', value: `${d('m2VerBad')} in ${d('m2Verify')} verify jobs`, verdict: d('m2VerBad') ? 'FAIL' : d('m2Verify') ? 'PASS' : 'OPEN' });
  gates.push({ name: 'faults / tripwires', value: `${d('snShadowFault')} / ${d('m2Trips')}`, verdict: d('snShadowFault') ? 'FAIL' : d('m2Trips') ? 'OPEN' : 'PASS' });
  const v = gates.some(g => g.verdict === 'FAIL') ? 'FAIL' : gates.some(g => g.verdict === 'OPEN') ? 'OPEN' : 'PASS';
  out(`P5 first run (this world): ${v}`);
  for (const g of gates) out(`  ${g.verdict.padEnd(4)} ${g.name}: ${g.value}`);
  return { gates, verdict: v };
}

export function mismatchLines(log: string) {
  const by: Record<string, string[]> = {};
  for (const l of log.split(/\r?\n/)) {
    const m = l.match(/snap audit MISMATCH (\S+) (.*)$/);
    if (m) (by[m[1]] ??= []).push(m[2]);
  }
  return by;
}

export function report(before: Counters, after: Counters, minutes: number, log = '') {
  const d = (k: string) => (after[k] ?? 0) - (before[k] ?? 0);
  const a = (k: string) => after[k] ?? 0;
  const lines: string[] = [];
  const out = (s: string) => lines.push(s);
  const ready = a('spReady');
  out(`spReady ${ready} (images ${ready & 1 ? 'on' : 'off'}, shared P1 sites ${ready & 2 ? 'y' : 'n'}, door channel ${ready & 4 ? 'y' : 'n'}); ` +
    `mode ${a('spMode')}; cap switch-off ${a('spCapOff')}; P1 sites ${a('snHooks')} own, ${a('snShared')} shared, ${a('snSkipped')} skipped`);
  const cuts = d('spCuts'), secs = minutes * 60;
  out(`Cuts ${cuts} (${(cuts / secs).toFixed(2)}/s); skipped: ${SKIPS.map((s, i) => `${s} ${d(`spCutSkip_${i}`)}`).join(', ')}; ` +
    `epochs ${d('spEpochs')}, full builds ${d('spFullBuilds')}; pending updates at the cut ${d('spPendingAtCut')}, deferred flushes ${d('spDeferredAtCut')}`);
  // v55 lazy Gtiles (docs/94 §21.9): cut cost = cut work + tiles built for a reader (armed P4 cut); tiles built only for the
  // audit are audit cost. Before v55 every dirty tile was built at every cut (inside spCutUs)
  const lazy = (after.version ?? 0) >= 55;
  const cutMsPerS = d('spCutUs') / 1000 / secs, auditMsPerS = (d('spAuditUs') + d('spTileFillUs_1')) / 1000 / secs;
  out(`Cut cost ${cutMsPerS.toFixed(3)} ms/s (max ${(a('spCutMaxUs') / 1000).toFixed(1)} ms); audit cost ${auditMsPerS.toFixed(3)} ms/s ` +
    `(max ${(a('spAuditMaxUs') / 1000).toFixed(1)} ms)`);
  if (lazy) {
    const per = (n: number, us: number) => n ? `${(us / n).toFixed(0)} us/tile` : '-';
    out(`Lazy tiles: built for a reader ${d('spTileFill_0')} (${(d('spTileFillUs_0') / 1000 / secs).toFixed(3)} ms/s, in the cut cost, ` +
      `${per(d('spTileFill_0'), d('spTileFillUs_0'))}), for the audit ${d('spTileFill_1')} (${(d('spTileFillUs_1') / 1000 / secs).toFixed(3)} ms/s, ` +
      `${per(d('spTileFill_1'), d('spTileFillUs_1'))}); not carried now ${a('spTileOut')}`);
  }
  // v56 H20 tripwire (docs/94 §21.8.1 risk 1): a variation grow of a portal the clone holds, during a clone run
  if ((after.version ?? 0) >= 56)
    out(`H20 tripwire: AddPortalLocationVariation on a clone-held portal in a clone run ${d('p4VarTrip')} ` +
      `(expected 0; non-zero = put D2 on that IC slot)`);
  // v57 (docs/94 §21.11): the lot-0 remove guard and the P4 deferral stop
  if ((after.version ?? 0) >= 57)
    out(`Lot-0 remove guard: ${d('lrPendFlush')} pending-remove flushes, ${d('lrPendBusy')} left on an iterated Q (expected 0); ` +
      `P4 deferral stops ${d('pcDeferStop')} (expected 0)`);
  out(`Images: ${a('spImgLive')} live in ${a('spVersionsLive')} version(s), ${(a('spKB') / 1024).toFixed(1)} MB now, max ${(a('spKBMax') / 1024).toFixed(1)} MB; ` +
    `${a('spLots')} lots, ${a('spTiles')} tiles; faults ${d('spFaults')}, dirty-set overflows ${d('spDirtyOver')}, key map full ${d('spKeyMapFull')}`);
  out(`\nPer input: rebuilt / audited / mismatches`);
  let audits = 0, mis = 0;
  const misBy = mismatchLines(log);
  INPUTS.forEach((n, i) => {
    const c = d(`spCopies_${i}`), au = d(`spAudits_${i}`), m = d(`spMis_${i}`);
    audits += au, mis += m;
    out(`  ${n.padEnd(7)} ${String(c).padStart(8)} / ${String(au).padStart(8)} / ${m}`);
    for (const l of (misBy[n] ?? []).slice(0, 5)) out(`      ${l}`);
  });
  out(`Door channel: ${DOOR.map((s, i) => `${s} ${d(`spDoor_${i}`)}`).join(', ')}`);

  // G3 (docs/94 §5) for this world: 0 mismatches in every input, cut cost <= 2 ms/s, memory <= 64 MB, enough audits
  const gates: Gate[] = [];
  gates.push({ name: 'build on', value: ready & 1 ? 'images on' : a('spCapOff') ? 'switched off at CapMB' : 'not running', verdict: ready & 1 ? 'PASS' : 'FAIL' });
  gates.push({ name: 'mismatches', value: `${mis} in ${audits} image audits`, verdict: mis ? 'FAIL' : 'PASS' });
  // lots are audited only when a per-lot strategy carries images (docs/94 §11; live default none)
  const missing = INPUTS.filter((n, i) => n !== 'lots' && !d(`spAudits_${i}`));
  gates.push({ name: 'audits', value: missing.length ? `none for ${missing.join(', ')}` : `${audits} (>= 5000 needed)`,
    verdict: missing.length ? 'FAIL' : audits >= 5000 ? 'PASS' : 'OPEN' });
  gates.push({ name: 'cut cost', value: `${cutMsPerS.toFixed(3)} ms/s (<= 2${lazy ? '; reader tile builds in, audit-only ones out' : ''})`,
    verdict: cutMsPerS <= 2 ? 'PASS' : 'FAIL' });
  gates.push({ name: 'memory', value: `${(a('spKBMax') / 1024).toFixed(1)} MB (<= 64)`, verdict: a('spKBMax') <= 64 * 1024 ? 'PASS' : 'FAIL' });
  gates.push({ name: 'faults', value: `${d('spFaults')} image builds stopped`, verdict: d('spFaults') ? 'OPEN' : 'PASS' });
  const g3 = gates.some(g => g.verdict === 'FAIL') ? 'FAIL' : gates.some(g => g.verdict === 'OPEN') ? 'OPEN' : 'PASS';
  out(`\nG3 (this world): ${g3}`);
  for (const g of gates) out(`  ${g.verdict.padEnd(4)} ${g.name}: ${g.value}`);
  const p3b = (after.version ?? 0) >= 49 && (a('rpReady') & 1 || a('rpCapOff')) ? replayReport(before, after, minutes, log, out) : undefined;
  const p5 = (after.version ?? 0) >= 61 && (a('m2Ready') || a('m2Off')) ? p5Report(before, after, out) : undefined;
  return { lines, gates, g3, p3b, p5 };
}

function selfCheck() {
  const ok: Counters = { version: 47, spReady: 7, spCuts: 2400, spCutUs: 600000, spAuditUs: 3000000, spKBMax: 20000, spKB: 19000,
    snShared: 9, snHooks: 23 };
  INPUTS.forEach((_, i) => { ok[`spAudits_${i}`] = 1000; ok[`spCopies_${i}`] = 10; });
  let r = report({}, ok, 10);
  assert.equal(r.g3, 'PASS');
  assert.match(r.lines.join('\n'), /Cut cost 1\.000 ms\/s/);
  // a mismatch with its log line; an input never audited; too few audits; cost and memory over
  const log = 'x\r\nsnap audit MISMATCH lots key 0x0000000000000123: image 400 bytes, live 400, first difference at +0x2C\r\n';
  r = report({}, { ...ok, spMis_7: 1 }, 10, log);
  assert.equal(r.g3, 'FAIL');
  assert.match(r.lines.join('\n'), /lots\s+10 \/\s+1000 \/ 1\n\s+key 0x0000000000000123: image 400 bytes/);
  r = report({}, { ...ok, spAudits_5: 0 }, 10);
  assert.equal(r.gates.find(g => g.name === 'audits')!.verdict, 'FAIL');
  assert.equal(report({}, { ...ok, spAudits_7: 0 }, 10).g3, 'PASS');  // no lot strategy: no lot audits needed
  const few: Counters = { ...ok };
  INPUTS.forEach((_, i) => { few[`spAudits_${i}`] = 10; });
  assert.equal(report({}, few, 10).g3, 'OPEN');
  r = report({}, { ...ok, spCutUs: 2000000, spKBMax: 70000 }, 10);
  assert.equal(r.gates.find(g => g.name === 'cut cost')!.verdict, 'FAIL');
  assert.equal(r.gates.find(g => g.name === 'memory')!.verdict, 'FAIL');
  assert.equal(report({}, { ...ok, spReady: 0, spCapOff: 1 }, 10).gates[0].value, 'switched off at CapMB');
  // the window: before is subtracted
  assert.equal(report({ spMis_0: 3 }, { ...ok, spMis_0: 3 }, 10).g3, 'PASS');
  // P3-B: no replay rows before v49 or with Lots=none; a clean replay run passes; a lot mismatch with its log line fails
  assert.equal(report({}, ok, 10).p3b, undefined);
  const rp: Counters = { ...ok, version: 53, plReady: 7, rpReady: 7, rpEpochs: 1, rpAudits: 2400, rpRec_1: 250000, rpRec_2: 240000, rpCopyUs: 300000,
    rpRebaseUs: 60000, rpKBMax: 40000, rpLots: 82 };
  r = report({}, rp, 10);
  assert.equal(r.p3b!.verdict, 'PASS');
  assert.match(r.lines.join('\n'), /records INS 250000, REM 240000/);
  assert.doesNotMatch(r.lines.join('\n'), /prebuild/);
  assert.match(report({}, { ...rp, rpPrebuilt: 80, rpPrebuildUs: 900000, rpPrebuildKB: 30720 }, 10).lines.join('\n'),
    /prebuild \(Prebuild=1\): 80 lot graphs at load \(0 failed\), 900 ms, process private \+30\.0 MB/);
  const rlog = 'snap replay audit MISMATCH lot 0x0000000000000123: hash differs, entries live 40 clone 39 (clone 82 lots, map 9 / live 9)\r\n';
  r = report({}, { ...rp, rpMis: 1 }, 10, rlog);
  assert.equal(r.p3b!.verdict, 'FAIL');
  assert.match(r.lines.join('\n'), /snap replay audit MISMATCH lot 0x0000000000000123/);
  assert.equal(report({}, { ...rp, rpDiverge: 2 }, 10).p3b!.verdict, 'OPEN');
  assert.equal(report({}, { ...rp, rpAudits: 50 }, 10).p3b!.verdict, 'OPEN');
  assert.equal(report({}, { ...rp, rpCopyUs: 900000 }, 10).p3b!.gates.find(g => g.name === 'sim cost')!.verdict, 'FAIL');
  assert.equal(report({}, { ...rp, rpReady: 5 }, 10).p3b!.verdict, 'FAIL');  // no clone held
  assert.equal(report({}, { ...rp, rpLivePool: 1 }, 10).p3b!.verdict, 'FAIL');  // the clone touched a game pool
  assert.equal(report({}, { ...rp, version: 51 }, 10).p3b!.verdict, 'OPEN');  // no private pools
  // v55 lazy tiles: audit-only tile builds count as audit cost, reader builds stay in the cut cost
  r = report({}, { ...ok, version: 55, spTileFill_0: 300, spTileFillUs_0: 30000, spTileFill_1: 900, spTileFillUs_1: 600000, spTileOut: 4 }, 10);
  assert.equal(r.g3, 'PASS');
  assert.match(r.lines.join('\n'), /Cut cost 1\.000 ms\/s .*audit cost 6\.000 ms\/s/);
  assert.match(r.lines.join('\n'), /Lazy tiles: built for a reader 300 \(0\.050 ms\/s, in the cut cost, 100 us\/tile\), for the audit 900 \(1\.000 ms\/s, 667 us\/tile\); not carried now 4/);
  assert.doesNotMatch(report({}, ok, 10).lines.join('\n'), /Lazy tiles/);
  // v56 H20 tripwire line: the window count, absent before v56
  assert.match(report({ p4VarTrip: 1 }, { ...ok, version: 56, p4VarTrip: 3 }, 10).lines.join('\n'),
    /H20 tripwire: AddPortalLocationVariation on a clone-held portal in a clone run 2 \(expected 0/);
  assert.doesNotMatch(report({}, { ...ok, version: 55 }, 10).lines.join('\n'), /H20 tripwire/);
  // v61 P5: no lines without Mode=2; a clean run passes; a verify M2, a fault or Mode=2 turned off fail
  assert.equal(report({}, { ...ok, version: 61 }, 10).p5, undefined);
  const p5: Counters = { ...ok, version: 61, m2Ready: 7, m2Take: 1, m2Sites: 90, m2Armed: 1000, m2Plans: 990, m2Taken: 800, m2Fb_0: 40, m2Fb_2: 50,
    m2FbNr: 10, m2Verify: 90, m2VerM_0: 88, m2VerM_1: 2, m2DoorAsk: 3000, m2DoorHist_2: 600, m2DoorWaitUs: 4e6, m2JobUs: 1e7 };
  r = report({}, p5, 10);
  assert.equal(r.p5!.verdict, 'PASS');
  const t5 = r.lines.join('\n');
  assert.match(t5, /taken 800 \(80\.8%\); the game's plan: clone 40, epoch 50; of capture: list not empty 0; nr skew 10/);
  assert.match(t5, /ask wait 4000\.0 ms of 10000\.0 ms clone run time \(0\.40;/);
  assert.equal(report({}, { ...p5, m2VerBad: 1, m2VerM_2: 1 }, 10).p5!.verdict, 'FAIL');
  assert.equal(report({}, { ...p5, snShadowFault: 1 }, 10).p5!.verdict, 'FAIL');
  r = report({}, { ...p5, m2Ready: 6, m2Off: 1, m2SiteAt: 0x82c6e0 }, 10);
  assert.equal(r.p5!.verdict, 'FAIL');
  assert.match(r.lines.join('\n'), /OFF: site owned by an unknown hook \(M2-1\) at 0x82c6e0/);
  assert.equal(report({}, { ...p5, m2Taken: 0 }, 10).p5!.verdict, 'OPEN');
  console.log('snap-audit-report self-check ok');
}

if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) {
  const args = process.argv.slice(2);
  if (args.includes('--self-check')) selfCheck();
  else if (!args.length || args[0].startsWith('--')) {
    console.log('usage: node tools/harness/snap-audit-report.ts <...-native.json> [--log <ts3native.log>] [--minutes 10] | --self-check');
    process.exitCode = 2;
  } else {
    const { before, after } = JSON.parse(readFileSync(args[0], 'utf8')) as { before: Counters; after: Counters };
    if ((after?.version ?? 0) < 47) throw new Error(`counters v${after?.version} < 47: not a [RouteSnap] P3 build`);
    const opt = (k: string) => { const i = args.indexOf(k); return i >= 0 ? args[i + 1] : undefined; };
    const logPath = opt('--log');
    console.log(report(before ?? {}, after, Number(opt('--minutes') ?? 10), logPath ? readFileSync(logPath, 'utf8') : '').lines.join('\n'));
  }
}
