// docs/154: bounded model checker of the async route job loop and its +0x918 lend windows (P5b, P5c, TailUnlock), H2's start
// hold, the gate / save / destroy / close drains, the replay consumer and a non-admitted main thread. Explicit-state BFS over
// every interleaving of the actors' atomic steps (one interlocked op or one guarded wait per step), with the transitions taken
// from native/ts3native (file:line in docs/154 §2). Checks: no deadlock, no lost job, every lend returned at J_DONE, no sim
// write while the worker reads live state (and no live read of a half-done sim write), save/destroy/close bodies never beside
// a job body, a non-admitted thread never gets the lent lock, a tripwire closes the window.
// Usage: node tools/model/p5-lock-model.ts [--self-check] [--only <config>] [--trace]
// --self-check: every config must end as its `expect` says (clean, or the named violation); mutations prove the checks bite.
// shortcut: abstract model (one world, 2 job slots, a few sim ops per run, infinite deadlines only); a clean result covers
// these interleavings, not the game's code paths the model leaves out (docs/154 §4).
export {}; // ES module marker first: `node --check` (CI syntax step) strips types only when the first parse error is ESM syntax

type Cfg = {
  name: string; ops: string[]; K: number; passOps?: string[];
  p5b?: boolean; p5c?: boolean; tu?: boolean; h2?: boolean; h2wd?: boolean; rp?: boolean; main?: boolean;
  door?: boolean; mgd?: boolean; trip?: boolean; d2?: boolean; tthrow?: boolean; wm?: boolean; wmTrip?: boolean; simWm?: boolean;
  cap?: boolean;
  fixNr?: boolean; fixHb?: boolean; fixWm?: boolean; mutGate?: boolean; mutNoDrop?: boolean;
  mutNoCloseRecheck?: boolean; mutNoBackstop?: boolean;  // undo fix A (CE-1) / fix D (CE-4), both in the code since docs/154 §7
  expect: string;  // 'clean' or a regex the outcome (violation text / 'deadlock: ...') must match
  why: string;
};
interface Sim { pc: string; n: number; stk: string[]; ac: string[]; hand: number; hand2: number; hbOff: number; mid: number; cb: number; bor: number;
  k: number; parked: number; took: number; lt: number; closed: number; }
interface Wk { pc: string; j: number; ph: string; exp: number; n: number; p5b: string; tu: number; rp: number; door: number;
  mgd: number; after: string; }
interface St { lo: number; lc: number; win: number; inN: number; lendBy: number; p5cJob: number; p5cOff: number; off: number;
  as: number; g0Win: number; g0In: number; closing: number; busy: number; world: number; jobs: string; cut: number[];
  ev: number; inv: number; wm: number; rp: number; snap: number; writes: number; hbJob: number; wd: number; viol: string;
  s: Sim; w: Wk; r: { pc: string; it: number }; m: { pc: string }; }
type Succ = { st: St; lbl: string };

const SIM = 1, WRK = 2, RPC = 3, MAIN = 4;
const cl = (x: St): St => ({ ...x, cut: x.cut.slice(), s: { ...x.s, stk: x.s.stk.slice(), ac: x.s.ac.slice() }, w: { ...x.w },
  r: { ...x.r }, m: { ...x.m } });
const bad = (st: St, msg: string) => { if (!st.viol) st.viol = msg; };
const canTake = (st: St, who: number) => st.lo === 0 || st.lo === who;
const asyncBusy = (st: St) => st.as === 1 || /[QR]/.test(st.jobs);
const setJob = (st: St, i: number, c: string) => { st.jobs = st.jobs.slice(0, i) + c + st.jobs.slice(i + 1); };

function init(c: Cfg): St {
  return { lo: 0, lc: 0, win: 0, inN: 0, lendBy: 0, p5cJob: 0, p5cOff: 0, off: 0, as: 0, g0Win: 0, g0In: 0, closing: 0, busy: 0,
    world: 1, jobs: 'FF', cut: [0, 0], ev: 0, inv: 0, wm: 0, rp: 0, snap: 0, writes: 0, hbJob: 0, wd: 0, viol: '',
    s: { pc: 'top', n: 0, stk: [], ac: [], hand: 0, hand2: 0, hbOff: 0, mid: 0, cb: 0, bor: 0, k: -1, parked: 0, took: 0, lt: 0, closed: 0 },
    w: { pc: 'idle', j: -1, ph: 'idle', exp: 0, n: 0, p5b: 'c', tu: 0, rp: 0, door: 0, mgd: 0, after: '' },
    r: { pc: c.rp ? 'r0' : 'done', it: 2 }, m: { pc: c.main ? 'm0' : 'done' } };
}

function take(st: St, who: number, c: Cfg) {
  if (who !== SIM && who !== WRK && st.win && st.lendBy && (c.p5b || c.tu)) bad(st, 'non-admitted thread got the lent +0x918');
  st.lo = who; st.lc++;
}
function relW(st: St) {  // worker Lock_Release (ServeAsync / Route_PlanLocked)
  if (st.lo !== WRK) { bad(st, 'worker released +0x918 it does not own (lend not returned)'); return; }
  if (--st.lc === 0) st.lo = 0;
}
// sim write classes: live = routing state the job reads, cache = steering's own cache (not read by the tail), cb = inside a
// callback the worker waits for, life = save / destroy / close body (object or world lifetime)
function simWrite(st: St, kind: string) {
  const ph = st.w.ph;
  if (kind === 'life') {
    if (st.as && ph !== 'idle' && ph !== 'pre') bad(st, 'save/destroy/close body while a job body runs');
    st.writes++;
    return;
  }
  if (ph === 'live') bad(st, `sim ${kind} write while the worker reads live state`);
  if (kind === 'cb' && ph !== 'proxy') bad(st, 'callback write while the worker is not waiting in it');
  if (kind !== 'cache') st.writes++;
}
function wRead(st: St) {  // the worker reads live routing state
  if (st.s.mid) bad(st, 'worker reads live state in the middle of a sim write (torn: lock handed back mid-write)');
  if (st.s.lt) bad(st, 'job reads live state during a save/destroy/close body');
  if (!st.world) bad(st, 'job runs on a torn-down world');
}

