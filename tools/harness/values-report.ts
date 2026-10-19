// Report of a [ProbeValues] run (docs/108): one line per docs item id with the measured value and the answer rule's verdict.
//   node tools/harness/values-report.ts probe-out/queue/<queue>-<date>-<job>-native.json [--log <...-ts3native.log>] [--world <file.world>]
//   node tools/harness/values-report.ts --self-check
// Reads the queue's native counter dump ({before, after}, ts3native counters v44, or v45 from 0.24.1 with the docs/112 fixes). Counts are after - before (the profile
// window) unless the line says "since start" (load-time work happens before the first read). Load values (tuning, origin,
// R, guards, sea) are the latest world's, from `after`. --log adds the per-world "values ..." lines (several worlds, G
// builds with caller and thread); --world checksums the world file's R with tools/rtdata-decode.ts for docs/99 F-2.
import { readFileSync } from 'node:fs';
import assert from 'node:assert/strict';
import { parse, loadFromPackage, rtChecksum } from '../rtdata-decode.ts';

type Counters = Record<string, number>;
type Line = { id: string; text: string; verdict: string };

const NO_VALUE = 0x7fc00000;
const f32 = (bits: number | undefined) => {
  if (bits === undefined || (bits >>> 0) === NO_VALUE) return NaN;
  const b = Buffer.alloc(4); b.writeUInt32LE(bits >>> 0); return b.readFloatLE(0);
};
const fmt = (x: number) => Number.isNaN(x) ? 'n/a' : Number(x.toPrecision(6)).toString();
const hex = (x: number | undefined) => '0x' + ((x ?? 0) >>> 0).toString(16).toUpperCase().padStart(8, '0');
const COST = ['keep', 'trespass', 'sidewalk', 'riding', 'street', 'water-lot'];
// docs/99 §2.1: 0x673C70 defaults and the Sims3.ini [Routing: World Routing Tuning] values (null = not in the ini)
export const DEFAULTS = { threshold: 50, short: [1.3, 2, 2, 2, 2, 2], long: [1.3, 2, 4, 10, 10, 10] };
export const INI = { threshold: 80, short: [1.3, 1.3, 1.5, null, 3.0, null], long: [1.3, 1.3, 1.7, null, 6.0, null] };
const ASTAR_BITS = [0x1, 0x2, 0x4, 0x8, 0x10, 0x20, 0x40, 0x80, 0x100, 0x200];
const LOT_BITS = [0x1, 0x2, 0x4, 0x8, 0x10, 0x20, 0x100, 0x80000, 0x100000, 0x200000, 0x2000000];
const FAMILY = ['no PlanCommon', 'Plan/Replan', 'point ICs'];
const PATH = ['other', 'goal step', 'lot search', 'lot A*', 'in-room'];
const LAZY = ['LotTable_Insert', 'LotGraph_Build', 'RoomGraph_FirstUse', 'ApplyLotUpdate', 'FlushDeferred (work)', 'EnsureLevelPolygons', 'mark 0x670510', 'mark 0x670750'];
const NATIVE_STATUS = new Set([1, 2, 3, 4, 10, 16, 18]); // docs/101 §4.2 (3 comes from C#; the native set plus 3)
const close = (a: number, b: number) => Math.abs(a - b) <= 1e-4 * Math.max(1, Math.abs(b));

