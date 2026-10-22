// [RouteJobPhases] report (docs/150 §7, counters v87): where the route worker's job time goes, ms/s per phase and job kind.
//   node tools/harness/route-phases-report.ts probe-out/queue/route-phases-<date>-<job>-native.json [...] [--seconds 300]
//   node tools/harness/route-phases-report.ts --self-check
//   node tools/harness/route-phases-report.ts --sync <ts3native.log> [...] [--min 300]   (docs/164 SyncPlans: per-plan splits)
// Counters v92 (docs/150 §14) add the allocator call-site table: printed after the phases when present (function names from
// game-copy/analysis/functions.csv when that file exists, --functions to point elsewhere).
// Reads the queue's native dump ({before, after}) and uses after - before over --seconds (the profile length, default 300).
// Pass (verify job): rjpReady 1, phases (self times, rest of the job included) sum to within 10% of asyncUs, every job kind
// that ran has entries > 0 in at least one timed phase.
import { existsSync, readFileSync } from 'node:fs';
import { parseArgs } from 'node:util';

type Counters = Record<string, number>;
// order = RJP_* in native/ts3native/routephases.cpp
export const PHASES = ['plan (own code)', 'cell A*', 'post pass', 'PathNodes_Insert', 'world A*', 'lot legs', 'portal A*',
  'candidate loops', 'LotGraph_Build', 'pending applies', 'water', 'allocator', 'Lock_Acquire', 'host-call wait', 'rest of job'];
const KINDS = ['own START', 'own A10', 'foreign'];

export function report(b: Counters, a: Counters, seconds: number) {
  const d = (k: string) => (a[k] ?? 0) - (b[k] ?? 0);
  const lines: string[] = [];
  const ready = a.rjpReady ?? 0, asyncUs = d('asyncUs');
  lines.push(`rjpReady ${ready}, sites ${a.rjpHooks ?? 0} timed, ${a.rjpSkipped ?? 0} skipped, frames dropped ${d('rjpDropped')}, ` +
    `asyncUs ${(asyncUs / 1000 / seconds).toFixed(1)} ms/s, ${seconds} s`);
  let total = 0, kindsOk = true;
  for (let k = 0; k < KINDS.length; k++) {
    const jobs = d(`rjpJobs_${k}`);
    if (!jobs) continue;
    const us = PHASES.map((_, p) => d(`rjpUs_${k}_${p}`)), n = PHASES.map((_, p) => d(`rjpN_${k}_${p}`));
    const sum = us.reduce((s, x) => s + x, 0);
    total += sum;
    if (!n.slice(0, PHASES.length - 1).some(x => x > 0)) kindsOk = false;
    lines.push(`  ${KINDS[k]}: jobs ${jobs}, ${(d(`rjpJobUs_${k}`) / 1000 / seconds).toFixed(1)} ms/s, mean ${(d(`rjpJobUs_${k}`) / 1000 / jobs).toFixed(2)} ms`);
    PHASES.map((name, p) => ({ name, p }))
      .filter(x => n[x.p] || us[x.p])
      .sort((x, y) => us[y.p] - us[x.p])
      .forEach(({ name, p }) => lines.push(`    ${name.padEnd(17)} ${(us[p] / 1000 / seconds).toFixed(2).padStart(7)} ms/s ` +
        `${(sum ? (100 * us[p]) / sum : 0).toFixed(1).padStart(5)}%  n ${n[p]}  max ${((a[`rjpMaxUs_${p}`] ?? 0) / 1000).toFixed(1)} ms`));
  }
  const ratio = asyncUs > 0 ? total / asyncUs : 0;
  const ok = ready === 1 && total > 0 && Math.abs(ratio - 1) <= 0.1 && kindsOk;
  lines.push(`phases sum ${(total / 1000 / seconds).toFixed(1)} ms/s = ${(100 * ratio).toFixed(1)}% of asyncUs; ` +
    `verdict: ${ok ? 'PASS' : 'FAIL'}${ready !== 1 ? ' (switch off or no site timed)' : ''}${!kindsOk ? ' (a kind without phase entries)' : ''}`);
  return { ok, ratio, lines };
}

