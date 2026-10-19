// Verdicts from a routing live-reads pass 2 file (tools/frida/live-reads2.js, docs/114, docs/51 §54):
//   node tools/harness/live-reads2-report.ts probe-out/profile/<tag>-livereads2.json [more files] [--world <file.world>] [--game-copy <dir>]
//   node tools/harness/live-reads2-report.ts --self-check     the script's grid walker on fake memory + the verdict rules, no game
// I-107-21 compares the 0xC6BAB0 answers with the world's blend layer 0 (tools/rtdata-decode.ts): --world, else
// <game-copy>/worlds/<Bridgeport|IslaParadiso|Sunset Valley>.world picked from -bp- / -ip- / -sv- in the file name
// (<game-copy> = --game-copy, else $GAME_COPY, else <repo>/game-copy). I-107-24 names return addresses from
// <game-copy>/analysis/functions.csv when it is there.
import assert from 'node:assert/strict';
import { existsSync, readFileSync } from 'node:fs';
import { basename, join, resolve } from 'node:path';
import { parseArgs } from 'node:util';
import { TYPE_BLEND0, blendByte, loadFromPackage, parseBlendLayer, type BlendLayer } from '../rtdata-decode.ts';

const ROOT = resolve(import.meta.dirname, '..', '..');
type Pt = { x: number; z: number; ans: number; lot: number; hb: number };
type Grid = { errors: string[]; cells?: number; expectCells?: number; records?: number; nonEmpty?: number; elemCount?: number; noLot?: number; maxPerCell?: number; top?: number[][]; keys?: number; bounds?: number[] };
type Res = { reason: string; seconds: number; refused: Record<string, string>; chained: Record<string, any>; simTid: number; errors: string[];
  dumps: { which: string; tid: number; grid: Grid; points: { points?: Pt[]; skipped?: string; error?: string; bounds?: number[] } }[];
  o58: { calls: number; byCaller: Record<string, { calls: number; nonEmpty: number; hist: Record<string, number>; added: number }>; idsIsPath60: number; idsOther: number; rows: any[] };
  plan: { calls: number; threads: Record<string, number>; beyondFarZ: number; inG: number; noStart: number; rows: any[]; routeListNonEmpty: number };
  grid: Record<'gridFill' | 'gridRemove', { calls: number; threads: Record<string, number>; callers: Record<string, number>; ecxIsP: number; offSim: { tid: number; caller: string; inBuild: boolean }[] }>;
  follow38: { calls: number; threads: Record<string, number>; ras: Record<string, number>; bt: { ra: string; frames: string[] }[] };
  houseboat: { calls: number; rows: { caller: string; flag: number; corners: number[][]; maxZ: number }[] } };

const top = (o: Record<string, number>, n = 6) => Object.entries(o).sort((a, b) => b[1] - a[1]).slice(0, n).map(([k, v]) => `${k}:${v}`).join(' ');
// routable answer of 0xC6BAB0 for a point inside the bounds: weight < 0.5 at floor(x), floor(z), or a lot there (docs/107 §11.2)
export const expectRoutable = (m: BlendLayer, p: Pt) => (blendByte(m, Math.floor(p.x), Math.floor(p.z)) < 128 || p.lot === 1 ? 1 : 0);

