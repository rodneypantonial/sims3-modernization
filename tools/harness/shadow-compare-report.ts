// Report of a routing snapshot P4 shadow run (docs/94 §5 phase P4, §4.3 classes, §13 record): classifies every shadow
// compare into M0-M5, prints the gate G4 verdict per world, a per-IC class table and the first mismatches with diffs.
//   node tools/harness/shadow-compare-report.ts <shadow.jsonl> [...] [--first 10]
//   node tools/harness/shadow-compare-report.ts --self-check
//   node tools/harness/shadow-compare-report.ts <shadow.jsonl> [...] --g1 [--history <glob> ...]
//     adds the G1 v2 tiered verdict (docs/94 §24.13): tier 1 = the copy's answer is a game answer to the same request in any
//     shadow file (default history probe-out/**/*-shadow.jsonl), tier 2 = flagged (route valid against the live obstacles in
//     the record, cost within 10%, <= 1 per 10,000 compares), tier 3 = FAIL; plus the replay-audit drift check of each input's
//     <run>-ts3native.log (every MISMATCH lot must show an equal audit later)
//
// Record (one JSON line per compare, written by the native side at collect; spec = docs/94 §13, keep both in step):
//   { "v": 2, "plan": 1234, "ic": "Plan" | "Replan" | "PlanFromPoint" | "ReplanFromPosition", "world": "BP",
//     "search": "G" | "R", "epoch": 77, "ver": 76,
//     "start": [x, y, z], "goal": [x, y, z],        request: first start record, game's chosen goal record
//     "tie": [0, 3],                                 goal indices of the job's tie set (docs/99 §4.1), [] or absent = no tie
//     "refused": null | "option" | "kind" | "lot" | "epoch" | "<reason>",   set = M4, no copy answer
//     "sub": "busy",                                 ts3native 0.33.1, nover / mat / epoch only: the check that refused (busy, off,
//                                                    portal, noclone, p3, copy, timeout, stale, passed, replaced, moved, shadow; 0.33.2: queued, lock, held)
//     "stale": false,                                routing writes landed between the cut and the live plan (ts3native
//                                                    0.33.2): a mismatch (M1-M3) is then M5, equal answers stay M0
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
//   "obst" (optional, ts3native 0.35.4, docs/94 §21.19; only on a final M2/M3, [RouteSnap] ShadowObst=1): the footprints
//   whose polygon points reach the box round both answers (+1 m), from the clone and from the live map read after the shadow:
//     { "box": [x0, z0, x1, z1], "liveState": "ok" | "busy" | "noworld", "liveW": n (recorded writes since the live plan
//       began: > 0 = the live set may be newer than what the live plan saw), "us": n, "cloneMore" / "liveMore": entries past
//       48 (-1 no map, -2 fault), "clone": [Ob], "live": [Ob] }
//     Ob = ["0xID", "0xVT", f34, fmeFlags, levels, invalidLevelMask, "0xHASH", x0, z0, x1, z1]   hash of every valid level's points
//     0.36.50 (docs/94 §24.8.4): Ob + [frame "w" | "l" (world box or the start lot's box), key ["0xSET", "0xORIGIN", nA, nB,
//       "0xKEYHASH"] | null (fp+0x20; ORIGIN = the live set a clone copy stands for)], and "walkC" / "walkL" {walked, lot,
//       lbox, near}. "lotdump" (M2LotDump=1): the start lot's Q leaf entries near the route, clone and live, with "diffs"
//       [[bits, cloneIndex, liveIndex]] (bits 1 lock keys, 2 key origin, 4 masks, 8 points, 16 +0x34; 0 = one side only)
import assert from 'node:assert/strict';
import { existsSync, globSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { basename, dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

type Node = [number, number, number, number, number, number];
type Path = { type: string; obj: string; len: number; o58?: string[]; nodes: Node[] };
type Answer = { mType: number; status: number; startIdx: number; goalIdx: number; paths: Path[] };
export type Rec = {
  v: number; plan: number; ic: string; world: string; search: 'G' | 'R'; epoch?: number; ver?: number;
  start?: number[] | null; goal?: number[] | null; tie?: number[]; refused?: string | null; sub?: string; stale?: boolean; nr?: number; m2?: number;
  gameUs: number; jobUs: number; game: Answer; copy: Answer | null; obst?: Obst; lotdump?: LotDump;
  src?: string;  // the shadow file it came from (set by the loader, not by the native side)
};
type Key = [string, string, number, number, string] | null;
type Ob = [string, string, number, number, number, number, string, number, number, number, number, string?, Key?];
// "lotdump" entry: [q, "0xFME", "0xID", "0xVT", f34, fmeFlags, mask4, mask8, levels, invalid, "0xHASH", levelHashes, cell, bounds, key]
type LdEnt = [number, string, string, string, number, number, number, number, number, number, string, string[], number[], number[], Key];
type LdSide = { state: string; lot: string; lbox: number[]; qs: number[][]; more: number; ents: LdEnt[] };
export type LotDump = { why: string; pt: number[]; box: number[]; clone: LdSide; live: LdSide; diffs: [number, number, number][] };
const keyTxt = (k: Key | undefined) => k === undefined ? '' : k === null ? ' keys n/a' :
  ` keys ${k[0]}${k[1] !== k[0] ? ` (origin ${k[1]})` : ''} ${k[2]}/${k[3]} ${k[4]}`;
// the lot dump's diff lines (docs/94 §24.8.4): one per entry that differs
export function lotDumpDiff(d: LotDump, max = 8): string[] {
  const ent = (e: LdEnt) => `Q ${e[0]} ${e[2]} vt ${e[3]} FME+0x10 ${e[5]} cell [${e[12].join(',')}] masks ${e[6]}/${e[7]} points ${e[10]}${keyTxt(e[14])}`;
  const bitTxt = (b: number) => ['lock keys', 'key origin', 'masks', 'points', '+0x34'].filter((_, i) => (b >> i) & 1).join(', ');
  const lines = [`lot dump (${d.why}) lot ${d.clone.lot}: clone ${d.clone.state} ${d.clone.ents.length}, live ${d.live.state} ${d.live.ents.length}, ${d.diffs.length} differ`];
  for (const [bits, c, l] of d.diffs.slice(0, max)) {
    if (c < 0) lines.push(`  only live: ${ent(d.live.ents[l])}`);
    else if (l < 0) lines.push(`  only clone: ${ent(d.clone.ents[c])}`);
    else lines.push(`  differs (bits ${bits}: ${bitTxt(bits)}): clone ${ent(d.clone.ents[c])} vs live ${ent(d.live.ents[l])}`);
  }
  return lines;
}
type Walk = { walked: number; lot: string; lbox: number[] | null; near: unknown[] };
export type Obst = { box: number[]; liveState: string; liveW: number; us: number; cloneMore: number; liveMore: number; clone: Ob[]; live: Ob[];
  walkC?: Walk; walkL?: Walk };

// the obstacle diff of an M2/M3 record (docs/94 §21.19): footprints only one side has, and footprints whose points differ
export function obstDiff(o: Obst, max = 8): string[] {
  const key = (b: Ob) => `${b[0]}/${b[1]}`;
  const fmt = (b: Ob) => `${b[0]} vt ${b[1]} xz [${b.slice(7, 11).map(v => +(+(v as number)).toFixed(3)).join(',')}]` +
    `${b[5] ? ` invalid levels 0x${b[5].toString(16)}` : ''}${b[11] ? ` (${b[11]} frame)` : ''}${keyTxt(b[12])}`;
  // 0.36.50: fp+0x20 differs (NULL vs set, counts, ids), or a clone copy stands for another live set than live holds
  const keyDiff = (c: Ob, l: Ob) => c[12] !== undefined && l[12] !== undefined && (!c[12] !== !l[12] ||
    JSON.stringify(c[12]?.slice(2)) !== JSON.stringify(l[12]?.slice(2)) || (!!c[12] && c[12][0] !== '0x00000000' && c[12][1] !== l[12]?.[0]));
  const live = new Map(o.live.map(b => [key(b), b])), clone = new Map(o.clone.map(b => [key(b), b]));
  const lines = [`obstacles in box [${o.box.join(',')}]: clone ${o.clone.length}${o.cloneMore ? ` (more ${o.cloneMore})` : ''}, ` +
    `live ${o.liveState === 'ok' ? o.live.length : o.liveState}${o.liveMore ? ` (more ${o.liveMore})` : ''}, ` +
    `writes since the live plan ${o.liveW}${o.liveW ? ' (live set may be newer)' : ''}`];
  if (o.liveState !== 'ok') return lines;
  const diff: string[] = [];
  let same = 0;
  for (const [k, c] of clone) {
    const l = live.get(k);
    if (!l) diff.push(`only clone: ${fmt(c)}`);
    else if (l[6] !== c[6] || l[5] !== c[5] || keyDiff(c, l)) diff.push(`differs: ${fmt(c)} (clone) vs ${fmt(l)} (live)`);
    else same++;
  }
  for (const [k, l] of live) if (!clone.has(k)) diff.push(`only live: ${fmt(l)}`);
  lines.push(`  ${same} equal, ${diff.length} differ${diff.length > max ? ` (first ${max})` : ''}`);
  for (const d of diff.slice(0, max)) lines.push(`  ${d}`);
  return lines;
}
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
  if (r.stale && (k.cls === 'M1' || k.cls === 'M2' || k.cls === 'M3'))
    return { cls: 'M5', why: [`stale (routing writes between the cut and the live plan), would be ${k.cls}`, ...k.why] };
  return k;
}

function classifyCopy(r: Rec): K {
  if (r.refused || !r.copy) return { cls: 'M4', why: [`refused: ${r.refused ?? 'no copy answer'}`] };
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
  // M4 by refusal (and its 0.33.1 sub-check), most first: what to raise next for G4's M4 < 5%
  const m4 = new Map<string, number>();
  for (const r of recs) if (r.refused || !r.copy) { const key = (r.refused ?? 'no copy') + (r.sub ? '/' + r.sub : ''); m4.set(key, (m4.get(key) ?? 0) + 1); }
  if (m4.size) out(`M4 by refusal: ${[...m4].sort((a, b) => b[1] - a[1]).map(([k2, v]) => `${k2} ${v}`).join(', ')}`);
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
    if (r.obst) for (const w of obstDiff(r.obst)) out(`    ${w}`);
    if (r.lotdump) for (const w of lotDumpDiff(r.lotdump)) out(`    ${w}`);
  }
  return { lines, verdicts };
}

