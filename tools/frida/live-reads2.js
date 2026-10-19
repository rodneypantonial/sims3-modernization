// Routing live reads, pass 2 (docs/114): read-only Frida script for TS3W.exe (Steam 1.67). Closes docs/51 §54 rows:
//   I-107-15/17  O+0x58 (path+0x60..+0x64) after RouteFollow_EditPathForRoute 0x85FF30: how often non-empty, which ids
//   I-107-16     the route's own list route+0x158+0x58 at Route_PlanCommon return (always empty?)
//   I-107-19     P's footprint grid P+0xDC..+0xE0: cell count (32-byte cells) and records per cell
//   I-107-20     thread per call of the grid fill 0x636BF0 and grid remove 0x62D9F0, by caller
//   I-107-21     World_IsPointRoutableTerrain 0xC6BAB0 on 20 points (inroom-free compare offline: live-reads2-report.ts --world)
//   I-107-24     return addresses of RouteFollow_Update38 0x853820 (follow behaviour slot +0x38)
//   I-100-13     houseboat frame: Lot_SetHouseboatParent 0xAB9C40 corners / flag, start point of every Route_PlanCommon 0x7606E0
//
//   python -m frida_tools.repl -n TS3W.exe -l tools/frida/live-reads2.js -P '{"out":"probe-out/profile/x","minutes":5}' -q -t 300
//   (profile-world.ts --livereads2 does this at the window start; queue job option run.profile.livereads2)
// Output: <out>-livereads2.json, read by tools/harness/live-reads2-report.ts. Progress line every 30 s on stdout.
//
// Writes no game memory. Calls two game functions, 0xC6BAB0 and World_LotLookup 0xC68990 (pure reads of world bounds, the
// blend layer and the lot index, thiscall ret 8), 20 times per dump, only inside the 0x85FF30 hook on the planning (sim)
// thread and only when their first 8 bytes are the game's. Hook prologues are checked first: a mismatch refuses that hook,
// except a jmp rel32 into executable memory (another Frida session or a ts3native detour on the same site), which is
// chained and listed. Every read is guarded (a bad pointer = an error entry, not a crash); every list is bounded.
// Safe unload (docs/114 §5, docs/51 §81): a NativeFunction call releases the JS lock, so finish() (timer or dispose) first sets
// `closing` (no new game call; checked before each one), waits up to 2 s for calls in flight to return, then detaches and flushes.
'use strict';

const cfg = { out: null, minutes: 5, addrs: null };
const A = { routingWorld: 0x11d9128, wm: 0x11ecbc4 };
const HOOKS = {
  editPathForRoute: [0x85ff30, '55 8b ec 83 e4 f0 83 ec'],
  planCommon: [0x7606e0, '55 8b ec 83 e4 f0 81 ec'],
  gridFill: [0x636bf0, '55 8b ec 83 e4 f0 81 ec'],
  gridRemove: [0x62d9f0, '83 ec 4c 53 55 56 8b f1'],
  follow38: [0x853820, '51 53 56 8b f1 8b 4e 14'],
  setHouseboatParent: [0xab9c40, '55 8b ec 83 e4 f0 8b 45'],
};
const CALLS = { isRoutable: [0xc6bab0, 'f3 0f 10 4c 24 04 83 ec'], lotLookup: [0xc68990, 'd9 44 24 08 83 ec 08 83'] };
// return address -> caller (direct E8 calls into the hooked functions, scanned from the exe)
const RA = {
  0x760f97: 'Route_PlanCommon', 0x755672: 'IC_Route_SetAGS', 0x758d4f: 'IC_Route_InsertRouteSubPathsAtIndex', 0x7627f9: 'IC_Route_InsertCustomPathAtIndex',
  0x6374ef: 'HangingFootprint_child_0x636BF0', 0x637784: 'PathPlanner_AddFootprint_0x6376D0', 0x6377e8: 'PathPlanner_AddFootprint_0x6376D0',
  0x63a16b: 'Fn_0x639DE0', 0x63aaa4: 'Fn_0x63A1C0', 0x63b3b3: 'Fn_0x63AFD0', 0x63b437: 'Fn_0x63AFD0', 0x63d1a3: 'LotGraph_Build_0x63CB90',
  0x62e412: 'Fn_0x62E3E0', 0x63590b: 'LotRouting_RemoveObjectFootprints_0x635700', 0x6379c6: 'Fn_0x637900',
  0x7888e0: 'IC_World_ParentLotToHouseboatImpl', 0xace4eb: 'Fn_0xACE4A0', 0xace8d9: 'Fn_0xACE4A0', 0xb04b32: 'Fn_0xB04AF0',
};
const BUILD = [[0x63cb90, 0x63d2e2, 'LotGraph_Build']]; // a frame in here = the lazy fill docs/107 §10.2 allows off the sim thread