export function report(before: Counters, after: Counters, opts: { world?: { a: number; b: number; wps: number } } = {}) {
  const d = (k: string) => (after[k] ?? 0) - (before[k] ?? 0);
  const a = (k: string) => after[k] ?? 0;
  const out: Line[] = [];
  const add = (id: string, text: string, verdict: string) => out.push({ id, text, verdict });
  const sum3 = (name: string, inner?: number) => [0, 1, 2].reduce((s, fam) => s + (inner === undefined ? d(`${name}_${fam}`) : d(`${name}_${fam}_${inner}`)), 0);

  const ready = a('pvReady');
  add('probe', `pvReady ${ready}, ${a('pvHooks')} sites hooked, ${a('pvSkipped')} skipped, ${a('pvWorlds')} world snapshots, frames not taken ${a('pvFrameFull')}`,
    ready === 3 && a('pvSkipped') === 0 && a('pvWorlds') >= 1 && a('pvFrameFull') === 0 ? 'ok' : 'CHECK (see the SKIP lines in the log)');

  // docs/99 F-1: tuning
  const t = [...Array(13).keys()].map(i => f32(after[`pvTune_${i}`]));
  const short = t.slice(1, 7), long = t.slice(7, 13);
  const isDef = close(t[0], DEFAULTS.threshold) && short.every((x, i) => close(x, DEFAULTS.short[i])) && long.every((x, i) => close(x, DEFAULTS.long[i]));
  const iniOk = close(t[0], INI.threshold) && [...INI.short.map((v, i) => [v, short[i]]), ...INI.long.map((v, i) => [v, long[i]])].every(([v, x]) => v === null || close(x as number, v as number));
  add('F-1', `threshold ${fmt(t[0])}; SHORT ${short.map((x, i) => `${COST[i]} ${fmt(x)}`).join(', ')}; LONG ${long.map((x, i) => `${COST[i]} ${fmt(x)}`).join(', ')}`,
    t.some(Number.isNaN) ? 'no value (no world snapshot)' : isDef ? 'defaults of 0x673C70 (the ini does NOT replace them)' : iniOk ? 'Sims3.ini values replace the defaults' : 'neither defaults nor the ini values: read the log line');

  // docs/99 F-2, docs/91 §6.6: R
  const ra = a('pvR_0') >>> 0, rb = a('pvR_1') >>> 0;
  let v2 = 'compare: node tools/rtdata-decode.ts <world file> --checksum';
  if (opts.world) v2 = opts.world.a === ra ? 'memory R = decoder records (blocked bit and surface byte masked)'
    : opts.world.b === rb ? 'type bits differ only (shoreline rewrite, docs/100 §3.2 step 8); rest equal' : `DIFFERS from the file (${hex(opts.world.a)} / ${hex(opts.world.b)}, ${opts.world.wps} waypoints)`;
  add('F-2', `R sum ${hex(ra)} / ${hex(rb)}, ${a('pvR_2')} waypoints, ${a('pvR_3')} blocked now`, v2);
  const v45 = a('version') >= 45;
  const base = v45 ? `the baseline (retaken after the G build ${a('pvRRebase')}x)` : 'the snapshot (0.24.0: before the load G build, docs/112 §3.1)';
  add('91 §6.6', `R checksum changes after ${base}: ${a('pvR_4')} (since start)`, a('pvR_4') === 0 ? 'R fixed apart from bit 7'
    : v45 ? 'R CHANGED after the G build (log "values R changed")' : 'changed, but 0.24.0 counts the load G build type rewrite too');
  if (v45) add('R blocked', `blocked-bit hash ${hex(a('pvRBlk_0'))} at the baseline, ${hex(a('pvRBlk_1'))} now, ${a('pvRBlk_2')} checks saw it change; RestoreLotBlockedBits ${d('pvRestore_0')} (in plan ${d('pvRestore_1')}, off sim ${d('pvRestore_2')}), ApplyLotUpdate ${d('pvLazy_3_0')}`,
    a('pvRBlk_2') ? 'blocked bits change in play (log "values R writer" names the lots)' : 'blocked bits unchanged after the baseline');

  // docs/99 F-7
  const jobs = d('pvJobs'), ties = d('pvTies');
  add('F-7', `RNG goal ties ${ties} in ${jobs} plans (${jobs ? ((100 * ties) / jobs).toFixed(2) : 0}%)`, ties ? 'ties happen: shadow compare must accept the tie set' : 'no tie in this window');

  // docs/99 F-11
  const low = [0, 1, 2, 3].map(b => sum3('pvAStarBit', b)), astar = sum3('pvAStar');
  add('F-11', `world A* ${astar}: bits 1/2/4/8 = ${low.join('/')}; +0x14 non-zero ${sum3('pvAStarRef14')}, +0x44 non-zero ${sum3('pvAStarRef44')}; other bits ` +
    ASTAR_BITS.slice(4).map((m, i) => `0x${m.toString(16)} ${sum3('pvAStarBit', i + 4)}`).join(', '),
    !astar ? 'no A* call' : low.every(x => x === 0) ? 'bits 1-8 never set: substitutions and factor-30 bypasses dead (as inferred)' : 'bits 1-8 SET: those branches are live');

  // docs/99 F-12
  const ox = f32(after.pvWm_0), oz = f32(after.pvWm_1);
  add('F-12', `world origin ${fmt(ox)}, ${fmt(oz)}; max ${fmt(f32(after.pvWm_2))}, ${fmt(f32(after.pvWm_3))}`,
    Number.isNaN(ox) ? 'no value' : ox === 0 && oz === 0 ? 'origin 0: the cost functions\' frame agrees' : 'origin NOT 0: cost functions mix frames (docs/99 §10.1 risk is real)');

  // docs/98 U6
  for (let fam = 0; fam < 3; fam++) {
    const n = d(`pvLotPlan_${fam}`);
    if (!n) continue;
    add('98 U6', `LotPlan_Inner ${FAMILY[fam]}: ${n} calls; ` + LOT_BITS.map((m, i) => `0x${m.toString(16)} ${d(`pvLotPlanBit_${fam}_${i}`)}`).join(', ') +
      `; obstacle mode ${d(`pvLotPlanObst_${fam}`)}`, 'print');
  }

  // docs/100 L1-L4, L6, L8, L10, docs/91 §6.4
  const gThr = [0, 1, 2].map(i => a(`pvGBuildThr_${i}`)), gb = a('pvGBuild_0') + a('pvGBuild_1');
  add('L1', `G builds since start ${gb} (message path ${a('pvGBuild_0')}, rebuild ${a('pvGBuild_1')}); thread main/sim/other ${gThr.join('/')}; ` +
    `${gb ? (a('pvGBuildUs') / gb / 1000).toFixed(1) : 0} ms avg, ${(a('pvGBuildMaxUs') / 1000).toFixed(1)} ms max; G built ${a('pvG_0')} cells ${a('pvG_1')} water ${a('pvG_2')} bodies ${a('pvG_3')}`,
    gb >= 1 && gThr[1] >= 1 ? 'built at load on the sim thread (docs/112 §5.4)' : gb ? 'built, but not on the sim thread' : 'NO BUILD seen');
  add('L2', `WM+0x1B4 ${a('pvWmDirty')}, world+0x983 ${a('pvWorld983')}, PostWaterGridDirty ${d('pvDirtyPosts')} (since start ${a('pvDirtyPosts')})`, 'print');
  add('L3', `G builds in the window ${d('pvGBuild_0') + d('pvGBuild_1')}; 0x82A030 from 0x8352F1/0x835390/0x8358B5 ${[0, 1, 2].map(i => d(`pvGMsg_${i}`)).join('/')}`,
    d('pvGBuild_0') + d('pvGBuild_1') === 0 ? 'no rebuild in play' : 'rebuilds in play');
  add('L4', `high-cost stamps 0x6733C0 ${d('pvHighCost')}, mark 0x670750 ${d('pvLazy_7_0')}, mark 0x670510 ${d('pvLazy_6_0')}`, 'print');
  add('L6', `blockedNodeMap 0x66FB40 ${d('pvBlk_0')}, 0x66F6D0 ${d('pvBlk_1')}, size max ${a('pvG_4')}`, 'print');
  add('L8', `BuildWorldRoutingData from 0xC6BE53/0xC6CD02/0x835A27 (0x7FDA974) since start ${[0, 1, 2].map(i => a(`pvRebuild_${i}`)).join('/')}`, a('pvRebuild_2') ? 'message 0x7FDA974 sent' : 'message 0x7FDA974 not seen');
  add('L10', `sea height ${fmt(f32(after.pvSeaY))}, changes ${a('pvSeaChanges')}`, a('pvSeaChanges') ? 'sea height CHANGES after load' : 'fixed after load');

  // docs/101
  add('101 U2', `guards 0x11CF010 ${hex(after.pvGuard_0)}, 0x11CF02C ${hex(after.pvGuard_1)}, 0x11CF030 ${hex(after.pvGuard_2)}`, 'print (compare with lot graph +0xB8)');
  add('101 U4', `interest point allocs ${a('pvIpAlloc')}, GetNumInterestPoints ${a('pvIpCalls')} (non-zero ${a('pvIpNonZero')}) since start`,
    a('pvIpAlloc') === 0 && a('pvIpNonZero') === 0 ? 'lists always empty (I-101-6 holds)' : 'NON-EMPTY lists exist');
  const st: string[] = []; let bad = 0;
  for (let fam = 0; fam < 2; fam++) for (let s = 0; s < 20; s++) {
    const n = d(`pvStatus_${fam}_${s}`);
    if (!n) continue;
    st.push(`${['Plan/Replan', 'point'][fam]} ${s === 19 ? '19+' : s}: ${n}`);
    if (!NATIVE_STATUS.has(s)) bad += n;
  }
  add('101 U5', st.join(', ') || 'no plan', !v45 ? 'no answer: 0.24.0 read the status from argument 2 (docs/112 §2), rerun with 0.24.1' : bad ? `${bad} plans with a status outside {1,2,3,4,10,16,18}` : 'statuses stay in {1,2,3,4,10,16,18}');
  const ps: string[] = [];
  for (let p = 0; p < 5; p++) {
    const row = [0, 1, 2, 3, 4].map(s => d(`pvPortal_${p}_${s}`));
    if (row.some(Boolean)) ps.push(`${PATH[p]} [0..3,other] ${row.join('/')}`);
  }
  add('101 U6', ps.join('; ') || 'no portal asked', 'print');
  const jobsH9 = d('pvH9Plans');
  add('101 U7', `H9 rewrites remove ${d('pvH9_0')} insert ${d('pvH9_1')}, inside plans ${d('pvH9InPlan')}; plans with a rewrite ${jobsH9} of ${jobs}, max ${a('pvH9Max')} in one plan`, 'print');
  add('101 U8', `partition grid 0xC08C30 ${d('pvPart_0')} (off sim ${d('pvPartOffSim_0')}), 0xC094E0 ${d('pvPart_1')} (off sim ${d('pvPartOffSim_1')})`,
    d('pvPartOffSim_0') + d('pvPartOffSim_1') ? 'writes OFF the sim thread' : d('pvPart_0') + d('pvPart_1') ? 'sim thread only' : 'no write in live play');
  if (v45) add('91 §6.2b', `builds by thread sim/worker/other: RoomGraph_FirstUse ${[0, 1, 2].map(i => d(`pvRoomBuild_${i}`)).join('/')} of ${d('pvLazy_2_0')} entries; EnsureLevelPolygons ${[0, 1, 2].map(i => d(`pvLevelBuild_${i}`)).join('/')} of ${d('pvLazy_5_0')} entries`, 'print');
  add('91 §6.2', LAZY.map((n, i) => `${n} ${d(`pvLazy_${i}_0`)} (in plan ${d(`pvLazy_${i}_1`)}, off sim ${d(`pvLazy_${i}_2`)})`).join('; '), 'print');

  // docs/105
  add('I-105-1', `event 68 from Lot_SetDisplayLevel ${d('pvEv68')}, inside CanSee ${d('pvEv68InCanSee')}`, d('pvEv68InCanSee') === 0 ? 'no event 68 from goal building (quiet flag holds)' : 'event 68 INSIDE CanSee: quiet flag misread');
  const raiseIn = [0, 1, 2].map(i => d(`pvRaiseIn_${i}`));
  add('I-105-3', v45 ? `World_CanSeeObject from 0x75AC22 ${d('pvCanSee')}; display-level calls inside it by thread sim/worker/other ${raiseIn.join('/')} (all callers ${d('pvCanSeeRaise')})`
    : `World_CanSeeObject from 0x75AC22 ${d('pvCanSee')}, display-level calls from any CanSee caller ${d('pvCanSeeRaise')} (0.24.0 label, docs/112 §5.1)`,
    v45 ? (raiseIn[1] + raiseIn[2] ? 'raises OFF the sim thread inside goal building' : raiseIn[0] ? 'raises on the sim thread only' : 'no raise inside goal building') : 'print (option bit not read)');
  const q = [0, 1].map(il => [0, 1, 2, 3, 4].map(s => d(`pvQuery_${il}_${s}`)));
  add('I-105-4', `portal queries initialLocked 0 [0..3,other] ${q[0].join('/')}; initialLocked 1 ${q[1].join('/')}`,
    q[1].some(Boolean) ? (q[1][2] ? 'script asked with initialLocked 1 and answered 2 (unlock) at least once' : 'script asked with initialLocked 1') : 'no call with initialLocked 1 in this window');
  add('I-105-5', `event 59 inside plans ${d('pvQueryInPlan')} over ${jobs} plans (${jobs ? (d('pvQueryInPlan') / jobs).toFixed(2) : 0} per plan)`, 'print (estimate 2-30)');
  return out;
}

