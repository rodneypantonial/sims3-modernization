// Verdicts from an in-room live-reads file (tools/frida/inroom-reads.js, docs/111, docs/51 §47):
//   node tools/harness/inroom-report.ts probe-out/profile/<tag>-inroom.json [more files]   one block per file
//   node tools/harness/inroom-report.ts --self-check     the script's tree walker on a fake memory + the verdict rules, no game
// Prints: I-98-15 (wrap pairs on opposite root borders), I-98-21 (lot rectangle and c at SetBounds), I-101-4 (static
// 0x11CF02C vs the built object's +0xB8), U6 (X+0x50 per in-room caller), U7 (X+0x4C at in-room entry).
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join, resolve } from 'node:path';

const ROOT = resolve(import.meta.dirname, '..', '..');
type Dump = { which: string; lots: { key: number[]; trees: any[] }[]; errors: string[]; pB8?: { f32: number }; cellStatic: { f32: number } };
type Res = { reason: string; seconds: number; refused: Record<string, string>; plans: number; threads: Record<string, number>; byCaller: Record<string, any>;
  flagBits: Record<string, number>; builds: number; buildRows: any[]; qtb: any[]; dumps: Dump[]; errors: string[] };

const near = (a: number, b: number) => Math.abs(a - b) < 1e-3;

