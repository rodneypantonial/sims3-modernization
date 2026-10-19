// Routing watch + Stalker live probe (docs/113): read-only Frida script for TS3W.exe (Steam 1.67), no game state is written.
// Closes docs/51 §53: docs/101 U10 (route +0x1F4 has no hidden reader), U1 (the plan's .data/.tls write set matches docs/101
// §2.2) and U3 (the clip scratch 0x1158580 is used inside plans, via 0x66AB70).
//
//   python -m frida_tools.repl -n TS3W.exe -l tools/frida/watch-stalk.js -P '{"out":"probe-out/profile/x","minutes":6}' -q -t 360
//   (profile-world.ts --watchstalk does this at the window start; queue job option run.profile.watchstalk)
// Output: <out>-watchstalk.json, read by tools/harness/watch-stalk-report.ts. Progress line every 30 s on stdout.
//
// Two phases, never together (a watch hit inside Stalker's code cache would report a cache address, not the reader):
// 1. watch (first watchS seconds, default half the run). Mid-function hooks right after the two +0x1F4 increments
//    (IC_Route_Replan 0x7654C1, IC_Route_ReplanFromPosition 0x765590; esi = route, no branch lands inside the 5 patched
//    bytes) queue routes whose vtable is 0x10047CC. A timer on the script thread arms up to `slots` debug registers, one
//    route each, as 4-byte 'rw' watchpoints on route+0x1F4 on every game thread (debug registers are per thread: new
//    threads are armed on the next tick). A watch ends when the route's vtable changes (freed or reused), after holdS, or
//    when maxHits is reached; maxRoutes caps the routes. Hits arrive as 'single-step' exceptions with the address of the
//    instruction after the access; the handler records it (module+offset, thread, whether every watched vtable was intact,
//    3 backtraces per site) and returns true. Expected: only the two IC writers (and route ctor/reset zeroing on reuse).
// 2. stalk (the rest). Interceptor on Route_PlanLocked 0x82C6E0; on the sim thread only ([0x11D8A8C]) and only inside a
//    sampling window (windowS every everyS, at most perWindow plans per window, maxPlans in all) the plan is followed with
//    Stalker. The JS transform puts a CModule C callout before every memory-writing instruction (capstone access 'w'/'rw',
//    plus x87 stores and cmpxchg/xadd, which capstone marks as reads) and one on 0x66AC37 (the clip scratch load); the
//    callout records written addresses inside .data/.tls 0x1146000-0x1235009 (address, count, first writer, plans).
//    Block starts in 0x66AB70-0x66AC53 are counted at compile time. Lock_Acquire/Release and 0x4E3F90, 0x78F2B0, 0x791C00
//    run natively (Stalker.exclude: their writes are not recorded). A window close forces an unfollow; a plan slower than
//    maxPlanMs stops stalking for the rest of the run (each followed plan recompiles its blocks: 50 ms - 1 s stall).
// Route release (docs/113 §8): a hook on the Route dtor 0x75D640 (reached from the deleting dtor 0x760350 when the last
//    reference goes, Release 0x8ABE60 -> [route+8] slot 0) marks a watched route released: later hits count as after-free
//    (the vtable check alone passed on freed memory that still held the old vtable, §7.1) and the timer unsets the slot.
// Worker mode (worker: true, docs/113 §8): the stalk phase follows Route_PlanLocked on the route worker instead of the sim
//    thread (RouteAsync on: the worker runs the whole original IC, so its Route_PlanLocked call goes through ts3native's
//    detour jump at 0x82C6E0; the strict check accepts an e9 whose target lies in a module matching detourMod, and Interceptor
//    follows that jump, so the hook sits on ts3native's PlanLockedHook). Worker tid = workerTid if given, else the first
//    non-sim thread that calls it (all non-sim callers are counted in stalk.tids). Looser caps (WORKER_CAPS): Stalker stalls
//    only the worker; the sim thread waits on a followed job only where it waits for any running job (gate entries on +0x918,
//    destroy drain, Mode 1 pumping), so a slow plan still costs sim time there and the caps stay.
// Hook clash: tools/frida/stutter-profile.js also hooks 0x82C6E0 (and 0x81B2A0 inside the plan). The prologue check is
// strict, so a prologue already patched by another Frida session is refused (listed in `refused`); do not run both:
// profile-world.ts --watchstalk skips the stutter profiler. agent.js's exception handler (ts3probe.py --exception-stacks
// only) may see watch hits first: it records the first 3 per site and returns false, so the hit still reaches this one.
'use strict';