// --- logic begin: pure functions over mem = {u8, u32, f32}(address number); no Frida API (live-reads2-report.ts --self-check runs them in node)
const hex = x => '0x' + (x >>> 0).toString(16);
const r3 = x => Math.round(x * 1e3) / 1e3;
// P's footprint grid (docs/107 §10.1, §3.1): vector of 32-byte cells P+0xDC..+0xE0; cell = hash set, buckets +4, count +8,
// (+0x0C read as the element count, unverified), records chained through rec+0x48, lot id at rec+0x40/+0x44
function gridDump(mem, addrs) {
  const r = { errors: [] };
  try {
    const w = mem.u32(addrs.routingWorld), P = w ? mem.u32(w + 0x95c) : 0, wm = mem.u32(addrs.wm);
    Object.assign(r, { P, wm });
    if (wm) {
      const b = [0xb4, 0xb8, 0xbc, 0xc0].map(o => mem.f32(wm + o));
      r.bounds = b.map(r3);
      r.expectCells = Math.trunc((b[2] - b[0] + 255) / 256) * Math.trunc((b[3] - b[1] + 255) / 256);
    }
    if (!P) { r.errors.push('no routing world or planner'); return r; }
    const beg = mem.u32(P + 0xdc), end = mem.u32(P + 0xe0), cap = mem.u32(P + 0xe4);
    Object.assign(r, { begin: beg, cellBytes: end - beg, capBytes: cap - beg, cells: (end - beg) / 32 });
    if (end < beg || (end - beg) % 32 || (end - beg) / 32 > 65536) { r.errors.push(`grid vector ${hex(beg)}..${hex(end)}`); return r; }
    Object.assign(r, { nonEmpty: 0, records: 0, elemCount: 0, noLot: 0, maxPerCell: 0, top: [], keys: 0 });
    const keys = new Set();
    for (let i = 0; i < r.cells; i++) {
      const c = beg + 32 * i, bk = mem.u32(c + 4), nb = mem.u32(c + 8);
      r.elemCount += mem.u32(c + 0xc);
      if (!bk || !nb) continue;
      if (nb > 1 << 16) { r.errors.push(`cell ${i}: ${nb} buckets`); continue; }
      const sentinel = mem.u32(bk + 4 * nb);
      let n = 0;
      for (let k = 0; k < nb; k++)
        for (let rec = mem.u32(bk + 4 * k); rec && rec !== sentinel && rec !== 0xffffffff; rec = mem.u32(rec + 0x48)) {
          if (++r.records > 200000) { r.errors.push('more than 200000 records'); return r; }
          n++;
          if (!mem.u32(rec + 0x40) && !mem.u32(rec + 0x44)) r.noLot++;
          if (keys.size < 50000) keys.add(`${mem.u32(rec)}:${mem.u32(rec + 4)}`);
        }
      if (n) { r.nonEmpty++; r.maxPerCell = Math.max(r.maxPerCell, n); r.top.push([i, n]); }
    }
    r.top = r.top.sort((a, b) => b[1] - a[1]).slice(0, 12);
    r.keys = keys.size;
  } catch (err) { r.errors.push(String(err.message ?? err)); }
  return r;
}
// 20 points on a 5 x 4 grid over the world bounds (inside, so the bounds test of 0xC6BAB0 passes)
function pointGrid(b) {
  const out = [];
  for (let j = 0; j < 4; j++) for (let i = 0; i < 5; i++)
    out.push([b[0] + (b[2] - b[0]) * (i + 0.5) / 5 + 0.25, b[1] + (b[3] - b[1]) * (j + 0.5) / 4 + 0.25]);
  return out;
}
// lot corners at Lot+0x134: four (x, z) float pairs, 8 bytes apart (G build loop at 0x67327C reads [ebx + (i & 3) * 8])
function corners(mem, lot) { const c = []; for (let i = 0; i < 4; i++) c.push([r3(mem.f32(lot + 0x134 + 8 * i)), r3(mem.f32(lot + 0x138 + 8 * i))]); return c; }
function inCorners(c, x, z) {
  const xs = c.map(p => p[0]), zs = c.map(p => p[1]);
  return x >= Math.min(...xs) - 1e-3 && x <= Math.max(...xs) + 1e-3 && z >= Math.min(...zs) - 1e-3 && z <= Math.max(...zs) + 1e-3;
}
// --- logic end

