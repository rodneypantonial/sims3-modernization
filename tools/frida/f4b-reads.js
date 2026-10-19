// F-4b live read (docs/99 §12.6, docs/51 §60 I-99-33 / I-99-37 / I-99-39, §64): read-only Frida script for TS3W.exe (Steam 1.67).
//   1. RoutingPath_CopyNodes 0x819E10 (thiscall, ret 0x20; stack argument 7 = the path): on leave the node count, y (+0x04) and
//      +0x20 of nodes 0, 1, n-2, n-1, path+0x8C, and the 2D / 3D sums of the segment lengths over all nodes.
//   2. Path slot +0x100 0x821E90 (thiscall(path)(point*), ret 4): on enter the point, node 0 y, node count, path+0x8C; on leave
//      the node count change (+1 = prepended), node 0 / node 1 y and +0x20, path+0x8C.
//   3. Agent y vs terrain height: the point of item 2 is the agent's transform position (0x852700 reads it through agent slot
//      +0x1C at 0x85289C). Its terrain height comes from World_GetTerrainHeightImpl 0x786200, the script's World.GetTerrainHeight
//      IC (cdecl(x, z, flag) -> st0, plain ret; it calls World_GetHeightAt 0xC6A250 with the world manager), called with flag 0
//      only inside item 2's hook when the return address is one of the follower's two call sites (0x8529F5, 0x852B21), i.e. on
//      the sim thread inside RouteFollow, the same thread and lock order (+0x918 -> +0x228) the game's own scripts use.
//
//   python -m frida_tools.repl -n TS3W.exe -l tools/frida/f4b-reads.js -P '{"out":"probe-out/profile/x","minutes":3}' -q -t 180
//   (profile-world.ts --f4b does this at the window start; queue job option run.profile.f4b)
// Output: <out>-f4b.json, read by tools/harness/f4b-report.ts. Progress line every 30 s on stdout.
//
// Writes no game memory. Hook prologues are checked first: a mismatch refuses that hook, except a jmp rel32 into executable
// memory (another Frida session or a ts3native detour on the same site), which is chained and listed. The terrain call runs only
// when its first 8 bytes are the game's. Every read is guarded (a bad pointer = an error entry, not a crash); every list is bounded.
// Safe unload (docs/99 §12.7, docs/51 §81): the terrain NativeFunction releases the JS lock during the call, so an unload could
// free the script while the sim thread is still inside the thunk (Frida 17 waits for it on the dummy, I-99-61; we do not rely
// on that). finish() (timer or dispose) first sets `closing` (no new game
// call), waits up to 2 s for the in-flight game calls to return, then detaches and flushes. The thunk page comes from
// VirtualAlloc and is never freed (a call that outlives the wait still returns into valid code).
'use strict';

const cfg = { out: null, minutes: 3, addrs: null };
const HOOKS = {
  copyNodes: [0x819e10, '55 8b ec 83 e4 f0 81 ec'],
  prepend: [0x821e90, '55 8b ec 83 e4 f0 83 ec'],
};
const CALLS = { terrain: [0x786200, '8b 0d c4 cb 1e 01 85 c9'] };
const FOLLOW_RA = [0x8529f5, 0x852b21]; // returns of the two `call [vtable+0x100]` sites in RouteFollow 0x852700
const ROWS = 400, TERRAIN_CALLS = 200;
let closing = false, inCall = 0, unloadWait = null; // inCall: threads inside a game call (JS lock released)

