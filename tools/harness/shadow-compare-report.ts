// Report of a routing snapshot P4 shadow run (docs/94 §5 phase P4, §4.3 classes, §13 record): classifies every shadow
// compare into M0-M5, prints the gate G4 verdict per world, a per-IC class table and the first mismatches with diffs.
//   node tools/harness/shadow-compare-report.ts <shadow.jsonl> [...] [--first 10]
//   node tools/harness/shadow-compare-report.ts --self-check
//
// Record (one JSON line per compare, written by the native side at collect; spec = docs/94 §13, keep both in step):
//   { "v": 2, "plan": 1234, "ic": "Plan" | "Replan" | "PlanFromPoint" | "ReplanFromPosition", "world": "BP",
//     "search": "G" | "R", "epoch": 77, "ver": 76,
//     "start": [x, y, z], "goal": [x, y, z],        request: first start record, game's chosen goal record
//     "tie": [0, 3],                                 goal indices of the job's tie set (docs/99 §4.1), [] or absent = no tie
//     "refused": null | "option" | "kind" | "lot" | "epoch" | "<reason>",   set = M4, no copy answer
//     "stale": false,                                a dirty mark for a lot on the path arrived after the cut = M5
//     "nr": 0,                                       v2 (docs/94 §17.4 D5): bits of the non-routing writer families that
//                                                    changed in the window and the plan read (1 physics, 2 terrain swap,
//                                                    4 destroy, 8 lot index, 16 world events 0x0D-0x10, 32 blend, 64 lock
//                                                    keys); a mismatch (M1-M3) with nr != 0 is time skew = M5
//     "m2": 0,                                       v3 (docs/94 §22, ts3native 0.33.0): 1 = a P5 [RouteSnap] Mode=2 verify job:
//                                                    the clone ran first, its answer is what Mode=2 takes, the game's was kept
//     "gameUs": 812, "jobUs": 640,                   game plan time on the sim thread, our worker job time
//     "game": Answer, "copy": Answer }               copy is null when refused
//   Answer = { "mType": 1, "status": 2, "startIdx": 0, "goalIdx": 3,
//     "paths": [ { "type": "0xC6600B67", "obj": "0x0", "len": 12.5,       PathType hash, path object id, path +0x8C
//                  "o58": ["0x5A1F00000001"],                             path options +0x58 id list (docs/93 §4.4)
//                  "nodes": [[x, y, z, seg, flags, type], ...] } ] }      48-byte node xyz, +0x20, +0x2C; O2 node +0x1C (-1 unknown)
//   Ids are hex strings (u64). Route cost = sum of path len (route +0xA4 is that sum).
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

type Node = [number, number, number, number, number, number];
type Path = { type: string; obj: string; len: number; o58?: string[]; nodes: Node[] };
type Answer = { mType: number; status: number; startIdx: number; goalIdx: number; paths: Path[] };
export type Rec = {
  v: number; plan: number; ic: string; world: string; search: 'G' | 'R'; epoch?: number; ver?: number;
  start?: number[] | null; goal?: number[] | null; tie?: number[]; refused?: string | null; stale?: boolean; nr?: number; m2?: number;
  gameUs: number; jobUs: number; game: Answer; copy: Answer | null;
};
export type Cls = 'M0' | 'M1' | 'M2' | 'M3' | 'M4' | 'M5';
// M1 sub-kinds (docs/94 §13.1): 'goal' = the job's RNG goal draw picked another member of the tie set; 'path' = an equal-cost
// alternative route (same result, start, goal, subpaths, end point and route cost within float rounding; other node count or
// xz); 'none' = a real M1 (same geometry, other y / +0x20 / flags / type / len: the data differs)
export type Tie = 'goal' | 'path' | 'none';