const cfg = { out: null, minutes: 6, mode: 'both', watchS: null, slots: 4, holdS: 30, maxRoutes: 40, maxHits: 5000,
  everyS: 20, windowS: 5, perWindow: 3, maxPlans: 24, maxPlanMs: 3000, worker: false, workerTid: 0, addrs: null };
const WORKER_CAPS = { perWindow: 6, maxPlans: 48, maxPlanMs: 5000 }; // worker mode defaults; explicit parameters still win
const A = {
  simTid: 0x11d8a8c, routeVt: 0x10047cc, data: [0x1146000, 0x1235009], cov: [0x66ab70, 0x66ac53], c37: 0x66ac37,
  exclude: [0x4e16f0, 0x4e17b0, 0x4e3f90, 0x78f2b0, 0x791c00], detourMod: 'ts3native',
  hooks: { replan: [0x7654c1, '8d 4c 24 08 51 8b ce e8'], replanPos: [0x765590, 'f3 0f 11 4c 24 18 f3 0f'], plan: [0x82c6e0, '81 ec d8 00 00 00 53 56'],
    routeDtor: [0x75d640, '83 ec 08 53 8b d9 8b 83'] },
};
let X = A; // A plus the test-only `addrs` override (tools/probe/watch-stalk-test.py)

// capstone marks these memory destinations as read-only (checked on Frida 17.23.3 / ia32); the mem operand is the store target
const STORES = new Set(['fst', 'fstp', 'fist', 'fistp', 'fisttp', 'fbstp', 'fnstcw', 'fstcw', 'fnstsw', 'fstsw', 'fnstenv', 'fstenv',
  'fnsave', 'fsave', 'fxsave', 'cmpxchg', 'cmpxchg8b', 'xadd', 'xchg']);
const REG = { eip: 0, edi: 1, esi: 2, ebp: 3, esp: 4, ebx: 5, edx: 6, ecx: 7, eax: 8 }; // GumIA32CpuContext order
const CSRC = `#include <gum/gumstalker.h>
typedef struct { guint32 pc; gint32 disp; guint8 base; guint8 index; guint8 scale; guint8 flags; guint32 size; } Site;
typedef struct { guint32 addr; guint32 n; guint32 pc; guint32 plans; guint32 last; } Ent;
#define NTAB 16384
extern Site sites[];
extern Ent tab[];
extern guint32 v[];
#define lo v[0]
#define hi v[1]
#define plan v[2]
#define used v[3]
#define full v[4]
#define segw v[5]
#define writes v[6]
#define inrange v[7]
#define c37 v[8]
#define c37plans v[9]
#define c37last v[10]
static guint32 reg (GumCpuContext * c, guint8 r) { return r == 0xff ? 0 : ((guint32 *) c)[r]; }
void on_write (GumCpuContext * c, gpointer ud)
{
  Site * s = &sites[(guint32) (gsize) ud];
  guint32 ea, len, a, h, i;
  writes++;
  if (s->flags & 1) { segw++; return; }
  ea = (guint32) s->disp + reg (c, s->base) + reg (c, s->index) * s->scale;
  len = (s->flags & 2) ? reg (c, 7) * s->size : s->size;
  if (len > 0x1000000) len = 0x1000000;
  if (len == 0 || ea >= hi || ea + len <= lo) return;
  a = ea < lo ? lo : ea;
  inrange++;
  h = (a * 2654435761u) >> 18;
  for (i = 0; i < NTAB; i++, h = (h + 1) & (NTAB - 1)) {
    Ent * e = &tab[h];
    if (e->addr == a) { e->n++; if (e->last != plan) { e->last = plan; e->plans++; } return; }
    if (e->addr == 0) {
      if (used >= NTAB / 2) { full++; return; }
      used++; e->addr = a; e->n = 1; e->pc = s->pc; e->plans = 1; e->last = plan; return;
    }
  }
}
void on_c37 (GumCpuContext * c, gpointer ud) { c37++; if (c37last != plan) { c37last = plan; c37plans++; } }
`;

