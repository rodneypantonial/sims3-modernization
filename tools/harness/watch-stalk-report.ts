// Verdicts from a routing watch + Stalker file (tools/frida/watch-stalk.js, docs/113, docs/51 §53):
//   node tools/harness/watch-stalk-report.ts probe-out/profile/<tag>-watchstalk.json [more files]   one block per file
//   node tools/harness/watch-stalk-report.ts --worker <file>  U1 verdict on route worker plans (docs/113 §8; also chosen when
//                                                             the file says worker mode); no verdict for a sim-thread file
//   node tools/harness/watch-stalk-report.ts --self-check     the verdict rules on made-up files, no game
// Prints: U10 (route +0x1F4 readers: watch hits by site), U1 (.data/.tls addresses written inside followed Route_PlanLocked
// calls vs the docs/101 §2.2 list), U3 (0x66AB70 block coverage and the 0x66AC37 counter per followed plan).
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

type Hit = { pc: string; at: string; vtOk: boolean; n: number; sim: number; tids: Record<string, number>; stacks: string[][] };
type Written = { addr: string; n: number; pc: string; at: string; plans: number };
type Res = { reason: string; seconds: number; refused: Record<string, string>; errors: string[]; params?: { worker?: boolean };
  watch: { routes: number; nHits: number; stray: number; noDr: number; armErrors: number; unsetErrors: number; replans: Record<string, number>; dropped: Record<string, number>; hits: Hit[] };
  stalk: { worker?: boolean; planVia?: string | null; workerTid?: number; workerCalls?: number; tids?: Record<string, number>; plans: number; simCalls: number; stopped: string | null; forced: number; c37: number; c37plans: number; cov: Record<string, number>; tableFull: number; siteFull: number;
    segWrites: number; rows: { ms: number; cov: boolean; c37: number }[]; written: Written[] } };

// the instruction after each +0x1F4 writer (docs/101 §5): a data watchpoint reports the address after the access
const WRITER_PC: Record<number, string> = { 0x7654c1: 'IC_Route_Replan add at 0x7654BA', 0x765590: 'IC_Route_ReplanFromPosition add at 0x765589' };
// zeroing writers (docs/101 §5): route ctor and two resets; a hit within 0x600 bytes after their start is put here [inferred bound]
const ZEROING: [number, string][] = [[0x75d4b0, 'Route_ctor 0x75D4B0'], [0x760200, '0x760200'], [0x760370, '0x760370']];
// docs/101 §2.2 items as [item, start, end exclusive]; the census lists start addresses, the end adds the last element's size
const KNOWN: [string, number, number][] = [
  ['#1 A* stamp', 0x11580c4, 0x11580c6], ['#2 clip scratch', 0x1158580, 0x11585a0], ['#3 quadtree count', 0x11cf168, 0x11cf16c],
  ['#4 pool', 0x11581dc, 0x11581e0], ['#5 pool', 0x1165b80, 0x1165b84], ['#5 pool', 0x1165b8c, 0x1165b90], ['#6 pool', 0x1165d48, 0x1165d4c],
  ['#7 TLS index', 0x114c908, 0x114c90c], ['#8 cached fn', 0x11cb920, 0x11cb924], ['#9 startup flag', 0x11e96c0, 0x11e96c2],
  ['#10 config bit', 0x11eceee, 0x11ecef0], ['#11 reload flag', 0x1157d28, 0x1157d2c], ['#12 guard', 0x11cf010, 0x11cf014],
  ['#13 static', 0x11cf018, 0x11cf02c], ['#14 cell size', 0x11cf02c, 0x11cf034], ['#15 static', 0x11cf090, 0x11cf0a4],
  ['#16 vectors', 0x11cf0b0, 0x11cf114], ['#17 bit table', 0x11cf180, 0x11cf188], ['#17 done flag', 0x11cf428, 0x11cf42c],
  ['#18 sqrt const', 0x11cf460, 0x11cf468], ['#19 const', 0x11d88d0, 0x11d88d8], ['#20 const', 0x11eb4c0, 0x11eb4c8],
  ['#21 const', 0x11eb4c8, 0x11eb4d0], ['#22 table', 0x11d1390, 0x11d1398], ['#23 vectors', 0x11d8930, 0x11d8980],
  ['#24 tangents', 0x11d8fc0, 0x11d8fcc], ['#25 tangents', 0x11d8fcc, 0x11d8fd8], ['#26 tangents', 0x11d8fd8, 0x11d8fe0],
  // pool locks of #4-#6: Lock_Acquire/Release run unstalked, so a write here comes from inline code [size 0x30 assumed]
  // docs/113 §7.2: the shared empty hash-table bucket of every empty table; 0xD2BC10 clears it, storing 0 over 0 [verified static]
  ['zero-store sentinel 0x114C900', 0x114c900, 0x114c904],
  ['pool lock 0x11CEFA8', 0x11cefa8, 0x11cefd8], ['pool lock 0x11D8F90', 0x11d8f90, 0x11d8fc0], ['pool lock 0x11D9068', 0x11d9068, 0x11d9098],
];
const N = (h: string) => parseInt(h, 16);
export const knownItem = (a: number) => KNOWN.find(([, lo, hi]) => a >= lo && a < hi)?.[0] ?? null;
export function hitClass(h: Hit): string {
  const pc = N(h.pc);
  if (WRITER_PC[pc]) return 'writer';
  if (!h.vtOk) return 'freed';
  if (ZEROING.some(([s]) => pc >= s && pc < s + 0x600)) return 'zeroing';
  return 'candidate';
}