// ---------------- sim thread ----------------
const lendOn = (c: Cfg) => !!(c.p5b || c.tu);
function admitted(st: St, cls: string) {  // p5b.cpp:69 P5bAllowed (ReleaseGates off)
  return st.lo === SIM || (!st.s.cb && (cls === 'steer' || (st.p5cJob === 1 && cls === 'free')));
}
const trySucc = (st: St, c: Cfg, cls: string) => (!lendOn(c) || !st.win || admitted(st, cls)) && canTake(st, SIM);
const call = (s: Sim, sub: string, ret: string) => { s.stk.push(ret); s.pc = sub; };
const ret = (s: Sim) => { s.pc = s.stk.pop()!; };
const acq = (s: Sim, cls: string, retPc: string) => { s.ac.push(cls); call(s, 'a0', retPc); };
function finishAcq(st: St) {
  const top = st.s.ac.pop()!;
  if (top.endsWith('!')) st.off--;  // the pump's g_rgOffSimOpen hold (ts3native.cpp:2962)
  ret(st.s);
}
function relS(st: St) {
  if (st.s.bor) { st.s.bor--; return; }  // routegate.cpp:137 borrowed release
  if (st.lo !== SIM) { bad(st, 'sim released +0x918 it does not own'); return; }
  if (--st.lc === 0) st.lo = 0;
}
function handBack(st: St, c: Cfg, field: 'hand' | 'hand2') {  // p5c.cpp:210 P5cHandBegin
  if (c.p5c && st.win && st.lo === SIM) {
    st.s[field] = st.lc; st.lo = 0; st.lc = 0; st.p5cOff = 1; st.hbJob = 1;
    if (c.fixHb) { st.off++; st.s.hbOff = 1; }  // FIX C: no job starts until the levels are back
  }
}