const EPS_POS = 1e-3;  // docs/94 M0: positions within 1e-3
const EPS_TIE = 1e-5;  // equal cost: float32 sums of segment lengths in another order differ by a few ULP (docs/94 §13.1)
const near = (a: number, b: number, eps = EPS_POS) => Math.abs(a - b) <= eps * Math.max(1, Math.abs(a), Math.abs(b));
const cost = (a: Answer) => a.paths.reduce((s, p) => s + p.len, 0);
const sameSet = (a: string[] = [], b: string[] = []) => a.length === b.length && [...a].sort().join() === [...b].sort().join();

type K = { cls: Cls; why: string[]; tie?: Tie };
// class plus the diff lines that put it there (empty for M0); v2: a mismatch with non-routing writes in the window is M5
export function classify(r: Rec): K {
  const k = classifyCopy(r);
  if (r.nr && (k.cls === 'M1' || k.cls === 'M2' || k.cls === 'M3'))
    return { cls: 'M5', why: [`nr 0x${r.nr.toString(16)} (non-routing writes in the window), would be ${k.cls}`, ...k.why] };
  return k;
}

function classifyCopy(r: Rec): K {
  if (r.refused || !r.copy) return { cls: 'M4', why: [`refused: ${r.refused ?? 'no copy answer'}`] };
  if (r.stale) return { cls: 'M5', why: ['stale version'] };
  const g = r.game, c = r.copy;
  if (g.mType !== c.mType || g.status !== c.status) return { cls: 'M3', why: [`mType/status game ${g.mType}/${g.status} copy ${c.mType}/${c.status}`] };
  if (g.mType !== 1) return { cls: 'M0', why: [] };  // same failure code: status only (docs/93 §8)
  const m2: string[] = [], m1: string[] = [], geo: string[] = [];  // geo: the route's geometry differs (subpaths, node count, xz)
  if (g.startIdx !== c.startIdx) m2.push(`start index game ${g.startIdx} copy ${c.startIdx}`);
  if (g.goalIdx !== c.goalIdx) {
    const tie = r.tie ?? [];
    // docs/99 §4.1, docs/94 §13.1: the job draws the goal with its own RDTSC-seeded RNG, any member of the tie set is valid
    if (tie.includes(g.goalIdx) && tie.includes(c.goalIdx)) return { cls: 'M1', tie: 'goal', why: [`goal-tie: game ${g.goalIdx} copy ${c.goalIdx}, tie set [${tie}]`] };
    m2.push(`goal index game ${g.goalIdx} copy ${c.goalIdx}${tie.length ? `, tie set [${tie}]` : ''}`);
  }
  const cg = cost(g), cc = cost(c);
  if (!near(cg, cc)) m2.push(`cost game ${cg.toFixed(4)} copy ${cc.toFixed(4)}`);
  const sig = (a: Answer) => a.paths.map(p => `${p.type}/${p.obj}`).join(' ');
  // another subpath type or object is never a tie: portal candidates are ordered by the copied portal list (docs/99 §4.4)
  if (sig(g) !== sig(c)) m2.push(`subpaths game [${sig(g)}] copy [${sig(c)}]`);
  else g.paths.forEach((gp, i) => {
    const cp = c.paths[i];
    if (!sameSet(gp.o58, cp.o58)) m2.push(`path ${i} O+0x58 game [${gp.o58 ?? []}] copy [${cp.o58 ?? []}]`);
    if (!near(gp.len, cp.len)) m1.push(`path ${i} +0x8C game ${gp.len} copy ${cp.len}`);
    if (gp.nodes.length !== cp.nodes.length) { geo.push(`path ${i} node count game ${gp.nodes.length} copy ${cp.nodes.length}`); return; }
    for (let n = 0; n < gp.nodes.length; n++) {
      const a = gp.nodes[n], b = cp.nodes[n], d: string[] = [];
      if (!near(a[0], b[0]) || !near(a[2], b[2])) { geo.push(`path ${i} node ${n}: xz ${a[0]},${a[2]} vs ${b[0]},${b[2]}`); break; }
      if (!near(a[1], b[1])) d.push(`y ${a[1]} vs ${b[1]}`);  // docs/99 §12.5: y and +0x20 feed every follow reader
      if (!near(a[3], b[3])) d.push(`+0x20 ${a[3]} vs ${b[3]}`);
      if (a[4] !== b[4]) d.push(`flags 0x${a[4].toString(16)} vs 0x${b[4].toString(16)}`);
      if (a[5] !== b[5]) d.push(`type ${a[5]} vs ${b[5]}`);
      if (d.length) { m1.push(`path ${i} node ${n}: ${d.join('; ')}`); break; }  // first differing node per path
    }
  });
  // end point: last node of the last path against the game's
  const end = (a: Answer) => a.paths.at(-1)?.nodes.at(-1);
  const ge = end(g), ce = end(c);
  if (ge && ce && (!near(ge[0], ce[0]) || !near(ge[2], ce[2])) && !m2.some(s => s.startsWith('goal'))) m2.push(`end point game ${ge[0]},${ge[2]} copy ${ce[0]},${ce[2]}`);
  // path tie (docs/94 §13.1): the searches break equal-f ties by insertion / heap order, so an order difference gives another
  // route of the same cost; ends, start and goal must agree and the cost must be equal to float rounding, not just within 1e-3
  if (geo.length && !m2.length && near(cg, cc, EPS_TIE))
    return { cls: 'M1', tie: 'path', why: [`path-tie: cost game ${cg} copy ${cc}`, ...geo, ...m1] };
  if (m2.length || geo.length) return { cls: 'M2', why: [...m2, ...geo, ...m1] };
  if (m1.length) return { cls: 'M1', tie: 'none', why: m1 };
  return { cls: 'M0', why: [] };
}