// --- logic begin: pure functions over mem = {u32, f32}(address number); no Frida API (f4b-report.ts --self-check runs them in node)
const r3 = x => Math.round(x * 1e3) / 1e3;
const isFlatY = y => Math.abs(y + 100) < 1e-3; // planner nodes carry y = -100 (docs/99 §11.2)
// route path: 48-byte nodes path+0x98..+0x9C, node {x, y, z, x} +0x10 direction, +0x20 segment length, +0x24 running length;
// path+0x8C total (docs/99 §11.2, §12.2)
function pathRead(mem, path) {
  const beg = mem.u32(path + 0x98), end = mem.u32(path + 0x9c), n = (end - beg) / 48;
  if (end < beg || !Number.isInteger(n) || n > 4096) return { error: `node vector ${beg}..${end}` };
  const r = { n, total: r3(mem.f32(path + 0x8c)), nodes: [], sum2d: 0, sum3d: 0, sumLen: 0, flatN: 0, layout: '' };
  const ys = [];
  let px = 0, py = 0, pz = 0;
  for (let i = 0; i < n; i++) {
    const a = beg + 48 * i, x = mem.f32(a), y = mem.f32(a + 4), z = mem.f32(a + 8);
    ys.push(y);
    if (isFlatY(y)) r.flatN++;
    if (i) { r.sum2d += Math.hypot(x - px, z - pz); r.sum3d += Math.hypot(x - px, y - py, z - pz); }
    if (i < n - 1) r.sumLen += mem.f32(a + 0x20);
    px = x; py = y; pz = z;
  }
  for (const i of [...new Set([0, 1, n - 2, n - 1])].filter(i => i >= 0 && i < n).sort((a, b) => a - b)) {
    const a = beg + 48 * i;
    r.nodes.push({ i, y: r3(mem.f32(a + 4)), len: r3(mem.f32(a + 0x20)), run: r3(mem.f32(a + 0x24)) });
  }
  r.sum2d = r3(r.sum2d); r.sum3d = r3(r.sum3d); r.sumLen = r3(r.sumLen);
  r.layout = layoutOf(ys);
  return r;
}
// flat: every y -100; real: no y -100; inner: some -100 and an inner node at a real height; ends: real only at the first/last node
function layoutOf(ys) {
  if (!ys.length) return 'empty';
  if (ys.every(isFlatY)) return 'flat';
  if (!ys.some(isFlatY)) return 'real';
  return ys.slice(1, -1).some(y => !isFlatY(y)) ? 'inner' : 'ends';
}
// --- logic end

let t0 = 0, done = false, timers = [], hooked = [], hookCount = 0, refused = {}, chained = {}, misses = 0, terrain = null;
const st = {
  errors: [],
  copy: { calls: 0, threads: {}, retFalse: 0, layouts: {}, layoutsSearched: {}, rows: [] },
  prepend: { calls: 0, threads: {}, ras: {}, delta: {}, rows: [] },
  terrain: { calls: 0, rows: [] },
};
const mem = { u32: a => ptr(a).readU32(), f32: a => ptr(a).readFloat() };
const nowS = () => Math.round((Date.now() - t0) / 100) / 10;
const hex = x => '0x' + (x >>> 0).toString(16);
function bump(o, k, n = 1, cap = 64) { if (o[k] === undefined && Object.keys(o).length >= cap) k = 'other'; o[k] = (o[k] || 0) + n; }
function err(where, e) { if (st.errors.length < 50) st.errors.push(`${where}: ${e.message ?? e}`); }
const followRa = () => cfg.addrs?.followRa ?? FOLLOW_RA;
const node = (path, i) => { const a = mem.u32(path + 0x98) + 48 * i; return { y: r3(mem.f32(a + 4)), len: r3(mem.f32(a + 0x20)) }; };
const count = path => (mem.u32(path + 0x9c) - mem.u32(path + 0x98)) / 48;