export function verdicts(r: Res, wantWorker = false): string[] {
  const out: string[] = [], w = r.watch, s = r.stalk, worker = !!(s.worker ?? r.params?.worker);
  // U10
  const by: Record<string, Hit[]> = {};
  for (const h of w.hits) (by[hitClass(h)] ??= []).push(h);
  const sum = (k: string) => (by[k] ?? []).reduce((a, h) => a + h.n, 0);
  if (!w.routes) out.push(`U10: no verdict (no route watched; replans ${JSON.stringify(w.replans)}${r.refused.replan ? ', replan hook refused' : ''})`);
  else if (!sum('writer') && !by.candidate) out.push(`U10: no verdict (${w.routes} routes watched, 0 writer hits: the watch never saw a known access, so a silent result proves nothing)`);
  else if (by.candidate) out.push(`U10: REFUTED (check each site first: the reported pc is the instruction after the access), ${by.candidate.length} unknown sites on live routes: ` +
    by.candidate.slice(0, 8).map(h => `${h.at} x${h.n}${h.sim ? ` (sim ${h.sim})` : ''}`).join(', '));
  else out.push(`U10: verified, ${w.routes} routes watched: only the IC writers (${sum('writer')} hits), zeroing ${sum('zeroing')}, after free/reuse ${sum('freed')}`);
  // stray = a trap from a slot already unset (the thread was in the handler during the unset; the handler clears it then)
  if (w.stray > 100 || w.noDr) out.push(`  WARN: ${w.stray} stray traps, ${w.noDr} without debug registers in the saved context (the handler could not clear them)`);
  if (w.armErrors || w.unsetErrors) out.push(`  WARN: ${w.armErrors} arm errors, ${w.unsetErrors} unset errors`);
  if (by.freed) out.push(`  after free/reuse (not readers of a live route): ${by.freed.slice(0, 6).map(h => `${h.at} x${h.n}`).join(', ')}`);
  if (w.dropped?.released) out.push(`  ${w.dropped.released} watches ended at the Route dtor (route released)`);
  // U1
  if (wantWorker && !worker) { out.push('U1 worker: no verdict (sim-thread file; run a *-watch-stalk-worker job)'); return out; }
  const who = worker ? `worker plans (tid ${s.workerTid || '?'}${s.planVia ? `, via ${s.planVia}` : ''})` : 'sim-thread plans (worker not followed)';
  const U1 = worker ? 'U1 worker' : 'U1';
  const others = Object.keys(s.tids ?? {}).filter(t => Number(t) !== s.workerTid);
  if (worker && others.length) out.push(`  WARN: other non-sim threads called Route_PlanLocked (${others.map(t => `${t} x${s.tids![t]}`).join(', ')}): check the worker tid`);
  const fresh = s.written.filter(x => !knownItem(N(x.addr)));
  const seen = [...new Set(s.written.map(x => knownItem(N(x.addr))).filter(Boolean))];
  if (!s.plans) out.push(`${U1}: no verdict (0 plans followed; ${worker ? `${s.workerCalls ?? 0} worker` : `${s.simCalls} sim-thread`} plans${r.refused.plan ? ', plan hook refused' : ''}${s.stopped ? `, stopped: ${s.stopped}` : ''})`);
  else if (fresh.length) out.push(`${U1}: REFUTED, ${fresh.length} written .data/.tls addresses outside docs/101 §2.2 in ${s.plans} plans: ` +
    fresh.slice(0, 12).map(x => `${x.addr} by ${x.at} (${x.plans} plans)`).join(', '));
  else out.push(`${U1}: verified on ${s.plans} ${who}: ${s.written.length} written addresses, all known (docs/101 §2.2, pool locks, docs/113 §7.2 sentinel: ${seen.join(', ') || 'none'})`);
  if (s.tableFull || s.siteFull || s.forced) out.push(`  WARN: table full ${s.tableFull}, site table full ${s.siteFull}, forced unfollows ${s.forced}`);
  if (s.plans) out.push(`  plans followed ${s.plans} of ${worker ? s.workerCalls : s.simCalls}; ms per followed plan max ${Math.max(...s.rows.map(x => x.ms))}, median ${s.rows.map(x => x.ms).sort((a, b) => a - b)[s.rows.length >> 1]}; fs/gs writes ${s.segWrites}`);
  // U3
  const covPlans = s.rows.filter(x => x.cov).length;
  if (!s.plans) out.push('U3: no verdict (0 plans followed)');
  else if (s.c37plans || covPlans) out.push(`U3: verified, 0x66AB70 runs inside plans: 0x66AC37 hit ${s.c37} times in ${s.c37plans} of ${s.plans} plans, blocks ${Object.keys(s.cov).length} (${covPlans} plans)`);
  else out.push(`U3: not seen in ${s.plans} followed plans (0x66AB70 never ran; the H13 route is rare or dead in play)`);
  return out;
}