// containing function per address from functions.csv (entry,end,name,...; end inclusive): sorted entries, binary search
export function namer(csv: string | null) {
  if (!csv) return (_x: number) => '';
  const rows = csv.split(/\r?\n/).slice(1).map(l => l.split(',')).filter(r => r.length >= 3)
    .map(r => [parseInt(r[0], 16), parseInt(r[1], 16), r[2]] as [number, number, string]).sort((x, y) => x[0] - y[0]);
  return (x: number) => {
    let lo = 0, hi = rows.length - 1, hit = -1;
    while (lo <= hi) { const m = (lo + hi) >> 1; if (rows[m][0] <= x) { hit = m; lo = m + 1; } else hi = m - 1; }
    return hit >= 0 && x <= rows[hit][1] ? rows[hit][2] : '';
  };
}

// v92: allocator rows by self time (raUs = 100 ns units), size / grow / per-job histograms; share = of the timed allocator calls
export function sites(b: Counters, a: Counters, seconds: number, name: (x: number) => string, top = 20) {
  const d = (k: string) => (a[k] ?? 0) - (b[k] ?? 0);
  const hex = (x: number) => '0x' + (x >>> 0).toString(16).toUpperCase();
  const jobs = [0, 1, 2].reduce((s, k) => s + d(`rjpJobs_${k}`), 0);
  const rows = [...Array(64).keys()].map(r => ({ site: a[`raSite_${r}`] ?? 0, parent: a[`raParent_${r}`] ?? 0, n: d(`raN_${r}`),
    hns: d(`raUs_${r}`), bytes: d(`raBytes_${r}`) })).filter(r => r.n > 0).sort((x, y) => y.hns - x.hns);
  const total = rows.reduce((s, r) => s + r.hns, 0);
  const lines = [`allocator sites (v92): allocs ${d('raAllocN')}, frees ${d('raFreeN')}, not in the table ${d('raFull')}, ` +
    `${(total / 1e4 / seconds).toFixed(2)} ms/s over ${rows.length} rows, ${jobs} jobs`];
  for (const r of rows.slice(0, top)) {
    const site = r.site - 5;  // the call instruction (E8 rel32) before the return address
    lines.push(`  ${hex(site)} ${(name(site) || '?').padEnd(28).slice(0, 28)} from ${hex(r.parent)} ` +
      `${(name(r.parent - 5) || '').padEnd(24).slice(0, 24)} ${(r.hns / 1e4 / seconds).toFixed(2).padStart(6)} ms/s ` +
      `${(total ? (100 * r.hns) / total : 0).toFixed(1).padStart(5)}%  n ${r.n} (${jobs ? (r.n / jobs).toFixed(1) : '-'}/job)` +
      `${r.bytes ? `  mean ${Math.round(r.bytes / r.n)} B` : ''}`);
  }
  const two = rows.slice(0, 2).reduce((s, r) => s + r.hns, 0);
  lines.push(`  top 2 rows ${(total ? (100 * two) / total : 0).toFixed(1)}% of allocator time`);
  const hist = (k: string, len: number, label: (i: number) => string) =>
    [...Array(len).keys()].filter(i => d(`${k}_${i}`)).map(i => `${label(i)}:${d(`${k}_${i}`)}`).join(' ');
  lines.push(`  sizes ${hist('raSizeLog2', 16, i => i ? `${2 ** (i + 2)}` : '<8')}`);
  lines.push(`  PathNodes_Insert grows by new capacity ${hist('raInsCap', 16, i => `${2 ** i}`)}; ` +
    `us/10 ${hist('raInsUs', 16, i => `${2 ** i}`)}`);
  lines.push(`  jobs by allocator calls ${hist('raJobHist', 8, i => ['0', '1+', '16+', '64+', '256+', '1K+', '4K+', '16K+'][i])}`);
  return { rows, total, two, lines };
}