const copyListener = {
  onEnter(args) {
    try { this.path = args[6].toUInt32(); this.tid = Process.getCurrentThreadId(); } catch (e) { this.path = 0; err('copy enter', e); }
  },
  onLeave(retval) {
    if (!this.path) return;
    try {
      const c = st.copy, ok = retval.toUInt32() & 0xff;
      c.calls++; bump(c.threads, this.tid);
      if (!ok) { c.retFalse++; return; }
      const r = pathRead(mem, this.path);
      if (r.error) { err('copy leave', r.error); return; }
      bump(c.layouts, `${r.layout}/${r.n > 2 ? 'searched' : 'direct'}`);
      if (r.n > 2) bump(c.layoutsSearched, r.layout);
      // keep every non-flat path up to the cap, flat ones only for the first half of it (the common case would crowd them out)
      if (c.rows.length < ROWS && (r.layout !== 'flat' || c.rows.length < ROWS / 2)) c.rows.push({ t: nowS(), tid: this.tid, path: hex(this.path), ...r });
    } catch (e) { err('copy leave', e); }
  },
};
const prependListener = {
  onEnter(args) {
    this.path = 0;
    try {
      const path = this.context.ecx.toUInt32(), p = args[0].toUInt32(), ra = this.returnAddress.toUInt32(), tid = Process.getCurrentThreadId(), P = st.prepend;
      P.calls++; bump(P.threads, tid); bump(P.ras, hex(ra));
      const n = count(path), pt = [r3(mem.f32(p)), r3(mem.f32(p + 4)), r3(mem.f32(p + 8))];
      this.path = path; this.n = n;
      this.row = { t: nowS(), tid, ra: hex(ra), path: hex(path), point: pt, nBefore: n, node0yBefore: n > 0 ? node(path, 0).y : null, totalBefore: r3(mem.f32(path + 0x8c)) };
      if (terrain && !closing && st.terrain.calls < (cfg.terrainCalls ?? TERRAIN_CALLS) && followRa().includes(ra)) {
        st.terrain.calls++;
        let h;
        inCall++;
        try { h = terrain(mem.f32(p), mem.f32(p + 8), 0); } finally { inCall--; }
        this.row.terrain = r3(h);
        if (st.terrain.rows.length < ROWS) st.terrain.rows.push({ t: this.row.t, point: pt, terrain: r3(h), dy: r3(pt[1] - h) });
      }
    } catch (e) { err('prepend enter', e); }
  },
  onLeave() {
    if (!this.path) return;
    try {
      const P = st.prepend, n = count(this.path), d = n - this.n, row = this.row;
      bump(P.delta, String(d));
      Object.assign(row, { delta: d, node0: n > 0 ? node(this.path, 0) : null, node1: n > 1 ? node(this.path, 1) : null, totalAfter: r3(mem.f32(this.path + 0x8c)) });
      if (P.rows.length < ROWS && (d !== 0 || P.rows.length < ROWS / 2)) P.rows.push(row);
    } catch (e) { err('prepend leave', e); }
  },
};

