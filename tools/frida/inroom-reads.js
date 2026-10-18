// In-room routing live reads (docs/111): read-only Frida script for TS3W.exe (Steam 1.67), no game state is written.
// Closes docs/51 §47: I-98-15 (walkable cells on two opposite quadtree root borders = a usable wrap edge), I-98-21 / I-101-4
// (lot rectangle and cell size c passed to LotQuadTree_SetBounds 0x6646F0, planner +0xB8, static 0x11CF02C), U6 (context flag
// words X+0x50 per in-room caller) and U7 (X+0x4C at in-room entry).
//
//   python -m frida_tools.repl -n TS3W.exe -l tools/frida/inroom-reads.js -P '{"out":"probe-out/profile/x","minutes":5}' -q -t 300
//   (profile-world.ts --inroom does this at the window start; queue job option run.profile.inroom)
// Output: <out>-inroom.json, read by tools/harness/inroom-report.ts. Progress line every 30 s on stdout.
//
// Hooks (prologue bytes checked first; a hook whose bytes differ is refused and listed, never patched half-way):
//   InRoomPlan 0x632E40 and InRoomPlanToPoint 0x632630 (onEnter: X = first stack argument; flags X+0x50, X+0x4C, caller;
//   onLeave: X+0x4C again), LotQuadTree_SetBounds 0x6646F0 (ecx = tree, 5 float arguments; onLeave: the tree's bounds,
//   root side and levels), LotRouting_QuadTreeBuild 0x62F8A0 (ecx and its +0xB8).
// Tree dumps (I-98-15) run inside the InRoomPlan/ToPoint onEnter, on the thread that plans (the sim thread with
// [RouteWorker]/[RouteAsync] off, as the live-reads jobs set them): no routing write is in progress there, so the walk sees a
// consistent tree. One dump at the window start, one ~20 s before the end. Every read is guarded (a bad pointer = an error
// entry, not a crash). The walk visits only cells that touch the root border.
'use strict';

const cfg = { out: null, minutes: 5, addrs: null };
const A = { world: 0x11d9128, cellStatic: 0x11cf02c };
const HOOKS = {
  inRoomPlan: [0x632e40, '55 8b ec 83 e4 f0 81 ec'],
  inRoomToPoint: [0x632630, '81 ec 80 00 00 00 55 8b'],
  setBounds: [0x6646f0, '51 8b 44 24 28 f3 0f 10'],
  quadTreeBuild: [0x62f8a0, '8b 44 24 08 83 ec 14 53'],
};
// callers of the in-room entry points (docs/98 §2); a return address maps to the nearest start below it within 0x2000
const CALLERS = [[0x63f6e0, 'LotPlan_Inner'], [0x633fa0, 'RoomProbe_0x633FA0'], [0x635080, 'Fn_0x635080']];

// --- walker begin: pure logic over mem = {u8, u32, s32, f32}(address number); no Frida API (inroom-report.ts --self-check runs it in node)
const FLT_MAX = 3.4028234663852886e38;
const WALKABLE = c => c === 0 || c === 1 || c === 13;
// sub-cell shapes (docs/98 §3.2): 0 whole, 1-4 halves, 5-20 quarters (4x4), 21-84 eighths (8x8), row-major, x = column
function shapeRect(s) {
  if (s === 0) return [0, 0, 1];
  if (s <= 4) return [0.5 * ((s - 1) & 1), 0.5 * ((s - 1) >> 1), 0.5];
  if (s <= 20) return [0.25 * ((s - 5) % 4), 0.25 * ((s - 5) >> 2), 0.25];
  return [0.125 * ((s - 21) % 8), 0.125 * ((s - 21) >> 3), 0.125];
}
function childShape(s, k) {
  const [ox, oz, size] = shapeRect(s), h = size / 2, x = ox + h * (k & 1), z = oz + h * (k >> 1);
  if (s === 0) return 1 + k;
  if (s <= 4) return 5 + Math.round(z * 4) * 4 + Math.round(x * 4);
  if (s <= 20) return 21 + Math.round(z * 8) * 8 + Math.round(x * 8);
  return -1;
}
const bitOfShape = s => { const [ox, oz] = shapeRect(s); return Math.round(oz * 8) * 8 + Math.round(ox * 8); };
const maskHas = (lo, hi, b) => (b < 32 ? (lo >>> b) & 1 : (hi >>> (b - 32)) & 1) === 1;