let t0 = 0, done = false, timers = [], hooked = [], hookCount = 0, refused = {}, chained = {}, lastCalls = 0, misses = 0;
const st = {
  want: 'start', dumps: [], errors: [], simVotes: {},
  o58: { calls: 0, byCaller: {}, idsIsPath60: 0, idsOther: 0, rows: [] },
  plan: { calls: 0, threads: {}, startN: {}, beyondFarZ: 0, inG: 0, noStart: 0, rows: [], routeListNonEmpty: 0, routeListRows: [] },
  grid: { gridFill: { calls: 0, threads: {}, callers: {}, ecxIsP: 0, offSim: [] }, gridRemove: { calls: 0, threads: {}, callers: {}, ecxIsP: 0, offSim: [] } },
  follow38: { calls: 0, threads: {}, ras: {}, bt: [] },
  houseboat: { calls: 0, rows: [] },
};
const mem = { u8: a => ptr(a).readU8(), u32: a => ptr(a).readU32(), s32: a => ptr(a).readS32(), f32: a => ptr(a).readFloat() };
const nowS = () => Math.round((Date.now() - t0) / 100) / 10;
const addrs = () => cfg.addrs ?? A;
const raMap = () => cfg.addrs?.ra ?? RA;
const TEXT = [0x401000, 0xf95000];
// stack words from esp that point into code (.text): a fuzzy caller chain without Thread.backtrace (no dbghelp, fixed cost).
// Reads at most 4 * words bytes of the calling thread's own stack; values are candidates, not proven frames.
function stackCode(ctx, words = 256) {
  const [lo, hi] = cfg.addrs?.text ?? TEXT, out = [];
  for (let n = words; n >= 16; n >>= 1) {
    try {
      for (const v of new Uint32Array(ctx.esp.readByteArray(4 * n))) if (v >= lo && v < hi) out.push(v);
      break;
    } catch (e) { /* the stack top is closer: read less */ }
  }
  return out;
}
const nameOf = ra => raMap()[ra] ?? hex(ra);
function bump(o, k, n = 1, cap = 64) { if (o[k] === undefined && Object.keys(o).length >= cap) k = 'other'; o[k] = (o[k] || 0) + n; }
function err(where, e) { if (st.errors.length < 50) st.errors.push(`${where}: ${e.message ?? e}`); }
const tidNow = () => Process.getCurrentThreadId();
const simTid = () => { let best = null; for (const [t, n] of Object.entries(st.simVotes)) if (!best || n > best[1]) best = [t, n]; return best ? Number(best[0]) : 0; };
const curRoute = {}; // tid -> stack of routes inside Route_PlanCommon
let routable = null, lotLookup = null, closing = false, inCall = 0, unloadWait = null; // inCall: threads inside a game call

function pointsCheck(wm) {
  if (!routable || !lotLookup || !wm) return { skipped: !wm ? 'no world manager' : 'call targets refused' };
  const b = [0xb4, 0xb8, 0xbc, 0xc0].map(o => mem.f32(wm + o)), pts = [];
  for (const [x, z] of pointGrid(b)) {
    if (closing) return { bounds: b.map(r3), closing: true, points: pts };
    let ans, lot;
    inCall++;
    try { ans = routable(ptr(wm), x, z); lot = lotLookup(ptr(wm), x, z); } finally { inCall--; }
    pts.push({ x: r3(x), z: r3(z), ans: ans & 0xff ? 1 : 0, lot: lot.isNull() ? 0 : 1, hb: lot.isNull() ? 0 : mem.u8(lot.toUInt32() + 0x444) });
  }
  return { bounds: b.map(r3), zone: r3(mem.f32(wm + 0xc4)), points: pts };
}
function dumpNow(tid) {
  const which = st.want, t = Date.now();
  st.want = null;
  const g = gridDump(mem, addrs());
  let p;
  try { p = pointsCheck(g.wm); } catch (e) { p = { error: String(e.message ?? e) }; }
  st.dumps.push({ which, t: nowS(), tid, ms: Date.now() - t, grid: g, points: p });
  console.log(`[livereads2] dump ${which}: grid ${g.cells ?? '?'} cells, ${g.records ?? '?'} records, points ${p.points?.length ?? p.skipped ?? p.error}, ${Date.now() - t} ms, errors ${g.errors.length}`);
  if (which === 'end') setTimeout(finish, 0);
}