function simSteps(st0: St, c: Cfg): Succ[] {
  const out: Succ[] = [];
  const go = (lbl: string, f: (st: St) => void) => { const st = cl(st0); f(st); out.push({ st, lbl: 'S:' + lbl }); };
  const s = st0.s, pc = s.pc;
  const cls = s.ac[s.ac.length - 1]?.replace('!', '') ?? '';
  switch (pc) {
    case 'end':  // the sim goes on ticking: the READY poll serves a running job's managed call (ts3native.cpp:2881)
      if (st0.inv === 1) go('READY poll: serve managed call', st => call(st.s, 'cb0', 'end'));
      return out;
    case 'top': {
      if (s.n >= c.K || s.closed) { go('end', st => { st.s.pc = 'end'; }); return out; }
      go('end', st => { st.s.pc = 'end'; });
      for (const op of c.ops) {
        if (op === 'start') {
          const i = st0.jobs.indexOf('F');
          if (i < 0 || st0.closing || !st0.world || st0.busy || st0.lo === SIM) continue;
          go('START j' + i, st => { setJob(st, i, 'Q'); st.cut[i] = st.writes; st.ev = 1; st.s.n++; });  // ts3native.cpp:2786
        } else if (op === 'collect') {
          for (let k = 0; k < 2; k++) if (st0.jobs[k] !== 'F') go('collect j' + k, st => { st.s.n++; st.s.k = k; call(st.s, 'co0', 'top'); });
        } else go(op, st => { st.s.n++; call(st.s, op, 'top'); });
      }
      return out;
    }
    // --- ops ---
    case 'steer': go('steer: acquire +0x918', st => acq(st.s, 'steer', 'st1')); break;
    case 'st1':
      if (c.simWm) go('steer: needs WM+0x228 (dirty lot index rebuild)', st => { st.s.pc = 'st1w'; });
      else go('steer: obstacle query (cache write)', st => { simWrite(st, 'cache'); st.s.pc = 'st2'; });
      break;
    case 'st1w': if (st0.wm === 0) go('steer: WM+0x228 taken', st => { st.wm = SIM; st.s.pc = 'st1x'; }); break;
    case 'st1x': go('steer: rebuild, WM released, query', st => { st.wm = 0; simWrite(st, 'cache'); st.s.pc = 'st2'; }); break;
    case 'st2': go('steer: release', st => { relS(st); ret(st.s); }); break;
    case 'writer': go('writer: acquire +0x918', st => acq(st.s, 'free', 'wr1')); break;
    case 'wr1': go('writer: first half written', st => { simWrite(st, 'live'); st.s.mid = 1; st.s.pc = 'wr2'; }); break;
    case 'wr2':
      if (c.d2) go('writer: nested D2 entry (P5cNrEnter)', st => call(st.s, 'n0', 'wr3'));
      else go('writer: no nested wait', st => { st.s.pc = 'wr3'; });
      break;
    case 'wr3': go('writer: second half written', st => { simWrite(st, 'live'); st.s.mid = 0; st.s.pc = 'wr4'; }); break;
    case 'wr4': go('writer: release', st => { relS(st); ret(st.s); }); break;
    case 'gate':  // routegate.cpp:284-296: g_rgOffSimOpen++, then read A_RUNNING
      if (c.mutGate) go('gate (MUTATION: reads A_RUNNING first)', st => { st.s.took = st.as; st.s.pc = 'g0m'; });
      else go('gate: g_rgOffSimOpen++', st => { st.off++; st.s.pc = 'g1'; });
      break;
    case 'g0m': go('gate: g_rgOffSimOpen++', st => { st.off++; st.s.pc = st.s.took ? 'g1t' : 'g2'; }); break;
    case 'g1':
      if (st0.as) go('gate: job running, wait for +0x918', st => { st.s.took = 1; acq(st.s, 'gate', 'g2'); });
      else go('gate: no job, frame open', st => { st.s.pc = 'g2'; });
      break;
    case 'g1t': go('gate: wait for +0x918', st => acq(st.s, 'gate', 'g2')); break;
    case 'g2': go('gate: leaf body (live write)', st => { simWrite(st, 'live'); st.s.pc = 'g3'; }); break;
    case 'g3': go('gate: leave', st => { if (st.s.took) relS(st); st.s.took = 0; st.off--; ret(st.s); }); break;
    case 'co0': {  // ts3native.cpp:2625 IcCollect
      const j = st0.jobs[s.k];
      if (j === 'Q') go(`collect: j${s.k} taken back, sync plan`, st => { setJob(st, s.k, 'F'); st.s.pc = 'sp0'; });
      else if (j === 'R') go(`collect: WaitJobDone j${s.k}`, st => call(st.s, 'wj0', 'co1'));
      else go(`collect: j${s.k} result taken`, st => { setJob(st, s.k, 'F'); ret(st.s); });
      break;
    }
    case 'co1': go('collect: done', st => { setJob(st, st.s.k, 'F'); ret(st.s); }); break;
    case 'sp0':  // RunPlan ts3native.cpp:2244 (P5cPlanHere when the lent lock is ours)
      if (st0.as && !(st0.win && st0.lo === SIM)) go('sync plan: WaitAsyncDone', st => call(st.s, 'wa0', 'sp1'));
      else go('sync plan: no wait', st => { st.s.pc = 'sp1'; });
      break;
    case 'sp1': go('sync plan: Route_PlanLocked acquire', st => acq(st.s, 'free', 'sp2')); break;
    case 'sp2': go('sync plan: plan (live)', st => { simWrite(st, 'live'); st.s.pc = 'sp3'; }); break;
    case 'sp3': go('sync plan: release', st => { relS(st); ret(st.s); }); break;
    case 'save':  // rtsites.cpp:258-264
      go('save: rtBusy++, g_rgOffSimOpen++', st => { st.busy++; st.off++; if (c.p5b) call(st.s, 'n0', 'sv1'); else st.s.pc = 'sv1'; });
      break;
    case 'sv1': go('save: RtWaitJob', st => call(st.s, 'rtw', 'sv2')); break;
    case 'sv2': go('save: body', st => { st.s.lt = 1; simWrite(st, 'life'); st.s.pc = 'sv3'; }); break;
    case 'sv3': go('save: leave', st => { st.s.lt = 0; st.busy--; st.off--; ret(st.s); }); break;
    case 'destroy': go('destroy: g_rgOffSimOpen++', st => { st.off++; st.s.pc = 'dd1'; }); break;  // rtsites.cpp:204-212
    case 'dd1':
      if (c.p5c && st0.p5cJob && st0.win) go('destroy: parked (P5cPark)', st => { st.s.parked = 1; st.s.pc = 'dd3'; });
      else if (c.p5b) go('destroy: D2 entry (P5cNrEnter)', st => call(st.s, 'n0', 'dd2'));
      else go('destroy: no D2', st => { st.s.pc = 'dd2'; });
      break;
    case 'dd2': go('destroy: RtWaitJob', st => call(st.s, 'rtw', 'dd3')); break;
    case 'dd3': go('destroy: body', st => { if (!st.s.parked) { st.s.lt = 1; simWrite(st, 'life'); } st.s.pc = 'dd4'; }); break;
    case 'dd4': go('destroy: leave', st => { if (!st.s.closed) st.s.lt = 0; st.s.parked = 0; st.off--; ret(st.s); }); break;
    case 'close':  // rtsites.cpp:250-256: rtClosing = 1, RtWaitJob; no g_rgOffSimOpen frame
      go('close: rtClosing = 1', st => { st.closing = 1; if (c.p5b) call(st.s, 'n0', 'c1'); else st.s.pc = 'c1'; });
      break;
    case 'c1': go('close: RtWaitJob', st => call(st.s, 'rtw', 'c2')); break;
    case 'c2': go('close: unlocked teardown', st => { st.s.lt = 1; st.s.closed = 1; simWrite(st, 'life'); st.s.pc = 'c3'; }); break;
    case 'c3': go('close: locked teardown acquire', st => acq(st.s, 'gate', 'c4')); break;
    case 'c4': go('close: locked teardown, world gone', st => { simWrite(st, 'life'); relS(st); st.world = 0; ret(st.s); }); break;
    case 'pass':  // locowin.cpp:36-62
      go('pass: g_g0Win = 1', st => { if (c.h2) st.g0Win = 1; st.s.pc = 'p1'; });
      break;
    case 'p1': go('pass: g_g0wIn = 1', st => { st.g0In = 1; st.s.pc = 'p2'; }); break;
    case 'p2':
      go('pass: nothing routed', st => { st.s.pc = 'p3'; });
      for (const op of c.passOps ?? []) {
        if (op === 'collect') {
          for (let k = 0; k < 2; k++) if (st0.jobs[k] !== 'F') go('pass: collect j' + k, st => { st.s.k = k; call(st.s, 'co0', 'p3'); });
        } else go('pass: ' + op, st => call(st.s, op, 'p3'));
      }
      break;
    case 'p3': go('pass: end (g_g0wIn = 0, g_g0Win = 0)', st => { st.g0In = 0; st.g0Win = 0; ret(st.s); }); break;
    // --- RtWaitJob rtsites.cpp:76 ---
    case 'rtw':
      if (!st0.as || (st0.lo === SIM && !(st0.win && st0.lendBy))) go('RtWaitJob: no job / real owner', st => ret(st.s));
      else if (s.cb) go('RtWaitJob: inside a callback (no wait)', st => ret(st.s));
      else go('RtWaitJob: WaitAsyncDone', st => { st.s.pc = 'wa0'; });
      break;
    // --- WaitAsyncDone ts3native.cpp:2424 ---
    case 'wa0': go('WaitAsyncDone: P5cHandBegin', st => { handBack(st, c, 'hand'); st.s.pc = 'wa1'; }); break;
    case 'wa1':
      if (!st0.as) go('WaitAsyncDone: no job running', st => { st.s.pc = 'wa2'; });
      else if (st0.inv === 1 && !s.cb) go('WaitAsyncDone: serve managed call', st => call(st.s, 'cb0', 'wa1'));
      break;
    case 'wa2':
      if (s.hand) go('P5cHandEnd: retake one level', st => { st.s.hand--; acq(st.s, 'free', 'wa2'); });
      else go('WaitAsyncDone: return', st => { if (st.s.hbOff) { st.off--; st.s.hbOff = 0; } ret(st.s); });
      break;
    // --- WaitJobDone ts3native.cpp:2440 ---
    case 'wj0': go('WaitJobDone: G0wDrop, P5cHandBegin', st => {
      if (st.g0Win && st.g0In && !c.mutNoDrop) st.g0Win = 0;  // locowin.cpp:29 G0wDrop
      handBack(st, c, 'hand'); st.s.pc = 'wj1';
    }); break;
    case 'wj1':
      if (!/[QR]/.test(st0.jobs[s.k])) go('WaitJobDone: job done', st => { st.s.pc = 'wa2'; });
      else if (st0.inv === 1 && !s.cb) go('WaitJobDone: serve managed call', st => call(st.s, 'cb0', 'wj1'));
      break;
    // --- P5cNrEnter p5c.cpp:233 / P4NrEnter p4.cpp:325 ---
    case 'n0': go('P5cNrEnter: hand back if lent', st => { if (st.snap) handBack(st, c, 'hand2'); st.s.pc = 'n1'; }); break;
    case 'n1':
      if (!st0.snap) go('P4NrEnter: no clone run', st => { st.s.pc = 'n2'; });
      else if (c.fixNr && st0.inv === 1 && !s.cb) go('P4NrEnter: serve managed call (FIX)', st => call(st.s, 'cb0', 'n1'));
      break;  // else spins: Sleep(0) while SnLive()
    case 'n2':
      if (s.hand2) go('P5cHandEnd: WaitAsyncDone', st => { st.s.hand = st.s.hand2; st.s.hand2 = 0; st.s.pc = 'wa1'; });
      else go('P5cNrEnter: return', st => ret(st.s));
      break;
    // --- ServeInvoke ts3native.cpp:1938 ---
    case 'cb0': go('ServeInvoke: managed call', st => { st.s.cb = 1; acq(st.s, 'cb', 'cb1'); }); break;
    case 'cb1': go('callback: routing write', st => { simWrite(st, 'cb'); st.s.pc = 'cb2'; }); break;
    case 'cb2': go('callback: release, g_invDone', st => { relS(st); st.s.cb = 0; st.inv = 2; ret(st.s); }); break;
    // --- LockAcquireHook / LockAcquireAsync / P5bLock ts3native.cpp:2901, 2940; p5b.cpp:80 ---
    case 'a0':
      if (s.cb && st0.as) go('acquire: borrow (callback)', st => { st.s.bor++; finishAcq(st); });
      else if (st0.lo === SIM) go('acquire: recursive', st => { st.lc++; finishAcq(st); });
      else if (!asyncBusy(st0)) go('acquire: no job, blocking', st => { st.s.pc = 'ab'; });
      else go('acquire: job busy, pump try', st => { st.s.pc = 'at'; });
      break;
    case 'ab':
      if (!lendOn(c) || admitted(st0, cls)) { if (canTake(st0, SIM)) go('acquire: got +0x918', st => { take(st, SIM, c); finishAcq(st); }); }
      else go('P5bLock: g_p5bIn++', st => { st.inN++; st.s.pc = 'ab1'; });
      break;
    case 'ab1':
      if (st0.win) go('P5bLock: window open, g_p5bIn--', st => { st.inN--; st.s.pc = 'abw'; });
      else go('P5bLock: no window, game Lock_Acquire', st => { st.s.pc = 'ab2'; });
      break;
    case 'abw': if (!st0.win) go('P5bLock: window closed', st => { st.s.pc = 'ab'; }); break;
    case 'ab2': if (canTake(st0, SIM)) go('acquire: got +0x918, g_p5bIn--', st => { take(st, SIM, c); st.inN--; finishAcq(st); }); break;
    case 'at':
      if (trySucc(st0, c, cls)) go('acquire: try ok', st => { take(st, SIM, c); finishAcq(st); });
      else go('acquire: try failed, pump (g_rgOffSimOpen++)', st => { st.off++; st.s.ac[st.s.ac.length - 1] += '!'; st.s.pc = 'ap'; });
      break;
    case 'ap':
      if (!asyncBusy(st0)) go('pump: no job left, blocking', st => { st.s.pc = 'ab'; });
      if (st0.inv === 1 && !s.cb) go('pump: serve managed call', st => call(st.s, 'cb0', 'ap'));
      if (trySucc(st0, c, cls)) go('pump: try ok', st => { take(st, SIM, c); finishAcq(st); });
      break;
    default: throw new Error('sim pc ' + pc);
  }
  return out;
}