// ---- G1 v2 (docs/94 §24.13, Rodney 2026-10-25): tiered rule. The game itself gives two answers to some requests (§24.8.2), so a
// copy answer that equals a game answer to the same request in any run is a known alternative (tier 1), not a clone defect.
export type G1Hit = { file: string; plan: number };
export type GameIndex = Map<string, Map<string, G1Hit[]>>;  // request key -> answer signature -> where the game gave it
// request = same IC, world, search kind, start and goal float-exact (numbers as parsed, so 1204.50 and 1204.5 are one key)
export const reqKey = (r: Pick<Rec, 'ic' | 'world' | 'search' | 'start' | 'goal'>) =>
  `${r.ic}|${r.world}|${r.search}|${r.start?.join(',') ?? 'null'}|${r.goal?.join(',') ?? 'null'}`;
// node for node float-exact, same subpath types and objects, same result
const ansSig = (a: Answer) => JSON.stringify([a.mType, a.status, a.paths.map(p => [p.type, p.obj, p.nodes])]);
// the request fields come first in every v2/v3 line: read only the head, parse the line only for a wanted request
const HEAD = /"ic":"([^"]*)","world":"([^"]*)","search":"([^"]*)"[^[]*?"start":(null|\[[^\]]*\]),"goal":(null|\[[^\]]*\])/;