let t0 = 0, done = false, phase = 'init', timers = [], hooked = [], refused = {}, cm = null, mm = null, planVia = null;
const errors = [];
const W = { replans: { replan: 0, replanPos: 0 }, badRoute: 0, badVt: 0, pending: [], seen: new Set(), slots: [], routes: 0,
  everArmed: false, nHits: 0, stray: 0, noDr: 0, hits: {}, ended: [], dropped: { freed: 0, released: 0, held: 0, end: 0, freedBeforeArm: 0 }, dtors: 0,
  armErrors: 0, unsetErrors: 0, threads: 0, stop: null };
const S = { calls: 0, simCalls: 0, otherThread: 0, plans: 0, open: false, winPlans: 0, windows: 0, following: 0, forced: 0,
  stopped: null, rows: [], cov: {}, curCov: false, sites: 0, siteFull: 0, transforms: 0, workerTid: 0, workerCalls: 0, tids: {} };
const mem = { u32: a => ptr(a).readU32() };
const hex = x => '0x' + (x >>> 0).toString(16);
const nowS = () => Math.round((Date.now() - t0) / 100) / 10;
const bump = (o, k, n = 1) => { o[k] = (o[k] || 0) + n; };
const err = m => { if (errors.length < 50) errors.push(`${nowS()} s: ${m}`); };
let mods = null;
function modOf(a) {
  mods ??= Process.enumerateModules().map(m => [m.base.toUInt32(), m.size, m.name]);
  for (const [b, n, name] of mods) if (a >= b && a < b + n) return `${name}+${hex(a - b)}`;
  return hex(a);
}
const simTid = () => mem.u32(X.simTid);
const V = { lo: 0, hi: 1, plan: 2, used: 3, full: 4, segw: 5, writes: 6, inrange: 7, c37: 8, c37plans: 9, c37last: 10 }; // v[] in CSRC
const vget = k => (mm ? mm.v.add(4 * V[k]).readU32() : 0);
const vset = (k, x) => mm.v.add(4 * V[k]).writeU32(x);