// ---------------- route worker (ServeAsync ts3native.cpp:2112) ----------------
function wrkSteps(st0: St, c: Cfg): Succ[] {
  const out: Succ[] = [];
  const go = (lbl: string, f: (st: St) => void) => { const st = cl(st0); f(st); out.push({ st, lbl: 'W:' + lbl }); };
  const w = st0.w;
  const retake = (st: St, after: string) => { st.w.after = after; st.w.pc = 'rt'; };
  const proxy = (st: St, after: string) => { st.w.ph = 'proxy'; st.inv = 1; st.w.after = after; st.w.pc = 'px1'; };
  switch (w.pc) {
    case 'idle': if (st0.ev) go('g_asyncReady', st => { st.ev = 0; st.w.pc = 'scan'; }); break;
    case 'scan': {
      const j = st0.jobs.indexOf('Q');
      go(j < 0 ? 'nothing queued, ServeAsync returns' : 'oldest queued j' + j, st => { st.w.j = j; st.w.pc = j < 0 ? 'idle' : 'claim'; });
      break;
    }
    case 'claim':
      go('CAS QUEUED->RUNNING', st => {
        if (st.jobs[w.j] === 'Q') { setJob(st, w.j, 'R'); st.w.pc = 'm1'; st.w.ph = 'pre'; } else st.w.pc = 'scan';
      });
      break;
    case 'm1':  // :2127
      go('world check (lock, rtClosing, rtBusy, quiet)', st => {
        if (!st.world || st.closing || st.busy) { setJob(st, w.j, 'K'); st.w.ph = 'idle'; st.w.pc = 'scan'; } else st.w.pc = 'spin';
      });
      break;
    case 'spin':  // :2142
      if (!st0.off && (!st0.g0Win || w.exp)) go('no frame open, no H2 hold', st => { st.w.pc = 'acq'; });
      if (c.h2wd && st0.g0Win && !w.exp) go('H2 watchdog (250 ms)', st => { st.w.exp = 1; st.wd++; });
      break;
    case 'acq': if (canTake(st0, WRK)) go('ServeAsync acquires +0x918', st => { take(st, WRK, c); st.w.pc = 'run'; }); break;
    case 'run': go('A_RUNNING', st => { st.as = 1; st.w.pc = 'chk'; }); break;
    case 'chk':  // :2148
      go('re-read world / rtClosing, g_rgOffSimOpen / H2', st => {  // fix A (CE-1): world and rtClosing first, then back off or out
        if (!c.mutNoCloseRecheck && (!st.world || st.closing)) { st.as = 0; relW(st); setJob(st, w.j, 'K'); st.w.ph = 'idle'; st.w.pc = 'scan'; return; }
        if (st.off || (st.g0Win && !w.exp)) { st.as = 0; relW(st); st.w.pc = 'spin'; return; }
        st.w.ph = 'live'; st.w.pc = 'pl';
      });
      break;
    case 'pl': go('Route_PlanLocked acquires (recursive)', st => { st.lc++; st.w.pc = 'l1'; }); break;
    case 'l1':
      go('reads live (start points, pending, lot table)', st => {
        wRead(st);
        st.w.pc = c.p5b ? 'b0' : c.tu ? 't0' : 'l3';
      });
      if (c.cap) go('FbJobCap: A* deadline hit', st => { wRead(st); st.w.pc = 'l4'; });
      break;
    // --- P5 clone run with P5b / P5c (p5.cpp:179, p5b.cpp, p5c.cpp) ---
    case 'b0':
      if (!c.rp) go('clone acquire', st => { st.w.pc = 'b1'; });
      else {
        if (st0.rp === 0) go('PcAcquire takes g_rpUse', st => { st.rp = WRK; st.w.rp = 1; st.w.pc = 'b1'; });
        else go('PcAcquire: g_rpUse busy until deadline (mat)', st => { st.w.pc = 'l3'; });
      }
      break;
    case 'b1': go('P5cLend / P5bOpen: g_p5bWin = 1', st => {
      if (c.p5c && !st.p5cOff) st.p5cJob = 1;
      st.win = 1; st.w.pc = 'b2';
    }); break;
    case 'b2':
      go('P5bOpen: g_p5bIn read', st => {
        if (st.inN) { st.win = 0; st.p5cJob = 0; st.w.p5b = 'd'; st.snap = 1; st.w.ph = 'hold'; st.w.pc = 'b3'; return; }  // refused
        st.w.n = st.lc; st.lo = 0; st.lc = 0; st.lendBy = 1; st.w.p5b = 'o'; st.snap = 1; st.w.ph = 'clone'; st.w.pc = 'b3';
      });
      break;
    case 'b3':
      go('clone run ends', st => { st.w.pc = 'b4'; });
      if (c.door && !w.door) go('door ask: P5bRetake (P5B_ASK)', st => { st.w.door = 1; if (st.w.p5b === 'o') retake(st, 'bd1'); else st.w.pc = 'bd1'; });
      if (c.mgd && !w.mgd) go('managed call in the clone run: P4Abort -> P5bAbort', st => {
        st.w.mgd = 1; if (st.w.p5b === 'o') retake(st, 'bm1'); else st.w.pc = 'bm1';
      });
      if (c.trip && w.p5b === 'o') go('tripwire (event / live lock): P5bAbort', st => { retake(st, 'b3t'); });
      if (c.wm && w.p5b === 'o') go('#5 stub: WM+0x228 in the clone run', st => {
        if (c.fixWm && c.simWm) retake(st, 'wmh'); else st.w.pc = 'wmw';
      });
      break;
    case 'b3t': go('rest of the run under the hold', st => { st.w.p5b = 'd'; st.w.ph = 'hold'; st.w.pc = 'b4'; }); break;
    case 'bd1': go('door ask: P4Disarm, proxied to the sim', st => { st.snap = 0; proxy(st, 'bd2'); }); break;
    case 'bd2': go('door ask answered: P4Arm, P5bOpen again', st => {
      st.snap = 1;
      if (st.w.p5b === 'd') { st.w.ph = 'hold'; st.w.pc = 'b3'; return; }
      st.win = 1; st.w.pc = 'b2';
    }); break;
    case 'bm1': go('managed call proxied (clone run still armed)', st => { st.w.p5b = 'd'; proxy(st, 'bm2'); }); break;
    case 'bm2': go('managed call answered', st => { st.w.ph = 'hold'; st.w.pc = 'b4'; }); break;
    case 'b4':
      if (w.p5b === 'o') go('P5cTake: P5bRetake', st => retake(st, 'b5'));
      else go('P5cTake: hold already back', st => { st.w.pc = 'b5'; });
      break;
    case 'b5':
      go('P5cStale / answer', st => {
        st.p5cJob = 0; st.snap = 0; st.w.p5b = 'c';
        if (st.w.rp) { st.rp = 0; st.w.rp = 0; }
        const stale = st.writes !== st.cut[w.j] || w.mgd;
        if (c.fixHb && st.hbJob) { st.w.ph = 'priv'; st.w.pc = 'l5'; return; }  // FIX: a hand-back fails the job (no live read)
        st.w.ph = 'live'; st.w.pc = stale ? 'l3' : 'l4';
      });
      break;
    // --- TailUnlock tailunlock.cpp:32-123 ---
    case 't0': go('TuOpen: g_p5bWin = 1', st => { st.win = 1; st.w.pc = 't1'; }); break;
    case 't1':
      go('TuOpen: g_p5bIn read', st => {
        if (st.inN) { st.win = 0; st.w.pc = 'l4'; return; }
        st.w.n = st.lc; st.lo = 0; st.lc = 0; st.lendBy = 2; st.w.tu = 1; st.w.ph = 'tail'; st.w.pc = 't2';
      });
      break;
    case 't2':
      go('passes 16-19 done: TuClose', st => retake(st, 'l4'));
      if (c.wm) go('pass 16: lot grid lookup (WM+0x228)', st => { if (c.fixWm && c.simWm) retake(st, 'wmh'); else st.w.pc = 'wmw'; });
      if (c.tthrow) go('exception unwinds through the passes (no TuClose)', st => { st.w.ph = 'priv'; st.w.pc = 'l5'; });
      break;
    // --- WM+0x228 inside a window (rtsites.cpp #5 stub / 0xC68680) ---
    case 'wmw': if (st0.wm === 0) go('WM+0x228 taken (window open)', st => { st.wm = WRK; st.w.pc = 'wm1'; }); break;
    case 'wm1':
      go('WM+0x228 released', st => { st.wm = 0; st.w.pc = c.tu ? 't2' : 'b3'; });
      if (c.wmTrip) go('tripwire inside WM scope: retake +0x918 holding WM+0x228', st => retake(st, 'wm2'));
      break;
    case 'wm2': go('WM+0x228 released (under the hold)', st => { st.wm = 0; st.w.tu = 0; st.w.p5b = st.w.p5b === 'o' ? 'd' : st.w.p5b; st.w.ph = 'hold'; st.w.pc = c.tu ? 'l4' : 'b4'; }); break;
    case 'wmh': if (st0.wm === 0) go('FIX: hold back first, then WM+0x228', st => { st.wm = WRK; st.w.pc = 'wm2'; }); break;
    // --- retake loop (P5bRetake p5b.cpp:148, TuClose tailunlock.cpp:66) ---
    case 'rt':
      if (canTake(st0, WRK)) go('retake +0x918 (all levels), g_p5bWin = 0', st => {
        st.lo = WRK; st.lc = st.w.n; st.win = 0; st.lendBy = 0;
        if (st.w.p5b === 'o') st.w.p5b = 'd';
        st.w.tu = 0; st.w.ph = 'hold'; st.w.pc = st.w.after;
      });
      break;
    case 'px1': if (st0.inv === 2) go('g_invDone', st => { st.inv = 0; st.w.pc = st.w.after; }); break;
    case 'l3': go('game plan reads live', st => { st.w.ph = 'live'; wRead(st); st.w.pc = 'l4'; }); break;
    case 'l4': go('IC tail reads live', st => { st.w.ph = 'live'; wRead(st); st.w.pc = 'l5'; }); break;
    case 'l5':
      go('Route_PlanLocked releases', st => {
        if (!c.mutNoBackstop && st.w.tu) { st.w.after = 'l5'; st.w.pc = 'rt'; return; }  // fix D (CE-4): LockReleaseHook / after CallIc: LendBackstop
        relW(st); st.w.pc = 'l6';
      });
      break;
    case 'l6':
      go('J_DONE: ServeAsync releases, A_IDLE', st => {
        if (st.win || st.lendBy) bad(st, 'J_DONE with the lend window still open');
        relW(st); st.as = 0; setJob(st, w.j, 'D');
        st.w = { pc: 'scan', j: -1, ph: 'idle', exp: 0, n: 0, p5b: 'c', tu: 0, rp: 0, door: 0, mgd: 0, after: '' };
        st.hbJob = 0;
      });
      break;
    default: throw new Error('worker pc ' + w.pc);
  }
  return out;
}