// class and multiplier slot of one footprint for the plain context (no agent, flags 0x10000), by the slot targets of
// docs/98 §5.1 / §15.3; dispatch on the slot address, not the vtable, so a copy of the vtable reads the same
function slotClass(mem, fp, sub, type, note) {
  const vt = mem.u32(fp), cs = mem.u32(vt + 8), b = mem.u8(fp + 0x34);
  switch (cs) {
    case 0x807090: if (mem.u32(fp + 0x20)) note.gated++; return b;           // no agent: gate not consulted [verified offline]
    case 0x80f840: note.oneAgent++; return 0;                                  // private footprint, no agent: 0 [verified offline]
    case 0x8066c0: return b;                                                   // 0 only for the agent's own object
    case 0x6119f0: return 0;
    case 0x807b70: return type === 0x1e ? 10 : b;                              // no options: water blocks, ponds use base
    case 0x80c300: note.inferred++; return (mem.u8(fp + 0x41) & 0xc) === 0xc ? 0 : [8, b, 8][sub] ?? 0;
    default: note.unknownSlots[`c${cs.toString(16)}`] = 1; return b;
  }
}
function slotMult(mem, fp, sub, type, note) {
  const vt = mem.u32(fp), ms = mem.u32(vt + 4), m = mem.f32(fp + 0x1c);
  switch (ms) {
    case 0x806f70: case 0x620dc0: return m;
    case 0x80f7d0: return 1.0;
    case 0x80c3d0: return sub === 0 || sub === 2 ? 1.0 : m;
    case 0x807c20: return type === 0x1e ? FLT_MAX : m;
    default: note.unknownSlots[`m${ms.toString(16)}`] = 1; return m;
  }
}
function entriesOf(mem, node) {
  const v = mem.u32(node + 8);
  if (v & 1) {
    const arr = v & ~1, n = mem.u32(node + 0xc), out = [];
    if (n > 4096) throw new Error(`leaf 0x${node.toString(16)}: ${n} entries`);
    for (let i = 0; i < n; i++) out.push([mem.u32(arr + 12 * i), mem.u32(arr + 12 * i + 4), mem.u32(arr + 12 * i + 8)]);
    return out;
  }
  return v ? [[v, mem.u32(node + 0xc), mem.u32(node + 0x10)]] : [];
}
// docs/98 §5.2 for one final cell (node 0 = empty quadrant), §5.3 adds nothing for the plain context
function cellClass(mem, tree, node, shape, note) {
  const defCls = mem.s32(tree + 0x2c), def = () => [defCls, WALKABLE(defCls) ? mem.f32(tree + 0x30) : FLT_MAX, 0];
  if (!node) return def();
  const masked = shape !== 0, bit = bitOfShape(shape);
  let cls = 0, mult = 1.0, any = false, keep2 = false, s16 = null, s0a = null, n = 0;
  for (const [fme, lo, hi] of entriesOf(mem, node)) {
    if (masked && !maskHas(lo, hi, bit)) continue;
    const fp = mem.u32(fme + 8), fl = mem.u32(fme + 0x10), sub = fl & 0xff;
    if (!fp) continue;
    const vt = mem.u32(fp);
    if (mem.u32(vt + 8) === 0x6119f0 && mem.u8(fp + 0x48)) continue;         // 0xFEAE08 ignore byte
    if (mem.u32(vt + 0xc) !== 0x624450) note.unknownSlots[`t${mem.u32(vt + 0xc).toString(16)}`] = 1;
    const type = mem.u8(fp + 0x35);
    n++;
    if (masked && (fl & 0x800)) { cls = slotClass(mem, fp, sub, type, note); mult = slotMult(mem, fp, sub, type, note); any = true; break; }
    if (masked && (fl & 0x400)) { any = true; continue; }
    if (type === 0x16) { s16 = [slotClass(mem, fp, sub, type, note), slotMult(mem, fp, sub, type, note)]; any = true; continue; }
    if (type === 0x0a) { s0a = [slotClass(mem, fp, sub, type, note), slotMult(mem, fp, sub, type, note)]; any = true; continue; }
    if (type === 0x1f) { any = true; keep2 = true; continue; }
    let c = slotClass(mem, fp, sub, type, note), m = slotMult(mem, fp, sub, type, note);
    if (type === 0x14) { c = 1; m = 100000; }
    cls = Math.max(cls, c); mult = Math.max(mult, m); any = true;
  }
  const s = s16 ?? s0a;
  if (s) { cls = Math.max(cls, s[0]); mult = Math.max(mult, s[1]); }
  if (cls === 2 && !keep2) cls = 0;
  if (!WALKABLE(cls)) mult = FLT_MAX;
  return any ? [cls, mult, n] : def();
}
// final cells that touch the root square's border, root units [0,1); node kind: bits 0-1 of +0x04 (1 internal), final mask bits 3-23
function borderCells(mem, tree, note) {
  const out = [], levels = mem.s32(tree + 0x40);
  const touches = (x, z, w) => x === 0 || z === 0 || x + w === 1 || z + w === 1;
  const emit = (node, shape, x, z, w, depth) => {
    const [cls, mult, n] = cellClass(mem, tree, node, shape, note);
    out.push({ x0: x, z0: z, x1: x + w, z1: z + w, depth, shape, empty: !node, cls, mult, n, walk: WALKABLE(cls) });
  };
  const inLeaf = (node, s, x, z, w, depth, mask) => {
    if (s >= 21 || ((mask >>> (s + 3)) & 1)) return emit(node, s, x, z, w, depth);
    for (let k = 0; k < 4; k++) {
      const h = w / 2, cx = x + h * (k & 1), cz = z + h * (k >> 1);
      if (touches(cx, cz, h)) inLeaf(node, childShape(s, k), cx, cz, h, depth, mask);
    }
  };
  const visit = (node, x, z, w, depth) => {
    if (!node) return emit(0, 0, x, z, w, depth);
    if (depth > Math.max(levels, 1) + 2 || depth > 16) throw new Error(`tree 0x${tree.toString(16)}: depth ${depth} > levels ${levels}`);
    const hdr = mem.u32(node + 4);
    if ((hdr & 3) !== 1) return inLeaf(node, 0, x, z, w, depth, hdr);
    for (let k = 0; k < 4; k++) {
      const h = w / 2, cx = x + h * (k & 1), cz = z + h * (k >> 1);
      if (touches(cx, cz, h)) visit(mem.u32(node + 0xc + 4 * k), cx, cz, h, depth + 1);
    }
  };
  visit(mem.u32(tree + 0x24), 0, 0, 1, 0);
  return out;
}
// wrap pairs of docs/98 §15.1: a walkable cell on side d and a walkable cell on the opposite border whose spans overlap
function wrapPairs(cells) {
  const pairs = [], side = { mx: c => c.x0 === 0, px: c => c.x1 === 1, mz: c => c.z0 === 0, pz: c => c.z1 === 1 };
  const counts = {};
  for (const [k, f] of Object.entries(side)) { const s = cells.filter(f); counts[k] = { cells: s.length, walkable: s.filter(c => c.walk).length }; }
  for (const [a, b, lo, hi] of [['mx', 'px', 'z0', 'z1'], ['mz', 'pz', 'x0', 'x1']])
    for (const p of cells.filter(c => c.walk && side[a](c)))
      for (const q of cells.filter(c => c.walk && side[b](c)))
        if (Math.min(p[hi], q[hi]) - Math.max(p[lo], q[lo]) > 0) pairs.push({ axis: a[1], a: p, b: q });
  return { counts, pairs };
}
function dumpTrees(mem, addrs) {
  const r = { lots: [], errors: [] };
  try { dumpInto(mem, addrs, r); } catch (err) { r.errors.push(String(err.message ?? err)); } // a bad table pointer: keep what was read
  return r;
}
function dumpInto(mem, addrs, r) {
  const w = mem.u32(addrs.world), P = w ? mem.u32(w + 0x95c) : 0;
  Object.assign(r, { world: w, P, cellStatic: { u32: mem.u32(addrs.cellStatic), f32: mem.f32(addrs.cellStatic) } });
  if (!P) { r.errors.push('no routing world or planner'); return; }
  r.pB8 = { u32: mem.u32(P + 0xb8), f32: mem.f32(P + 0xb8) };
  const buckets = mem.u32(P + 0x4c), nb = mem.u32(P + 0x50);
  if (nb > 1 << 20) { r.errors.push(`bucket count ${nb}`); return; }
  const end = mem.u32(buckets + 4 * nb);
  let seen = 0;
  for (let i = 0; i < nb; i++)
    for (let node = mem.u32(buckets + 4 * i); node && node !== end; node = mem.u32(node + 0x190)) {
      if (++seen > 4096) { r.errors.push('more than 4096 lot nodes'); return; }
      const e = node + 0x10, lot = { key: [mem.u32(node), mem.u32(node + 4)], baseLevel: mem.s32(e + 0x40), trees: [] };
      const tb = mem.u32(e + 0x74), te = mem.u32(e + 0x78);
      for (let t = tb, li = 0; t < te && li < 32; t += 4, li++) {
        const tree = mem.u32(t), tr = { idx: li };
        if (!tree) { lot.trees.push({ idx: li, missing: true }); continue; }
        try {
          Object.assign(tr, { level: mem.s32(tree + 0x44), bounds: [0x4, 0x8, 0xc, 0x10].map(o => mem.f32(tree + o)), side: mem.f32(tree + 0x38),
            levels: mem.s32(tree + 0x40), defCls: mem.s32(tree + 0x2c), defMult: mem.f32(tree + 0x30) });
          const note = { gated: 0, oneAgent: 0, inferred: 0, unknownSlots: {} };
          const cells = borderCells(mem, tree, note), wp = wrapPairs(cells);
          Object.assign(tr, { borderCells: cells.length, sides: wp.counts, wrapPairs: wp.pairs.length,
            examples: wp.pairs.slice(0, 4).map(p => ({ axis: p.axis, a: [p.a.x0, p.a.z0, p.a.x1, p.a.z1, p.a.cls], b: [p.b.x0, p.b.z0, p.b.x1, p.b.z1, p.b.cls] })),
            walkBorder: cells.filter(c => c.walk).slice(0, 8).map(c => [c.x0, c.z0, c.x1, c.z1, c.cls, c.n, c.empty ? 1 : 0]), note });
        } catch (err) { tr.error = String(err.message ?? err); }
        lot.trees.push(tr);
      }
      r.lots.push(lot);
    }
}
// --- walker end