export type Verdict = { world: string; search: string; n: Record<Cls, number>; compares: number; gameUs: number; jobUs: number;
  goalTie: number; pathTie: number; m1NonTie: number; tieSets: number; p: { job50: number; job95: number; game50: number; game95: number };
  verdict: 'PASS' | 'FAIL' | 'OPEN'; reasons: string[] };

const pctl = (xs: number[], q: number) => { if (!xs.length) return 0; const s = [...xs].sort((a, b) => a - b); return s[Math.min(s.length - 1, Math.floor(q * s.length))]; };
const TIESET_MAX = 0.005;  // records with a goal tie set: live F-7 rate 0 / 21,063 BP, 2 / 8,257 IP (docs/108 §7); above 0.5% the rule is suspect

// G4 (docs/94 §13.2): >= 10k compares, M3 = 0, M2 = 0 and real M1 = 0 under G (M2 explained under R), M4 < 5%, worker job time
// <= game plan time (sum, p50, p95). Ties are counted apart: goal ties pass; path ties under G are OPEN (the clone's order differs
// from the game's: read each), under R they pass. Compares = fresh records with both answers (M0-M3); M4 share is over all records.
export function gate(world: string, recs: Rec[], minCompares = 10000): Verdict {
  const n = { M0: 0, M1: 0, M2: 0, M3: 0, M4: 0, M5: 0 } as Record<Cls, number>;
  let gameUs = 0, jobUs = 0, goalTie = 0, pathTie = 0, m1NonTie = 0, tieSets = 0;
  const jobs: number[] = [], games: number[] = [];
  const search = [...new Set(recs.map(r => r.search))].join('+');
  for (const r of recs) {
    const { cls, tie } = classify(r);
    n[cls]++;
    if (cls === 'M1') tie === 'goal' ? goalTie++ : tie === 'path' ? pathTie++ : m1NonTie++;
    if (cls < 'M4') { gameUs += r.gameUs; jobUs += r.jobUs; jobs.push(r.jobUs); games.push(r.gameUs); if (r.tie?.length) tieSets++; }
  }
  const compares = n.M0 + n.M1 + n.M2 + n.M3, fails: string[] = [], open: string[] = [];
  const p = { job50: pctl(jobs, 0.5), job95: pctl(jobs, 0.95), game50: pctl(games, 0.5), game95: pctl(games, 0.95) };
  const R = search === 'R';
  if (compares < minCompares) fails.push(`${compares} compares < ${minCompares}`);
  if (n.M3) fails.push(`M3 = ${n.M3}`);
  if (n.M2) (R ? open : fails).push(`M2 = ${n.M2}${R ? ' (R: explain each class)' : ''}`);
  // under G the copy runs the game's own code on the same data: a real M1 is a data difference (docs/94 §4.4 K3 G: M0 every case)
  if (m1NonTie) (R ? open : fails).push(`M1 (not a tie) = ${m1NonTie}${R ? ' (R: explain each)' : ''}`);
  if (pathTie && !R) open.push(`path ties = ${pathTie} under G (the clone's insertion order differs: read each)`);
  if (tieSets > TIESET_MAX * compares) open.push(`goal tie sets on ${pct(tieSets, compares)} of compares > ${100 * TIESET_MAX}% (check the tie rule)`);
  if (n.M4 >= 0.05 * recs.length) fails.push(`M4 ${pct(n.M4, recs.length)} >= 5%`);
  if (jobUs > gameUs) fails.push(`job time ${ms(jobUs)} > game plan time ${ms(gameUs)}`);
  if (p.job50 > p.game50 || p.job95 > p.game95) fails.push(`job time p50/p95 ${p.job50}/${p.job95} us > game ${p.game50}/${p.game95} us`);
  return { world, search, n, compares, gameUs, jobUs, goalTie, pathTie, m1NonTie, tieSets, p,
    verdict: fails.length ? 'FAIL' : open.length ? 'OPEN' : 'PASS', reasons: [...fails, ...open] };
}