const editPathListener = {
  onEnter(args) {
    try {
      const tid = tidNow(), ra = this.returnAddress.toUInt32();
      this.path = args[0].toUInt32(); this.ids = args[1].toUInt32(); this.ra = ra; this.tid = tid;
      this.before = this.ids ? (mem.u32(this.ids + 4) - mem.u32(this.ids)) / 8 : 0;
      bump(st.simVotes, tid);
      if (st.want) dumpNow(tid);
    } catch (e) { this.ids = 0; err('editPath enter', e); }
  },
  onLeave() {
    if (!this.ids) return;
    try {
      const o = st.o58, beg = mem.u32(this.ids), n = (mem.u32(this.ids + 4) - beg) / 8, name = nameOf(this.ra);
      o.calls++;
      if (this.ids === this.path + 0x60) o.idsIsPath60++; else o.idsOther++;
      if (!(n >= 0 && n <= 4096 && Number.isInteger(n))) { err('editPath leave', `ids vector ${hex(beg)} count ${n}`); return; }
      const c = o.byCaller[name] ??= { calls: 0, nonEmpty: 0, hist: {}, added: 0 };
      c.calls++; if (n) c.nonEmpty++; bump(c.hist, n > 8 ? '9+' : String(n)); c.added += n - this.before;
      if (n && o.rows.length < 300) {
        const route = (curRoute[this.tid] ?? []).slice(-1)[0], fol = route ? [mem.u32(route + 0x40), mem.u32(route + 0x44)] : null;
        const ids = []; for (let i = 0; i < Math.min(n, 6); i++) ids.push(`${hex(mem.u32(beg + 8 * i + 4))}:${hex(mem.u32(beg + 8 * i))}`);
        o.rows.push({ t: nowS(), caller: name, n, ids, follower: fol ? `${hex(fol[1])}:${hex(fol[0])}` : null, hasFollower: fol ? ids.includes(`${hex(fol[1])}:${hex(fol[0])}`) : null });
      }
    } catch (e) { err('editPath leave', e); }
  },
};
const planListener = {
  onEnter(args) {
    try {
      const tid = tidNow(), route = this.context.ecx.toUInt32(), p = st.plan, v = args[1].toUInt32();
      this.route = route; this.tid = tid;
      (curRoute[tid] ??= []).push(route);
      p.calls++; bump(p.threads, tid);
      const beg = v ? mem.u32(v) : 0, n = v ? (mem.u32(v + 4) - beg) / 64 : 0;
      bump(p.startN, n > 4 ? '5+' : String(n));
      if (!n || !beg) { p.noStart++; return; }
      const x = mem.f32(beg), y = mem.f32(beg + 4), z = mem.f32(beg + 8), wm = mem.u32(addrs().wm), maxZ = wm ? mem.f32(wm + 0xc0) : NaN;
      if (z > maxZ) {
        p.beyondFarZ++;
        if (p.rows.length < 200) {
          const hb = st.houseboat.rows.findIndex(h => h.flag && inCorners(h.corners, x, z));
          p.rows.push({ t: nowS(), tid, follower: `${hex(mem.u32(route + 0x44))}:${hex(mem.u32(route + 0x40))}`, start: [r3(x), r3(y), r3(z)], n, maxZ: r3(maxZ), hbLot: hb });
        }
      } else p.inG++;
    } catch (e) { err('planCommon enter', e); }
  },
  onLeave() {
    try {
      const s = curRoute[this.tid]; if (s) s.pop();
      if (!this.route) return;
      const b = mem.u32(this.route + 0x1b0), e = mem.u32(this.route + 0x1b4); // route+0x158 options, +0x58 id vector
      if (e !== b) { st.plan.routeListNonEmpty++; if (st.plan.routeListRows.length < 20) st.plan.routeListRows.push({ t: nowS(), n: (e - b) / 8 }); }
    } catch (e) { err('planCommon leave', e); }
  },
};
function gridListener(fn) {
  return {
    onEnter() {
      try {
        const g = st.grid[fn], tid = tidNow(), ra = this.returnAddress.toUInt32(), name = nameOf(ra);
        g.calls++; bump(g.threads, tid); bump(g.callers, `${name}@${tid}`);
        const w = mem.u32(addrs().routingWorld), P = w ? mem.u32(w + 0x95c) : 0;
        if (P && this.context.ecx.toUInt32() === P) g.ecxIsP++;
        const sim = simTid();
        if (sim && tid !== sim && g.offSim.length < 32) {
          const ranges = cfg.addrs?.build ?? BUILD, inR = a => ranges.some(([lo, hi]) => a >= lo && a < hi);
          const frames = stackCode(this.context, 1024);
          g.offSim.push({ t: nowS(), tid, caller: name, inBuild: inR(ra) || frames.some(inR), frames: frames.slice(0, 8).map(hex) });
        }
      } catch (e) { err(`${fn} enter`, e); }
    },
  };
}
const followListener = {
  onEnter() {
    try {
      const f = st.follow38, ra = this.returnAddress.toUInt32(), k = hex(ra), first = f.ras[k] === undefined;
      f.calls++; bump(f.threads, tidNow()); bump(f.ras, k, 1, 32);
      if (first && f.bt.length < 4) f.bt.push({ ra: k, frames: stackCode(this.context).slice(0, 8).map(hex) });
    } catch (e) { err('follow38 enter', e); }
  },
};
const houseboatListener = {
  onEnter(args) {
    try { this.lot = this.context.ecx.toUInt32(); this.id = [args[0].toUInt32(), args[1].toUInt32()]; this.ra = this.returnAddress.toUInt32(); } catch (e) { this.lot = 0; err('houseboat enter', e); }
  },
  onLeave() {
    if (!this.lot) return;
    try {
      const h = st.houseboat, wm = mem.u32(addrs().wm);
      h.calls++;
      if (h.rows.length < 64) h.rows.push({ t: nowS(), caller: nameOf(this.ra), lot: hex(this.lot), boat: `${hex(this.id[1])}:${hex(this.id[0])}`, flag: mem.u8(this.lot + 0x444),
        corners: corners(mem, this.lot), maxZ: wm ? r3(mem.f32(wm + 0xc0)) : null, zone: wm ? r3(mem.f32(wm + 0xc4)) : null });
    } catch (e) { err('houseboat leave', e); }
  },
};