let t0 = 0, done = false, timers = [], hooked = [], hookCount = 0, refused = {}, lastProgress = { plans: 0 }, misses = 0;
const st = { plans: 0, threads: {}, byCaller: {}, flagBits: {}, builds: 0, buildRows: [], buildAgg: {}, qtb: [], dumps: [], want: 'start', errors: [] };
const mem = {
  u8: a => ptr(a).readU8(), u32: a => ptr(a).readU32(), s32: a => ptr(a).readS32(), f32: a => ptr(a).readFloat(),
};
const scratch = Memory.alloc(4); // float bits of a stack argument (setBounds runs on the sim thread only)
const hex = x => '0x' + (x >>> 0).toString(16);
const r4 = x => Math.round(x * 1e4) / 1e4;
const nowS = () => Math.round((Date.now() - t0) / 100) / 10;
function callerOf(ra) {
  let best = null;
  for (const [a, n] of CALLERS) if (ra >= a && ra - a < 0x2000 && (!best || a > best[0])) best = [a, n];
  return best ? best[1] : hex(ra);
}
function bump(o, k, n = 1) { o[k] = (o[k] || 0) + n; }

function onInRoom(kind) {
  return {
    onEnter(args) {
      try {
        const X = args[0].toUInt32(), ra = this.returnAddress.toUInt32(), tid = Process.getCurrentThreadId();
        const flags = mem.u32(X + 0x50), c4c = mem.s32(X + 0x4c), agent = mem.u32(X + 0x40);
        st.plans++; bump(st.threads, tid);
        const key = `${kind}@${callerOf(ra)}`, c = st.byCaller[key] ??= { n: 0, ras: {}, flags: {}, x4c: {}, x4cChanged: 0, noAgent: 0 };
        c.n++; bump(c.ras, hex(ra)); bump(c.flags, hex(flags)); bump(c.x4c, String(c4c)); if (!agent) c.noAgent++;
        for (let b = 0; b < 32; b++) if ((flags >>> b) & 1) bump(st.flagBits, hex(1 << b));
        this.x = X; this.c4c = c4c; this.c = c;
        if (st.want) {
          const which = st.want, t = Date.now();
          st.want = null;
          const d = dumpTrees(mem, cfg.addrs ?? A);
          st.dumps.push(Object.assign(d, { which, t: nowS(), tid, ms: Date.now() - t }));
          console.log(`[inroom] dump ${which}: ${d.lots.length} lots, ${d.lots.reduce((a, l) => a + l.trees.length, 0)} trees, ${Date.now() - t} ms, errors ${d.errors.length}`);
          if (which === 'end') setTimeout(finish, 0);
        }
      } catch (e) { if (st.errors.length < 50) st.errors.push(`${kind} enter: ${e.message}`); }
    },
    onLeave() {
      try { if (this.x && mem.s32(this.x + 0x4c) !== this.c4c) this.c.x4cChanged++; } catch (e) { /* X gone: no count */ }
    },
  };
}
const setBoundsListener = {
  onEnter(args) {
    try {
      this.tree = this.context.ecx.toUInt32();
      this.a = [0, 1, 2, 3, 4].map(i => { scratch.writeU32(args[i].toUInt32()); return scratch.readFloat(); });
      this.u = [5, 6, 7].map(i => args[i].toUInt32());
      this.ra = this.returnAddress.toUInt32();
    } catch (e) { this.tree = 0; if (st.errors.length < 50) st.errors.push(`setBounds enter: ${e.message}`); }
  },
  onLeave() {
    if (!this.tree) return;
    try {
      const t = this.tree, [x0, z0, x1, z1, c] = this.a;
      const row = { t: nowS(), ra: hex(this.ra), args: this.a.map(r4), extra: this.u.map(hex),
        lotRect: [r4(x0 + c), r4(z0 + c), r4(x1 - c), r4(z1 - c)], bounds: [0x4, 0x8, 0xc, 0x10].map(o => r4(mem.f32(t + o))),
        side: r4(mem.f32(t + 0x38)), levels: mem.s32(t + 0x40), cellStatic: r4(mem.f32((cfg.addrs ?? A).cellStatic)) };
      st.builds++;
      if (st.buildRows.length < 400) st.buildRows.push(row);
      bump(st.buildAgg, `c=${row.args[4]} min=${row.lotRect[0]},${row.lotRect[1]} boundsMin=${row.bounds[0]},${row.bounds[1]}`);
    } catch (e) { if (st.errors.length < 50) st.errors.push(`setBounds leave: ${e.message}`); }
  },
};
const qtbListener = {
  onEnter(args) {
    try {
      if (st.qtb.length >= 200) return;
      const o = this.context.ecx.toUInt32(), w = mem.u32((cfg.addrs ?? A).world), P = w ? mem.u32(w + 0x95c) : 0;
      st.qtb.push({ t: nowS(), ecxIsP: o === P, ecxB8: { u32: hex(mem.u32(o + 0xb8)), f32: r4(mem.f32(o + 0xb8)) },
        cellStatic: { u32: hex(mem.u32((cfg.addrs ?? A).cellStatic)), f32: r4(mem.f32((cfg.addrs ?? A).cellStatic)) },
        args: [0, 1, 2, 3].map(i => hex(args[i].toUInt32())) });
    } catch (e) { if (st.errors.length < 50) st.errors.push(`quadTreeBuild enter: ${e.message}`); }
  },
};

