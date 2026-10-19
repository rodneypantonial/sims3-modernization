// Verdicts from an F-4b live read (tools/frida/f4b-reads.js, docs/99 §12.6 and §12.7, docs/51 §60 and §64):
//   node tools/harness/f4b-report.ts probe-out/profile/<tag>-f4b.json [more files]
//   node tools/harness/f4b-report.ts --self-check     the script's path reader on fake memory + the outcome rules, no game
// Prints the docs/99 §12.6 outcome row (A flat / B prepended / C ends real / D inner real) and the I-99-33, I-99-37, I-99-39
// and item-3 (agent y vs terrain) verdicts. A file holds one job; pass several to see each.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { parseArgs } from 'node:util';

const ROOT = resolve(import.meta.dirname, '..', '..');
type Node = { i: number; y: number; len: number; run: number };
type PathRow = { n: number; total: number; sum2d: number; sum3d: number; sumLen: number; flatN: number; layout: string; nodes: Node[] };
type PrependRow = { ra: string; point: number[]; nBefore: number; delta: number; node0: { y: number; len: number } | null; node1: { y: number; len: number } | null; totalBefore: number; totalAfter: number; terrain?: number };
export type Res = { reason: string; seconds: number; refused: Record<string, string>; chained: Record<string, any>; errors: string[];
  copy: { calls: number; threads: Record<string, number>; retFalse: number; layouts: Record<string, number>; layoutsSearched: Record<string, number>; rows: PathRow[] };
  prepend: { calls: number; threads: Record<string, number>; ras: Record<string, number>; delta: Record<string, number>; rows: PrependRow[] };
  terrain: { calls: number; rows: { point: number[]; terrain: number; dy: number }[] } };

const isFlatY = (y: number) => Math.abs(y + 100) < 1e-3;
const median = (a: number[]) => { const s = [...a].sort((x, y) => x - y); return s.length ? s[Math.floor(s.length / 2)] : NaN; };
const top = (o: Record<string, number>) => Object.entries(o).sort((a, b) => b[1] - a[1]).map(([k, v]) => `${k}:${v}`).join(' ') || 'none';
const f2 = (x: number) => (Number.isFinite(x) ? x.toFixed(2) : '-');

// docs/99 §12.6 outcome table; the rows are checked in the order D, C, B, A (a real inner node outranks everything else)
export function outcome(r: Res): { row: string; text: string } {
  const s = r.copy.layoutsSearched, n = (k: string) => s[k] ?? 0, searched = Object.values(s).reduce((a, b) => a + b, 0);
  const pre = r.prepend.rows.filter(x => x.delta === 1), long = pre.filter(x => !isFlatY(x.point[1]) && (x.node0?.len ?? 0) >= 100);
  if (!r.copy.calls) return { row: '-', text: 'no verdict: no CopyNodes call' };
  if (!searched) return { row: '-', text: `no verdict: no searched route (n > 2) among ${r.copy.calls} CopyNodes calls` };
  if (n('inner') + n('real')) return { row: 'D', text: `inner nodes at real heights on ${n('inner') + n('real')} of ${searched} searched paths: a y writer exists outside the scan (§12.2 negative wrong)` };
  if (n('ends')) return { row: 'C', text: `start/goal at real height next to -100 nodes on ${n('ends')} of ${searched} searched paths (${n('flat')} flat): §3 holds; the inflation starts at build time` };
  if (long.length) return { row: 'B', text: `every searched path flat (${searched}); slot +0x100 prepended ${long.length} real-y points with node 0 +0x20 >= 100 m (of ${pre.length} prepends, ${r.prepend.calls} calls): the prepended layout is live; measure walk-style effects next` };
  if (pre.length) return { row: '-', text: `every searched path flat (${searched}); ${pre.length} prepends, none with a real y and +0x20 >= 100 m: no table row, read the prepend rows` };
  return { row: 'A', text: `every searched path flat (${searched}); slot +0x100 never added a node (${r.prepend.calls} calls${r.prepend.calls ? `, delta ${top(r.prepend.delta)}` : ''}): flat paths, F-4b harmless; §3 corrected (start/goal heights only on direct routes)` };
}