export function indexGameAnswers(files: string[], want: Set<string>): GameIndex {
  const idx: GameIndex = new Map();
  if (!want.size) return idx;
  for (const f of files) for (const l of readFileSync(f, 'utf8').split('\n')) {
    const m = HEAD.exec(l.slice(0, 512));
    if (!m) continue;
    const key = reqKey({ ic: m[1], world: m[2], search: m[3] as 'G', start: JSON.parse(m[4]), goal: JSON.parse(m[5]) });
    if (!want.has(key)) continue;
    const r = JSON.parse(l) as Rec;
    if (!r.game) continue;
    const bySig = idx.get(key) ?? new Map<string, G1Hit[]>();
    const sig = ansSig(r.game), hits = bySig.get(sig) ?? [];
    hits.push({ file: basename(f), plan: r.plan }); bySig.set(sig, hits); idx.set(key, bySig);
  }
  return idx;
}

// a copy segment meets an axis-aligned box (Liang-Barsky clip on xz)
function segHitsBox(ax: number, az: number, bx: number, bz: number, x0: number, z0: number, x1: number, z1: number) {
  let t0 = 0, t1 = 1;
  const dx = bx - ax, dz = bz - az;
  for (const [p, q] of [[-dx, ax - x0], [dx, x1 - ax], [-dz, az - z0], [dz, z1 - az]]) {
    if (p === 0) { if (q < 0) return false; continue; }
    const t = q / p;
    if (p < 0) { if (t > t1) return false; if (t > t0) t0 = t; } else { if (t < t0) return false; if (t < t1) t1 = t; }
  }
  return true;
}