function selfCheck() {
  assert.equal(knownItem(0x11cf02b), '#13 static');
  assert.equal(knownItem(0x11cf02c), '#14 cell size');
  assert.equal(knownItem(0x11580c6), null);
  assert.equal(knownItem(0x114c900), 'zero-store sentinel 0x114C900');
  assert.equal(knownItem(0x114c904), null);
  const hit = (pc: string, vtOk = true, n = 5): Hit => ({ pc, at: pc, vtOk, n, sim: n, tids: {}, stacks: [] });
  assert.equal(hitClass(hit('0x7654c1')), 'writer');
  assert.equal(hitClass(hit('0x75d500')), 'zeroing');
  assert.equal(hitClass(hit('0x4f0000', false)), 'freed');
  assert.equal(hitClass(hit('0x4f0000')), 'candidate');
  const base = (): Res => ({ reason: 'complete', seconds: 360, refused: {}, errors: [],
    watch: { routes: 12, nHits: 40, stray: 2, noDr: 0, armErrors: 0, unsetErrors: 0, replans: { replan: 300, replanPos: 20 }, dropped: {}, hits: [hit('0x7654c1', true, 30), hit('0x765590', true, 8), hit('0x5a0000', false, 2)] },
    stalk: { plans: 20, simCalls: 900, stopped: 'maxPlans 20', forced: 0, c37: 0, c37plans: 0, cov: {}, tableFull: 0, siteFull: 0, segWrites: 3,
      rows: Array.from({ length: 20 }, () => ({ ms: 200, cov: false, c37: 0 })),
      written: [{ addr: '0x11580c4', n: 20, pc: '0x61e360', at: 'TS3W.exe+0x21e360', plans: 20 }, { addr: '0x11cf168', n: 2, pc: '0x664650', at: 'x', plans: 2 }] } });
  let v = verdicts(base());
  assert.match(v[0], /^U10: verified, 12 routes/);
  assert.ok(v.some(l => l.startsWith('U1: verified on 20')));
  assert.ok(v.some(l => l.startsWith('U3: not seen in 20')));
  const r = base();
  r.watch.hits.push(hit('0x4f1234')); r.watch.nHits = 45; r.watch.stray = 500;
  r.stalk.written.push({ addr: '0x1200000', n: 1, pc: '0x500000', at: 'TS3W.exe+0x100000', plans: 1 });
  r.stalk.c37 = 4; r.stalk.c37plans = 2; r.stalk.rows[0].cov = true; r.stalk.cov = { '0x66ab70': 1 };
  v = verdicts(r);
  assert.match(v[0], /^U10: REFUTED.*0x4f1234 x5/);
  assert.ok(v.some(l => l.includes('WARN: 500 stray traps')) && !verdicts(base()).some(l => l.includes('stray')));
  assert.ok(v.some(l => /^U1: REFUTED, 1 written.*0x1200000/.test(l)));
  assert.ok(v.some(l => l.startsWith('U3: verified, 0x66AB70 runs inside plans: 0x66AC37 hit 4 times in 2 of 20')));
  const e = base();
  e.watch.hits = []; e.stalk.plans = 0; e.stalk.rows = [];
  v = verdicts(e);
  assert.match(v[0], /^U10: no verdict \(12 routes watched, 0 writer hits/);
  assert.ok(v.some(l => l.startsWith('U1: no verdict')) && v.some(l => l.startsWith('U3: no verdict')));
  // worker mode (docs/113 §8): verdict names the worker, warns on a second non-sim caller; --worker on a sim file gives none
  const k = base();
  Object.assign(k.stalk, { worker: true, workerTid: 77, workerCalls: 400, tids: { 77: 400 }, planVia: 'ts3native.asi detour' });
  k.stalk.written.push({ addr: '0x114c900', n: 300, pc: '0xd2bc42', at: 'TS3W.exe+0x92bc42', plans: 18 });
  v = verdicts(k, true);
  assert.ok(v.some(l => /^U1 worker: verified on 20 worker plans \(tid 77, via ts3native\.asi detour\)/.test(l)) && !v.some(l => l.includes('WARN: other')));
  assert.ok(v.some(l => l.startsWith('  plans followed 20 of 400;')));
  k.stalk.tids = { 77: 400, 12: 3 };
  assert.ok(verdicts(k).some(l => l.includes('WARN: other non-sim threads called Route_PlanLocked (12 x3)')));
  assert.ok(verdicts(base(), true).some(l => l.startsWith('U1 worker: no verdict (sim-thread file')));
  const rl = base(); rl.watch.dropped = { released: 3 };
  assert.ok(verdicts(rl).some(l => l.includes('3 watches ended at the Route dtor')));
  console.log('watch-stalk-report self-check: PASS');
}

if (process.argv.includes('--self-check')) selfCheck();
else {
  const wantWorker = process.argv.includes('--worker'), files = process.argv.slice(2).filter(a => a !== '--worker');
  if (!files.length) { console.error('usage: node tools/harness/watch-stalk-report.ts <tag>-watchstalk.json [...] | --self-check'); process.exit(2); }
  for (const f of files) {
    const r = JSON.parse(readFileSync(f, 'utf8')) as Res;
    console.log(`${f}: ${r.reason}, ${r.seconds} s; refused: ${Object.keys(r.refused).join(', ') || 'none'}; probe errors ${r.errors.length}${r.errors[0] ? ` (${r.errors[0]})` : ''}`);
    for (const l of verdicts(r, wantWorker)) console.log(l);
  }
}