export function verdicts(r: Res): string[] {
  const out: string[] = [], c = r.copy, P = r.prepend, o = outcome(r);
  out.push(`run: ${r.reason}, ${r.seconds} s; refused ${Object.keys(r.refused).join(', ') || 'none'}; chained ${Object.keys(r.chained).join(', ') || 'none'}; errors ${r.errors.length}${r.errors.length ? ` (${r.errors.slice(0, 3).join('; ')})` : ''}`);
  out.push(`item 1 CopyNodes: ${c.calls} calls (${c.retFalse} returned false), threads ${top(c.threads)}; layouts ${top(c.layouts)}`);
  const searchedRows = c.rows.filter(x => x.n > 2), flat = searchedRows.filter(x => x.layout === 'flat');
  if (searchedRows.length) {
    out.push(`  searched rows kept ${searchedRows.length}: path+0x8C / 2D sum median ${f2(median(searchedRows.map(x => x.total / (x.sum2d || 1))))}, ` +
      `|path+0x8C - 3D sum| median ${f2(median(searchedRows.map(x => Math.abs(x.total - x.sum3d))))} m, |sum of +0x20 - 3D sum| median ${f2(median(searchedRows.map(x => Math.abs(x.sumLen - x.sum3d))))} m`);
    const ex = searchedRows.find(x => x.layout !== 'flat') ?? searchedRows[0];
    out.push(`  example (${ex.layout}, n ${ex.n}): ${ex.nodes.map(d => `#${d.i} y ${d.y} +0x20 ${d.len}`).join(', ')}; +0x8C ${ex.total}, 2D ${ex.sum2d}`);
  }
  const direct = c.rows.filter(x => x.n <= 2);
  if (direct.length) out.push(`  direct (n <= 2) rows ${direct.length}: layouts ${top(direct.reduce((a: Record<string, number>, x) => (a[x.layout] = (a[x.layout] ?? 0) + 1, a), {}))}`);
  out.push(`item 2 slot +0x100: ${P.calls} calls, delta ${top(P.delta)}, return addresses ${top(P.ras)}`);
  const pre = P.rows.filter(x => x.delta === 1);
  if (pre.length) out.push(`  prepends: point y - node 1 y median ${f2(median(pre.map(x => x.point[1] - (x.node1?.y ?? NaN))))} m, node 0 +0x20 median ${f2(median(pre.map(x => x.node0?.len ?? NaN)))} m, ` +
    `+0x8C after / before median ${f2(median(pre.map(x => x.totalAfter / (x.totalBefore || 1))))}`);
  const T = r.terrain.rows;
  out.push(`item 3 agent y vs terrain (World_GetTerrainHeightImpl 0x786200): ${!T.length ? 'no reading (no prepend from the follower sites)' :
    `${T.length} readings, |agent y - terrain| median ${f2(median(T.map(x => Math.abs(x.dy))))} m, max ${f2(Math.max(...T.map(x => Math.abs(x.dy))))} m; ` +
    `${T.every(x => Math.abs(x.dy) <= 0.5) ? 'agent at ground height: §12.4 candidate (a) refuted' : `${T.filter(x => Math.abs(x.dy) > 0.5).length} readings more than 0.5 m off the ground: §12.4 candidate (a) supported for those`}`}`);
  // ledger rows (docs/51 §60)
  const ends = c.layoutsSearched.ends ?? 0, searched = Object.values(c.layoutsSearched).reduce((a, b) => a + b, 0);
  const first100 = searchedRows.filter(x => x.layout === 'ends' && (x.nodes[0].len >= 100 || x.nodes[x.nodes.length - 2]?.len >= 100)).length;
  out.push(`I-99-33: ${!searched ? 'no verdict (no searched route)' : ends ? `verified on ${ends} of ${searched} searched paths (first or last +0x20 >= 100 m in ${first100} kept rows)` : o.row === 'D' ? 'REFUTED as worded: inner nodes carry real heights (see outcome D)' : `REFUTED: every searched path flat (${flat.length} kept rows), +0x20 matches the 2D distance (start/goal y also -100)`}`);
  out.push(`I-99-37: ${!P.calls ? 'REFUTED (never +1): slot +0x100 never called in the window' : o.row === 'B' ? `verified: ${pre.length} prepends with a real y` : pre.length ? `partly: ${pre.length} prepends, but not the real-y / +0x20 >= 100 m layout everywhere (see rows)` : `REFUTED (never +1): ${P.calls} calls, delta ${top(P.delta)}`}`);
  out.push(`I-99-39: ${o.row === 'A' || o.row === 'B' ? `verified: every searched path flat${direct.length ? `; direct rows ${direct.filter(x => x.layout !== 'flat').length} of ${direct.length} carry a real y` : ''}` : o.row === 'C' || o.row === 'D' ? `REFUTED (outcome ${o.row})` : 'no verdict'}`);
  out.push(`outcome (docs/99 §12.6): ${o.row} - ${o.text}`);
  return out;
}

function loadLogic() {
  const src = readFileSync(join(ROOT, 'tools', 'frida', 'f4b-reads.js'), 'utf8');
  const block = src.slice(src.indexOf('// --- logic begin'), src.indexOf('// --- logic end'));
  return new Function(`${block}; return { pathRead, layoutOf };`)();
}

function selfCheck() {
  const { pathRead, layoutOf } = loadLogic();
  // fake memory: one path at 0x1000, nodes at 0x2000
  const base = 0x1000, dv = new DataView(new ArrayBuffer(0x4000));
  const mem = { u32: (a: number) => dv.getUint32(a - base, true), f32: (a: number) => dv.getFloat32(a - base, true) };
  const build = (pts: number[][]) => {
    dv.setUint32(0x98, 0x2000, true); dv.setUint32(0x9c, 0x2000 + 48 * pts.length, true);
    let total = 0;
    pts.forEach(([x, y, z], i) => {
      const o = 0x1000 + 48 * i, nx = pts[i + 1], len = nx ? Math.hypot(nx[0] - x, nx[1] - y, nx[2] - z) : 0;
      dv.setFloat32(o, x, true); dv.setFloat32(o + 4, y, true); dv.setFloat32(o + 8, z, true); dv.setFloat32(o + 0x20, len, true); dv.setFloat32(o + 0x24, total, true);
      total += len;
    });
    dv.setFloat32(0x8c, total, true);
  };
  build([[0, -100, 0], [3, -100, 4], [6, -100, 8], [6, -100, 9]]);
  let p = pathRead(mem, base);
  assert.equal(p.n, 4); assert.equal(p.layout, 'flat'); assert.equal(p.sum2d, 11); assert.equal(p.total, 11); assert.equal(p.flatN, 4);
  assert.deepEqual(p.nodes.map((d: Node) => d.i), [0, 1, 2, 3]);
  build([[0, 2, 0], [3, -100, 4], [6, -100, 8], [7, -100, 8], [9, 3, 8]]);
  p = pathRead(mem, base);
  assert.equal(p.layout, 'ends'); assert.equal(p.sum2d, 13); assert.ok(p.total > 200 && Math.abs(p.total - p.sum3d) < 1e-2 && Math.abs(p.sumLen - p.sum3d) < 1e-2);
  assert.deepEqual(p.nodes.map((d: Node) => d.i), [0, 1, 3, 4]); assert.ok(p.nodes[0].len > 100 && p.nodes[2].len > 100);
  build([[0, 2, 0], [3, 2, 4]]); assert.equal(pathRead(mem, base).layout, 'real');
  build([[0, -100, 0]]); p = pathRead(mem, base); assert.deepEqual(p.nodes.map((d: Node) => d.i), [0]); assert.equal(p.sum2d, 0);
  dv.setUint32(0x9c, 0x2001, true); assert.ok(pathRead(mem, base).error);
  assert.equal(layoutOf([-100, 5, -100]), 'inner'); assert.equal(layoutOf([1, -100]), 'ends'); assert.equal(layoutOf([]), 'empty');
  // outcome rules
  const row = (layout: string, n = 5, y0 = -100, len0 = 5): PathRow => ({ n, total: 20, sum2d: 20, sum3d: 20, sumLen: 20, flatN: n, layout, nodes: [{ i: 0, y: y0, len: len0, run: 0 }, { i: 1, y: -100, len: 5, run: 5 }, { i: n - 2, y: -100, len: 5, run: 10 }, { i: n - 1, y: -100, len: 0, run: 20 }] });
  const res = (searched: Record<string, number>, rows: PathRow[], prep: PrependRow[] = [], calls = prep.length): Res => ({ reason: 'complete', seconds: 177, refused: {}, chained: {}, errors: [],
    copy: { calls: 50, threads: { 1: 50 }, retFalse: 0, layouts: {}, layoutsSearched: searched, rows }, prepend: { calls, threads: {}, ras: {}, delta: {}, rows: prep },
    terrain: { calls: prep.length, rows: prep.map(x => ({ point: x.point, terrain: x.point[1] - 0.01, dy: 0.01 })) } });
  const pr = (y: number, len: number): PrependRow => ({ ra: '0x852b21', point: [1, y, 2], nBefore: 5, delta: 1, node0: { y, len }, node1: { y: -100, len: 5 }, totalBefore: 20, totalAfter: 20 + len, terrain: y });
  let v = verdicts(res({ flat: 9 }, [row('flat'), row('real', 2, 3)])).join('\n');
  assert.match(v, /outcome \(docs\/99 §12\.6\): A - every searched path flat \(9\); slot \+0x100 never added a node \(0 calls\)/);
  assert.match(v, /I-99-33: REFUTED: every searched path flat/); assert.match(v, /I-99-37: REFUTED \(never \+1\): slot \+0x100 never called/);
  assert.match(v, /I-99-39: verified: every searched path flat; direct rows 1 of 1 carry a real y/); assert.match(v, /item 3 .*no reading/);
  v = verdicts(res({ flat: 9 }, [row('flat')], [pr(2.5, 102.5), pr(3, 103)])).join('\n');
  assert.match(v, /outcome \(docs\/99 §12\.6\): B - .*prepended 2 real-y points/); assert.match(v, /I-99-37: verified: 2 prepends/);
  assert.match(v, /item 3 .*2 readings, .*agent at ground height: §12\.4 candidate \(a\) refuted/);
  v = verdicts(res({ flat: 9 }, [row('flat')], [pr(-100, 4)])).join('\n');
  assert.match(v, /outcome \(docs\/99 §12\.6\): - - every searched path flat \(9\); 1 prepends, none/); assert.match(v, /I-99-37: partly/);
  v = verdicts(res({ flat: 3, ends: 6 }, [row('ends', 5, 2, 102)])).join('\n');
  assert.match(v, /outcome \(docs\/99 §12\.6\): C - start\/goal at real height .* on 6 of 9 searched paths \(3 flat\)/);
  assert.match(v, /I-99-33: verified on 6 of 9 searched paths \(first or last \+0x20 >= 100 m in 1 kept rows\)/); assert.match(v, /I-99-39: REFUTED \(outcome C\)/);
  v = verdicts(res({ flat: 3, ends: 6, inner: 1 }, [row('inner')])).join('\n');
  assert.match(v, /outcome \(docs\/99 §12\.6\): D - inner nodes at real heights on 1 of 10/); assert.match(v, /I-99-33: verified on 6 of 10/);
  assert.match(verdicts(res({ flat: 3, real: 2 }, [row('real')])).join('\n'), /I-99-33: REFUTED as worded[\s\S]*outcome \(docs\/99 §12\.6\): D - inner nodes at real heights on 2 of 5/);
  assert.match(verdicts(res({}, [row('real', 2, 3)])).join('\n'), /outcome \(docs\/99 §12\.6\): - - no verdict: no searched route/);
  console.log('f4b-report self-check: PASS');
}

const { values, positionals } = parseArgs({ allowPositionals: true, options: { 'self-check': { type: 'boolean' } } });
if (values['self-check']) selfCheck();
else {
  if (!positionals.length) { console.error('usage: node tools/harness/f4b-report.ts <tag>-f4b.json [...] | --self-check'); process.exit(2); }
  for (const f of positionals) {
    console.log(f);
    for (const l of verdicts(JSON.parse(readFileSync(f, 'utf8')))) console.log(`  ${l}`);
  }
}