export function verdicts(r: Res, opt: { mask?: BlendLayer | null; maskName?: string; fnName?: (a: number) => string } = {}): string[] {
  const out: string[] = [];
  // I-107-15 / I-107-17 / I-107-16
  const o = r.o58, ne = Object.values(o.byCaller).reduce((a, c) => a + c.nonEmpty, 0), pc = o.byCaller['Route_PlanCommon'];
  if (!o.calls) out.push('I-107-15: no verdict (no 0x85FF30 call)');
  else out.push(`I-107-15: ${ne ? 'verified' : 'REFUTED (always empty: is getter 0x545170 +0x58 on this object?)'}, O+0x58 non-empty after ${ne} of ${o.calls} EditPathForRoute calls` +
    `${pc ? ` (Route_PlanCommon ${pc.nonEmpty}/${pc.calls}, ${(100 * pc.nonEmpty / pc.calls).toFixed(1)} %)` : ''}; ids vector = path+0x60 in ${o.idsIsPath60} of ${o.idsIsPath60 + o.idsOther} calls`);
  for (const [k, c] of Object.entries(o.byCaller)) out.push(`  ${k}: ${c.calls} calls, ${c.nonEmpty} non-empty, ids added ${c.added}; count histogram ${top(c.hist, 10)}`);
  const withF = o.rows.filter(x => x.hasFollower).length;
  out.push(`I-107-17: open (ids logged, object kinds not resolved here): ${o.rows.length} non-empty lists kept; follower's own id in ${withF}; ids per list ${top(o.rows.reduce((a: Record<string, number>, x) => (a[x.n] = (a[x.n] ?? 0) + 1, a), {}), 6)}; first ${o.rows.slice(0, 3).map(x => `[${x.ids.join(' ')}] follower ${x.follower}`).join('; ')}`);
  out.push(`I-107-16: ${!r.plan.calls ? 'no verdict (no Route_PlanCommon call)' : r.plan.routeListNonEmpty ? `REFUTED, route+0x158+0x58 non-empty at ${r.plan.routeListNonEmpty} PlanCommon returns (a writer was missed)` : `verified live, route+0x158+0x58 empty at all ${r.plan.calls} PlanCommon returns`}`);
  // I-107-19
  for (const d of r.dumps) {
    const g = d.grid;
    if (g.cells === undefined) { out.push(`I-107-19 (${d.which}): no grid read (${g.errors.join('; ')})`); continue; }
    const sizeOk = g.cells === g.expectCells;
    out.push(`I-107-19 (${d.which}): ${sizeOk ? 'size verified' : g.cells === 0 ? 'grid empty (not sized yet: lazy size on the first insert)' : 'REFUTED size'}: ${g.cells} cells, formula ${g.expectCells} from bounds ${g.bounds?.join(',')}; ` +
      `${g.records} records in ${g.nonEmpty} cells (max ${g.maxPerCell} per cell, ${g.keys} distinct keys, ${g.noLot} with no lot); +0x0C element counts sum ${g.elemCount} (${g.elemCount === g.records ? '= walked records' : 'differs from the walk: +0x0C is not the count'}); busiest cells ${g.top?.slice(0, 6).map(([i, n]) => `${i}:${n}`).join(' ')}${g.errors.length ? `; errors ${g.errors.join('; ')}` : ''}`);
  }
  if (!r.dumps.length) out.push('I-107-19: no dump taken');
  // I-107-20
  const sim = r.simTid;
  for (const fn of ['gridFill', 'gridRemove'] as const) {
    const g = r.grid[fn], off = Object.entries(g.threads).filter(([t]) => Number(t) !== sim), offN = off.reduce((a, [, n]) => a + n, 0), notBuild = g.offSim.filter(x => !x.inBuild);
    const v = !g.calls ? 'no calls' : !sim ? 'no verdict (sim thread unknown: no 0x85FF30 call)' : !offN ? 'verified: every call on the sim thread' : notBuild.length ? `REFUTED: ${notBuild.length} of ${g.offSim.length} sampled off-sim calls without LotGraph_Build on the stack (callers ${[...new Set(notBuild.map(x => x.caller))].join(', ')})` : `verified: ${offN} off-sim calls, every sampled one (${g.offSim.length}) under LotGraph_Build`;
    out.push(`I-107-20 ${fn === 'gridFill' ? '0x636BF0' : '0x62D9F0'}: ${v}; ${g.calls} calls, threads ${top(g.threads)} (sim ${sim}), ecx = P in ${g.ecxIsP}; callers ${top(g.callers, 10)}`);
  }
  // I-107-21
  const pts = r.dumps.flatMap(d => (d.points.points ?? []).map(p => ({ ...p, which: d.which })));
  if (!pts.length) out.push(`I-107-21: no points (${r.dumps.map(d => d.points.skipped ?? d.points.error).join('; ') || 'no dump'})`);
  else if (!opt.mask) out.push(`I-107-21: ${pts.length} answers logged, no world file to compare (pass --world): ${pts.slice(0, 20).map(p => `(${p.x},${p.z})=${p.ans}${p.lot ? 'L' : ''}`).join(' ')}`);
  else {
    const bad = pts.filter(p => expectRoutable(opt.mask!, p) !== p.ans), onMask = pts.filter(p => blendByte(opt.mask!, Math.floor(p.x), Math.floor(p.z)) >= 128);
    out.push(`I-107-21: ${bad.length ? 'REFUTED' : 'verified'} against ${opt.maskName}: ${pts.length - bad.length} of ${pts.length} answers match (${onMask.length} points on the mask, ${pts.filter(p => p.lot).length} on a lot)` +
      `${bad.length ? `; mismatches ${bad.slice(0, 6).map(p => `(${p.x},${p.z}) live ${p.ans} byte ${blendByte(opt.mask!, Math.floor(p.x), Math.floor(p.z))} lot ${p.lot}`).join(' ')}` : ''}${onMask.length && onMask.length < pts.length ? '' : ' (points all on one side of the mask: weak check)'}`);
  }
  // I-107-24
  const f = r.follow38, fn = opt.fnName ?? ((a: number) => '');
  out.push(`I-107-24: ${f.calls ? `${Object.keys(f.ras).length} return address(es) for ${f.calls} calls on thread(s) ${top(f.threads)}: ${top(f.ras, 8).split(' ').map(s => { const a = parseInt(s, 16); const n = fn(a); return n ? `${s} (${n})` : s; }).join(' ')}` : 'no call to 0x853820 in the window (slot +0x38 idle here; I-107-23 says "never called = dead in vanilla")'}`);
  for (const b of f.bt) out.push(`  stack from ${b.ra}: ${b.frames.map(x => { const n = fn(parseInt(x, 16)); return n ? `${x} (${n})` : x; }).join(' < ')}`);
  // I-100-13
  const hbIn = r.plan.rows.filter(x => x.hbLot >= 0).length;
  out.push(`I-100-13: ${r.plan.beyondFarZ ? `verified (lot slot frame): ${r.plan.beyondFarZ} of ${r.plan.calls} plans start beyond WM+0xC0, where only houseboat slots lie (${hbIn} inside the corners of a houseboat lot parented in the window); followers ${[...new Set(r.plan.rows.map(x => x.follower))].slice(0, 6).join(' ')}` : `open: no plan started beyond WM+0xC0 in ${r.plan.calls} plans (no Sim planned on board in the window, or on-board routes use the display frame; check that the repro steps ran)`}`);
  out.push(`  SetHouseboatParent calls ${r.houseboat.calls}${r.houseboat.rows.length ? `: ${r.houseboat.rows.slice(0, 4).map(h => `${h.caller} flag ${h.flag} corners ${JSON.stringify(h.corners)} maxZ ${h.maxZ}`).join('; ')}` : ''}; plans without start points ${r.plan.noStart}`);
  if (Object.keys(r.refused).length) out.push(`refused: ${JSON.stringify(r.refused)}`);
  if (Object.keys(r.chained).length) out.push(`chained on an existing jump: ${JSON.stringify(r.chained)}`);
  if (r.errors.length) out.push(`probe errors (${r.errors.length}): ${r.errors.slice(0, 3).join(' | ')}`);
  out.push(`run: ${r.reason}, ${r.seconds} s, dumps ${r.dumps.map(d => d.which).join(' ') || 'none'}`);
  return out;
}