const pct = (a: number, b: number) => `${b ? (100 * a / b).toFixed(2) : '0.00'}%`;
const ms = (us: number) => `${(us / 1000).toFixed(1)} ms`;

export function report(recs: Rec[], first = 10, minCompares = 10000) {
  const lines: string[] = [], out = (s: string) => lines.push(s);
  const worlds = [...new Set(recs.map(r => r.world))];
  const verdicts = worlds.map(w => gate(w, recs.filter(r => r.world === w), minCompares));
  for (const v of verdicts) {
    const tot = Object.values(v.n).reduce((s, x) => s + x, 0);
    out(`${v.world} (${v.search}): ${tot} records, ${v.compares} compares; ` +
      (['M0', 'M1', 'M2', 'M3', 'M4', 'M5'] as Cls[]).map(c => `${c} ${v.n[c]} (${pct(v.n[c], tot)})`).join(', ') +
      `; M1 = goal-tie ${v.goalTie} + path-tie ${v.pathTie} + real ${v.m1NonTie}; tie sets ${v.tieSets}; job ${ms(v.jobUs)} vs game ` +
      `${ms(v.gameUs)} (p50 ${v.p.job50}/${v.p.game50} us, p95 ${v.p.job95}/${v.p.game95} us)`);
    out(`  G4 ${v.verdict}${v.reasons.length ? ': ' + v.reasons.join('; ') : ''}`);
  }
  // v3 m2 (docs/94 §22): P5 verify jobs; their copy answer is what Mode=2 takes on the jobs it does not verify
  const m2 = recs.filter(r => r.m2);
  if (m2.length) {
    const n: Record<Cls, number> = { M0: 0, M1: 0, M2: 0, M3: 0, M4: 0, M5: 0 };
    for (const r of m2) n[classify(r).cls]++;
    out(`P5 verify records (m2): ${m2.length}; ${(Object.keys(n) as Cls[]).map(c => `${c} ${n[c]}`).join(', ')}; ` +
      `M2 + M3 = ${n.M2 + n.M3} answers Mode=2 would have taken wrong (G5 needs 0)`);
  }
  out('\nPer IC (all worlds): M0 / M1 / M2 / M3 / M4 / M5');
  const byIc = new Map<string, Record<Cls, number>>();
  const bad: { r: Rec; cls: Cls; why: string[] }[] = [];
  for (const r of recs) {
    const k = classify(r);
    const row = byIc.get(r.ic) ?? { M0: 0, M1: 0, M2: 0, M3: 0, M4: 0, M5: 0 };
    row[k.cls]++; byIc.set(r.ic, row);
    if (k.cls === 'M1' || k.cls === 'M2' || k.cls === 'M3') bad.push({ r, ...k });
  }
  for (const [ic, row] of byIc) out(`  ${ic.padEnd(20)} ${Object.values(row).join(' / ')}`);
  // M3 first, then M2, then M1, file order inside a class
  bad.sort((a, b) => (b.cls > a.cls ? 1 : b.cls < a.cls ? -1 : 0));
  out(`\nFirst ${Math.min(first, bad.length)} of ${bad.length} mismatches (M3, M2, M1):`);
  for (const { r, cls, why } of bad.slice(0, first)) {
    out(`  ${cls} plan ${r.plan} ${r.ic} ${r.world} epoch ${r.epoch ?? '?'} ver ${r.ver ?? '?'} start ${r.start ?? '?'} goal ${r.goal ?? '?'}`);
    for (const w of why) out(`    ${w}`);
  }
  return { lines, verdicts };
}