// v96 (docs/159 §5, hostcalls.cpp): host-call split by serve site, door repeats, DoorMemo. Gate (docs/159 §5 decision): H2 worth
// turning on when the repeat asks' wait >= 4 ms/s (BP) with 0 answer changes; H1 when queue in limiter sleep >= 3 ms/s (clean run).
// The probe pass: rjpReady 1, hc counters > 0, split sum within 10% of the host phase minus nested, 0 answer changes, 0 mismatches.
export const HC_SITES = ['lock pump +0x918', 'lock pump other', 'READY poll', 'WaitAsyncDone', 'WaitJobDone', 'RunPlan'];
export function hostSplit(b: Counters, a: Counters, seconds: number) {
  const d = (k: string) => (a[k] ?? 0) - (b[k] ?? 0);
  const ms = (us: number) => (us / 1000 / seconds).toFixed(2);
  const lines: string[] = [];
  const ready = a.hcReady ?? 0;
  const n = HC_SITES.map((_, s) => d(`hcN_${s}`)), q = HC_SITES.map((_, s) => d(`hcQueueUs_${s}`));
  const calls = n.reduce((s, x) => s + x, 0), queue = q.reduce((s, x) => s + x, 0);
  const serve = d('hcServeUs'), ret = d('hcReturnUs'), nested = d('hcNestedUs'), sleep = d('hcQueueSleepUs');
  const host = [0, 1, 2].reduce((s, k) => s + d(`rjpUs_${k}_13`), 0);  // RJP_HOST self time (nested plans are its children)
  const split = queue + serve + ret, ratio = host > 0 ? split / host : 0;
  lines.push(`host-call split (v96): hcReady ${ready} (1 stamps, 2 door stub, 4 limiter ring), dmMode ${a.dmMode ?? 0}, ` +
    `${calls} calls (${(calls / seconds).toFixed(1)}/s), other than door asks ${d('hcOtherCalls')}`);
  HC_SITES.forEach((name, s) => { if (n[s]) lines.push(`  ${name.padEnd(17)} n ${String(n[s]).padStart(7)}  queue ${ms(q[s]).padStart(6)} ms/s  mean ${(q[s] / n[s] / 1000).toFixed(2)} ms`); });
  lines.push(`  queue ${ms(queue)} ms/s (max ${((a.hcQueueMaxUs ?? 0) / 1000).toFixed(1)} ms), in limiter sleep ${ms(sleep)} ms/s ` +
    `(${queue ? ((100 * sleep) / queue).toFixed(0) : 0}%, ${d('hcSleepN')} limiter passes with a call pending), service ${ms(serve)}, ` +
    `return ${ms(ret)}, nested plans ${ms(nested)} ms/s`);
  lines.push(`  split sum ${ms(split)} ms/s = ${(100 * ratio).toFixed(1)}% of the host phase ${ms(host)} ms/s`);
  const asks = d('hcDoorQ'), rep = d('hcDoorRepeatJob'), repUs = d('hcDoorRepeatJobWaitUs'), diff = d('hcDoorRepeatDiff');
  const jobs = [0, 1, 2].reduce((s, k) => s + d(`rjpJobs_${k}`), 0);
  lines.push(`  door asks ${asks} (${jobs ? (asks / jobs).toFixed(1) : '-'}/job, wait ${ms(d('hcDoorWaitUs'))} ms/s); repeats in a job ` +
    `${rep} (${asks ? ((100 * rep) / asks).toFixed(1) : 0}%), their wait ${ms(repUs)} ms/s, answer changes ${diff}; ` +
    `same key from another job within 1 s ${d('hcDoorRepeatXJob')} (changes ${d('hcDoorRepeatXJobDiff')}); table full ${d('hcDoorFull')}`);
  const mis = d('dmMismatch');
  if (a.dmMode || d('dmHits') || d('dmMisses') || d('dmShadow'))
    lines.push(`  door memo: hits ${d('dmHits')}, misses ${d('dmMisses')}, fall-through not in a job ${d('dmFb_0')} / nested ${d('dmFb_1')} / ` +
      `full ${d('dmFb_2')} / clone ${d('dmFb_3')}; shadow compares ${d('dmShadow')}, mismatches ${mis}`);
  const gateH2 = repUs / 1000 / seconds >= 4 && diff === 0 && mis === 0;
  const gateH1 = sleep / 1000 / seconds >= 3;
  lines.push(`  docs/159 gate: H2 (door memo) ${gateH2 ? 'MET' : 'not met'} (repeat wait ${ms(repUs)} ms/s >= 4 on BP, changes ${diff}, ` +
    `mismatches ${mis}); H1 (serve in limiter) ${gateH1 ? 'met' : 'not met'} (sleep queue ${ms(sleep)} >= 3 ms/s, clean run only)`);
  const ok = (ready & 3) === 3 && calls > 0 && asks > 0 && diff === 0 && mis === 0 && Math.abs(ratio - 1) <= 0.1;
  lines.push(`  probe verdict: ${ok ? 'PASS' : 'FAIL'}${(ready & 3) !== 3 ? ' (stamps or door stub off)' : ''}${!calls || !asks ? ' (no counts)' : ''}` +
    `${diff || mis ? ' (answer changes: the key misses an input, keep DoorMemo off)' : ''}${Math.abs(ratio - 1) > 0.1 ? ' (split vs host phase off by > 10%)' : ''}`);
  return { ok, gateH2, gateH1, ratio, lines };
}