// tier 2 validity from what the record holds: true = no copy segment meets a live footprint's bounds (so it crosses none);
// null = undecided (counts as FAIL until the native live check exists, §24.13.3). The record has bounds, not polygons, so it
// can prove "crosses nothing" but never "crosses one".
export function liveValidity(r: Rec): { ok: true | null; why: string } {
  const o = r.obst;
  if (!o) return { ok: null, why: 'no obst block' };
  if (o.liveState !== 'ok') return { ok: null, why: `live obstacle read ${o.liveState}` };
  if (!o.walkL) return { ok: null, why: 'obstacle walk before 0.36.50 (its live sets were always empty, §24.8.3 (c))' };
  if (/^0x0+$/.test(o.walkL.lot)) return { ok: null, why: 'no lot frame (lot lookup found no lot, §24.10-§24.12)' };
  if (o.liveMore) return { ok: null, why: `live list truncated (more ${o.liveMore})` };
  if (o.live.some(b => b[11] === 'l')) return { ok: null, why: 'live footprint in the lot frame (no transform in the record)' };
  for (const p of r.copy?.paths ?? []) for (let i = 1; i < p.nodes.length; i++) {
    const a = p.nodes[i - 1], b = p.nodes[i];
    const hit = o.live.find(f => segHitsBox(a[0], a[2], b[0], b[2], f[7], f[8], f[9], f[10]));
    if (hit) return { ok: null, why: `copy segment ${i} meets the bounds of live footprint ${hit[0]} (polygon not in the record)` };
  }
  return { ok: true, why: `${o.live.length} live footprints, no copy segment meets their bounds` };
}

