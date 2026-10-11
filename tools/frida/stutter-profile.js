// Sim-thread stutter profiler for TS3W.exe (Steam build, SHA-256 10b2423e...0609).
// Times what the simulation loop (MonoScriptHost::Simulate, 0xD81840) spends per iteration:
// script tick, route searches, GC stop-the-world marking, finalizers, catch-up steps.
// Addresses and reasons: docs/04-re-findings.md.
//
// Run (game already in Live mode):
//   frida -n TS3W.exe -l tools/frida/stutter-profile.js
//   frida -n TS3W.exe -l tools/frida/stutter-profile.js -P '{"out":"C:/Users/Public/ts3prof","hitchMs":50}'
// Output: <out>-seconds.csv (one row per second) and <out>-hitches.jsonl (one line per slow iteration).
// Default <out> is ts3prof-<timestamp> in the game's working directory (Game\Bin).

'use strict';

const HOOKS = {
  // name: [address, expected first bytes]
  simulate: [0xd81840, '81 ec 88 00 00 00 53 55'],
  scriptTick: [0xd80bc0, '55 8b ec 83 e4 f8 81 ec'],
  catchUpSteps: [0xd70730, '83 ec 1c 53 55 56 57 89'],
  gcTryToCollect: [0xe4a050, '83 3d 94 30 1f 01 00 74'],
  gcStoppedMark: [0xe49980, '51 e8 1a 78 00 00 a1 60'],
  gcInvokeFinalizers: [0xe697f0, 'e8 eb 1b fe ff 85 c0 74'],
  routeOuter: [0x82c6e0, '81 ec d8 00 00 00 53 56'], // lock + setup + search + unlock, returns success in AL
  routeSearch: [0x81b2a0, '55 8b ec 83 e4 f0 81 ec'], // the A* search itself
  Route_Plan: [0x765240, '55 8b ec 83 e4 f0 0f 57'],
  Route_Replan: [0x765550, '55 8b ec 83 e4 f0 8b 0d'],
  Route_PlanFromPoint: [0x74c820, '55 8b ec 83 e4 f0 8b 0d'],
  Route_ReplanFromPosition: [0x765160, '0f 57 c0 8b 0d c4 40 1d'],
  Route_IsPointRoutable: [0x765490, '8b 0d c4 40 1d 01 83 ec'],
};
const ROUTE_ENTRIES = ['Route_Plan', 'Route_Replan', 'Route_PlanFromPoint', 'Route_ReplanFromPosition', 'Route_IsPointRoutable'];

let cfg = { out: null, hitchMs: 50 };

// --- high-resolution clock (QPC), in ms ---
const k32 = Process.getModuleByName('kernel32.dll');
const qpc = new NativeFunction(k32.getExportByName('QueryPerformanceCounter'), 'int', ['pointer']);
const qpf = new NativeFunction(k32.getExportByName('QueryPerformanceFrequency'), 'int', ['pointer']);
const qbuf = Memory.alloc(8);
qpf(qbuf);
const ticksPerMs = Number(qbuf.readS64()) / 1000;
function now() { qpc(qbuf); return Number(qbuf.readS64()) / ticksPerMs; }

// --- per-iteration and per-second accumulators (sim thread only, except routeOtherThreads) ---
function blankIter() {
  return { script: 0, gcMark: 0, gcMarks: 0, gcAborted: 0, gcForced: 0, gcForcedMs: 0, fin: 0, finRun: 0,
           steps: 0, route: 0, routeSearch: 0, routes: 0, routeFails: 0, byEntry: {} };
}
function blankSecond() {
  return { iters: 0, maxIterMs: 0, hitches: 0, script: 0, gcMark: 0, gcMarks: 0, gcAborted: 0, gcForced: 0,
           gcForcedMs: 0, maxGcMarkMs: 0, fin: 0, finRun: 0, maxSteps: 0, route: 0, routeSearch: 0, routes: 0,
           routeFails: 0, maxRouteMs: 0, routeOtherThreads: 0 };
}
let simThread = null;
let iter = blankIter();
let iterStart = 0;
let sec = blankSecond();
let secStart = 0;
let inTryToCollect = false;
const entryStack = []; // script entry point for the current route call (sim thread)

let csv = null, jsonl = null;

function openOutputs() {
  const base = cfg.out || `ts3prof-${new Date().toISOString().replace(/[:.]/g, '-')}`;
  csv = new File(`${base}-seconds.csv`, 'w');
  jsonl = new File(`${base}-hitches.jsonl`, 'w');
  csv.write(Object.keys(blankSecond()).concat(['t']).join(',') + '\n');
  csv.flush();
  console.log(`[ts3prof] writing ${base}-seconds.csv and ${base}-hitches.jsonl (hitch >= ${cfg.hitchMs} ms)`);
}

function round(x) { return Math.round(x * 100) / 100; }

function endIteration(t) {
  if (iterStart === 0) { iterStart = t; secStart = t; return; }
  const wall = t - iterStart;
  sec.iters++;
  sec.maxIterMs = Math.max(sec.maxIterMs, wall);
  for (const k of ['script', 'gcMark', 'gcMarks', 'gcAborted', 'gcForced', 'gcForcedMs', 'fin', 'finRun',
                   'route', 'routeSearch', 'routes', 'routeFails']) sec[k] += iter[k];
  sec.maxSteps = Math.max(sec.maxSteps, iter.steps);
  if (wall >= cfg.hitchMs) {
    sec.hitches++;
    const rec = { t: round(t), wallMs: round(wall) };
    for (const [k, v] of Object.entries(iter)) rec[k] = typeof v === 'number' ? round(v) : v;
    jsonl.write(JSON.stringify(rec) + '\n');
    jsonl.flush();
  }
  if (t - secStart >= 1000) flushSecond(t);
  iter = blankIter();
  iterStart = t;
}