export function verdicts(r: Res): string[] {
  const out: string[] = [];
  // I-98-15: every tree of every dump; the end dump has the most lots
  const byTree = new Map<string, any>(); // one row per lot level tree, the latest dump wins
  for (const d of r.dumps) for (const l of d.lots) for (const t of l.trees) if (!t.missing) byTree.set(`${l.key.join(':')}/${t.idx}`, { ...t, which: d.which, key: l.key });
  const trees = [...byTree.values()];
  const ok = trees.filter(t => !t.error), bad = trees.filter(t => t.error);
  const withPairs = ok.filter(t => t.wrapPairs > 0);
  const caveat = ok.filter(t => t.note?.gated || t.note?.oneAgent).length;
  const unknown = [...new Set(ok.flatMap(t => Object.keys(t.note?.unknownSlots ?? {})))];
  if (!ok.length) out.push(`I-98-15: no verdict (no tree read; ${bad.length} tree errors${bad[0] ? `: ${bad[0].error}` : ''})`);
  else if (withPairs.length) out.push(`I-98-15: REFUTED, ${withPairs.length} of ${ok.length} trees have a walkable wrap pair (first: lot ${withPairs[0].key.map((k: number) => k.toString(16)).join(':')} level ${withPairs[0].level}, ${withPairs[0].wrapPairs} pairs, e.g. ${JSON.stringify(withPairs[0].examples?.[0])}); the replacement must copy the wrap edge`);
  else out.push(`I-98-15: verified for the plain context, 0 walkable wrap pairs in ${ok.length} trees (${new Set(ok.map(t => t.key.join(':'))).size} lots)`);
  if (ok.length) out.push(`  border cells ${ok.reduce((a, t) => a + t.borderCells, 0)}, walkable on -x/+x/-z/+z: ${['mx', 'px', 'mz', 'pz'].map(s => ok.reduce((a, t) => a + (t.sides?.[s]?.walkable ?? 0), 0)).join('/')}; trees with agent-dependent border entries: ${caveat}; tree errors: ${bad.length}${unknown.length ? `; unknown slots ${unknown.join(',')} (class read as byte +0x34)` : ''}`);
  // I-98-21 from SetBounds; else bounds only from the dumps
  const rows = r.buildRows;
  const cs = [...new Set(rows.map(b => b.args[4]))], mins = [...new Set(rows.map(b => `${b.lotRect[0]},${b.lotRect[1]}`))], bmin = [...new Set(rows.map(b => `${b.bounds[0]},${b.bounds[1]}`))];
  if (rows.length) {
    const fromZero = rows.every(b => near(b.lotRect[0], 0) && near(b.lotRect[1], 0)), cIn = cs.every(c => c >= 0.5 - 1e-6 && c <= 1 + 1e-6);
    const bm1 = rows.every(b => near(b.bounds[0], -1) && near(b.bounds[1], -1));
    out.push(`I-98-21: ${fromZero && cIn && bm1 ? 'verified' : 'REFUTED'} over ${r.builds} SetBounds calls (${rows.length} kept): c ${cs.join('/')}, lot rect min ${mins.slice(0, 6).join(' ')}, bounds min ${bmin.slice(0, 6).join(' ')}${fromZero && cIn && bm1 ? '' : ' -> redo the docs/98 §15.1 size table with these values'}`);
  } else {
    const tb = [...new Set(ok.map(t => `${Math.round(t.bounds[0] * 1e3) / 1e3},${Math.round(t.bounds[1] * 1e3) / 1e3}`))];
    out.push(`I-98-21: no SetBounds call in the window; tree bounds min from the dumps ${tb.slice(0, 6).join(' ') || 'none'} (${tb.length === 1 && tb[0] === '-1,-1' ? 'consistent with' : 'does not show'} lot-local from 0 with c <= 1); P+0xB8 ${r.dumps[0]?.pB8?.f32 ?? '?'}, static ${r.dumps[0]?.cellStatic.f32 ?? '?'}`);
  }
  // I-101-4: the static equals +0xB8 of every object QuadTreeBuild runs on
  if (r.qtb.length) {
    const same = r.qtb.every(q => q.ecxB8.u32 === q.cellStatic.u32), isP = r.qtb.every(q => q.ecxIsP);
    out.push(`I-101-4: ${same ? 'verified' : 'REFUTED'} over ${r.qtb.length} QuadTreeBuild calls: +0xB8 ${[...new Set(r.qtb.map(q => q.ecxB8.f32))].join('/')}, static ${[...new Set(r.qtb.map(q => q.cellStatic.f32))].join('/')}; ecx is P in ${isP ? 'every' : 'not every'} call`);
  } else out.push('I-101-4: no QuadTreeBuild call in the window');
  // U6 / U7 per caller
  out.push(`U6/U7: ${r.plans} in-room entries on ${Object.keys(r.threads).length} thread(s); context flag bits seen: ${Object.entries(r.flagBits).map(([b, n]) => `${b}:${n}`).join(' ') || 'none'}`);
  for (const [k, c] of Object.entries(r.byCaller).sort((a: any, b: any) => b[1].n - a[1].n)) {
    const nz = Object.entries(c.x4c as Record<string, number>).filter(([v]) => v !== '0').reduce((a, [, n]) => a + n, 0);
    out.push(`  ${k}: ${c.n} (no agent ${c.noAgent}); flags ${Object.entries(c.flags).sort((a: any, b: any) => b[1] - a[1]).slice(0, 8).map(([f, n]) => `${f}:${n}`).join(' ')}; X+0x4C non-zero ${nz}, changed by the search ${c.x4cChanged}`);
  }
  const x4c = Object.values(r.byCaller).reduce((a: number, c: any) => a + Object.entries(c.x4c as Record<string, number>).filter(([v]) => v !== '0').reduce((s, [, n]) => s + n, 0), 0);
  out.push(`U7: ${r.plans ? (x4c ? `X+0x4C non-zero in ${x4c} entries: the §5.3 else-branch is live` : 'X+0x4C = 0 at every in-room entry: the §5.3 else-branch is not reached in this window') : 'no verdict (no in-room entry)'}`);
  if (Object.keys(r.refused).length) out.push(`refused hooks: ${JSON.stringify(r.refused)}`);
  if (r.errors.length) out.push(`probe errors (${r.errors.length}): ${r.errors.slice(0, 3).join(' | ')}`);
  out.push(`run: ${r.reason}, ${r.seconds} s, dumps ${r.dumps.map(d => `${d.which}(${d.lots.length} lots${d.errors.length ? `, ${d.errors.join(';')}` : ''})`).join(' ') || 'none'}`);
  return out;
}

function walker() {
  const src = readFileSync(join(ROOT, 'tools', 'frida', 'inroom-reads.js'), 'utf8');
  const block = src.slice(src.indexOf('// --- walker begin'), src.indexOf('// --- walker end'));
  assert.ok(block.includes('function dumpTrees'), 'walker block not found in inroom-reads.js');
  return new Function(`${block}; return { dumpTrees, borderCells, childShape, bitOfShape, FLT_MAX };`)();
}