// replay-audit drift (ts3native log): every "snap replay audit MISMATCH lot X" must be repaired by the forced rebase. The log
// has no per-lot "rebased" line, so repaired = a later MISMATCH line of X names a last equal ring after this one; recurred =
// a later line of X names none or an older one; timeout = the AuditHoldRebase hold of X released by timeout; else unconfirmed.
export type AuditEv = { lot: string; ring: number; fate: 'repaired' | 'recurred' | 'timeout' | 'unconfirmed' };
export function auditDrift(log: string): AuditEv[] {
  const evs: (AuditEv & { lastEq: number })[] = [];
  for (const l of log.split(/\r?\n/)) {
    let m = /snap replay audit MISMATCH lot (0x[0-9A-Fa-f]+): .*\(ring (\d+), cut -?\d+, last equal ring (none|\d+)/.exec(l);
    if (m) {
      const ring = +m[2], lastEq = m[3] === 'none' ? 0 : +m[3];
      for (const e of evs) if (e.lot === m[1] && e.ring < ring && e.fate === 'unconfirmed') e.fate = lastEq > e.ring ? 'repaired' : 'recurred';
      if (!evs.some(e => e.lot === m![1] && e.ring === ring)) evs.push({ lot: m[1], ring, lastEq, fate: 'unconfirmed' });  // Q line + hash line
      continue;
    }
    m = /snap replay audit: lot (0x[0-9A-Fa-f]+) rebase hold released by timeout/.exec(l);
    if (m) for (const e of evs) if (e.lot === m[1] && e.fate === 'unconfirmed') e.fate = 'timeout';
  }
  return evs.map(({ lastEq: _, ...e }) => e);
}

export type G1Row = { plan: number; src?: string; cls: Cls; tier: 1 | 2 | 3; why: string };
export type G1 = { world: string; compares: number; tiers: [number, number, number]; rows: G1Row[]; audit: AuditEv[] | null;
  verdict: 'PASS' | 'PASS-FLAGGED' | 'FAIL'; reasons: string[] };
const TIER2_LEN = 0.10, TIER2_RATE = 1 / 10000;

export function g1(world: string, recs: Rec[], idx: GameIndex, audit: AuditEv[] | null, minCompares = 10000): G1 {
  const rows: G1Row[] = [];
  let compares = 0;
  for (const r of recs) {
    const k = classify(r);
    if (k.cls === 'M4' || k.cls === 'M5') continue;
    compares++;
    if (k.cls === 'M0' || (k.cls === 'M1' && k.tie === 'goal')) continue;  // goal ties pass as in G4
    const row = (tier: 1 | 2 | 3, why: string) => rows.push({ plan: r.plan, src: r.src, cls: k.cls, tier, why });
    if (k.cls === 'M3') { row(3, `M3: ${k.why[0]}`); continue; }
    if (k.cls === 'M1' && k.tie === 'none') { row(3, `real M1: ${k.why[0]}`); continue; }
    // M2 and path ties: another route to the same request
    const seen = idx.get(reqKey(r));
    const hits = seen?.get(ansSig(r.copy!))?.filter(h => !(h.file === (r.src && basename(r.src)) && h.plan === r.plan));
    if (hits?.length) { row(1, `copy = game answer of ${hits.slice(0, 3).map(h => `${h.file} plan ${h.plan}`).join(', ')}${hits.length > 3 ? ` (+${hits.length - 3})` : ''}`); continue; }
    const hist = `request in history: ${[...(seen?.values() ?? [])].reduce((s, h) => s + h.length, 0)} game answers, ${seen?.size ?? 0} distinct`;
    const cg = cost(r.game), cc = cost(r.copy!);
    if (Math.abs(cc - cg) > TIER2_LEN * cg) { row(3, `cost game ${cg.toFixed(4)} copy ${cc.toFixed(4)}: over 10%; ${hist}`); continue; }
    const v = liveValidity(r);
    row(v.ok ? 2 : 3, `${v.ok ? 'valid' : 'undecided'}: ${v.why}; cost game ${cg.toFixed(4)} copy ${cc.toFixed(4)}; ${hist}`);
  }
  const tiers: [number, number, number] = [1, 2, 3].map(t => rows.filter(x => x.tier === t).length) as [number, number, number];
  const fails: string[] = [];
  if (compares < minCompares) fails.push(`${compares} compares < ${minCompares}`);
  if (tiers[2]) fails.push(`tier 3 = ${tiers[2]}`);
  if (tiers[1] > TIER2_RATE * compares) fails.push(`tier 2 = ${tiers[1]} > 1 per 10,000 compares`);
  if (!audit) fails.push('no ts3native log: replay audit unchecked');
  else {
    const bad = audit.filter(e => e.fate !== 'repaired');
    if (bad.length) fails.push(`replay-audit drift not shown repaired: ${bad.length} of ${audit.length} on ` +
      `${new Set(bad.map(e => e.lot)).size} lots (${(['recurred', 'timeout', 'unconfirmed'] as const).map(f => `${f} ${bad.filter(e => e.fate === f).length}`).join(', ')})`);
  }
  return { world, compares, tiers, rows, audit, reasons: fails, verdict: fails.length ? 'FAIL' : tiers[1] ? 'PASS-FLAGGED' : 'PASS' };
}

export function g1Report(recs: Rec[], idx: GameIndex, audit: AuditEv[] | null, minCompares = 10000) {
  const lines: string[] = [], verdicts: G1[] = [];
  for (const w of [...new Set(recs.map(r => r.world))]) {
    const v = g1(w, recs.filter(r => r.world === w), idx, audit, minCompares);
    verdicts.push(v);
    lines.push(`G1 v2 ${w}: ${v.compares} compares; tier 1 ${v.tiers[0]}, tier 2 ${v.tiers[1]}, tier 3 ${v.tiers[2]}; replay audit ` +
      (audit ? `${audit.length} mismatches, repaired ${audit.filter(e => e.fate === 'repaired').length}` : 'no log'));
    lines.push(`  G1 ${v.verdict}${v.reasons.length ? ': ' + v.reasons.join('; ') : ''}`);
    for (const r of v.rows) lines.push(`  plan ${r.plan} ${r.cls} tier ${r.tier}: ${r.why}`);
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
  assert.equal(cls(rec({ stale: true })), 'M0');  // stale with equal answers: still a compare
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
  assert.equal(gate('BP', [...many(10), rec({ stale: true, jobUs: 1e6, copy: ans({ mType: 2 }) })], 10).verdict, 'PASS');      // M5 not a compare
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
  // obstacle sets (ts3native 0.35.4): the diff names footprints one side lacks and footprints whose points differ
  const ob = (id: string, h: string, x = 1): Ob => [id, '0x00FEBBC0', 3, 0, 1, 0, h, x, 2, x + 0.4, 2.4];
  const obst: Obst = { box: [0, 1, 5, 6], liveState: 'ok', liveW: 0, us: 40, cloneMore: 0, liveMore: 0,
    clone: [ob('0x1', '0xA'), ob('0x2', '0xB'), ob('0x3', '0xC')], live: [ob('0x1', '0xA'), ob('0x2', '0xD', 1.5), ob('0x4', '0xE')] };
  const od = obstDiff(obst).join('\n');
  assert.match(od, /clone 3, live 3, writes since the live plan 0\n  1 equal, 3 differ/);
  assert.match(od, /differs: 0x2 vt 0x00FEBBC0 xz \[1,2,1.4,2.4\] \(clone\) vs 0x2 vt 0x00FEBBC0 xz \[1.5,2,1.9,2.4\] \(live\)/);
  assert.match(od, /only clone: 0x3 /); assert.match(od, /only live: 0x4 /);
  assert.deepEqual(obstDiff({ ...obst, liveState: 'busy', liveW: 2 }), ['obstacles in box [0,1,5,6]: clone 3, live busy, writes since the live plan 2 (live set may be newer)']);
  assert.match(report([rec({ copy: ans({ goalIdx: 1 }), obst })], 5, 1).lines.join('\n'), /M2 plan 1[^\n]*\n    goal[^\n]*\n    obstacles in box/);
  // 0.36.50 (docs/94 §24.8.4): frame + lock key on each Ob; a NULL clone set vs a live set differs with equal points
  const k2: Key = ['0x0ABC0000', '0x0ABC0000', 2, 0, '0x1'], k0: Key = ['0x00000000', '0x00000000', 0, 0, '0x0'];
  const ok = (k: Key): Ob => ['0x5', '0x00FEBBC0', 3, 0, 1, 0, '0xF', 50.5, 32, 51, 32.5, 'l', k];
  const od2 = obstDiff({ ...obst, clone: [ok(k0)], live: [ok(k2)] }).join('\n');
  assert.match(od2, /0 equal, 1 differ\n  differs: 0x5 vt 0x00FEBBC0 xz \[50.5,32,51,32.5\] \(l frame\) keys 0x00000000 0\/0 0x0 \(clone\) vs .* keys 0x0ABC0000 2\/0 0x1 \(live\)/);
  assert.match(obstDiff({ ...obst, clone: [ok(k2)], live: [ok(k2)] }).join('\n'), /1 equal, 0 differ/);
  const le = (k: Key): LdEnt => [0, '0x100', '0x6C11003401401605', '0x00FEBBC0', 3, 65537, 65280, 3, 1, 0, '0xF', ['0xF'], [30, 30, 62, 62], [50.5, 32, 51, 32.5], k];
  const side = (ents: LdEnt[]): LdSide => ({ state: 'ok', lot: '0x6C1100342B1C14A0', lbox: [], qs: [], more: 0, ents });
  const ld: LotDump = { why: 'm2', pt: [1250.5, 1731.8], box: [0, 0, 1, 1], clone: side([le(k0)]), live: side([le(k2), le(k2)]), diffs: [[1, 0, 0], [0, -1, 1]] };
  assert.match(lotDumpDiff(ld).join('\n'), /^lot dump \(m2\) lot 0x6C1100342B1C14A0: clone ok 1, live ok 2, 2 differ\n  differs \(bits 1: lock keys\): clone Q 0 0x6C11003401401605 .*cell \[30,30,62,62\].* keys 0x00000000 0\/0 0x0 vs live .* keys 0x0ABC0000 2\/0 0x1\n  only live: Q 0 /);

  // G1 v2 (docs/94 §24.13): tiers from a fixture history file, live validity from obst, audit drift from a log
  const req = (i: number): Partial<Rec> => ({ start: [i + 0.5, 1, 2.25], goal: [i + 3, 1, 4] });
  const altB = withNodes([base[0], node(1.5), base[2]], { len: 2.1 });  // another route, 5% longer: M2
  const wOb = (x0: number, x1: number): Ob => ['0x9', '0x00FEBBC0', 3, 0, 1, 0, '0xA', x0, -1, x1, 1, 'w', null];
  const okObst: Obst = { box: [-1, -1, 3, 1], liveState: 'ok', liveW: 0, us: 1, cloneMore: 0, liveMore: 0, clone: [], live: [wOb(10, 11)],
    walkC: { walked: 9, lot: '0x6C11', lbox: [0, 0, 64, 64], near: [] }, walkL: { walked: 9, lot: '0x6C11', lbox: [0, 0, 64, 64], near: [] } };
  const dir = mkdtempSync(join(tmpdir(), 'g1v2-'));
  try {
    // history: run H's game gave answer B to request 1; request 2 only answer A
    const hist = join(dir, 'runH-shadow.jsonl');
    writeFileSync(hist, [rec({ plan: 41, ...req(1), game: altB, copy: altB }), rec({ plan: 42, ...req(2) }), rec({ plan: 43, ...req(1), search: 'R', game: ans(), copy: ans() })]
      .map(x => JSON.stringify({ v: x.v, plan: x.plan, ic: x.ic, world: x.world, search: x.search, start: x.start, goal: x.goal, ...x }))  // native key order
      .join('\n') + '\n');
    const ins: Rec[] = [
      rec({ plan: 1, ...req(1), copy: altB, src: 'run1-shadow.jsonl' }),                                   // tier 1: copy = run H plan 41
      rec({ plan: 2, ...req(2), copy: altB, obst: okObst }),                                               // tier 2: valid, 5%
      rec({ plan: 3, ...req(3), copy: altB, obst: { ...okObst, liveState: 'busy' } }),                     // tier 3: undecided
      rec({ plan: 4, ...req(4), copy: withNodes([base[0], node(1.5), base[2]], { len: 3 }), obst: okObst }), // tier 3: cost 50%
      rec({ plan: 5, ...req(5), copy: ans({ mType: 16, status: 8 }) }),                                    // tier 3: M3
      rec({ plan: 6, ...req(6), copy: withNodes([base[0], node(1, 0), base[2]]) }),                         // tier 3: real M1
      rec({ plan: 7, ...req(7), copy: altB, obst: { ...okObst, live: [wOb(0.5, 0.7)] } }),                 // tier 3: segment in a live bbox
      rec({ plan: 8, ...req(8), copy: altB, obst: { ...okObst, walkL: undefined } }),                      // tier 3: old walk
    ];
    const idx = indexGameAnswers([hist], new Set(ins.map(reqKey)));
    assert.equal(idx.get(reqKey(ins[0]))?.size, 1);  // the search R record of the same points is another request
    const v1 = g1('BP', [...ins, ...many(10)], idx, [], 1);
    const tierOf = (p: number) => v1.rows.find(x => x.plan === p)?.tier;
    assert.deepEqual([1, 2, 3, 4, 5, 6, 7, 8].map(tierOf), [1, 2, 3, 3, 3, 3, 3, 3]);
    assert.match(v1.rows[0].why, /copy = game answer of runH-shadow\.jsonl plan 41/);
    assert.match(v1.rows.find(x => x.plan === 3)!.why, /^undecided: live obstacle read busy/);
    assert.match(v1.rows.find(x => x.plan === 7)!.why, /meets the bounds of live footprint 0x9/);
    assert.equal(v1.verdict, 'FAIL'); assert.deepEqual(v1.tiers, [1, 1, 6]);
    assert.match(v1.reasons.join(';'), /tier 3 = 6;tier 2 = 1 > 1 per 10,000 compares/);
    // tier 1 alone passes; one tier 2 in 10,000 compares is PASS-FLAGGED, in fewer it fails the rate cap
    assert.equal(g1('BP', [ins[0], ...many(5)], idx, [], 1).verdict, 'PASS');
    assert.equal(g1('BP', [ins[1], ...many(9999)], idx, [], 10000).verdict, 'PASS-FLAGGED');
    assert.equal(g1('BP', [ins[1], ...many(9998)], idx, [], 1).verdict, 'FAIL');
    assert.match(g1('BP', many(5), idx, null, 1).reasons[0], /no ts3native log/);
    // audit drift: A repaired (a later line names an equal audit after it), B recurred, C hold timeout, D unconfirmed
    const log = [
      'snap replay audit MISMATCH lot 0xA: Q 1 (level index) entries live 5 clone 4 (ring 100, cut 10, last equal ring none)',
      'snap replay audit MISMATCH lot 0xA: hash differs, entries live 5 clone 4 (clone 2 lots, map 9 / live 9) (ring 100, cut 10, last equal ring none)',
      'snap replay audit MISMATCH lot 0xB: hash differs, entries live 5 clone 4 (clone 2 lots, map 9 / live 9) (ring 300, cut 30, last equal ring none)',
      'snap replay audit MISMATCH lot 0xA: hash differs, entries live 6 clone 5 (clone 2 lots, map 9 / live 9) (ring 200, cut 20, last equal ring 150, 50 records)',
      'snap replay audit MISMATCH lot 0xB: hash differs, entries live 5 clone 4 (clone 2 lots, map 9 / live 9) (ring 400, cut 40, last equal ring none)',
      'snap replay audit MISMATCH lot 0xC: hash differs, entries live 5 clone 4 (clone 2 lots, map 9 / live 9) (ring 500, cut 50, last equal ring none)',
      'snap replay audit: lot 0xC rebase hold released by timeout (the diff did not run within one cut; 1 so far)',
      'snap replay audit MISMATCH lot 0xD: hash differs, entries live 5 clone 4 (clone 2 lots, map 9 / live 9) (ring 600, cut 60, last equal ring none)',
    ].join('\n');
    const ev = auditDrift(log);
    assert.deepEqual(ev.map(e => `${e.lot}@${e.ring}:${e.fate}`), ['0xA@100:repaired', '0xB@300:recurred', '0xA@200:unconfirmed', '0xB@400:unconfirmed', '0xC@500:timeout', '0xD@600:unconfirmed']);
    assert.match(g1('BP', many(5), idx, ev, 1).reasons[0], /not shown repaired: 5 of 6 on 4 lots \(recurred 1, timeout 1, unconfirmed 3\)/);
    assert.equal(g1('BP', many(5), idx, ev.slice(0, 1), 1).verdict, 'PASS');
    assert.match(g1Report([...ins.slice(0, 2), ...many(3)], idx, [], 1).lines.join('\n'), /^G1 v2 BP: 5 compares; tier 1 1, tier 2 1, tier 3 0;[^\n]*\n  G1 FAIL: tier 2 = 1 > 1 per 10,000/);
  } finally { rmSync(dir, { recursive: true, force: true }); }
  console.log('shadow-compare-report self-check ok');
}

if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) {
  const args = process.argv.slice(2);
  if (args.includes('--self-check')) selfCheck();
  else {
    const fi = args.indexOf('--first');
    const first = fi >= 0 ? Number(args[fi + 1]) : 10;
    const hist = args.flatMap((a, i) => a === '--history' ? [args[i + 1]] : []);
    const files = args.filter((a, i) => !a.startsWith('--') && !(fi >= 0 && i === fi + 1) && args[i - 1] !== '--history');
    if (!files.length) {
      console.log('usage: node tools/harness/shadow-compare-report.ts <shadow.jsonl> [...] [--first 10] [--g1 [--history <glob>]] | --self-check');
      process.exitCode = 2;
    } else {
      const recs: Rec[] = [];
      for (const f of files) for (const [i, l] of readFileSync(f, 'utf8').split(/\r?\n/).entries()) {
        if (!l.trim()) continue;
        const r = JSON.parse(l);
        if (r.v !== 1 && r.v !== 2 && r.v !== 3) throw new Error(`${f}:${i + 1}: record v${r.v}, this report reads v1-v3 (docs/94 §13)`);
        r.src = f;
        recs.push(r);
      }
      const { lines, verdicts } = report(recs, first);
      console.log(lines.join('\n'));
      if (verdicts.some(v => v.verdict === 'FAIL')) process.exitCode = 1;
      if (args.includes('--g1')) {
        const root = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
        const histFiles = new Set([...files.map(f => resolve(f)),
          ...(hist.length ? hist : ['probe-out/**/*-shadow.jsonl']).flatMap(g => globSync(g, { cwd: root }).map(f => resolve(root, f)))]);
        // only the requests of the inputs' M2s and path ties are looked up, so the history read stays one pass
        const want = new Set(recs.filter(r => { const k = classify(r); return k.cls === 'M2' || k.tie === 'path'; }).map(reqKey));
        const idx = indexGameAnswers([...histFiles], want);
        console.log(`\nG1 v2 (docs/94 §24.13): history ${histFiles.size} shadow files, ${want.size} requests looked up`);
        for (const f of files) {  // one run per file: its own ts3native log for the audit check
          const log = f.replace(/-shadow\.jsonl$/, '-ts3native.log');
          const audit = log !== f && existsSync(log) ? auditDrift(readFileSync(log, 'utf8')) : null;
          const g = g1Report(recs.filter(r => r.src === f), idx, audit);
          console.log(`${basename(f)}\n${g.lines.join('\n')}`);
          if (g.verdicts.some(v => v.verdict === 'FAIL')) process.exitCode = 1;
        }
      }
    }
  }
}