// --- watch phase (U10) ---
function onReplan(kind) {
  return {
    onEnter() {
      if (phase !== 'watch') return;
      W.replans[kind]++;
      const r = this.context.esi.toUInt32();
      if (W.seen.has(r) || W.pending.length >= 8 || W.routes + W.pending.length >= cfg.maxRoutes) return;
      try { if (mem.u32(r) !== X.routeVt) { W.badVt++; return; } } catch (e) { W.badRoute++; return; }
      W.seen.add(r); W.pending.push(r);
    },
  };
}
function onRouteDtor(r) {
  if (phase !== 'watch') return;
  W.dtors++;
  const s = W.slots.find(x => x && x.route === r);
  if (s) { s.released ??= nowS(); return; } // the timer unsets it; hits until then count as after-free
  const i = W.pending.indexOf(r);
  if (i >= 0) { W.pending.splice(i, 1); W.dropped.freedBeforeArm++; }
}
const threadsById = () => new Map(Process.enumerateThreads().map(t => [t.id, t]));
function unsetSlot(i, why) {
  const s = W.slots[i];
  if (!s) return;
  W.slots[i] = null;
  const live = threadsById();
  for (const id of s.tids) {
    const t = live.get(id);
    if (!t) continue; // exited: its debug registers went with it
    try { t.unsetHardwareWatchpoint(i); } catch (e) { W.unsetErrors++; err(`unset slot ${i} tid ${id}: ${e.message}`); }
  }
  W.dropped[why]++;
  if (W.ended.length < 200) W.ended.push({ route: hex(s.route), slot: i, from: s.from, held: nowS() - s.from, threads: s.tids.size, end: why, released: s.released ?? null });
}
function armTick() {
  if (phase !== 'watch') return;
  for (let i = 0; i < cfg.slots; i++) {
    const s = W.slots[i];
    if (!s) continue;
    if (s.released) { unsetSlot(i, 'released'); continue; }
    let gone = false;
    try { gone = mem.u32(s.route) !== X.routeVt; } catch (e) { gone = true; }
    if (gone) unsetSlot(i, 'freed');
    else if (Date.now() - s.t > cfg.holdS * 1000) unsetSlot(i, 'held');
  }
  if (W.stop) { endWatch(W.stop); return; }
  for (let i = 0; i < cfg.slots && W.pending.length; i++) {
    if (W.slots[i]) continue;
    const r = W.pending.shift();
    try { if (mem.u32(r) !== X.routeVt) { W.dropped.freedBeforeArm++; i--; continue; } } catch (e) { W.dropped.freedBeforeArm++; i--; continue; }
    W.slots[i] = { route: r, t: Date.now(), from: nowS(), tids: new Set() };
    W.routes++;
  }
  if (!W.slots.some(Boolean)) return;
  const threads = Process.enumerateThreads();
  W.threads = threads.length;
  for (const t of threads)
    for (let i = 0; i < cfg.slots; i++) {
      const s = W.slots[i];
      if (!s || s.tids.has(t.id)) continue;
      try { t.setHardwareWatchpoint(i, ptr(s.route + 0x1f4), 4, 'rw'); s.tids.add(t.id); W.everArmed = true; }
      catch (e) { W.armErrors++; err(`arm slot ${i} tid ${t.id}: ${e.message}`); }
    }
}
// x86 CONTEXT (winnt.h): ContextFlags +0, Dr0..Dr3 +4..+0x10, Dr6 +0x14, Dr7 +0x18; CONTEXT_DEBUG_REGISTERS = 0x10010
function onException(d) {
  if (d.type !== 'single-step' || !W.everArmed) return false;
  try {
    const tid = Process.getCurrentThreadId(), pc = d.address.toUInt32(), nc = d.nativeContext;
    const hasDr = !nc.isNull() && (nc.readU32() & 0x10010) === 0x10010;
    let slot = -1;
    if (hasDr) {
      const dr6 = nc.add(0x14).readU32();
      slot = [0, 1, 2, 3].find(i => dr6 & (1 << i)) ?? -1;
      // the thread resumes from this saved context, which holds the debug registers of the moment of the trap: write the
      // intended state into it, or an unset or re-arm the timer did while this thread waited here would be undone
      let dr7 = nc.add(0x18).readU32();
      for (let i = 0; i < 4; i++) {
        const s = phase === 'watch' ? W.slots[i] : null;
        dr7 &= ~((3 << (2 * i)) | (0xf << (16 + 4 * i)));
        if (s && s.tids.has(tid)) { nc.add(4 + 4 * i).writeU32(s.route + 0x1f4); dr7 |= (1 << (2 * i)) | (0xf << (16 + 4 * i)); }
      }
      nc.add(0x18).writeU32(dr7 >>> 0); nc.add(0x14).writeU32(0);
    } else W.noDr++;
    const s = slot >= 0 ? W.slots[slot] : W.slots.find(Boolean);
    if (phase !== 'watch' || !s) { W.stray++; return true; }
    W.nHits++;
    let vtOk = false; // live: vtable intact and the dtor not seen (a freed block can keep the old vtable, §7.1)
    try { vtOk = !s.released && mem.u32(s.route) === X.routeVt; } catch (e) { /* unmapped: freed */ }
    const key = hex(pc) + (vtOk ? '' : ':freed');
    if (!W.hits[key] && Object.keys(W.hits).length >= 500) return true;
    const h = W.hits[key] ??= { pc: hex(pc), at: modOf(pc), vtOk, n: 0, sim: 0, tids: {}, stacks: [] };
    h.n++; bump(h.tids, tid);
    if (tid === simTid()) h.sim++;
    if (h.stacks.length < 3) h.stacks.push(Thread.backtrace(d.context, Backtracer.FUZZY).slice(0, 8).map(p => modOf(p.toUInt32())));
    if (W.nHits >= cfg.maxHits && !W.stop) W.stop = `maxHits ${cfg.maxHits}`;
  } catch (e) { err(`hit: ${e.message}`); }
  return true; // ours: a data watchpoint trap fires after the access, resuming at d.address is correct
}
function endWatch(why, next = true) {
  if (phase !== 'watch') return;
  for (let i = 0; i < cfg.slots; i++) unsetSlot(i, 'end');
  W.endReason = why; W.pending = [];
  console.log(`[watchstalk] watch phase over (${why}): ${W.routes} routes, ${W.nHits} hits`);
  phase = 'idle';
  if (next && cfg.mode === 'both' && !refused.plan && !refused.install)
    try { startStalk(); } catch (e) { phase = 'idle'; refused.stalk = String(e.message); err(`stalk start: ${e.stack}`); }
}