// ---------------- replay consumer (replay.cpp:3139 RpDrain) and a non-admitted main / EA thread ----------------
function rpSteps(st0: St, c: Cfg): Succ[] {
  const out: Succ[] = [];
  const go = (lbl: string, f: (st: St) => void) => { const st = cl(st0); f(st); out.push({ st, lbl: 'R:' + lbl }); };
  switch (st0.r.pc) {
    case 'done': break;
    case 'r0':
      if (!st0.r.it) go('done', st => { st.r.pc = 'done'; });
      else if (st0.rp === 0) go('g_rpUse exclusive', st => { st.rp = RPC; st.r.it--; st.r.pc = 'r1'; });
      break;
    case 'r1':
      go('try +0x918', st => {
        if ((!lendOn(c) || !st.win) && canTake(st, RPC)) { take(st, RPC, c); st.r.pc = 'r2'; } else { st.rp = 0; st.r.pc = 'r0'; }
      });
      break;
    case 'r2': go('apply records, release both', st => {
      if (st.w.ph === 'clone') bad(st, 'replay consumer writes the clone during a clone run');
      st.lc--; if (!st.lc) st.lo = 0; st.rp = 0; st.r.pc = 'r0';
    }); break;
  }
  return out;
}
function mainSteps(st0: St, c: Cfg): Succ[] {
  const out: Succ[] = [];
  const go = (lbl: string, f: (st: St) => void) => { const st = cl(st0); f(st); out.push({ st, lbl: 'M:' + lbl }); };
  switch (st0.m.pc) {
    case 'done': break;
    case 'm0':
      if (!lendOn(c)) { if (canTake(st0, MAIN)) go('Lock_Acquire', st => { take(st, MAIN, c); st.m.pc = 'm3'; }); }
      else go('P5bLock: g_p5bIn++', st => { st.inN++; st.m.pc = 'm1'; });
      break;
    case 'm1':
      if (st0.win) go('window open: g_p5bIn--, wait', st => { st.inN--; st.m.pc = 'mw'; });
      else go('no window: game Lock_Acquire (blocking)', st => { st.m.pc = 'm2'; });
      break;
    case 'mw': if (!st0.win) go('window closed', st => { st.m.pc = 'm0'; }); break;
    case 'm2': if (canTake(st0, MAIN)) go('got +0x918, g_p5bIn--', st => { take(st, MAIN, c); st.inN--; st.m.pc = 'm3'; }); break;
    case 'm3': go('body, release', st => { if (st.w.ph === 'live') bad(st, 'main writes while the worker reads live'); st.lc--; if (!st.lc) st.lo = 0; st.m.pc = 'done'; }); break;
  }
  return out;
}