// docs/164 [RouteJobPhases] SyncPlans: the per-plan split lines of a ts3native log
//   route phases: sync plan N ms <succeeded|failed|no-plan>, entry E, route H, planlocked P; loops entry=n/ms ground=n/ms
//   prepass=n/ms othercaller=n/ms; ms plan=.. cell=.. ... other=..
export type SyncPlan = { ms: number; res: string; entry: string; route: string; planlocked: number;
  loops: Record<string, [number, number]>; phases: Record<string, number> };
export function parseSyncLines(log: string): SyncPlan[] {
  const out: SyncPlan[] = [];
  const re = /route phases: sync plan (\d+) ms (\S+), entry (.+?), route (\S+), planlocked (\d+); loops (.*?); ms(.*)$/;
  for (const line of log.split(/\r?\n/)) {
    const m = re.exec(line);
    if (!m) continue;
    const loops: Record<string, [number, number]> = {}, phases: Record<string, number> = {};
    for (const [, k, n, ms] of m[6].matchAll(/(\w+)=(\d+)\/(\d+)/g)) loops[k] = [+n, +ms];
    for (const [, k, v] of m[7].matchAll(/(\w+)=(\d+)/g)) phases[k] = +v;
    out.push({ ms: +m[1], res: m[2], entry: m[3], route: m[4], planlocked: +m[5], loops, phases });
  }
  return out;
}
// One row per plan >= min ms: the top phases, the loop split and whether SyncCapMs (world A* node clock) could have cut it:
// "cap: yes" only when world A* self time is >= half the plan (docs/164 §3: no other phase is capped)
export function syncReport(plans: SyncPlan[], min: number) {
  const lines: string[] = [];
  const big = plans.filter(p => p.ms >= min).sort((a, b) => b.ms - a.ms);
  const sum: Record<string, number> = {};
  for (const p of big) {
    for (const [k, v] of Object.entries(p.phases)) sum[k] = (sum[k] ?? 0) + v;
    const top = Object.entries(p.phases).filter(([, v]) => v > 0).sort((a, b) => b[1] - a[1]).slice(0, 4)
      .map(([k, v]) => `${k} ${v} (${Math.round((100 * v) / Math.max(1, p.ms))}%)`).join(', ');
    const l = p.loops, world = p.phases.world ?? 0;
    lines.push(`${String(p.ms).padStart(6)} ms ${p.res.padEnd(9)} ${p.entry} route ${p.route} x${p.planlocked}: ${top}; entry loop ` +
      `${l.entry?.[0] ?? 0} x ${l.entry?.[1] ?? 0} ms, open ground ${l.ground?.[0] ?? 0} x ${l.ground?.[1] ?? 0} ms; cap: ${world * 2 >= p.ms ? 'yes' : 'no'}`);
  }
  const total = big.reduce((s, p) => s + p.ms, 0);
  lines.unshift(`sync plans >= ${min} ms: ${big.length} of ${plans.length} lines, ${total} ms; by phase: ` +
    Object.entries(sum).filter(([, v]) => v > 0).sort((a, b) => b[1] - a[1])
      .map(([k, v]) => `${k} ${(total ? (100 * v) / total : 0).toFixed(1)}%`).join(', '));
  return { big, lines };
}