// --- stalk phase (U1, U3) ---
function siteOf(ins, a) {
  const mn = ins.mnemonic.replace(/^(rep|repe|repne|lock) /, '');
  let op = ins.operands.find(o => o.type === 'mem' && /w/.test(o.access ?? ''));
  if (!op && STORES.has(mn)) op = ins.operands.find(o => o.type === 'mem');
  if (!op || mn === 'lea' || mn === 'nop') return -1;
  if (S.sites >= 65536) { S.siteFull++; return -1; }
  const ix = S.sites++, v = op.value, p = mm.sites.add(ix * 16);
  const seg = v.segment === 'fs' || v.segment === 'gs';
  p.writeU32(a); p.add(4).writeS32(v.disp | 0); p.add(8).writeU8(REG[v.base] ?? 0xff); p.add(9).writeU8(REG[v.index] ?? 0xff);
  p.add(10).writeU8(v.scale || 1); p.add(11).writeU8((seg ? 1 : 0) | (/^rep/.test(ins.mnemonic) ? 2 : 0)); p.add(12).writeU32(op.size || 4);
  return ix;
}
// Stalker recompiles on every follow, so the site table would grow per plan; one entry per instruction address instead
const siteIx = new Map();
function transform(iterator) {
  S.transforms++;
  let ins = iterator.next(), first = true;
  while (ins !== null) {
    const a = ins.address.toUInt32();
    if (first && a >= X.cov[0] && a < X.cov[1]) { bump(S.cov, hex(a)); S.curCov = true; }
    first = false;
    if (a === X.c37) iterator.putCallout(cm.on_c37);
    let ix = siteIx.get(a);
    if (ix === undefined) { ix = siteOf(ins, a); siteIx.set(a, ix); }
    if (ix >= 0) iterator.putCallout(cm.on_write, ptr(ix));
    iterator.keep();
    ins = iterator.next();
  }
}
const planListener = {
  onEnter() {
    if (phase !== 'stalk') return;
    S.calls++;
    const tid = Process.getCurrentThreadId();
    if (tid === simTid()) S.simCalls++;
    else {
      S.otherThread++;
      if (cfg.worker) { if (S.tids[tid] || Object.keys(S.tids).length < 16) bump(S.tids, tid); S.workerTid ||= tid; }
    }
    if (tid !== (cfg.worker ? S.workerTid : simTid())) return;
    if (cfg.worker) S.workerCalls++;
    if (!S.open || S.following || S.stopped || S.winPlans >= cfg.perWindow || S.plans >= cfg.maxPlans) return;
    S.plans++; S.winPlans++;
    vset('plan', S.plans);
    this.plan = S.plans; this.t = Date.now(); this.c37 = vget('c37'); this.w = vget('writes');
    S.following = tid; S.curCov = false;
    Stalker.follow(tid, { transform });
  },
  onLeave() {
    if (!this.plan) return;
    const forced = S.following !== Process.getCurrentThreadId();
    if (!forced) { Stalker.unfollow(S.following); S.following = 0; }
    const ms = Date.now() - this.t;
    S.rows.push({ plan: this.plan, t: nowS(), ms, forced, cov: S.curCov, c37: vget('c37') - this.c37, writes: vget('writes') - this.w });
    if (ms > cfg.maxPlanMs && !S.stopped) S.stopped = `plan ${this.plan} took ${ms} ms > maxPlanMs ${cfg.maxPlanMs}`;
    if (S.plans >= cfg.maxPlans && !S.stopped) S.stopped = `maxPlans ${cfg.maxPlans}`;
    setTimeout(() => Stalker.garbageCollect(), 50);
  },
};
function closeWindow() {
  S.open = false;
  if (S.following) { Stalker.unfollow(S.following); S.following = 0; S.forced++; err('window closed during a followed plan: forced unfollow'); }
}
function startStalk() {
  phase = 'stalk';
  // the C state lives in JS-allocated memory passed as symbols (CModule's own globals are not writable from JS here)
  const zero = n => { const p = Memory.alloc(n); p.writeByteArray(new ArrayBuffer(n)); return p; };
  mm = { sites: zero(65536 * 16), tab: zero(16384 * 20), v: zero(64) };
  cm = new CModule(CSRC, mm);
  vset('lo', X.data[0]); vset('hi', X.data[1]);
  for (const a of X.exclude) Stalker.exclude({ base: ptr(a), size: 16 }); // calls to the start run natively
  hooked.push(Interceptor.attach(ptr(X.hooks.plan[0]), planListener));
  const open = () => {
    if (done || S.stopped) return;
    S.open = true; S.winPlans = 0; S.windows++;
    timers.push(setTimeout(closeWindow, cfg.windowS * 1000));
  };
  open();
  timers.push(setInterval(open, cfg.everyS * 1000));
  console.log(`[watchstalk] stalk phase: window ${cfg.windowS} s every ${cfg.everyS} s, ${cfg.perWindow} plans per window, ${cfg.maxPlans} max`);
}
function readTable() {
  if (!mm) return [];
  const n = 16384, b = new DataView(mm.tab.readByteArray(n * 20)), out = [];
  for (let i = 0; i < n; i++) {
    const a = b.getUint32(i * 20, true);
    if (a) out.push({ addr: hex(a), n: b.getUint32(i * 20 + 4, true), pc: hex(b.getUint32(i * 20 + 8, true)), at: modOf(b.getUint32(i * 20 + 8, true)), plans: b.getUint32(i * 20 + 12, true) });
  }
  return out.sort((x, y) => parseInt(x.addr, 16) - parseInt(y.addr, 16));
}