// ---------------- explorer ----------------
type Result = { name: string; states: number; outcome: string; trace: string[] };
function terminalCheck(st: St): string {
  if (/R/.test(st.jobs)) return 'job left RUNNING';
  if (/Q/.test(st.jobs) && st.w.pc === 'idle' && !st.ev) return 'lost job: QUEUED with the worker asleep (no g_asyncReady)';
  if (st.lo || st.win || st.inN || st.off || st.p5cJob || st.wm || st.rp || st.lendBy) return `terminal state not clean (lo ${st.lo} win ${st.win} inN ${st.inN} off ${st.off} p5cJob ${st.p5cJob} wm ${st.wm} rp ${st.rp})`;
  return '';
}
function explore(c: Cfg, limit = 3_000_000): Result {
  const s0 = init(c);
  const k0 = JSON.stringify(s0);
  const par = new Map<string, [string, string]>([[k0, ['', '']]]);
  const q: St[] = [s0];
  const keys: string[] = [k0];
  const traceOf = (k: string) => { const t: string[] = []; for (let x = k; par.get(x)![0]; x = par.get(x)![0]) t.push(par.get(x)![1]); return t.reverse(); };
  for (let i = 0; i < q.length; i++) {
    if (par.size > limit) return { name: c.name, states: par.size, outcome: 'state limit', trace: [] };
    const st = q[i], key = keys[i];
    const succ = [...simSteps(st, c), ...wrkSteps(st, c), ...rpSteps(st, c), ...mainSteps(st, c)];
    if (!succ.length) {
      const done = st.s.pc === 'end' && st.w.pc === 'idle' && st.r.pc === 'done' && st.m.pc === 'done';
      const why = done ? terminalCheck(st) : `deadlock: sim at ${st.s.pc} [${st.s.stk.join('<')}], worker at ${st.w.pc} (${st.w.ph}), lock owner ${st.lo}, win ${st.win}, inv ${st.inv}, snap ${st.snap}, wm ${st.wm}`;
      if (why) return { name: c.name, states: par.size, outcome: why, trace: traceOf(key) };
      continue;
    }
    for (const { st: n, lbl } of succ) {
      const nk = JSON.stringify(n);
      if (par.has(nk)) continue;
      par.set(nk, [key, lbl]);
      if (n.viol) return { name: c.name, states: par.size, outcome: n.viol, trace: traceOf(nk) };
      if (n.wd && !c.h2wd) return { name: c.name, states: par.size, outcome: 'H2 watchdog fired', trace: traceOf(nk) };
      q.push(n); keys.push(nk);
    }
    q[i] = undefined as unknown as St; keys[i] = '';  // shortcut: BFS frees expanded states, keeps only the parent map
  }
  return { name: c.name, states: par.size, outcome: 'clean', trace: [] };
}