function bytesAt(a) { return Array.from(new Uint8Array(ptr(a).readByteArray(8))).map(b => b.toString(16).padStart(2, '0')).join(' '); }
function hookAt(name, [a, expect], listener) {
  const got = bytesAt(a);
  if (expect && got !== expect) {
    const tgt = got.startsWith('e9 ') ? (a + 5 + ptr(a + 1).readS32()) >>> 0 : 0, range = tgt ? Process.findRangeByAddress(ptr(tgt)) : null;
    if (!range || !range.protection.includes('x')) { refused[name] = `bytes ${got}, expected ${expect}`; console.log(`[livereads2] ${name} refused: ${refused[name]}`); return; }
    chained[name] = { target: hex(tgt), module: Process.findModuleByAddress(ptr(tgt))?.name ?? 'anonymous (another Frida session?)' };
    console.log(`[livereads2] ${name}: existing jmp to ${chained[name].target} (${chained[name].module}), chained`);
  }
  hooked.push(Interceptor.attach(ptr(a), listener)); hookCount++;
}
function write(reason) {
  const res = { version: 1, reason, params: { minutes: cfg.minutes }, seconds: nowS(), refused, chained, hooks: hookCount, simTid: simTid(), unloadWait, ...st };
  const f = new File(`${cfg.out}-livereads2.json`, 'w');
  f.write(JSON.stringify(res, null, 1)); f.flush(); f.close();
  console.log(`[livereads2] wrote ${cfg.out}-livereads2.json (${reason})`);
}
function finish(reason = 'complete') {
  if (done) return;
  done = true; closing = true;
  timers.forEach(clearInterval); timers.forEach(clearTimeout);
  // bounded wait for game calls in flight; Thread.sleep lets go of the JS lock so the caller can come back into its callback
  const w0 = Date.now();
  while (inCall > 0 && Date.now() - w0 < 2000) Thread.sleep(0.005);
  unloadWait = { ms: Date.now() - w0, inCall };
  if (inCall) err('unload', `${inCall} game call(s) still in flight after 2 s; detaching anyway`);
  for (const h of hooked) h.detach();
  hooked = [];
  Interceptor.flush();
  write(reason);
}
function progress() {
  if (done) return;
  const left = Math.max(0, cfg.minutes * 60 - (Date.now() - t0) / 1000), n = st.o58.calls + st.follow38.calls + st.plan.calls;
  console.log(`[livereads2] ${nowS()} s: editPath ${st.o58.calls}, plans ${st.plan.calls}, fill ${st.grid.gridFill.calls}, remove ${st.grid.gridRemove.calls}, follow38 ${st.follow38.calls}, houseboat ${st.houseboat.calls}, dumps ${st.dumps.map(d => d.which).join('+') || 'none'}, job ${Math.ceil(left / 60)} min left`);
  // fail-fast: the start dump needs one 0x85FF30 call; 2 checks of 30 s with no routing call at all = fail with the reason
  if (st.want === 'start' && n === lastCalls) {
    if (++misses >= 2) { console.log(`[livereads2] FAIL: no route plan or follow call for ${misses * 30} s, start dump not taken (game paused or not routing?)`); finish(`fail: no route plan for ${misses * 30} s`); }
  } else misses = 0;
  lastCalls = n;
}
function start(parameters) {
  if (t0) return;
  Object.assign(cfg, parameters || {});
  if (!cfg.out) cfg.out = `livereads2-${new Date().toISOString().replace(/[:.]/g, '-')}`;
  t0 = Date.now();
  const H = cfg.addrs?.hooks ?? HOOKS, C = cfg.addrs?.calls ?? CALLS; // test-only override: tools/probe/live-reads2-test.py
  for (const [k, [a, expect]] of Object.entries(C)) {
    const got = bytesAt(a);
    if (got !== expect) { refused[k] = `call target bytes ${got}, expected ${expect}`; continue; }
    if (k === 'isRoutable') routable = new NativeFunction(ptr(a), 'uint8', ['pointer', 'float', 'float'], 'thiscall');
    else lotLookup = new NativeFunction(ptr(a), 'pointer', ['pointer', 'float', 'float'], 'thiscall');
  }
  try {
    hookAt('editPathForRoute', H.editPathForRoute, editPathListener);
    hookAt('planCommon', H.planCommon, planListener);
    hookAt('gridFill', H.gridFill, gridListener('gridFill'));
    hookAt('gridRemove', H.gridRemove, gridListener('gridRemove'));
    hookAt('follow38', H.follow38, followListener);
    hookAt('setHouseboatParent', H.setHouseboatParent, houseboatListener);
  } catch (e) { refused.install = String(e.message); }
  if (refused.editPathForRoute) { finish('fail: no 0x85FF30 hook (dumps and O+0x58 need it)'); return; }
  console.log(`[livereads2] ${hookCount} hooks, ${cfg.minutes} min; refused: ${Object.keys(refused).join(', ') || 'none'}; chained: ${Object.keys(chained).join(', ') || 'none'}`);
  timers.push(setInterval(progress, 30000));
  timers.push(setTimeout(() => { if (!st.want) st.want = 'end'; }, Math.max(10, cfg.minutes * 60 - 25) * 1000));
  timers.push(setTimeout(() => finish('end dump not taken (no 0x85FF30 call in the last 25 s)'), Math.max(15, cfg.minutes * 60 - 3) * 1000));
}
setTimeout(() => start({}), 1000); // fallback when the host does not call init

rpc.exports = {
  init(stage, parameters) { start(parameters); },
  dispose() { finish('detached before the end'); },
};