// --- common ---
function bytesAt(a) { return Array.from(new Uint8Array(ptr(a).readByteArray(8))).map(b => b.toString(16).padStart(2, '0')).join(' '); }
function check(name) {
  const [a, expect] = X.hooks[name];
  const got = bytesAt(a);
  if (got === expect) return true;
  if (name === 'plan' && cfg.worker && got.startsWith('e9')) { // ts3native's detour (InstallDetour: e9 straight to the hook)
    const t = (a + 5 + ptr(a + 1).readS32()) >>> 0, m = Process.findModuleByAddress(ptr(t));
    if (m && new RegExp(X.detourMod, 'i').test(m.name)) { planVia = `${m.name} detour, hook at ${modOf(t)}`; return true; }
  }
  refused[name] = `bytes ${got}, expected ${expect}` + (got.startsWith('e9') ? ' (a jump: another Frida session hooks it, e.g. stutter-profile.js; run without it)' : '');
  console.log(`[watchstalk] ${name} refused: ${refused[name]}`);
  return false;
}
function write(reason) {
  const c = vget;
  const res = { version: 1, reason, params: { ...cfg, addrs: undefined }, seconds: nowS(), refused, hooks: hooked.length,
    watch: { replans: W.replans, badRoute: W.badRoute, badVt: W.badVt, routes: W.routes, threads: W.threads, nHits: W.nHits, dtors: W.dtors,
      stray: W.stray, noDr: W.noDr, endReason: W.endReason ?? null, armErrors: W.armErrors, unsetErrors: W.unsetErrors,
      dropped: W.dropped, ended: W.ended, hits: Object.values(W.hits).sort((x, y) => y.n - x.n) },
    stalk: { worker: cfg.worker, planVia, workerTid: S.workerTid, workerCalls: S.workerCalls, tids: S.tids, calls: S.calls, simCalls: S.simCalls, otherThread: S.otherThread, plans: S.plans, windows: S.windows, forced: S.forced,
      stopped: S.stopped, sites: S.sites, siteFull: S.siteFull, transforms: S.transforms, rows: S.rows, cov: S.cov,
      c37: c('c37'), c37plans: c('c37plans'), writes: c('writes'), inrange: c('inrange'), segWrites: c('segw'), tableFull: c('full'),
      data: X.data.map(hex), excluded: X.exclude.map(hex), written: readTable() },
    errors };
  const f = new File(`${cfg.out}-watchstalk.json`, 'w');
  f.write(JSON.stringify(res, null, 1)); f.flush(); f.close();
  console.log(`[watchstalk] wrote ${cfg.out}-watchstalk.json (${reason})`);
}
function finish(reason = 'complete') {
  if (done) return;
  if (phase === 'watch') endWatch(reason, false);
  done = true; phase = 'done';
  timers.forEach(clearInterval); timers.forEach(clearTimeout);
  closeWindow();
  for (const h of hooked) h.detach();
  hooked = [];
  Interceptor.flush();
  write(reason);
}
let last = { replans: 0, plans: 0 }, misses = 0;
const planCalls = () => (cfg.worker ? S.workerCalls : S.simCalls);
function progress() {
  if (done) return;
  const left = Math.max(0, cfg.minutes * 60 - (Date.now() - t0) / 1000), rp = W.replans.replan + W.replans.replanPos;
  console.log(`[watchstalk] ${nowS()} s, ${phase}: replans ${rp}, routes ${W.routes} (live ${W.slots.filter(Boolean).length}), hits ${W.nHits}, threads ${W.threads}; ` +
    `plans followed ${S.plans} of ${planCalls()} ${cfg.worker ? `worker (tid ${S.workerTid || '?'})` : 'sim'} plans${S.stopped ? ` (stopped: ${S.stopped})` : ''}; job ${Math.ceil(left / 60)} min left`);
  // fail-fast: 2 checks of 30 s without the event the phase needs = fail with the reason
  const stuck = phase === 'watch' ? rp === last.replans : phase === 'stalk' ? planCalls() === last.plans : false;
  if (stuck && ++misses >= 2) {
    const what = phase === 'watch' ? 'route replan' : cfg.worker ? 'worker Route_PlanLocked call' : 'sim-thread Route_PlanLocked call';
    console.log(`[watchstalk] FAIL: no ${what} for ${misses * 30} s (game paused or not routing?)`);
    finish(`fail: no ${what} for ${misses * 30} s`);
    return;
  }
  if (!stuck) misses = 0;
  last = { replans: rp, plans: planCalls() };
}
function start(parameters) {
  if (t0) return;
  Object.assign(cfg, parameters?.worker ? WORKER_CAPS : {}, parameters || {});
  S.workerTid = cfg.workerTid || 0;
  if (!cfg.out) cfg.out = `watchstalk-${new Date().toISOString().replace(/[:.]/g, '-')}`;
  X = { ...A, ...(cfg.addrs || {}), hooks: { ...A.hooks, ...(cfg.addrs?.hooks || {}) } };
  t0 = Date.now();
  const watchS = cfg.mode === 'stalk' ? 0 : cfg.mode === 'watch' ? cfg.minutes * 60 : (cfg.watchS ?? cfg.minutes * 30);
  try {
    if (cfg.mode !== 'stalk') {
      const ok = ['replan', 'replanPos'].filter(check);
      for (const n of ok) hooked.push(Interceptor.attach(ptr(X.hooks[n][0]), onReplan(n)));
      // optional: when refused, the vtable check alone decides liveness, as before §8
      if (ok.length && check('routeDtor')) hooked.push(Interceptor.attach(ptr(X.hooks.routeDtor[0]), { onEnter() { onRouteDtor(this.context.ecx.toUInt32()); } }));
    }
    if (cfg.mode !== 'watch') check('plan');
  } catch (e) { refused.install = String(e.message); }
  const watchOk = cfg.mode !== 'stalk' && hooked.length > 0, stalkOk = cfg.mode !== 'watch' && !refused.plan && !refused.install;
  if (!watchOk && !stalkOk) { finish('fail: no usable hook'); return; }
  console.log(`[watchstalk] ${cfg.minutes} min, mode ${cfg.mode}${cfg.worker ? ' (worker)' : ''}${planVia ? `, plan via ${planVia}` : ''}; refused: ${Object.keys(refused).join(', ') || 'none'}`);
  timers.push(setInterval(progress, 30000));
  if (watchOk && watchS > 0) {
    Process.setExceptionHandler(onException);
    phase = 'watch';
    timers.push(setInterval(armTick, 250));
    timers.push(setTimeout(() => endWatch('time'), watchS * 1000));
  } else if (stalkOk) startStalk();
  timers.push(setTimeout(() => finish(), Math.max(10, cfg.minutes * 60 - 3) * 1000));
}
setTimeout(() => start({}), 1000); // fallback when the host does not call init

rpc.exports = {
  init(stage, parameters) { start(parameters); },
  dispose() { finish('detached before the end'); },
};