function flushSecond(t) {
  const row = Object.values(sec).map(v => round(v)).concat([round(t)]);
  csv.write(row.join(',') + '\n');
  csv.flush();
  if (sec.hitches > 0 || sec.maxIterMs > cfg.hitchMs)
    console.log(`[ts3prof] ${sec.iters} it/s, max ${round(sec.maxIterMs)} ms, hitches ${sec.hitches}, ` +
      `routes ${sec.routes} (${sec.routeFails} failed, ${round(sec.route)} ms), gc ${round(sec.gcMark)} ms ` +
      `(${sec.gcAborted} aborted, ${sec.gcForced} forced ${round(sec.gcForcedMs)} ms), fin ${round(sec.fin)} ms`);
  sec = blankSecond();
  secStart = t;
}

// --- hooks ---
function addr(name) {
  const [a, expect] = HOOKS[name];
  const p = ptr(a);
  const got = Array.from(new Uint8Array(p.readByteArray(8))).map(b => b.toString(16).padStart(2, '0')).join(' ');
  if (got !== expect) throw new Error(`${name} @ ${p}: bytes ${got}, expected ${expect}. Different TS3W.exe build?`);
  return p;
}

function timed(name, onDone, onEnterExtra) {
  Interceptor.attach(addr(name), {
    onEnter(args) { this.t0 = now(); if (onEnterExtra) onEnterExtra.call(this, args); },
    onLeave(retval) { onDone.call(this, now() - this.t0, retval); },
  });
}

function install() {
  const mod = Process.getModuleByName('TS3W.exe');
  if (!mod.base.equals(ptr(0x400000))) throw new Error(`TS3W.exe loaded at ${mod.base}, expected 0x400000`);
  addr('simulate'); // only verified, never hooked: it loops forever

  // One GC_try_to_collect call per Simulate iteration: use its entry as the iteration boundary.
  Interceptor.attach(addr('gcTryToCollect'), {
    onEnter() {
      if (simThread === null) simThread = Process.getCurrentThreadId();
      if (Process.getCurrentThreadId() !== simThread) return;
      endIteration(now());
      inTryToCollect = true;
    },
    onLeave() { inTryToCollect = false; },
  });

  timed('gcStoppedMark', function (ms, retval) {
    if (Process.getCurrentThreadId() !== simThread) return;
    iter.gcMark += ms; iter.gcMarks++;
    sec.maxGcMarkMs = Math.max(sec.maxGcMarkMs, ms);
    if (retval.toInt32() === 0) iter.gcAborted++; // stop function fired: mark work thrown away
    if (!this.perFrame) { iter.gcForced++; iter.gcForcedMs += ms; } // collection triggered by an allocation
  }, function () { this.perFrame = inTryToCollect; });

  timed('gcInvokeFinalizers', function (ms, retval) {
    if (Process.getCurrentThreadId() !== simThread) return;
    iter.fin += ms; iter.finRun += retval.toInt32();
  });

  timed('scriptTick', function (ms) {
    if (Process.getCurrentThreadId() === simThread) iter.script += ms;
  });

  Interceptor.attach(addr('catchUpSteps'), {
    onEnter(args) { if (Process.getCurrentThreadId() === simThread) iter.steps += args[0].toInt32(); },
  });

  for (const name of ROUTE_ENTRIES) {
    Interceptor.attach(addr(name), {
      onEnter() { entryStack.push(name); },
      onLeave() { entryStack.pop(); },
    });
  }

  timed('routeSearch', function (ms) {
    if (Process.getCurrentThreadId() === simThread) iter.routeSearch += ms;
  });

  timed('routeOuter', function (ms, retval) {
    if (Process.getCurrentThreadId() !== simThread) { sec.routeOtherThreads++; return; }
    const ok = (retval.toInt32() & 0xff) !== 0;
    const entry = entryStack.length ? entryStack[entryStack.length - 1] : 'other';
    iter.route += ms; iter.routes++;
    if (!ok) iter.routeFails++;
    sec.maxRouteMs = Math.max(sec.maxRouteMs, ms);
    const e = iter.byEntry[entry] || (iter.byEntry[entry] = { n: 0, fail: 0, ms: 0 });
    e.n++; if (!ok) e.fail++; e.ms = round(e.ms + ms);
  });

  console.log('[ts3prof] hooks installed; the sim thread is detected on the first GC_try_to_collect');
}

let started = false;
function start(parameters) {
  if (started) return;
  started = true;
  cfg = Object.assign(cfg, parameters || {});
  openOutputs();
  try {
    install();
  } catch (e) {
    console.error(`[ts3prof] NOT PROFILING: ${e.message}`); // a wrong build must never be hooked half-way
  }
}
setTimeout(() => start({}), 1000); // fallback when the host does not call init

rpc.exports = {
  init(stage, parameters) { start(parameters); },
  dispose() {
    if (csv) { flushSecond(now()); csv.close(); jsonl.close(); }
  },
};