// ---------------- configs ----------------
const BASE = ['start', 'collect', 'steer', 'gate'];
const CFGS: Cfg[] = [
  { name: 'base', ops: [...BASE, 'destroy', 'save'], K: 4, expect: 'clean', why: 'A10 job loop, gate enforce, save / destroy drains' },
  { name: 'base-main-rp', ops: BASE, K: 3, main: true, rp: true, expect: 'clean', why: 'plus a main-thread acquirer and the replay consumer' },
  { name: 'h2', ops: ['start', 'collect', 'pass'], passOps: ['steer', 'gate', 'collect'], K: 4, h2: true, expect: 'clean', why: 'H2 start hold; every sim wait for a held job drops the window (no watchdog)' },
  { name: 'close', ops: ['start', 'collect', 'close'], K: 3, expect: 'clean', why: 'fix A (CE-1): ServeAsync re-reads rtClosing / g_rgLock after A_RUNNING (Dekker with RT_CLOSING), backs out J_SKIPPED' },
  { name: 'p5b', ops: [...BASE, 'destroy'], K: 3, p5b: true, door: true, trip: true, expect: 'clean', why: 'P5b lends to steering; door ask, tripwire' },
  { name: 'p5b-main-rp', ops: ['start', 'steer'], K: 2, p5b: true, door: true, main: true, rp: true, expect: 'clean', why: 'g_p5bIn Dekker against a blocking non-admitted acquirer; replay consumer' },
  { name: 'p5b-mgd-d2', ops: ['start', 'destroy'], K: 2, p5b: true, mgd: true, expect: 'deadlock: sim at n1', why: 'managed-call tripwire proxies to the sim while the run stays armed; a D2 entry spins on SnLive without serving it' },
  { name: 'p5b-mgd-d2-fixed', ops: ['start', 'destroy', 'collect'], K: 3, p5b: true, mgd: true, fixNr: true, expect: 'clean', why: 'FIX B: P4NrEnter serves the job\'s managed calls on the owner thread' },
  { name: 'p5c', ops: ['start', 'collect', 'steer', 'writer', 'destroy'], K: 3, p5b: true, p5c: true, door: true, expect: 'clean', why: 'P5c admits plain writers; destroys park; stale answers refused' },
  { name: 'p5c-handback', ops: ['start', 'writer'], K: 2, p5b: true, p5c: true, d2: true, expect: 'torn', why: 'a writer under the lent lock reaches a D2 entry: P5cHandBegin hands the lock back mid-write' },
  { name: 'p5c-handback-fixed', ops: ['start', 'writer', 'collect'], K: 3, p5b: true, p5c: true, d2: true, fixHb: true, expect: 'clean', why: 'FIX C: a hand-back fails the job (no live read after the retake) and holds g_rgOffSimOpen until the levels are back' },
  { name: 'tu', ops: [...BASE, 'destroy'], K: 3, tu: true, wm: true, cap: true, expect: 'clean', why: 'TailUnlock lends to steering; WM+0x228 in the window; FbJobCap' },
  { name: 'tu-main', ops: ['start', 'steer'], K: 2, tu: true, main: true, expect: 'clean', why: 'g_p5bIn Dekker for TuOpen' },
  { name: 'tu-throw', ops: ['start', 'collect'], K: 2, tu: true, tthrow: true, expect: 'clean', why: 'fix D (CE-4): an unwind through passes 16-19 skips TuClose; LendBackstop takes the levels back at the next worker release' },
  { name: 'tu-wm-trip', ops: ['start', 'steer'], K: 2, tu: true, wm: true, wmTrip: true, simWm: true, expect: 'deadlock', why: 'tripwire retake inside a WM+0x228 scope while the sim\'s admitted steering waits for WM+0x228 (inverted order)' },
  { name: 'tu-wm-trip-fixed', ops: ['start', 'steer'], K: 2, tu: true, wm: true, wmTrip: true, simWm: true, fixWm: true, expect: 'clean', why: 'FIX E: with the lot index dirty the worker takes the hold back before WM+0x228' },
  { name: 'p5b-wm-trip', ops: ['start', 'steer'], K: 2, p5b: true, wm: true, wmTrip: true, simWm: true, expect: 'deadlock', why: 'same inversion in a P5b clone run (#5 stub keeps WM+0x228 in clone runs, docs/94 D1)' },
  // mutations: the checks must bite
  { name: 'mut-close-recheck', ops: ['start', 'collect', 'close'], K: 3, mutNoCloseRecheck: true, expect: 'during a save/destroy/close body|close body while a job body runs', why: 'MUTATION (CE-1, code before fix A): rtClosing checked only before the start handshake; RT_CLOSING has no g_rgOffSimOpen frame' },
  { name: 'mut-tu-nobackstop', ops: ['start', 'collect'], K: 2, tu: true, tthrow: true, mutNoBackstop: true, expect: 'lend not returned', why: 'MUTATION (CE-4, code before fix D): the release after the unwind is not the worker\'s' },
  { name: 'mut-gate-order', ops: ['start', 'gate'], K: 2, mutGate: true, expect: 'while the worker reads live', why: 'MUTATION: gate reads A_RUNNING before publishing g_rgOffSimOpen' },
  { name: 'mut-h2-nodrop', ops: ['start', 'collect', 'pass'], passOps: ['collect'], K: 3, h2: true, mutNoDrop: true, expect: 'deadlock: sim at wj1', why: 'MUTATION: WaitJobDone without G0wDrop (and no watchdog)' },
  { name: 'h2-watchdog', ops: ['start', 'collect', 'pass'], passOps: ['collect'], K: 3, h2: true, h2wd: true, mutNoDrop: true, expect: 'clean', why: 'the same with the 250 ms watchdog: no deadlock, a held start goes after the timeout' },
];

function main() {
  const argv = process.argv.slice(2);
  const only = argv.includes('--only') ? argv[argv.indexOf('--only') + 1] : '';
  const showTrace = argv.includes('--trace');
  const self = argv.includes('--self-check');
  let fail = 0;
  for (const c of CFGS) {
    if (only && c.name !== only) continue;
    const t0 = Date.now();
    const r = explore(c);
    const ok = c.expect === 'clean' ? r.outcome === 'clean' : new RegExp(c.expect).test(r.outcome);
    if (!ok) fail++;
    console.log(`${ok ? 'OK  ' : 'FAIL'} ${c.name.padEnd(20)} ${String(r.states).padStart(8)} states ${String(Date.now() - t0).padStart(6)} ms  ${r.outcome}`);
    if ((showTrace || !ok) && r.trace.length) for (const l of r.trace) console.log('       ' + l);
  }
  if (self) {
    console.log(fail ? `self-check FAILED: ${fail} config(s) not as expected` : 'self-check ok');
    process.exit(fail ? 1 : 0);
  }
}
main();