function hookAt(name, [a, expect], listener) {
  const p = ptr(a), got = Array.from(new Uint8Array(p.readByteArray(8))).map(b => b.toString(16).padStart(2, '0')).join(' ');
  if (expect && got !== expect) { refused[name] = `bytes ${got}, expected ${expect}`; console.log(`[inroom] ${name} refused: ${refused[name]}`); return; }
  hooked.push(Interceptor.attach(p, listener)); hookCount++;
}
function write(reason) {
  const res = { version: 1, reason, params: { minutes: cfg.minutes }, seconds: nowS(), refused, hooks: hookCount, ...st };
  const f = new File(`${cfg.out}-inroom.json`, 'w');
  f.write(JSON.stringify(res, null, 1)); f.flush(); f.close();
  console.log(`[inroom] wrote ${cfg.out}-inroom.json (${reason})`);
}
function finish(reason = 'complete') {
  if (done) return;
  done = true;
  timers.forEach(clearInterval); timers.forEach(clearTimeout);
  for (const h of hooked) h.detach();
  hooked = [];
  Interceptor.flush();
  write(reason);
}
function progress() {
  if (done) return;
  const left = Math.max(0, cfg.minutes * 60 - (Date.now() - t0) / 1000);
  console.log(`[inroom] ${nowS()} s: plans ${st.plans}, builds ${st.builds}, dumps ${st.dumps.map(d => d.which).join('+') || 'none'}, job ${Math.ceil(left / 60)} min left`);
  // fail-fast: the start dump needs one in-room plan; 2 checks of 30 s without any plan = fail with the reason
  if (st.plans === lastProgress.plans && st.want) {
    if (++misses >= 2) { console.log(`[inroom] FAIL: no in-room plan for ${misses * 30} s, ${st.want} dump not taken (game paused or not routing?)`); finish(`fail: no in-room plan for ${misses * 30} s`); }
  } else misses = 0;
  lastProgress = { plans: st.plans };
}
function start(parameters) {
  if (t0) return;
  Object.assign(cfg, parameters || {});
  if (!cfg.out) cfg.out = `inroom-${new Date().toISOString().replace(/[:.]/g, '-')}`;
  t0 = Date.now();
  const H = cfg.addrs?.hooks ?? HOOKS; // test-only override: tools/probe/inroom-reads-test.py
  if (cfg.addrs?.callers) CALLERS.splice(0, CALLERS.length, ...cfg.addrs.callers);
  try {
    hookAt('inRoomPlan', H.inRoomPlan, onInRoom('InRoomPlan'));
    hookAt('inRoomToPoint', H.inRoomToPoint, onInRoom('InRoomPlanToPoint'));
    hookAt('setBounds', H.setBounds, setBoundsListener);
    hookAt('quadTreeBuild', H.quadTreeBuild, qtbListener);
  } catch (e) { refused.install = String(e.message); }
  if (refused.inRoomPlan && refused.inRoomToPoint) { finish('fail: no in-room hook (dumps need one)'); return; }
  console.log(`[inroom] ${hooked.length} hooks, ${cfg.minutes} min; refused: ${Object.keys(refused).join(', ') || 'none'}`);
  timers.push(setInterval(progress, 30000));
  timers.push(setTimeout(() => { if (!st.want) st.want = 'end'; }, Math.max(10, cfg.minutes * 60 - 25) * 1000));
  timers.push(setTimeout(() => finish('end dump not taken (no in-room plan in the last 25 s)'), Math.max(15, cfg.minutes * 60 - 3) * 1000));
}
setTimeout(() => start({}), 1000); // fallback when the host does not call init

rpc.exports = {
  init(stage, parameters) { start(parameters); },
  dispose() { finish('detached before the end'); },
};