if (process.argv[1] && import.meta.filename === process.argv[1]) {
  const { values, positionals } = parseArgs({ allowPositionals: true, options: { 'self-check': { type: 'boolean' }, seconds: { type: 'string', default: '300' },
    functions: { type: 'string', default: 'game-copy/analysis/functions.csv' }, sync: { type: 'boolean' }, min: { type: 'string', default: '300' } } });
  if (values.sync && !values['self-check']) {
    if (!positionals.length) { console.error('usage: node tools/harness/route-phases-report.ts --sync <ts3native.log> [...] [--min 300]'); process.exit(2); }
    let any = false;
    for (const f of positionals) {
      const r = syncReport(parseSyncLines(readFileSync(f, 'utf8')), Number(values.min));
      any ||= r.big.length > 0;
      console.log(`== ${f}\n${r.lines.join('\n')}`);
    }
    process.exitCode = any ? 0 : 1;  // queue pass rule: split lines present
  } else if (values['self-check']) {
    const log = 'x\n[t] route phases: sync plan 7361 ms failed, entry IC Plan/NoStart/nojob, route 1234ABCD, planlocked 3; loops entry=5/640 ' +
      'ground=0/0 prepass=5/20 othercaller=0/0; ms plan=40 cell=4100 post=100 insert=20 world=700 lotleg=300 portal=12 cand=50 lotgraph=0 ' +
      'pending=0 water=0 alloc=900 lock=0 other=1139\n[t] route phases: sync plan 120 ms succeeded, entry Route_PlanLocked (no IC), ' +
      'route 00000000, planlocked 1; loops entry=0/0 ground=1/90 prepass=0/0 othercaller=0/0; ms plan=1 cell=0 post=0 insert=0 world=100 ' +
      'lotleg=0 portal=0 cand=0 lotgraph=0 pending=0 water=0 alloc=0 lock=0 other=19\n';
    const ps = parseSyncLines(log), sr = syncReport(ps, 300);
    if (ps.length !== 2 || ps[0].loops.entry[0] !== 5 || ps[0].phases.cell !== 4100 || ps[1].entry !== 'Route_PlanLocked (no IC)' ||
        sr.big.length !== 1 || !sr.lines[1].includes('cell 4100 (56%)') || !sr.lines[1].includes('entry loop 5 x 640 ms') ||
        !sr.lines[1].includes('cap: no') || syncReport(ps, 100).lines[2]?.includes('cap: yes') !== true)
      throw new Error('route-phases-report sync self-check failed\n' + sr.lines.join('\n'));
    const b: Counters = { asyncUs: 1000 };
    const a: Counters = { version: 87, rjpReady: 1, rjpHooks: 15, asyncUs: 30_001_000, rjpJobs_0: 1000, rjpJobUs_0: 20_000_000,
      rjpUs_0_1: 8_000_000, rjpN_0_1: 3000, rjpUs_0_11: 6_000_000, rjpN_0_11: 90000, rjpUs_0_14: 6_000_000, rjpN_0_14: 1000,
      rjpJobs_2: 10, rjpJobUs_2: 10_000_000, rjpUs_2_4: 9_000_000, rjpN_2_4: 40, rjpUs_2_14: 1_000_000, rjpN_2_14: 10 };
    const r = report(b, a, 300);
    if (!r.ok || Math.abs(r.ratio - 1) > 1e-9 || !r.lines.some(l => l.includes('cell A*') && l.includes('26.67 ms/s') && l.includes('40.0%')))
      throw new Error('route-phases-report self-check failed\n' + r.lines.join('\n'));
    a.rjpUs_0_14 = 1_000_000;  // 5 s missing: 83% of asyncUs
    if (report(b, a, 300).ok) throw new Error('route-phases-report self-check: a 17% gap must fail');
    const fn = namer('entry,end,name\n0x629140,0x629276,PathNodes_Insert\n0x64e380,0x64e63f,PostPass\n');
    if (fn(0x6291de) !== 'PathNodes_Insert' || fn(0x629277) !== '' || fn(0x500000) !== '') throw new Error('namer self-check failed');
    const s = sites({ raN_3: 10, raUs_3: 1000 }, { rjpJobs_0: 100, raAllocN: 500, raSite_3: 0x6291e3, raParent_3: 0x64e400, raN_3: 410,
      raUs_3: 3_001_000, raBytes_3: 26240, raSite_9: 0x64cad9, raN_9: 100, raUs_9: 1_000_000, raInsCap_2: 7 }, 100, fn);
    if (s.rows[0].n !== 400 || s.total !== 4_000_000 || !s.lines.some(l => l.includes('0x6291DE PathNodes_Insert') &&
        l.includes('3.00 ms/s') && l.includes('75.0%') && l.includes('4.0/job')) || !s.lines.some(l => l.includes('4:7')))
      throw new Error('route-phases-report sites self-check failed\n' + s.lines.join('\n'));
    // v96 host split: 100 s, host phase 10 s; queue 6 s (sleep 3 s), service 3 s, return 1 s; repeats' wait 0.5 s = 5 ms/s
    const hb: Counters = {}, ha: Counters = { hcReady: 7, dmMode: 2, rjpJobs_0: 100, rjpUs_0_13: 10_000_000, hcN_0: 400, hcQueueUs_0: 2_000_000,
      hcN_2: 600, hcQueueUs_2: 4_000_000, hcServeUs: 3_000_000, hcReturnUs: 1_000_000, hcQueueSleepUs: 3_000_000, hcDoorQ: 1000,
      hcDoorRepeatJob: 300, hcDoorRepeatJobWaitUs: 500_000, hcDoorWaitUs: 9_000_000, dmShadow: 300 };
    const h = hostSplit(hb, ha, 100);
    if (!h.ok || !h.gateH2 || !h.gateH1 || Math.abs(h.ratio - 1) > 1e-9 || !h.lines.some(l => l.includes('READY poll') && l.includes('40.00 ms/s')) ||
        !h.lines.some(l => l.includes('30.0%') && l.includes('5.00 ms/s')))
      throw new Error('route-phases-report host split self-check failed\n' + h.lines.join('\n'));
    ha.dmMismatch = 1;
    if (hostSplit(hb, ha, 100).ok || hostSplit(hb, ha, 100).gateH2) throw new Error('route-phases-report self-check: a mismatch must fail');
    console.log('route-phases-report self-check ok');
  } else {
    if (!positionals.length) { console.error('usage: node tools/harness/route-phases-report.ts <native.json> [...] [--seconds 300] | --self-check'); process.exit(2); }
    let ok = true;
    for (const f of positionals) {
      const j = JSON.parse(readFileSync(f, 'utf8'));
      const r = report(j.before ?? {}, j.after ?? j, Number(values.seconds));
      console.log(`== ${f}\n${r.lines.join('\n')}`);
      if ((j.after ?? j).version >= 92) {
        const name = namer(existsSync(values.functions!) ? readFileSync(values.functions!, 'utf8') : null);
        console.log(sites(j.before ?? {}, j.after ?? j, Number(values.seconds), name).lines.join('\n'));
      }
      if ((j.after ?? j).version >= 96 && (j.after ?? j).hcReady) {
        const h = hostSplit(j.before ?? {}, j.after ?? j, Number(values.seconds));
        console.log(h.lines.join('\n'));
        ok &&= h.ok;
      }
      ok &&= r.ok;
    }
    process.exitCode = ok ? 0 : 1;
  }
}
