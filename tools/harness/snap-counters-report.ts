// Report of a [RouteSnap] Count=1 run (docs/94 §5 phase P1, ts3native 0.25.0, counters v46; P1b 0.27.0, v48): per-writer
// rates per minute with the thread split and the sim-thread source split, IC slots, world and object events, G builds, route
// START windows, design B records, and the gate G1 verdict as docs/94 §11.5 defines it for design B (v46 runs: the checks
// that need P1b counters are OPEN).
//   node tools/harness/snap-counters-report.ts probe-out/queue/<queue>-<date>-<job>-native.json [--minutes 10]
//   node tools/harness/snap-counters-report.ts --self-check
// Counts are after - before (the profile window, --minutes long). Unattributed callers are named from
// tools/ghidra/symbols/ts3w-1.67.symbols.csv and, when present, game-copy/analysis/functions.csv.
import { readFileSync, existsSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import assert from 'node:assert/strict';

type Counters = Record<string, number>;
const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..', '..');

// routesnap.cpp SL_* order; `writer` rows are the writer layer G1 rates and off-sim checks use
export const LEAVES = [
  'W1 LotQuadTree_ApplyInsert', 'W2 LotQuadTree_ApplyRemove', 'W3 LotRouting_QuadTreeBuild', 'W4 LotGraph_Build',
  'W5 LotTable_Insert', 'W6 footprint polygon caches (8)', '0x8321F0 UpdateObjectFootprintCore', '0x831BA0 AddPrivateFootprint',
  '0x82A0C0 footprint remove', '0x635700 RemoveObjectFootprints', '0x829D50 RemoveLot', '0x82B590 ApplyLotUpdate (R bit 7)',
  '0x82A190 RestoreLotBlockedBits (R bit 7)', '0x636BF0 grid AddHangingFootprint', '0x62D9F0 grid remove',
  '0x6733C0 high-cost polygon', '0x670510 high-cost remove', 'G build (5 call sites)', 'blend layer 0 reload 0xC62AC0'];
const WRITER_ROWS = 17;  // rows 0-16; G build and blend are epochs, not dirty marks
const CLASS = ['sim', 'worker', 'other'];
const SRC = ['none', 'IC', 'world event', 'object event', 'Footprint_SetEnabled'];
// v48 (routesnap.cpp SRC_* 5..11, RK_*): P1b sources and design B record kinds
export const SRC2 = ['platform 0xB57F20', 'user tool 0xD442B0', 'tree init 0x948F30', 'lazy terrain 0x834B80', 'road plan 0xC7BF70',
  'deferred flush 0x666A40', 'design D drain'];
export const REC = ['W1 insert', 'W2 remove', 'W3 tree create', 'W5 lot table', 'grid add', 'grid remove', 'high-cost add',
  'high-cost remove', 'R bit 7', 'lock-key IC', 'portal field IC'];
const REC_SUPPRESSED = 11;
const SEC_BINS = ['0', '1-250', '251-500', '501-1000', '1001-2000', '2001-5000', '5001-10000', '>10000'];
const SEC_TOP = [0, 250, 500, 1000, 2000, 5000, 10000, Infinity];
const COPY_BINS = ['<=0.1', '<=0.25', '<=0.5', '<=1', '<=2', '<=5', '<=10', '>10'];  // ms
const COPY_TOP = [100, 250, 500, 1000, 2000, 5000, 10000, Infinity];  // us
const REBASE_ICS = [4, 5, 6, 7, 8, 9, 10, 11, 22, 29, 30, 31, 32, 33];
export const ICS = ['FootprintSetLockKeys', 'FootprintAddLockKey', 'FootprintRemoveLockKey', 'FootprintClearLockKeys', 'AddPortal',
  'AddPortalLocationVariation', 'AddDivingBoardPortals', 'AddWaterSlidePortals', 'AddPoolPortals', 'AddPlatformPortals', 'AddStairPortals',
  'RemovePortals', 'SetPortalCost', 'SetPortalAgeGenderFlags', 'SetPortalLockStatus', 'SetOceanPortalOwner', 'DestroyPortalHandle',
  'MarkRoomForUpdate', 'AddDiscourageFootprint', 'RemoveDiscourageFootprint', 'AddPrivateFootprint', 'RemovePrivateFootprint',
  'OnExitBuildBuy', 'World_LotClusterTrees', 'World_DoorLockStateChanged', 'World_ObjectSetHiddenFlags',
  'World_ObjectSetHiddenFlagsWithWorldFootprint', 'World_ObjectEnableFootprint', 'World_ObjectResetFootprintState',
  'World_BuildRoutingData', 'World_BulldozeLot', 'World_CreateHouseboatLot', 'World_DeleteHouseboatLot', 'World_LotSetTerrainHeight',
  'World_LotSetEntranceByTargetPoint', 'Route_Plan (START)', 'Route_Replan (START)', 'Route_PlanFromPoint (START)',
  'Route_ReplanFromPosition (START)'];
const OBJ = ['4 added', '6 destroyed', '9 lot', '0x0A lot', '0x0B in scene', '0x0E transform', '0x18 visual flags', '0x21 parent'];
const GSITE = ['0x82A098', '0x834A82', '0x8352F1', '0x835390', '0x8358B5'];
const LOT_BINS = ['0', '1', '2', '3', '4', '5', '6-10', '>10'], LOT_TOP = [0, 1, 2, 3, 4, 5, 10, Infinity];
const WRITE_BINS = ['0', '1-2', '3-5', '6-10', '11-20', '21-50', '51-100', '>100'];

// p-th percentile bin of a histogram (index), -1 when empty
export function pctBin(h: number[], p: number) {
  const n = h.reduce((s, x) => s + x, 0);
  if (!n) return -1;
  let acc = 0;
  for (let i = 0; i < h.length; i++) { acc += h[i]; if (acc >= p * n) return i; }
  return h.length - 1;
}

export type Gate = { name: string; value: string; verdict: 'PASS' | 'FAIL' | 'OPEN' };

export function report(before: Counters, after: Counters, minutes: number, name: (a: number) => string = a => hex(a)) {
  const d = (k: string) => (after[k] ?? 0) - (before[k] ?? 0);
  const a = (k: string) => after[k] ?? 0;
  const perMin = (x: number) => (x / minutes).toFixed(x / minutes < 10 ? 1 : 0);
  const lines: string[] = [];
  const out = (s: string) => lines.push(s);
  const ready = a('snReady');
  out(`snReady ${ready} (leaf ${ready & 1 ? 'y' : 'n'}, World_FireEvent ${ready & 2 ? 'y' : 'n'}, IC slots ${ready & 4 ? 'y' : 'n'}, object nodes ${ready & 8 ? 'y' : 'n'}); ` +
    `${a('snHooks')} sites own, ${a('snShared')} shared, ${a('snSkipped')} skipped; ${a('snIcSlots')}/39 IC slots; ${a('snObjNodes')} object nodes ` +
    `(${a('snObjRegFails')} register fails); frames not taken ${a('snFrameFull')}`);

  out(`\nWriter leaves, per minute (window ${minutes} min): total | sim / worker / other | sim by source: ${SRC.join(' / ')}`);
  const v48 = a('version') >= 48;
  let writes = 0, offSim = 0, simRows = 0;
  const offRows: string[] = [];
  LEAVES.forEach((leaf, r) => {
    const cls = [0, 1, 2].map(c => d(`snLeaf_${r}_${c}`));
    const tot = cls[0] + cls[1] + cls[2];
    const src = [0, 1, 2, 3, 4].map(s => d(`snSrc_${r}_${s}`));
    if (r < WRITER_ROWS) { writes += tot; offSim += cls[2]; if (cls[2]) offRows.push(`${leaf} ${cls[2]}`); }
    const src2 = SRC2.map((_, s) => d(`snSrc2_${r}_${s}`));
    out(`  ${leaf.padEnd(42)} ${perMin(tot).padStart(8)} | ${cls.map(perMin).join(' / ')}${r < WRITER_ROWS ? ` | ${src.map(perMin).join(' / ')}` : ''}` +
      (v48 && r < WRITER_ROWS ? ` | ${src2.map(perMin).join(' / ')}` : ''));
    if (r < WRITER_ROWS) simRows += cls[0];
  });
  if (v48) out(`  (last group: sim by P1b source ${SRC2.join(' / ')}; W6 with no source = cache fills ${perMin(d('snCacheFill'))}/min; ` +
    `P1b frame entries ${SRC2.slice(0, 6).map((s, i) => `${s.split(' ')[0]} ${d(`snFrameCalls_${i}`)}`).join(', ')})`);
  out(`  G builds by call site: ${GSITE.map((s, i) => `${s} ${d(`snGSite_${i}`)}`).join(', ')}; sequence: last G build #${a('snSeqGBuild')}, last world event 7 #${a('snSeqEv7')}` +
    (a('snSeqGBuild') && a('snSeqEv7') ? (a('snSeqGBuild') < a('snSeqEv7') ? ' (G build before event 7)' : ' (a G build after event 7)') : ''));
  out(`  Footprint_SetEnabled calls per minute: ${[0, 1, 2].map(c => `${CLASS[c]} ${perMin(d(`snFpEn_${c}`))}`).join(', ')}`);

  out(`\nIC slots (calls / sim writes inside, whole window):`);
  ICS.forEach((ic, i) => { const n = d(`snIc_${i}`), w = d(`snIcWrites_${i}`); if (n || w) out(`  ${ic.padEnd(46)} ${n} / ${w}`); });
  out(`  off the sim thread: ${d('snIcOffSim')}; object chains still open at an IC entry: ${d('snObjOpenAtIc')}`);
  const wev: string[] = [];
  for (let i = 0; i < 96; i++) { const n = d(`snWev_${i}`); if (n) wev.push(`${i === 95 ? '>=95' : '0x' + i.toString(16)} ${n}`); }
  out(`World events by id: ${wev.join(', ') || 'none'}; off the sim thread ${d('snWevOffSim')}`);
  out(`Object events per minute (sim / worker / other): ${OBJ.map((o, i) => `${o} ${[0, 1, 2].map(c => perMin(d(`snObj_${i}_${c}`))).join('/')}`).join('; ')}`);

  const ua = d('snUnattributed');
  const keys: string[] = [];
  let topCaller = 0, uaOther = 0;
  if (v48) {  // 64 keys: leaf << 24 | return address, and the next frame (heuristic)
    const rows: [number, string][] = [];
    for (let i = 0; i < 64; i++) {
      const k = a(`snUa2Key_${i}`) >>> 0, nx = a(`snUa2Next_${i}`) >>> 0, n = d(`snUa2N_${i}`);
      if (!k || !n) continue;
      const ret = k & 0xFFFFFF;
      rows.push([n, `${LEAVES[k >>> 24]?.split(' ')[0] ?? k >>> 24} at ${hex(ret)} ${name(ret)} <- ${nx ? `${hex(nx)} ${name(nx)}` : '?'}: ${n}`]);
    }
    rows.sort((x, y) => y[0] - x[0]);
    topCaller = rows.length ? rows[0][0] : 0;
    uaOther = d('snUa2Other');
    keys.push(...rows.map(r => r[1]));
  } else {
    for (let i = 0; i < 16; i++) { const k = a(`snUaKey_${i}`) >>> 0, n = d(`snUaN_${i}`); if (k && n) { keys.push(`${hex(k)} ${name(k)}: ${n}`); topCaller = Math.max(topCaller, n); } }
    uaOther = d('snUaOther');
  }
  if (uaOther) keys.push(`other callers: ${uaOther}`);
  out(`\nUnattributed sim-thread leaf hits: ${ua}${keys.length ? '\n  ' + keys.join('\n  ') : ''}`);

  const starts = d('snStarts');
  const lots = LOT_BINS.map((_, i) => d(`snStartLots_${i}`)), wr = WRITE_BINS.map((_, i) => d(`snStartWrites_${i}`));
  out(`\nRoute STARTs: ${starts}; known dirty lots per START ${LOT_BINS.map((b, i) => `${b}:${lots[i]}`).join(' ')}; ` +
    `writes per START ${WRITE_BINS.map((b, i) => `${b}:${wr[i]}`).join(' ')}; STARTs with writes of unknown lot ${d('snStartUnknown')}; set overflow ${d('snSetFull')}`);

  const secs = minutes * 60;
  const p95 = pctBin(lots, 0.95);
  out(`  information (no longer gates, docs/94 §11.5): ${(starts / secs).toFixed(1)} STARTs/s; known dirty lots per START p95 ` +
    `${p95 < 0 ? '-' : LOT_BINS[p95]} (${starts ? Math.round(100 * d('snStartUnknown') / starts) : 0}% of STARTs also had writes of unknown lot); ` +
    `writer rows ${(writes / secs).toFixed(0)}/s all threads`);

  // design B records (v48), else the v46 estimate from the leaf rows (route worker excluded, no buckets)
  const rec = REC.map((_, i) => d(`snRec_${i}`));
  let records = rec.reduce((s, x) => s + x, 0);
  const secHist = SEC_BINS.map((_, i) => d(`snRecSec_${i}`));
  if (v48) {
    out(`\nDesign B records (docs/94 §11.4 step 2; per second): ${REC.map((r, i) => `${r} ${(rec[i] / secs).toFixed(1)}`).join(', ')}; ` +
      `suppressed in rebase IC frames ${d(`snRec_${REC_SUPPRESSED}`)}`);
    out(`  1-s buckets: ${SEC_BINS.map((b, i) => `${b}:${secHist[i]}`).join(' ')}; most in one second since start (loads included) ${a('snRecMaxSec')}`);
    const cn = d('snCloneN');
    out(`  record bookkeeping ${d('snRecUs')} us; sim W1 clone-equivalent copies ${cn} (${d('snCloneUs')} us, ` +
      `${cn ? Math.round(d('snCloneBytesK') * 1024 / cn) : 0} bytes avg, ${a('snCloneMaxBytes')} max, ${d('snCloneFault')} faults)`);
  } else {
    records = [0, 1, 2, 4, 13, 14, 15, 16, 11, 12].reduce((s, r) => s + d(`snLeaf_${r}_0`) + d(`snLeaf_${r}_2`), 0);  // W1 W2 W3 W5 grid hc R
  }
  const rebases = d(`snLeaf_3_0`) + d(`snLeaf_10_0`) + REBASE_ICS.reduce((s, i) => s + d(`snIc_${i}`), 0);
  const copies = COPY_BINS.map((_, i) => d(`snLotCopyUs_${i}`));
  if (v48) out(`  rebases ${rebases} (sim lot graph builds ${d('snLeaf_3_0')}, lot removes ${d('snLeaf_10_0')}, rebase ICs ${REBASE_ICS.reduce((s, i) => s + d(`snIc_${i}`), 0)}); ` +
    `timed lot copies ${d('snLotCopyN')} by ms ${COPY_BINS.map((b, i) => `${b}:${copies[i]}`).join(' ')}; max ${a('snLotCopyMaxUs')} us, ${a('snLotCopyMaxKB')} KB; faults ${d('snLotCopyFault')}`);

  // G1 for design B (docs/94 §11.5); PASS only when all pass
  const gates: Gate[] = [];
  const need = (n: string, value: string) => gates.push({ name: n, value: `${value} (needs the P1b counters, v48)`, verdict: 'OPEN' });
  const pct = (x: number) => simRows ? (100 * x / simRows) : 0;
  const g1ok = pct(ua) <= 1 && pct(topCaller) < 0.5 && pct(uaOther) <= 1;
  const g1v = `${ua} = ${pct(ua).toFixed(2)}% of ${simRows} sim writer rows; top caller ${pct(topCaller).toFixed(2)}%, other bucket ${pct(uaOther).toFixed(2)}%`;
  if (v48) gates.push({ name: '1 unattributed', value: g1v, verdict: g1ok ? 'PASS' : 'FAIL' });
  else need('1 unattributed', g1v);
  const mean = records / secs;
  const worst = pctBin(secHist, 1), p99 = pctBin(secHist, 0.99);
  if (v48) gates.push({ name: '2 records', value: `${mean.toFixed(0)}/s mean (<= 1000); worst 1-s bucket ${worst < 0 ? '-' : SEC_BINS[worst]}, p99 ${p99 < 0 ? '-' : SEC_BINS[p99]} (<= 5000)`,
    verdict: worst < 0 ? 'OPEN' : mean <= 1000 && SEC_TOP[worst] <= 5000 ? 'PASS' : 'FAIL' });
  else need('2 records', `~${mean.toFixed(0)}/s mean from the leaf rows`);
  const costUs = (d('snRecUs') + d('snCloneUs')) / secs;
  if (v48) gates.push({ name: '3 record cost', value: `${(costUs / 1000).toFixed(3)} ms/s (bookkeeping + clone-equivalent copies; <= 1)`, verdict: costUs <= 1000 ? 'PASS' : 'FAIL' });
  else need('3 record cost', '-');
  const rb = rebases / secs, cp95 = pctBin(copies, 0.95);
  const rbv = `${rb.toFixed(2)}/s (<= 1); lot copy p95 ${cp95 < 0 ? '-' : COPY_BINS[cp95] + ' ms'} (<= 2 ms)`;
  if (v48) gates.push({ name: '4 rebases', value: rbv, verdict: rb > 1 || (cp95 >= 0 && COPY_TOP[cp95] > 2000) ? 'FAIL' : cp95 < 0 ? 'OPEN' : 'PASS' });
  else need('4 rebases', rbv);
  gates.push({ name: '5 off-sim writers', value: offSim ? offRows.join(', ') : 'none outside the sim and the route worker', verdict: offSim ? 'FAIL' : 'PASS' });
  const setup = (ready & 7) === 7 && a('snSkipped') === 0 && a('snIcSlots') === 39 && a('snFrameFull') === 0;
  gates.push({ name: '6 probe complete', value: setup ? 'all sites, slots and frames' : 'see SKIP routesnap lines in the log', verdict: setup ? 'PASS' : 'FAIL' });
  const g1 = gates.some(g => g.verdict === 'FAIL') ? 'FAIL' : gates.some(g => g.verdict === 'OPEN') ? 'OPEN (explain or add each source, then re-run)' : 'PASS';
  out(`\nG1: ${g1}`);
  for (const g of gates) out(`  ${g.verdict.padEnd(4)} ${g.name}: ${g.value}`);
  return { lines, gates, g1 };
}

const hex = (x: number) => '0x' + (x >>> 0).toString(16).toUpperCase().padStart(8, '0');

function namerFromFiles(): (a: number) => string {
  const fns: [number, string][] = [];
  for (const l of readFileSync(join(ROOT, 'tools', 'ghidra', 'symbols', 'ts3w-1.67.symbols.csv'), 'utf8').split(/\r?\n/).slice(1)) {
    const c = l.split(',');
    if (c[0]?.startsWith('0x') && (c[2] === 'function' || c[2] === 'icall')) fns.push([parseInt(c[0], 16), c[1]]);
  }
  const csv = join(ROOT, 'game-copy', 'analysis', 'functions.csv');
  if (existsSync(csv)) for (const l of readFileSync(csv, 'utf8').split(/\r?\n/).slice(1)) { const c = l.split(','); if (c[0]) fns.push([parseInt(c[0], 16), c[2]]); }
  fns.sort((x, y) => x[0] - y[0]);
  return (a: number) => {
    let lo = 0, hi = fns.length - 1, at = -1;
    while (lo <= hi) { const m = (lo + hi) >> 1; if (fns[m][0] <= a) { at = m; lo = m + 1; } else hi = m - 1; }
    return at < 0 ? '?' : fns[at][1] + (a === fns[at][0] ? '' : `+0x${(a - fns[at][0]).toString(16)}`);
  };
}

function selfCheck() {
  assert.equal(pctBin([0, 0, 0], 0.95), -1);
  assert.equal(pctBin([90, 5, 5], 0.95), 1);
  assert.equal(pctBin([10, 0, 0, 0, 0, 0, 0, 1], 0.95), 7);
  assert.equal(pctBin([10, 3, 0, 1, 0], 1), 3);  // the worst non-empty bin
  const before: Counters = { snLeaf_6_0: 100 };
  const after: Counters = { version: 46, snReady: 15, snHooks: 25, snShared: 7, snIcSlots: 39, snLeaf_6_0: 100 + 6000, snLeaf_11_1: 300,
    snSrc_6_1: 4000, snSrc_6_3: 2000, snStarts: 100, snStartLots_0: 50, snStartLots_1: 40, snStartLots_3: 10, snStartUnknown: 80,
    snIc_35: 100, snIcWrites_4: 3, snIc_4: 1, snWev_7: 1, snWev_59: 20, snObj_7_0: 600, snSeqGBuild: 1, snSeqEv7: 2, snGSite_3: 1 };
  // a v46 (P1) run: the design B checks that need P1b counters stay OPEN; the leaf-row record estimate is printed
  let r = report(before, after, 10);
  assert.match(r.g1, /^OPEN/);
  assert.match(r.lines.join('\n'), /0x8321F0 UpdateObjectFootprintCore\s+600 \| 600 \/ 0\.0 \/ 0\.0 \| 0\.0 \/ 400 \/ 0\.0 \/ 200 \/ 0\.0\r?$/m);
  assert.match(r.lines.join('\n'), /G build before event 7/);
  assert.match(r.lines.join('\n'), /dirty lots per START p95 3 \(80% of STARTs/);
  assert.equal(r.gates.filter(g => g.verdict === 'OPEN').length, 4);
  r = report({}, { ...after, snUnattributed: 4, snUaKey_0: 0x835300, snUaN_0: 4 }, 10, a => a === 0x835300 ? 'RoutingWorld_OnMessage' : '?');
  assert.match(r.lines.join('\n'), /0x00835300 RoutingWorld_OnMessage: 4/);
  // v48 (P1b): all six pass
  const p1b: Counters = { ...after, version: 48, snRec_0: 600, snRec_1: 300, snRecSec_1: 590, snRecSec_2: 10, snRecUs: 1000,
    snCloneUs: 2000, snCloneN: 600, snCloneBytesK: 300, snLeaf_3_0: 60, snLotCopyN: 60, snLotCopyUs_2: 60, snSrc2_6_0: 30,
    snCacheFill: 50, snFrameCalls_0: 12 };
  r = report(before, p1b, 10);
  assert.equal(r.g1, 'PASS', r.gates.map(g => `${g.verdict} ${g.name}: ${g.value}`).join('\n'));
  assert.match(r.gates.find(g => g.name === '2 records')!.value, /^2\/s mean .*worst 1-s bucket 251-500/);
  assert.match(r.gates.find(g => g.name === '3 record cost')!.value, /^0\.005 ms\/s/);
  assert.match(r.gates.find(g => g.name === '4 rebases')!.value, /^0\.10\/s .*p95 <=0\.5 ms/);
  assert.match(r.lines.join('\n'), /600 \| 600 \/ 0\.0 \/ 0\.0 \| 0\.0 \/ 400 \/ 0\.0 \/ 200 \/ 0\.0 \| 3\.0 \/ 0\.0/);
  // unattributed: 0.67% of sim rows in total passes, but one caller at 0.67% fails; the 64-key table names leaf, caller, next frame
  r = report(before, { ...p1b, snUnattributed: 40, snUa2Key_0: (6 << 24 | 0x835300), snUa2Next_0: 0x7654321, snUa2N_0: 40 }, 10,
    a => a === 0x835300 ? 'RoutingWorld_OnMessage' : '?');
  assert.equal(r.gates.find(g => g.name === '1 unattributed')!.verdict, 'FAIL');
  assert.match(r.lines.join('\n'), /0x8321F0 at 0x00835300 RoutingWorld_OnMessage <- 0x07654321 \?: 40/);
  r = report(before, { ...p1b, snUnattributed: 20, snUa2Key_0: (6 << 24 | 0x835300), snUa2N_0: 20 }, 10);
  assert.equal(r.gates.find(g => g.name === '1 unattributed')!.verdict, 'PASS');
  // a 5001-10000 second, a slow lot copy, too many rebases, an "other" thread writer, a skipped site: FAIL each
  r = report(before, { ...p1b, snRecSec_6: 1, snLotCopyUs_5: 60, snIc_22: 1000, snLeaf_13_2: 2, snSkipped: 1 }, 10);
  for (const n of ['2 records', '4 rebases', '5 off-sim writers', '6 probe complete']) assert.equal(r.gates.find(g => g.name === n)!.verdict, 'FAIL', n);
  assert.equal(r.g1, 'FAIL');
  // blend reload on main is known: not an off-sim writer; no lot copy timed yet: rebases OPEN
  r = report(before, { ...p1b, snLeaf_18_2: 3, snLotCopyUs_2: 0 }, 10);
  assert.equal(r.gates.find(g => g.name === '5 off-sim writers')!.verdict, 'PASS');
  assert.equal(r.gates.find(g => g.name === '4 rebases')!.verdict, 'OPEN');
  console.log('snap-counters-report self-check ok');
}

if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) {
  const args = process.argv.slice(2);
  if (args.includes('--self-check')) selfCheck();
  else if (!args.length || args[0].startsWith('--')) {
    console.log('usage: node tools/harness/snap-counters-report.ts <...-native.json> [--minutes 10] | --self-check');
    process.exitCode = 2;
  } else {
    const { before, after } = JSON.parse(readFileSync(args[0], 'utf8')) as { before: Counters; after: Counters };
    if ((after?.version ?? 0) < 46) throw new Error(`counters v${after?.version} < 46: not a [RouteSnap] build`);
    const mi = args.indexOf('--minutes');
    const minutes = mi >= 0 ? Number(args[mi + 1]) : 10;
    console.log(report(before ?? {}, after, minutes, namerFromFiles()).lines.join('\n'));
  }
}