if (process.argv[1] && import.meta.filename === process.argv[1]) {
  const args = process.argv.slice(2);
  if (args[0] === '--self-check') {
    const tune = [80, 1.3, 1.3, 1.5, 2, 3, 2, 1.3, 1.3, 1.7, 10, 6, 10];
    const bits = (x: number) => { const b = Buffer.alloc(4); b.writeFloatLE(x); return b.readInt32LE(0); };
    const after: Counters = { version: 45, pvReady: 3, pvHooks: 43, pvWorlds: 1, pvJobs: 100, pvTies: 3, pvAStar_1: 50, pvAStarBit_1_4: 20, pvWm_0: 0, pvWm_1: 0,
      pvR_0: 0x8f578136 | 0, pvR_1: 0xf11fc59f | 0, pvR_2: 39265, pvStatus_0_1: 90, pvStatus_0_7: 1, pvGBuild_0: 1, pvGBuildThr_0: 1, pvGBuildUs: 120000,
      pvQuery_1_2: 4, pvQueryInPlan: 300, pvRaiseIn_0: 5, pvRaiseIn_1: 2, pvRoomBuild_1: 7, pvLazy_2_0: 900, pvRBlk_0: 1, pvRBlk_1: 2, pvRBlk_2: 1,
      pvRRebase: 1, pvRestore_0: 3 };
    tune.forEach((x, i) => { after[`pvTune_${i}`] = bits(x); });
    const L = report({ pvJobs: 0 }, after, { world: { a: 0x8f578136, b: 0xf11fc59f, wps: 39265 } });
    const v = (id: string) => L.find(l => l.id === id)!.verdict;
    assert.equal(v('probe'), 'ok');
    assert.match(v('F-1'), /Sims3.ini values replace/);
    assert.match(v('F-2'), /memory R = decoder/);
    assert.match(v('F-7'), /ties happen/);
    assert.match(v('F-11'), /never set/);
    assert.match(v('F-12'), /origin 0/);
    assert.match(v('L1'), /sim thread/);
    assert.match(v('101 U5'), /1 plans with a status outside/);
    assert.match(v('I-105-4'), /answered 2/);
    assert.match(v('I-105-3'), /OFF the sim thread/);
    assert.match(L.find(l => l.id === 'I-105-3')!.text, /sim\/worker\/other 5\/2\/0/);
    assert.match(L.find(l => l.id === '91 §6.2b')!.text, /RoomGraph_FirstUse 0\/7\/0 of 900/);
    assert.match(v('R blocked'), /change in play/);
    assert.match(L.find(l => l.id === '91 §6.6')!.text, /retaken after the G build 1x/);
    const old = report({}, { ...after, version: 44 });
    assert.match(old.find(l => l.id === '101 U5')!.verdict, /argument 2/);
    assert.ok(!old.some(l => l.id === 'R blocked' || l.id === '91 §6.2b'));
    tune.forEach((_, i) => { after[`pvTune_${i}`] = bits([50, 1.3, 2, 2, 2, 2, 2, 1.3, 2, 4, 10, 10, 10][i]); });
    after.pvAStarBit_0_1 = 2;
    after.pvAStar_0 = 2;
    const L2 = report({}, after, { world: { a: 1, b: 0xf11fc59f, wps: 39265 } });
    assert.match(L2.find(l => l.id === 'F-1')!.verdict, /defaults/);
    assert.match(L2.find(l => l.id === 'F-2')!.verdict, /type bits differ only/);
    assert.match(L2.find(l => l.id === 'F-11')!.verdict, /SET/);
    assert.equal(f32(NO_VALUE), NaN);
    console.log('values-report self-check ok');
  } else if (!args.length || args[0].startsWith('--')) {
    console.log('usage: node tools/harness/values-report.ts <...-native.json> [--log <...-ts3native.log>] [--world <file.world>] | --self-check');
    process.exitCode = 2;
  } else {
    const { before, after } = JSON.parse(readFileSync(args[0], 'utf8')) as { before: Counters; after: Counters };
    if ((after?.version ?? 0) < 44) throw new Error(`counters v${after?.version} < 44: not a [ProbeValues] build`);
    const wi = args.indexOf('--world');
    let world: { a: number; b: number; wps: number } | undefined;
    if (wi >= 0) { const r = parse(loadFromPackage(args[wi + 1])); const x = rtChecksum(r), y = rtChecksum(r, 0x60); world = { a: x.sum, b: y.sum, wps: x.wps }; }
    for (const l of report(before ?? {}, after, { world })) console.log(`${l.id.padEnd(8)} ${l.text}\n         -> ${l.verdict}`);
    const li = args.indexOf('--log');
    if (li >= 0) for (const l of readFileSync(args[li + 1], 'utf8').split(/\r?\n/)) if (/\bvalues\b|SKIP values|probe values/.test(l)) console.log(l);
  }
}