// cdecl(float, float, int) -> st0 through a thunk of ours that moves st0 to eax: Frida 17's NativeFunction returns garbage
// for an x87 'float' return on ia32 (checked on the dummy, 2026-10-18). The thunk re-pushes the 3 arguments, calls, pops st0.
let thunkPage = null;
function floatThunk(target) {
  // VirtualAlloc(NULL, page, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE), never freed: leaks one page per load on purpose
  const va = new NativeFunction(Process.getModuleByName('kernel32.dll').getExportByName('VirtualAlloc'), 'pointer', ['pointer', 'ulong', 'uint32', 'uint32'], 'stdcall');
  thunkPage = va(NULL, Process.pageSize, 0x3000, 0x40);
  if (thunkPage.isNull()) throw new Error('VirtualAlloc failed for the terrain thunk');
  const w = new X86Writer(thunkPage, { pc: thunkPage });
  for (let i = 0; i < 3; i++) w.putBytes([0xff, 0x74, 0x24, 0x0c]); // push dword [esp+0xc]: flag, z, x
  w.putCallAddress(ptr(target));
  w.putAddRegImm('esp', 12);
  w.putBytes([0x83, 0xec, 0x04, 0xd9, 0x1c, 0x24, 0x58]); // sub esp,4; fstp dword [esp]; pop eax
  w.putRet(); w.flush();
  const f = new NativeFunction(thunkPage, 'uint32', ['float', 'float', 'int'], 'mscdecl'), dv = new DataView(new ArrayBuffer(4));
  return (x, z, flag) => { dv.setUint32(0, f(x, z, flag), true); return dv.getFloat32(0, true); };
}
function bytesAt(a) { return Array.from(new Uint8Array(ptr(a).readByteArray(8))).map(b => b.toString(16).padStart(2, '0')).join(' '); }
function hookAt(name, [a, expect], listener) {
  const got = bytesAt(a);
  if (expect && got !== expect) {
    const tgt = got.startsWith('e9 ') ? (a + 5 + ptr(a + 1).readS32()) >>> 0 : 0, range = tgt ? Process.findRangeByAddress(ptr(tgt)) : null;
    if (!range || !range.protection.includes('x')) { refused[name] = `bytes ${got}, expected ${expect}`; console.log(`[f4b] ${name} refused: ${refused[name]}`); return; }
    chained[name] = { target: hex(tgt), module: Process.findModuleByAddress(ptr(tgt))?.name ?? 'anonymous (another Frida session?)' };
    console.log(`[f4b] ${name}: existing jmp to ${chained[name].target} (${chained[name].module}), chained`);
  }
  hooked.push(Interceptor.attach(ptr(a), listener)); hookCount++;
}
function write(reason) {
  const res = { version: 1, reason, params: { minutes: cfg.minutes }, seconds: nowS(), refused, chained, hooks: hookCount, unloadWait, ...st };
  const f = new File(`${cfg.out}-f4b.json`, 'w');
  f.write(JSON.stringify(res, null, 1)); f.flush(); f.close();
  console.log(`[f4b] wrote ${cfg.out}-f4b.json (${reason})`);
}
function finish(reason = 'complete') {
  if (done) return;
  done = true; closing = true;
  timers.forEach(clearInterval); timers.forEach(clearTimeout);
  // bounded wait for game calls in flight; Thread.sleep lets go of the JS lock so the caller can come back into its callback
  const w0 = Date.now();
  while (inCall > 0 && Date.now() - w0 < 2000) Thread.sleep(0.005);
  unloadWait = { ms: Date.now() - w0, inCall };
  if (inCall) err('unload', `${inCall} game call(s) still in flight after 2 s; detaching anyway (thunk kept)`);
  for (const h of hooked) h.detach();
  hooked = [];
  Interceptor.flush();
  write(reason);
}
function progress() {
  if (done) return;
  const left = Math.max(0, cfg.minutes * 60 - (Date.now() - t0) / 1000);
  console.log(`[f4b] ${nowS()} s: copy ${st.copy.calls} (${Object.entries(st.copy.layouts).map(([k, v]) => `${k}:${v}`).join(' ') || 'none'}), prepend ${st.prepend.calls} (delta ${Object.entries(st.prepend.delta).map(([k, v]) => `${k}:${v}`).join(' ') || 'none'}), terrain ${st.terrain.calls}, job ${Math.ceil(left / 60)} min left`);
  // fail-fast: no CopyNodes call at all for 2 checks of 30 s = nothing plans (game paused, no Sim routing?)
  if (!st.copy.calls && !st.prepend.calls) {
    if (++misses >= 2) { console.log(`[f4b] FAIL: no CopyNodes or prepend call for ${misses * 30} s (game paused or no route planned?)`); finish(`fail: no route built for ${misses * 30} s`); }
  } else misses = 0;
}
function start(parameters) {
  if (t0) return;
  Object.assign(cfg, parameters || {});
  if (!cfg.out) cfg.out = `f4b-${new Date().toISOString().replace(/[:.]/g, '-')}`;
  t0 = Date.now();
  const H = cfg.addrs?.hooks ?? HOOKS, C = cfg.addrs?.calls ?? CALLS; // test-only overrides (also cfg.terrainCalls): tools/probe/f4b-reads-test.py
  const [ta, tb] = C.terrain, got = bytesAt(ta);
  if (got !== tb) refused.terrain = `call target bytes ${got}, expected ${tb}`;
  else terrain = floatThunk(ta);
  try {
    hookAt('copyNodes', H.copyNodes, copyListener);
    hookAt('prepend', H.prepend, prependListener);
  } catch (e) { refused.install = String(e.message); }
  if (refused.copyNodes) { finish('fail: no 0x819E10 hook (item 1 needs it)'); return; }
  console.log(`[f4b] ${hookCount} hooks, ${cfg.minutes} min; refused: ${Object.keys(refused).join(', ') || 'none'}; chained: ${Object.keys(chained).join(', ') || 'none'}`);
  timers.push(setInterval(progress, 30000));
  timers.push(setTimeout(() => finish('complete'), Math.max(15, cfg.minutes * 60 - 3) * 1000));
}
setTimeout(() => start({}), 1000); // fallback when the host does not call init

rpc.exports = {
  init(stage, parameters) { start(parameters); },
  dispose() { finish('detached before the end'); },
};