function selfCheck() {
  const node = (x: number, y = -100, seg = 1, flags = 0, type = 0): Node => [x, y, 0, seg, flags, type];
  const ans = (o: Partial<Answer> = {}): Answer => ({ mType: 1, status: 2, startIdx: 0, goalIdx: 0,
    paths: [{ type: '0xC6600B67', obj: '0x0', len: 2, o58: ['0x1'], nodes: [node(0, 10, 0, 0), node(1), node(2, 10, 1, 8)] }], ...o });
  const rec = (o: Partial<Rec> = {}): Rec => ({ v: 1, plan: 1, ic: 'Replan', world: 'BP', search: 'G', gameUs: 100, jobUs: 50, game: ans(), copy: ans(), ...o });
  const cls = (r: Rec) => classify(r).cls;
  const withNodes = (nodes: Node[], o: Partial<Path> = {}) => ans({ paths: [{ ...ans().paths[0], nodes, ...o }] });
  const base = ans().paths[0].nodes;

  assert.equal(cls(rec()), 'M0');
  assert.equal(cls(rec({ game: ans({ mType: 16, status: 8 }), copy: ans({ mType: 16, status: 8, goalIdx: 4 }) })), 'M0');  // failure: status only
  assert.equal(cls(rec({ refused: 'lot', copy: null })), 'M4');
  assert.equal(cls(rec({ stale: true, copy: ans({ mType: 2 }) })), 'M5');
  assert.equal(cls(rec({ copy: ans({ mType: 16, status: 8 }) })), 'M3');
  assert.equal(cls(rec({ copy: ans({ status: 1 }) })), 'M3');
  // goal ties: any member of the tie set is M1 goal-tie; outside the set M2
  let k = classify(rec({ tie: [0, 2], copy: ans({ goalIdx: 2 }) }));
  assert.equal(k.cls, 'M1'); assert.match(k.why[0], /^goal-tie/);
  assert.equal(cls(rec({ tie: [0, 2], copy: ans({ goalIdx: 3 }) })), 'M2');
  assert.equal(cls(rec({ copy: ans({ goalIdx: 1 }) })), 'M2');
  assert.equal(cls(rec({ copy: ans({ startIdx: 1 }) })), 'M2');
  // cost, subpath type, object id, O+0x58, end point: M2
  assert.equal(cls(rec({ copy: withNodes(base, { len: 2.5 }) })), 'M2');
  assert.equal(cls(rec({ copy: withNodes(base, { type: '0x1' }) })), 'M2');
  assert.equal(cls(rec({ copy: withNodes(base, { obj: '0x5' }) })), 'M2');
  assert.equal(cls(rec({ copy: withNodes(base, { o58: [] }) })), 'M2');
  assert.equal(cls(rec({ copy: withNodes(base, { o58: ['0x1'] }) })), 'M0');  // order-free set compare
  assert.equal(cls(rec({ copy: withNodes([base[0], base[1], node(2.5, 10, 1, 8)]) })), 'M2');
  // same geometry, other node data: real M1 (y, +0x20, flags, type)
  for (const n of [node(1, 0), node(1, -100, 1.2), node(1, -100, 1, 0x10), node(1, -100, 1, 0, 3)]) {
    k = classify(rec({ copy: withNodes([base[0], n, base[2]]) }));
    assert.equal(k.cls, 'M1', JSON.stringify(n)); assert.equal(k.tie, 'none'); assert.match(k.why[0], /^path 0 node 1: /);
  }
  // other geometry at an equal cost (float rounding only): path tie; the same with a cost off by more than rounding: M2
  k = classify(rec({ copy: withNodes([base[0], node(1.5), base[2]]) }));
  assert.equal(k.cls, 'M1'); assert.equal(k.tie, 'path'); assert.match(k.why[1], /^path 0 node 1: xz 1,0 vs 1.5,0/);
  k = classify(rec({ copy: withNodes([base[0], base[1], base[1], base[2]]) }));
  assert.equal(k.tie, 'path'); assert.match(k.why[1], /node count game 3 copy 4/);
  assert.equal(classify(rec({ copy: withNodes([base[0], node(1.5), base[2]], { len: 2 + 1e-6 }) })).tie, 'path');  // 5e-7 relative
  assert.equal(cls(rec({ copy: withNodes([base[0], node(1.5), base[2]], { len: 2.0005 }) })), 'M2');            // within 1e-3, not a tie  // another subpath type or object (door) at the same cost stays M2 (portal choice is ordered by the copied portal list, docs/99
  // §4.4); other geometry with another start index stays M2
  assert.equal(cls(rec({ copy: withNodes(base, { obj: '0x5' }) })), 'M2');
  assert.equal(cls(rec({ copy: ans({ startIdx: 1, paths: [{ ...ans().paths[0], nodes: [base[0], node(1.5), base[2]] }] }) })), 'M2');
  assert.equal(cls(rec({ copy: withNodes(base.map(n => [n[0] + 1e-4, ...n.slice(1)] as Node)) })), 'M0');  // within 1e-3
  // v2 nr (docs/94 §17.4 D5): a mismatch with non-routing writes in the window is M5; a match stays M0
  k = classify(rec({ v: 2, nr: 4, copy: ans({ goalIdx: 1 }) }));
  assert.equal(k.cls, 'M5'); assert.match(k.why[0], /^nr 0x4 .*would be M2/);
  assert.equal(cls(rec({ v: 2, nr: 1 })), 'M0');
  assert.equal(cls(rec({ v: 2, nr: 1, refused: 'clone', copy: null })), 'M4');
  // a line exactly as ts3native 0.28.0 writes it (p4.cpp P4Record, selftest TestP4): M0, and M1 with one node's y changed
  const nat = '{"v":2,"plan":100,"ic":"Replan","world":"ST","search":"G","epoch":1,"ver":9,"start":[1.5,2,3],"goal":[3.5,4,5],"tie":[],' +
    '"refused":null,"stale":false,"nr":0,"gameUs":1,"jobUs":0,"game":{"mType":1,"status":2,"startIdx":0,"goalIdx":1,"paths":[{"type":' +
    '"0xC6600B67","obj":"0x0","len":7.5,"o58":[],"nodes":[[10,-100,20,0,0,-1],[11,-100,19,1.25,0,-1],[12,-100,18,2.5,8,-1]]}]},"copy":' +
    '{"mType":1,"status":2,"startIdx":0,"goalIdx":1,"paths":[{"type":"0xC6600B67","obj":"0x0","len":7.5,"o58":[],"nodes":[[10,-100,20,0,0,-1],' +
    '[11,-100,19,1.25,0,-1],[12,-100,18,2.5,8,-1]]}]}}';
  assert.equal(cls(JSON.parse(nat)), 'M0');
  const natY = nat.slice(0, nat.lastIndexOf('[11,-100')) + '[11,0' + nat.slice(nat.lastIndexOf('[11,-100') + 8);
  assert.equal(cls(JSON.parse(natY)), 'M1');
  assert.equal(cls(JSON.parse(natY.replace('"nr":0', '"nr":4'))), 'M5');
  // v3 (ts3native 0.33.0, P5 verify job, p5.cpp P5Plan -> P4Record): "m2":1, the same classes; the report adds the P5 line
  const nat3 = nat.replace('{"v":2,', '{"v":3,').replace('"ic":"Replan"', '"ic":"ReplanFromPosition"').replace('"nr":0,', '"nr":0,"m2":1,');
  assert.equal(cls(JSON.parse(nat3)), 'M0');
  const v3Y = natY.replace('{"v":2,', '{"v":3,').replace('"nr":0,', '"nr":0,"m2":1,');
  assert.match(report([JSON.parse(nat3), JSON.parse(v3Y), rec()], 5, 1).lines.join('\n'),
    /P5 verify records \(m2\): 2; M0 1, M1 1, M2 0, M3 0, M4 0, M5 0; M2 \+ M3 = 0/);
  assert.doesNotMatch(report([rec()], 5, 1).lines.join('\n'), /P5 verify/);

  // gate: 10 compares needed (scaled down), M3 / M2 under G / M4 >= 5% / slow jobs fail; M2 under R and non-tie M1 under G are OPEN
  const many = (n: number, o: Partial<Rec> = {}) => Array.from({ length: n }, (_, i) => rec({ plan: i, ...o }));
  assert.equal(gate('BP', many(10), 10).verdict, 'PASS');
  assert.equal(gate('BP', many(9), 10).verdict, 'FAIL');
  assert.equal(gate('BP', [...many(20), rec({ copy: ans({ mType: 2 }) })], 10).verdict, 'FAIL');
  assert.equal(gate('BP', [...many(20), rec({ copy: ans({ goalIdx: 1 }) })], 10).verdict, 'FAIL');
  let v = gate('IP', [...many(20, { search: 'R' }), rec({ search: 'R', copy: ans({ goalIdx: 1 }) })], 10);
  assert.equal(v.verdict, 'OPEN'); assert.match(v.reasons[0], /R: explain/);
  assert.equal(gate('BP', [...many(19), rec({ refused: 'kind', copy: null })], 10).verdict, 'FAIL');   // 1/20 = 5%
  assert.equal(gate('BP', [...many(20), rec({ refused: 'kind', copy: null })], 10).verdict, 'PASS');   // 1/21 < 5%
  assert.equal(gate('BP', many(10, { jobUs: 200 }), 10).verdict, 'FAIL');
  assert.equal(gate('BP', [...many(10), rec({ stale: true, jobUs: 1e6 })], 10).verdict, 'PASS');      // M5 not a compare
  // ties counted apart: a goal tie passes; tie sets above 0.5% of compares are OPEN (rule suspect)
  v = gate('BP', [...many(300), rec({ tie: [0, 1], copy: ans({ goalIdx: 1 }) })], 10);
  assert.equal(v.verdict, 'PASS'); assert.equal(v.n.M1, 1); assert.equal(v.goalTie, 1); assert.equal(v.m1NonTie, 0); assert.equal(v.tieSets, 1);
  v = gate('BP', [...many(98), rec({ tie: [0, 1] }), rec({ tie: [0, 1], copy: ans({ goalIdx: 1 }) })], 10);
  assert.equal(v.verdict, 'OPEN'); assert.match(v.reasons[0], /goal tie sets on 2.00% of compares/);
  // path ties: OPEN under G (read each), pass under R; a real M1: FAIL under G, OPEN under R
  const tieRec = (o: Partial<Rec> = {}) => rec({ copy: withNodes([base[0], node(1.5), base[2]]), ...o });
  v = gate('BP', [...many(10), tieRec()], 10);
  assert.equal(v.verdict, 'OPEN'); assert.equal(v.pathTie, 1); assert.match(v.reasons[0], /path ties = 1 under G/);
  assert.equal(gate('IP', [...many(10, { search: 'R' }), tieRec({ search: 'R' })], 10).verdict, 'PASS');
  const realM1 = (o: Partial<Rec> = {}) => rec({ copy: withNodes([base[0], node(1, 0), base[2]]), ...o });
  assert.equal(gate('BP', [...many(10), realM1()], 10).verdict, 'FAIL');
  assert.equal(gate('IP', [...many(10, { search: 'R' }), realM1({ search: 'R' })], 10).verdict, 'OPEN');
  // job time at p95 above the game's fails even when the sum is below
  v = gate('BP', [...many(18, { jobUs: 1 }), ...many(2, { jobUs: 500 })], 10);
  assert.ok(v.jobUs < v.gameUs); assert.equal(v.verdict, 'FAIL'); assert.match(v.reasons[0], /p50\/p95 1\/500 us > game 100\/100/);

  // report: per world verdict lines, mismatch list ordered M3 > M2 > M1 with diffs
  const r = report([...many(10), rec({ world: 'IP', plan: 7, copy: withNodes([base[0], node(1, 0), base[2]]) }),
    rec({ world: 'IP', plan: 8, copy: ans({ mType: 16, status: 8 }) })], 5, 10);
  const txt = r.lines.join('\n');
  assert.match(txt, /^BP \(G\): 10 records, 10 compares; M0 10/m);
  assert.match(txt, /G4 PASS/);
  assert.match(txt, /IP \(G\).*M1 = goal-tie 0 \+ path-tie 0 \+ real 1.*\n  G4 FAIL: 2 compares < 10; M3 = 1; M1 \(not a tie\) = 1/);
  assert.match(txt, /M3 plan 8 Replan IP[^\n]*\n    mType\/status game 1\/2 copy 16\/8\n  M1 plan 7[^\n]*\n    path 0 node 1: y -100 vs 0/);
  console.log('shadow-compare-report self-check ok');
}

if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) {
  const args = process.argv.slice(2);
  if (args.includes('--self-check')) selfCheck();
  else {
    const fi = args.indexOf('--first');
    const first = fi >= 0 ? Number(args[fi + 1]) : 10;
    const files = args.filter((a, i) => !a.startsWith('--') && !(fi >= 0 && i === fi + 1));
    if (!files.length) {
      console.log('usage: node tools/harness/shadow-compare-report.ts <shadow.jsonl> [...] [--first 10] | --self-check');
      process.exitCode = 2;
    } else {
      const recs: Rec[] = [];
      for (const f of files) for (const [i, l] of readFileSync(f, 'utf8').split(/\r?\n/).entries()) {
        if (!l.trim()) continue;
        const r = JSON.parse(l);
        if (r.v !== 1 && r.v !== 2 && r.v !== 3) throw new Error(`${f}:${i + 1}: record v${r.v}, this report reads v1-v3 (docs/94 §13)`);
        recs.push(r);
      }
      const { lines, verdicts } = report(recs, first);
      console.log(lines.join('\n'));
      if (verdicts.some(v => v.verdict === 'FAIL')) process.exitCode = 1;
    }
  }
}