function selfCheck() {
  const W = walker();
  // shape tables of docs/98 §3.2 / §15.4 (childShape = the game's 0xFEE518 rows read from the exe; bitOfShape 0-20)
  assert.deepEqual([0, 1, 2, 3, 4, 5, 20].map(s => [0, 1, 2, 3].map(k => W.childShape(s, k))),
    [[1, 2, 3, 4], [5, 6, 9, 10], [7, 8, 11, 12], [13, 14, 17, 18], [15, 16, 19, 20], [21, 22, 29, 30], [75, 76, 83, 84]]);
  assert.deepEqual(Array.from({ length: 21 }, (_, s) => W.bitOfShape(s)), [0, 0, 4, 32, 36, 0, 2, 4, 6, 16, 18, 20, 22, 32, 34, 36, 38, 48, 50, 52, 54]);
  // fake 32-bit memory: a bump allocator over one buffer; reads outside it throw like a bad pointer
  const B = 0x10000000, buf = Buffer.alloc(1 << 16);
  let top = 0x100;
  const alloc = (n: number) => { const a = B + top; top += (n + 15) & ~15; return a; };
  const w32 = (a: number, v: number) => buf.writeUInt32LE(v >>> 0, a - B), wf = (a: number, v: number) => buf.writeFloatLE(v, a - B), w8 = (a: number, v: number) => buf.writeUInt8(v, a - B);
  const mem = { u8: (a: number) => buf.readUInt8(a - B), u32: (a: number) => buf.readUInt32LE(a - B), s32: (a: number) => buf.readInt32LE(a - B), f32: (a: number) => buf.readFloatLE(a - B) };
  const vt = alloc(16); w32(vt + 4, 0x806f70); w32(vt + 8, 0x807090); w32(vt + 0xc, 0x624450);
  const fpOf = (cls: number, type = 1) => { const fp = alloc(0x50); w32(fp, vt); w8(fp + 0x34, cls); w8(fp + 0x35, type); wf(fp + 0x1c, 1.5); return fp; };
  const fme = (fp: number) => { const e = alloc(0x14); w32(e + 8, fp); return e; };
  const leaf = (finalMask: number, entries: [number, number, number][]) => {
    const n = alloc(0x14); w32(n + 4, finalMask << 3);
    if (entries.length === 1) [w32(n + 8, entries[0][0]), w32(n + 0xc, entries[0][1]), w32(n + 0x10, entries[0][2])];
    else if (entries.length) { const arr = alloc(12 * entries.length); entries.forEach((e, i) => e.forEach((v, j) => w32(arr + 12 * i + 4 * j, v))); w32(n + 8, arr | 1); w32(n + 0xc, entries.length); }
    return n;
  };
  const internal = (kids: number[]) => { const n = alloc(0x1c); w32(n + 4, 1); kids.forEach((k, i) => w32(n + 0xc + 4 * i, k)); return n; };
  const tree = (root: number, defCls: number) => { const t = alloc(0x58); [-1, -1, 31, 31].forEach((v, i) => wf(t + 4 + 4 * i, v)); w32(t + 0x24, root); w32(t + 0x2c, defCls); wf(t + 0x30, 1); wf(t + 0x38, 32); w32(t + 0x40, 6); return t; };
  const walk = () => leaf(1, [[fme(fpOf(0)), ~0, ~0]]);
  // T1: walkable quadrants 0 and 3 only (diagonal): no overlap across either axis -> 0 pairs
  const t1 = tree(internal([walk(), 0, 0, walk()]), 12);
  // T2: quadrants 0, 1, 3 walkable -> -x q0 / +x q1 (z 0-.5) and -z q1 / +z q3 (x .5-1): 2 pairs
  const t2 = tree(internal([walk(), walk(), 0, walk()]), 12);
  // T3: root leaf split into halves (final bits of shapes 1-4); an entry covering shape 1 only, class 10 (bit 0 of the mask);
  // halves 2-4 have no applying entry -> default class 0 (walkable): pairs s3-s4 (x axis) and s2-s4 (z axis)
  const t3 = tree(leaf(0b11110, [[fme(fpOf(10)), 0x0f0f0f0f, 0]]), 0);
  // T4: a type 0x14 footprint (class 1, mult 100000) and a type 0x1F 'keep class 2' footprint of class 2 on one leaf
  const t4 = tree(internal([leaf(1, [[fme(fpOf(9, 0x14)), ~0, ~0], [fme(fpOf(2, 0x1f)), ~0, ~0]]), 0, 0, 0]), 12);
  const P = alloc(0xc0), world = alloc(0x960), wg = alloc(4), cell = alloc(8), bk = alloc(12), node = alloc(0x1a0), vec = alloc(20);
  w32(wg, world); // the global holding the routing world (0x11D9128 in the game)
  w32(world + 0x95c, P); wf(cell, 0.5); wf(P + 0xb8, 0.5); w32(P + 0x4c, bk); w32(P + 0x50, 2); w32(bk, node); w32(node, 5); w32(node + 0x190, 0);
  [t1, t2, t3, t4, 0].forEach((t, i) => w32(vec + 4 * i, t)); w32(node + 0x10 + 0x74, vec); w32(node + 0x10 + 0x78, vec + 20);
  const d = W.dumpTrees(mem, { world: wg, cellStatic: cell });
  assert.deepEqual(d.errors, []);
  assert.equal(d.lots.length, 1);
  const tr = d.lots[0].trees;
  assert.equal(tr.length, 5); assert.ok(tr[4].missing);
  assert.deepEqual(tr.map((t: any) => t.wrapPairs), [0, 2, 2, 0, undefined]);
  assert.deepEqual(tr[0].sides, { mx: { cells: 2, walkable: 1 }, px: { cells: 2, walkable: 1 }, mz: { cells: 2, walkable: 1 }, pz: { cells: 2, walkable: 1 } });
  const c3 = W.borderCells(mem, t3, { gated: 0, oneAgent: 0, inferred: 0, unknownSlots: {} });
  assert.deepEqual(c3.map((c: any) => [c.shape, c.cls, c.walk]), [[1, 10, false], [2, 0, true], [3, 0, true], [4, 0, true]]);
  assert.equal(c3[0].mult, W.FLT_MAX);
  const c4 = W.borderCells(mem, t4, { gated: 0, oneAgent: 0, inferred: 0, unknownSlots: {} });
  assert.deepEqual([c4[0].cls, c4[0].mult, c4[0].n], [1, 100000, 2]); // type 0x14 -> class 1, mult 100000; a 0x1F entry only marks keep-2, its class is not maxed in
  // a bad tree pointer: one error entry, the rest still read
  w32(vec + 16, B + (1 << 20));
  const d2 = W.dumpTrees(mem, { world: wg, cellStatic: cell });
  assert.ok(d2.lots[0].trees[4].error, 'bad pointer must give an error entry');
  // verdict rules
  const res: Res = { reason: 'complete', seconds: 300, refused: {}, plans: 3, threads: { 7: 3 }, flagBits: { '0x10000': 3 }, errors: [],
    byCaller: { 'InRoomPlan@LotPlan_Inner': { n: 3, noAgent: 0, flags: { '0x10000': 3 }, x4c: { 0: 3 }, x4cChanged: 0 } },
    builds: 1, buildRows: [{ args: [-0.5, -0.5, 20.5, 30.5, 0.5], lotRect: [0, 0, 20, 30], bounds: [-1, -1, 21, 31] }],
    qtb: [{ ecxIsP: true, ecxB8: { u32: '0x3f000000', f32: 0.5 }, cellStatic: { u32: '0x3f000000', f32: 0.5 } }], dumps: [{ ...d, which: 'end' }] };
  let v = verdicts(res).join('\n');
  assert.match(v, /I-98-15: REFUTED, 2 of 4 trees/);
  assert.match(v, /I-98-21: verified over 1 SetBounds/);
  assert.match(v, /I-101-4: verified over 1 QuadTreeBuild/);
  assert.match(v, /U7: X\+0x4C = 0 at every/);
  res.dumps = [{ ...d, which: 'end', lots: [{ key: [5, 0], trees: [tr[0], tr[3]] }] }];
  res.buildRows = [{ args: [99.5, 9.5, 120.5, 40.5, 0.5], lotRect: [100, 10, 120, 40], bounds: [99, 9, 121, 41] }];
  res.byCaller['InRoomPlan@RoomProbe_0x633FA0'] = { n: 1, noAgent: 1, flags: { '0x10000': 1 }, x4c: { 4: 1 }, x4cChanged: 1 };
  v = verdicts(res).join('\n');
  assert.match(v, /I-98-15: verified for the plain context, 0 walkable wrap pairs in 2 trees/);
  assert.match(v, /I-98-21: REFUTED/);
  assert.match(v, /U7: X\+0x4C non-zero in 1 entries/);
  console.log('inroom-report self-check: PASS');
}

if (process.argv.includes('--self-check')) selfCheck();
else {
  const files = process.argv.slice(2);
  if (!files.length) { console.error('usage: node tools/harness/inroom-report.ts <tag>-inroom.json [...] | --self-check'); process.exit(2); }
  for (const f of files) { console.log(f); for (const l of verdicts(JSON.parse(readFileSync(f, 'utf8')))) console.log(`  ${l}`); }
}