function logic() {
  const src = readFileSync(join(ROOT, 'tools', 'frida', 'live-reads2.js'), 'utf8');
  const block = src.slice(src.indexOf('// --- logic begin'), src.indexOf('// --- logic end'));
  assert.ok(block.includes('function gridDump'), 'logic block not found in live-reads2.js');
  return new Function(`${block}; return { gridDump, pointGrid, corners, inCorners };`)();
}

function selfCheck() {
  const L = logic();
  const B = 0x10000000, buf = Buffer.alloc(1 << 16);
  let topA = 0x100;
  const alloc = (n: number) => { const a = B + topA; topA += (n + 15) & ~15; return a; };
  const w32 = (a: number, v: number) => buf.writeUInt32LE(v >>> 0, a - B), wf = (a: number, v: number) => buf.writeFloatLE(v, a - B);
  const mem = { u8: (a: number) => buf.readUInt8(a - B), u32: (a: number) => buf.readUInt32LE(a - B), f32: (a: number) => buf.readFloatLE(a - B) };
  // world 0..600 x 0..300 -> 3 x 2 = 6 cells; cell 0: 2 buckets, chain of 2 + 1 record (one without a lot); cell 4: 1 record; sentinel ~0
  const wg = alloc(4), wmg = alloc(4), world = alloc(0x960), P = alloc(0xf0), wm = alloc(0xd0), cells = alloc(32 * 6);
  w32(wg, world); w32(world + 0x95c, P); w32(wmg, wm); [0, 0, 600, 300].forEach((v, i) => wf(wm + 0xb4 + 4 * i, v));
  w32(P + 0xdc, cells); w32(P + 0xe0, cells + 32 * 6); w32(P + 0xe4, cells + 32 * 6);
  const rec = (lot: number, key: number, next = 0) => { const r = alloc(0x50); w32(r, key); w32(r + 0x40, lot); w32(r + 0x48, next); return r; };
  const bk0 = alloc(12), bk4 = alloc(8);
  w32(bk0, rec(7, 1, rec(7, 2))); w32(bk0 + 4, rec(0, 3)); w32(bk0 + 8, 0xffffffff); w32(cells + 4, bk0); w32(cells + 8, 2); w32(cells + 0xc, 3);
  w32(bk4, rec(9, 1)); w32(bk4 + 4, 0xffffffff); w32(cells + 4 * 32 + 4, bk4); w32(cells + 4 * 32 + 8, 1); w32(cells + 4 * 32 + 0xc, 1);
  const g = L.gridDump(mem, { routingWorld: wg, wm: wmg });
  assert.deepEqual(g.errors, []);
  assert.deepEqual([g.cells, g.expectCells, g.records, g.nonEmpty, g.maxPerCell, g.noLot, g.elemCount, g.keys], [6, 6, 4, 2, 3, 1, 4, 3]);
  assert.deepEqual(g.top, [[0, 3], [4, 1]]);
  w32(P + 0xe0, cells + 32 * 6 + 4); // a vector that is not whole cells: error, no walk
  assert.match(L.gridDump(mem, { routingWorld: wg, wm: wmg }).errors[0], /grid vector/);
  w32(P + 0xe0, cells + 32 * 6);
  w32(cells + 2 * 32 + 4, B + (1 << 20)); w32(cells + 2 * 32 + 8, 1); // a bad bucket pointer: one error entry
  assert.ok(L.gridDump(mem, { routingWorld: wg, wm: wmg }).errors.length === 1);
  const pts = L.pointGrid([0, 0, 600, 300]);
  assert.equal(pts.length, 20); assert.ok(pts.every(([x, z]: number[]) => x > 0 && x < 600 && z > 0 && z < 300));
  const lot = alloc(0x170); [[10, 2100], [30, 2100], [30, 2120], [10, 2120]].forEach(([x, z], i) => { wf(lot + 0x134 + 8 * i, x); wf(lot + 0x138 + 8 * i, z); });
  const c = L.corners(mem, lot);
  assert.deepEqual(c, [[10, 2100], [30, 2100], [30, 2120], [10, 2120]]);
  assert.ok(L.inCorners(c, 20, 2110) && !L.inCorners(c, 20, 2050));
  // verdict rules; mask 64 x 64 m, block (0,0) = 255 (16 x 16 m), rest 0
  const mask = { version: 0x300, w: 64, h: 64, bx: 4, bz: 4, layer: 0, blocks: [255, ...Array(15).fill(0)], uniform: 16, raw: 0 } as BlendLayer;
  const P0 = { x: 5, z: 5, ans: 0, lot: 0, hb: 0 }, P1 = { x: 5, z: 5, ans: 1, lot: 1, hb: 0 }, P2 = { x: 40, z: 40, ans: 1, lot: 0, hb: 0 };
  const res: Res = { reason: 'complete', seconds: 300, refused: {}, chained: {}, simTid: 7, errors: [],
    dumps: [{ which: 'start', tid: 7, grid: g, points: { points: [P0, P1, P2] } }],
    o58: { calls: 10, idsIsPath60: 10, idsOther: 0, byCaller: { Route_PlanCommon: { calls: 10, nonEmpty: 3, hist: { 0: 7, 1: 3 }, added: 3 } }, rows: [{ n: 1, ids: ['0x0:0x5'], follower: '0x0:0x9', hasFollower: false }] },
    plan: { calls: 10, threads: { 7: 10 }, beyondFarZ: 0, inG: 10, noStart: 0, rows: [], routeListNonEmpty: 0 },
    grid: { gridFill: { calls: 4, threads: { 7: 3, 9: 1 }, callers: {}, ecxIsP: 4, offSim: [{ tid: 9, caller: 'LotGraph_Build_0x63CB90', inBuild: true }] },
      gridRemove: { calls: 2, threads: { 7: 1, 9: 1 }, callers: {}, ecxIsP: 2, offSim: [{ tid: 9, caller: '0x401000', inBuild: false }] } },
    follow38: { calls: 5, threads: { 7: 5 }, ras: { '0x84a000': 5 }, bt: [] }, houseboat: { calls: 0, rows: [] } };
  let v = verdicts(res, { mask, maskName: 'fake' }).join('\n');
  assert.match(v, /I-107-15: verified, O\+0x58 non-empty after 3 of 10/);
  assert.match(v, /I-107-16: verified live/);
  assert.match(v, /I-107-19 \(start\): size verified: 6 cells, formula 6/);
  assert.match(v, /= walked records/);
  assert.match(v, /I-107-20 0x636BF0: verified: 1 off-sim calls, every sampled one/);
  assert.match(v, /I-107-20 0x62D9F0: REFUTED: 1 of 1/);
  assert.match(v, /I-107-21: verified against fake: 3 of 3/);
  assert.match(v, /I-107-24: 1 return address/);
  assert.match(v, /I-100-13: open: no plan started beyond/);
  res.dumps[0].points.points = [{ ...P0, ans: 1 }, P2];
  res.o58.byCaller.Route_PlanCommon.nonEmpty = 0;
  res.plan = { ...res.plan, beyondFarZ: 2, routeListNonEmpty: 1, rows: [{ follower: '0x0:0x9', hbLot: 0 }, { follower: '0x0:0x9', hbLot: -1 }] };
  res.follow38 = { calls: 0, threads: {}, ras: {}, bt: [] };
  v = verdicts(res, { mask, maskName: 'fake' }).join('\n');
  assert.match(v, /I-107-15: REFUTED \(always empty/);
  assert.match(v, /I-107-16: REFUTED/);
  assert.match(v, /I-107-21: REFUTED against fake: 1 of 2/);
  assert.match(v, /I-107-24: no call to 0x853820/);
  assert.match(v, /I-100-13: verified \(lot slot frame\): 2 of 10 plans start beyond WM\+0xC0, where only houseboat slots lie \(1 inside the corners/);
  console.log('live-reads2-report self-check: PASS');
}

function functionNamer(gc: string) {
  const f = join(gc, 'analysis', 'functions.csv');
  if (!existsSync(f)) return undefined;
  const fs = readFileSync(f, 'utf8').split(/\r?\n/).map(l => l.split(',')).filter(p => /^0x/.test(p[0])).map(p => [parseInt(p[0], 16), parseInt(p[1], 16), p[2]] as [number, number, string]).sort((a, b) => a[0] - b[0]);
  return (a: number) => { let lo = 0, hi = fs.length - 1, best = -1; while (lo <= hi) { const m = (lo + hi) >> 1; if (fs[m][0] <= a) { best = m; lo = m + 1; } else hi = m - 1; } return best >= 0 && a <= fs[best][1] ? `${fs[best][2]}+0x${(a - fs[best][0]).toString(16)}` : ''; };
}

const { values, positionals } = parseArgs({ allowPositionals: true, options: { 'self-check': { type: 'boolean' }, world: { type: 'string' }, 'game-copy': { type: 'string' } } });
if (values['self-check']) selfCheck();
else {
  if (!positionals.length) { console.error('usage: node tools/harness/live-reads2-report.ts <tag>-livereads2.json [...] [--world <file.world>] [--game-copy <dir>] | --self-check'); process.exit(2); }
  const gc = values['game-copy'] ?? process.env.GAME_COPY ?? join(ROOT, 'game-copy'), fnName = functionNamer(gc);
  const masks = new Map<string, BlendLayer | null>();
  for (const f of positionals) {
    const tag = basename(f), auto = /-bp-/.test(tag) ? 'Bridgeport' : /-ip-/.test(tag) ? 'IslaParadiso' : /-sv-/.test(tag) ? 'Sunset Valley' : '';
    const wf = values.world ?? (auto ? join(gc, 'worlds', `${auto}.world`) : '');
    if (wf && !masks.has(wf)) masks.set(wf, existsSync(wf) ? parseBlendLayer(loadFromPackage(wf, TYPE_BLEND0)) : null);
    console.log(f);
    for (const l of verdicts(JSON.parse(readFileSync(f, 'utf8')), { mask: wf ? masks.get(wf) : null, maskName: basename(wf), fnName })) console.log(`  ${l}`);
  }
}
