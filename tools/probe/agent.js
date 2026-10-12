// Frida agent for live inspection of TS3W.exe. Loaded by tools/probe/ts3probe.py, which serves it over HTTP.
// Read-only by design: it reports state and records crashes, it never patches the game.
'use strict';

const k32 = Process.getModuleByName('kernel32.dll');
const VirtualQuery = new NativeFunction(k32.getExportByName('VirtualQuery'), 'uint', ['pointer', 'pointer', 'uint']);
const MBI_SIZE = 28; // MEMORY_BASIC_INFORMATION (32-bit)
const MEM_COMMIT = 0x1000, MEM_RESERVE = 0x2000, MEM_FREE = 0x10000;
const MEM_IMAGE = 0x1000000, MEM_MAPPED = 0x40000, MEM_PRIVATE = 0x20000;

const startedAt = Date.now();
const events = []; // ring buffer of exceptions and notes
let eventSeq = 0;
function pushEvent(e) {
  e.seq = ++eventSeq;
  e.time = new Date().toISOString();
  events.push(e);
  if (events.length > 500) events.shift();
  send({ type: 'event', event: e }); // host appends it to events.jsonl at once, so a crash still leaves a record
}

// Stack walking in JS only: every read goes through Frida's guarded memory API, which throws instead of crashing.
// (Frida's native Backtracer.FUZZY read unmapped stack memory and crashed the game on 2026-10-11.)
let codeMap = new ModuleMap();
function isCode(p) {
  if (codeMap.has(p)) return true;
  codeMap.update(); // a DLL may have loaded since
  return codeMap.has(p);
}
function safeRead(p) {
  try { return p.readPointer(); } catch (e) { return null; }
}
// Mono interpreter frames of a thread (docs/04 F6), read-only. ThreadContext lives in the thread's TLS slot whose
// index is at [0x11f4160] (TlsGetValue in 0xe59fd0); ThreadContext +8 = current_frame, frame +0 = parent [verified].
const ntQueryThread = new NativeFunction(Process.getModuleByName('ntdll.dll').getExportByName('NtQueryInformationThread'), 'int', ['pointer', 'int', 'pointer', 'uint', 'pointer']);
const tebCache = new Map();
function tebOf(tid, h) {
  if (tebCache.has(tid)) return tebCache.get(tid);
  const tbi = Memory.alloc(28); // THREAD_BASIC_INFORMATION: ExitStatus, TebBaseAddress, ...
  const teb = ntQueryThread(h, 0, tbi, 28, NULL) === 0 ? tbi.add(4).readPointer() : null;
  if (teb) tebCache.set(tid, teb);
  return teb;
}
// The script (sim) thread is the one whose interpreter TLS slot is set; steadier than catching Simulate on its stack.
let interpTid = null;
function findInterpThread() {
  if (interpTid !== null) return interpTid;
  const idx = ptr('0x11f4160').readU32();
  for (const t of Process.enumerateThreads()) {
    const h = OpenThread(0x0040, 0, t.id);
    if (h.isNull()) continue;
    try {
      const teb = tebOf(t.id, h);
      const slot = teb ? (idx < 64 ? teb.add(0xe10 + idx * 4) : safeRead(teb.add(0xf94))?.add((idx - 64) * 4)) : null;
      const v = slot ? safeRead(slot) : null;
      if (v && !v.isNull()) { interpTid = t.id; break; }
    } finally { CloseHandle(h); }
  }
  return interpTid;
}
const methodNames = new Map();
// metadata names end at the first non-printable byte (readCString runs past them here)
const cstr = p => { if (!p || p.isNull()) return ''; try { const b = new Uint8Array(p.readByteArray(120)); let e = 0; while (e < b.length && b[e] >= 0x20 && b[e] < 0x7f) e++; return String.fromCharCode(...b.subarray(0, e)); } catch (e) { return null; } };
function className(klass) { // MonoClass name/namespace +0x34/+0x38
  const cn = cstr(safeRead(klass.add(0x34))), ns = cstr(safeRead(klass.add(0x38)));
  return cn ? `${ns ? ns + '.' : ''}${cn}` : null;
}
function monoMethodName(m) { // MonoMethod klass +8, name +0x18
  const klass = safeRead(m.add(8)), n = cstr(safeRead(m.add(0x18))), c = klass && !klass.isNull() ? className(klass) : null;
  return c && n ? `${c}.${n}` : null;
}
function methodNameOfInfo(info) {
  const key = info.toString();
  if (methodNames.has(key)) return methodNames.get(key);
  const m = safeRead(info.add(8));
  const name = m && !m.isNull() ? monoMethodName(m) : null;
  if (name) methodNames.set(key, name); // heap metadata is stable; failures are retried next time
  return name ?? '?';
}
// Copies the chain (first 0x30 bytes of each frame) while the thread is suspended: the frames live on its stack.
function interpFrames(tid, max) {
  if (tid === Process.getCurrentThreadId()) throw new Error('cannot sample the probe thread itself');
  const h = OpenThread(0x0002 | 0x0040, 0, tid); // SUSPEND_RESUME | QUERY_INFORMATION
  if (h.isNull()) throw new Error(`OpenThread ${tid} failed`);
  const frames = [];
  let idx = null, ctx = null;
  try {
    const teb = tebOf(tid, h);
    if (!teb) return { error: 'no TEB' };
    idx = ptr('0x11f4160').readU32();
    if (SuspendThread(h) === 0xffffffff) throw new Error('SuspendThread failed');
    try {
      const slot = idx < 64 ? teb.add(0xe10 + idx * 4) : safeRead(teb.add(0xf94))?.add((idx - 64) * 4);
      ctx = slot ? safeRead(slot) : null;
      let f = ctx && !ctx.isNull() ? safeRead(ctx.add(8)) : null;
      for (let i = 0; i < (max || 32) && f && !f.isNull(); i++) {
        let words; try { words = Array.from(new Uint32Array(f.readByteArray(0x30))); } catch (e) { break; }
        frames.push({ at: f.toString(), words });
        f = ptr(words[0]);
      }
    } finally {
      ResumeThread(h);
    }
  } finally {
    CloseHandle(h);
  }
  return { tlsIndex: idx, context: ctx ? ctx.toString() : null, frames };
}

function walk(pc, sp, fp, max) {
  const out = [pc.toString()];
  // 1. frame-pointer chain (exact where the code keeps EBP frames)
  let frame = fp;
  for (let i = 0; i < max && !frame.isNull(); i++) {
    const ret = safeRead(frame.add(4)), next = safeRead(frame);
    if (ret === null || next === null || !isCode(ret)) break;
    out.push(ret.toString());
    if (next.compare(frame) <= 0) break; // the chain must go up the stack
    frame = next;
  }
  if (out.length >= 4) return out.slice(0, max);
  // 2. fallback: values on the top of the stack that point into loaded code (may include stale ones)
  for (let off = 0; off < 2048 && out.length < max; off += 4) {
    const v = safeRead(sp.add(off));
    if (v === null) break;
    if (isCode(v)) out.push(v.toString() + '?');
  }
  return out;
}
function frames(context, max) {
  try {
    return walk(context.pc, context.sp, context.ebp, max);
  } catch (e) {
    return [`stack walk failed: ${e.message}`];
  }
}

// One thread's registers without enumerating (and suspending) every thread in the game.
const k32t = Process.getModuleByName('kernel32.dll');
const OpenThread = new NativeFunction(k32t.getExportByName('OpenThread'), 'pointer', ['uint', 'int', 'uint']);
const SuspendThread = new NativeFunction(k32t.getExportByName('SuspendThread'), 'uint', ['pointer']);
const ResumeThread = new NativeFunction(k32t.getExportByName('ResumeThread'), 'uint', ['pointer']);
const GetThreadContext = new NativeFunction(k32t.getExportByName('GetThreadContext'), 'int', ['pointer', 'pointer']);
const CloseHandle = new NativeFunction(k32t.getExportByName('CloseHandle'), 'int', ['pointer']);
const CTX = Memory.alloc(1024); // x86 CONTEXT is 716 bytes
function threadStack(tid, max) {
  if (tid === Process.getCurrentThreadId()) throw new Error('cannot sample the probe thread itself');
  const h = OpenThread(0x0002 | 0x0008 | 0x0040, 0, tid); // SUSPEND_RESUME | GET_CONTEXT | QUERY_INFORMATION
  if (h.isNull()) throw new Error(`OpenThread ${tid} failed`);
  let pc, sp, fp;
  try {
    if (SuspendThread(h) === 0xffffffff) throw new Error('SuspendThread failed');
    try {
      CTX.writeU32(0x10003); // CONTEXT_CONTROL | CONTEXT_INTEGER
      if (!GetThreadContext(h, CTX)) throw new Error('GetThreadContext failed');
      fp = CTX.add(0xb4).readPointer(); pc = CTX.add(0xb8).readPointer(); sp = CTX.add(0xc4).readPointer();
    } finally {
      ResumeThread(h); // resume before walking: the walk only reads memory
    }
  } finally {
    CloseHandle(h);
  }
  return walk(pc, sp, fp, max);
}

const SIMULATE = [ptr(0xd81840), ptr(0xd82100)]; // MonoScriptHost::Simulate (tools/ghidra/anchors.csv)
let simTid = null;
function findSimThread() {
  if (simTid !== null) return simTid;
  for (const t of Process.enumerateThreads()) {
    const st = frames(t.context, 24);
    if (st.some(a => { const p = ptr(a.replace('?', '')); return p.compare(SIMULATE[0]) >= 0 && p.compare(SIMULATE[1]) < 0; })) { simTid = t.id; break; }
  }
  return simTid;
}

function regs(c) {
  const out = {};
  for (const r of ['eip', 'esp', 'ebp', 'eax', 'ebx', 'ecx', 'edx', 'esi', 'edi']) if (c[r] !== undefined) out[r] = c[r].toString();
  return out;
}

// Native exceptions (access violations etc.). The game handles some itself, so we only record and pass them on.
// The game can raise thousands of handled exceptions (e.g. while rebuilding scriptCache), and a stack walk per
// exception starves every other probe call: full detail only for the first hits per site and per second.
const exceptionCounts = {};
let exceptionWindow = { second: 0, n: 0 };
function installExceptionHandler() { Process.setExceptionHandler(d => {
  const site = `${d.type}@${d.address}`;
  const seen = exceptionCounts[site] = (exceptionCounts[site] || 0) + 1;
  const second = Math.floor(Date.now() / 1000);
  if (exceptionWindow.second !== second) exceptionWindow = { second, n: 0 };
  if (seen > 3 || ++exceptionWindow.n > 10) return false;
  pushEvent({
    kind: 'exception', type: d.type, address: d.address.toString(),
    memory: d.memory ? { operation: d.memory.operation, address: d.memory.address.toString() } : null,
    registers: regs(d.context), stack: frames(d.context, 48), thread: Process.getCurrentThreadId(),
  });
  return false;
}); }

function owner(base) {
  const m = Process.findModuleByAddress(base);
  return m ? m.name : null;
}

// --- ts3agent mailbox (mods/ts3agent/src/Mailbox.cs): a byte[] in the game, serviced once per sim tick ---
const MBX = { header: 64, reqCap: 64 * 1024, capacity: 1024 * 1024, version: 1, salt: 0x53334d43,
              offNonce: 20, offReqSeq: 24, offRespSeq: 28, offReqLen: 32, offRespLen: 36, offCapacity: 40,
              offCheck: 44, offHeartbeat: 48, offState: 56 };
const MBX_MAGIC = 'TS3AGENT-MBOX-01';
let mailbox = null;
let mailboxId = 0;

function mailboxValid(p) {
  try {
    if (p.readUtf8String(16) !== MBX_MAGIC) return false;
    const nonce = p.add(MBX.offNonce).readU32(), cap = p.add(MBX.offCapacity).readU32();
    return p.add(16).readU32() === MBX.version && cap === MBX.capacity &&
           ((MBX.version ^ nonce ^ cap ^ MBX.salt) >>> 0) === p.add(MBX.offCheck).readU32();
  } catch (e) {
    return false;
  }
}

let lastScan = { at: 0, ms: 0, ranges: 0, bytes: 0 };
function findMailbox() {
  if (mailbox && mailboxValid(mailbox)) return mailbox;
  mailbox = null;
  if (Date.now() - lastScan.at < 5000) return null; // a full scan costs seconds; never queue them up
  const t0 = Date.now();
  lastScan = { at: t0, ms: 0, ranges: 0, bytes: 0 };
  const pattern = Array.from(MBX_MAGIC).map(c => c.charCodeAt(0).toString(16).padStart(2, '0')).join(' ');
  for (const r of Process.enumerateRanges('rw-')) {
    if (r.size < MBX.capacity) continue; // the buffer is one 1 MB array
    lastScan.ranges++; lastScan.bytes += r.size;
    for (const m of Memory.scanSync(r.base, r.size, pattern)) {
      if (mailboxValid(m.address)) { mailbox = m.address; lastScan.ms = Date.now() - t0; return mailbox; }
    }
  }
  lastScan.ms = Date.now() - t0;
  return null;
}

// Non-blocking: post a request, then the host polls mailboxReply (a blocking wait here would stall every other RPC).
function mailboxSend(cmd, args) {
  const p = findMailbox();
  if (!p) throw new Error('ts3agent mailbox not found: is build/ts3agent.package installed and the game past startup?');
  if (p.add(MBX.offReqSeq).readU32() !== p.add(MBX.offRespSeq).readU32()) throw new Error('previous request still pending');
  const req = Object.assign({ id: ++mailboxId, cmd }, args ? { args } : {});
  const text = JSON.stringify(req);
  const byteLen = unescape(encodeURIComponent(text)).length; // UTF-8 length
  if (byteLen > MBX.reqCap) throw new Error('request too large');
  p.add(MBX.header).writeUtf8String(text);
  p.add(MBX.offReqLen).writeU32(byteLen);
  const seq = (p.add(MBX.offReqSeq).readU32() + 1) >>> 0;
  p.add(MBX.offReqSeq).writeU32(seq); // publish last; the game answers on its next tick
  return seq;
}

function mailboxReply(seq) {
  const p = findMailbox();
  if (!p || p.add(MBX.offRespSeq).readU32() !== seq) return null;
  const len = p.add(MBX.offRespLen).readU32();
  return JSON.parse(p.add(MBX.header + MBX.reqCap).readUtf8String(len));
}

// Network watch (opt-in, /net?start=1): does the game still go online during play, and does it block? Counts calls,
// total and worst ms per API and target, and how many ran on the sim thread. Only the system DLLs are hooked (the
// ASI loader's wininet.dll proxy forwards to them).
const NET_APIS = {
  'ws2_32.dll': ['connect', 'getaddrinfo', 'gethostbyname', 'GetAddrInfoW'],
  'wininet.dll': ['InternetOpenA', 'InternetOpenW', 'InternetConnectA', 'InternetConnectW', 'InternetOpenUrlA', 'InternetOpenUrlW',
    'HttpSendRequestA', 'HttpSendRequestW', 'InternetReadFile', 'InternetGetConnectedState'],
  'winhttp.dll': ['WinHttpConnect', 'WinHttpSendRequest', 'WinHttpReceiveResponse', 'WinHttpGetIEProxyConfigForCurrentUser'],
};
const net = { started: null, hooked: [], calls: {} };
function netTarget(api, args) {
  try {
    if (api === 'connect') {
      const sa = args[1];
      if (sa.readU16() !== 2) return 'af ' + sa.readU16(); // AF_INET only
      return [0, 1, 2, 3].map(i => sa.add(4 + i).readU8()).join('.') + ':' + ((sa.add(2).readU8() << 8) | sa.add(3).readU8());
    }
    if (api === 'getaddrinfo' || api === 'gethostbyname') return args[0].readCString();
    if (api === 'GetAddrInfoW' || api === 'InternetOpenUrlW' || api === 'InternetConnectW') return args[api === 'GetAddrInfoW' ? 0 : 1].readUtf16String();
    if (api === 'InternetOpenUrlA' || api === 'InternetConnectA') return args[1].readCString();
    if (api === 'WinHttpConnect') return args[1].readUtf16String();
  } catch (e) { return '?'; }
  return '';
}
function netStart() {
  if (net.started) return;
  net.started = Date.now();
  for (const [dll, apis] of Object.entries(NET_APIS)) {
    const m = Process.findModuleByName(dll); // system DLLs only; a module not loaded yet is simply not watched
    if (m === null || !/\\(system32|syswow64)\\/i.test(m.path)) continue;
    for (const api of apis) {
      const f = m.findExportByName(api);
      if (f === null) continue;
      Interceptor.attach(f, {
        onEnter(args) { this.t = Date.now(); this.key = api + ' ' + netTarget(api, args); this.sim = Process.getCurrentThreadId() === simTid; }, // set once /simstack found it
        onLeave() {
          const ms = Date.now() - this.t, c = net.calls[this.key] ??= { n: 0, ms: 0, maxMs: 0, onSimThread: 0 };
          c.n++; c.ms += ms; c.maxMs = Math.max(c.maxMs, ms); if (this.sim) c.onSimThread++;
        },
      });
      net.hooked.push(dll + '!' + api);
    }
  }
}

// Routing-lock watch (opt-in, /routelock?start=1, stop=1 detaches; docs/18 step 1a): who holds RoutingWorld+0x918,
// how long, and how much of each plan a worker could overlap with sim-thread work. Read-only hooks; the lock is learned
// from the first Route_PlanLocked (ecx = RoutingWorld). Lock_Acquire is the engine-wide lock (resource lock too), so
// every acquire enters this script: `acquireHooksPerSec` shows the cost; stop it when done.
const ROUTE_PLAN_LOCKED = ptr(0x82c6e0), LOCK_ACQUIRE = ptr(0x4e16f0), LOCK_RELEASE = ptr(0x4e17b0), LOCOMOTION_UPDATE = ptr(0x83ced0);
const qpcBuf = Memory.alloc(8);
const qpcFn = new NativeFunction(k32.getExportByName('QueryPerformanceCounter'), 'int', ['pointer']);
const qpfBuf = Memory.alloc(8);
new NativeFunction(k32.getExportByName('QueryPerformanceFrequency'), 'int', ['pointer'])(qpfBuf);
const qpfPerMs = qpfBuf.readU64().toNumber() / 1000;
function nowMs() { qpcFn(qpcBuf); return qpcBuf.readU64().toNumber() / qpfPerMs; }
const rl = { started: null, listeners: [], lock: null, simTid: null, iterations: 0, acquireHooks: 0, threads: {},
             plans: [], plansByThread: {}, planMs: 0, overlapMs: 0, gaps: [], pending: null, depth: {} };
const OVERLAP_BUCKETS = [0.1, 0.5, 1, 2, 5, 10, 20, 50, 100, Infinity];
function rlThread(tid) {
  return rl.threads[tid] ??= { acquires: 0, waitMs: 0, maxWaitMs: 0, holdMs: 0, maxHoldMs: 0, held: 0 };
}
function routeLockStart() {
  if (rl.started) return;
  rl.started = nowMs();
  rl.simTid = findInterpThread();
  rl.listeners.push(Interceptor.attach(ROUTE_PLAN_LOCKED, {
    onEnter() {
      if (rl.lock === null) rl.lock = this.context.ecx.add(0x918);
      this.t = nowMs();
    },
    onLeave() {
      const tid = Process.getCurrentThreadId(), end = nowMs(), len = end - this.t;
      rl.planMs += len;
      rl.plansByThread[tid] = (rl.plansByThread[tid] || 0) + 1;
      if (rl.plans.length < 20000) rl.plans.push(len);
      rl.pending = { end, len }; // closed by the sim thread's next acquire of the routing lock
    },
  }));
  rl.listeners.push(Interceptor.attach(LOCK_ACQUIRE, {
    onEnter() {
      rl.acquireHooks++;
      this.match = rl.lock !== null && this.context.ecx.equals(rl.lock);
      if (!this.match) return;
      this.t = nowMs();
      const tid = Process.getCurrentThreadId();
      if (tid === rl.simTid && rl.pending && !(rl.depth[tid] > 0)) {
        const gap = this.t - rl.pending.end, ov = Math.min(rl.pending.len, gap);
        rl.overlapMs += ov;
        if (rl.gaps.length < 20000) rl.gaps.push(ov);
        rl.pending = null;
      }
    },
    onLeave() {
      if (!this.match) return;
      const tid = Process.getCurrentThreadId(), now = nowMs(), w = now - this.t, th = rlThread(tid);
      th.acquires++; th.waitMs += w; th.maxWaitMs = Math.max(th.maxWaitMs, w);
      if (!(rl.depth[tid] > 0)) th.held = now;
      rl.depth[tid] = (rl.depth[tid] || 0) + 1;
    },
  }));
  rl.listeners.push(Interceptor.attach(LOCK_RELEASE, {
    onEnter() {
      if (rl.lock === null || !this.context.ecx.equals(rl.lock)) return;
      const tid = Process.getCurrentThreadId();
      if (!(rl.depth[tid] > 0)) return; // acquired before the watch started
      if (--rl.depth[tid] === 0) {
        const th = rlThread(tid), h = nowMs() - th.held;
        th.holdMs += h; th.maxHoldMs = Math.max(th.maxHoldMs, h);
      }
    },
  }));
  rl.listeners.push(Interceptor.attach(LOCOMOTION_UPDATE, { onEnter() { rl.iterations++; } }));
}
function routeLockStop() {
  for (const l of rl.listeners) l.detach();
  rl.listeners = [];
}
// Scheduler-stage stall watch (opt-in, /taskstall?start=1&ms=250, stop=1 unhooks; docs/04 F17). Interceptor.attach on
// the seams MonoScriptHost::Simulate calls each iteration; per call only one QPC read on enter and one on leave. A 50 ms
// timer on the probe thread looks at the open stage; once it has run `ms`, it reads the current task every `sample` ms
// (cap 40): ObjectGuid host+0xBD0/+0xBD4, context *(host+0xBC8) ([0] MonoMethod, [1] task object; set by 0xD7F000)
// and the sim thread's managed stack (interpFrames, suspends it for microseconds). One record per stall goes to the host
// (probe-out/<session>-taskstall.jsonl) and to a ring of the last 50.
// S3PP's SimulatorStageProfiler detours the same prologues: set it off in Sims3Performance.ini first (the byte check
// below refuses a detoured prologue rather than hooking S3PP's jump).
const STAGES = { taskObjects: [0xd7fc60, '55 8b ec 83 e4 f8 83 ec'], oneShot: [0xd74800, '83 ec 10 53 55 56 57 8b'],
                 threaded: [0xd7f8b0, '55 8b ec 83 e4 f8 81 ec'] };
const ts = { started: null, listeners: [], timer: null, ms: 250, sampleMs: 250, open: null, stalls: [], counts: {} };
function taskNow(host) {
  let guid = null;
  try { guid = '0x' + host.add(0xbd0).readU64().toString(16); } catch (e) { /* host gone */ }
  const slot = safeRead(host.add(0xbc8)), ctx = slot && !slot.isNull() ? safeRead(slot) : null;
  const m = ctx && !ctx.isNull() ? safeRead(ctx) : null, obj = ctx && !ctx.isNull() ? safeRead(ctx.add(4)) : null;
  const vt = obj && !obj.isNull() ? safeRead(obj) : null, klass = vt && !vt.isNull() ? safeRead(vt) : null; // MonoVTable +0 = klass
  return { guid: guid === '0x0' ? null : guid, class: klass && !klass.isNull() ? className(klass) : null,
           method: m && !m.isNull() ? monoMethodName(m) : null };
}
function taskStallTick() {
  const o = ts.open;
  if (!o) return;
  const at = nowMs() - o.t0;
  if (at < ts.ms || at < o.nextAt || o.samples.length >= 40) return;
  o.nextAt = at + ts.sampleMs;
  let methods;
  try {
    const r = interpFrames(o.tid, 64);
    methods = r.error ? [r.error] : r.frames.map(f => methodNameOfInfo(ptr(f.words[1])));
  } catch (e) { methods = [`sample failed: ${e.message}`]; }
  o.samples.push({ atMs: Math.round(at), task: taskNow(o.host), methods });
}
function taskStallStart() {
  if (ts.started) return;
  for (const [name, [a, expect]] of Object.entries(STAGES)) { // all or nothing, before the first attach
    const got = Array.from(new Uint8Array(ptr(a).readByteArray(8))).map(b => b.toString(16).padStart(2, '0')).join(' ');
    if (got !== expect) throw new Error(`${name} @ 0x${a.toString(16)}: bytes ${got}, expected ${expect}` +
      (got.startsWith('e9') ? ' (detoured: turn off S3PP SimulatorStageProfiler)' : ' (different TS3W.exe build?)'));
  }
  ts.started = Date.now();
  for (const [stage, [a]] of Object.entries(STAGES)) {
    ts.listeners.push(Interceptor.attach(ptr(a), {
      onEnter() {
        this.o = { stage, t0: nowMs(), host: this.context.ecx, tid: Process.getCurrentThreadId(), wall: Date.now(), nextAt: 0, samples: [] };
        ts.open = this.o;
      },
      onLeave() {
        const o = this.o, ms = nowMs() - o.t0;
        if (ts.open === o) ts.open = null;
        if (ms < ts.ms) return;
        const c = ts.counts[stage] ??= { n: 0, totalMs: 0, maxMs: 0 };
        c.n++; c.totalMs = Math.round(c.totalMs + ms); c.maxMs = Math.max(c.maxMs, Math.round(ms));
        const by = {}; // the task seen in most samples; samples keep their own (a stage can run many slow tasks)
        for (const s of o.samples) { const k = JSON.stringify(s.task); by[k] = (by[k] || 0) + 1; }
        const top = Object.entries(by).sort((x, y) => y[1] - x[1])[0];
        const rec = { time: new Date(o.wall).toISOString(), end: new Date().toISOString(), stage, ms: Math.round(ms),
                      task: top ? JSON.parse(top[0]) : null, samples: o.samples };
        ts.stalls.push(rec);
        if (ts.stalls.length > 50) ts.stalls.shift();
        send({ type: 'taskstall', stall: rec }); // the host appends it to probe-out/<session>-taskstall.jsonl
      },
    }));
  }
  ts.timer = setInterval(taskStallTick, 50);
}
function taskStallStop() {
  for (const l of ts.listeners) l.detach();
  ts.listeners = [];
  if (ts.timer !== null) clearInterval(ts.timer);
  ts.timer = null; ts.open = null; ts.started = null;
}

function pct(a, q) {
  if (!a.length) return null;
  const s = a.slice().sort((x, y) => x - y);
  return Math.round(s[Math.min(s.length - 1, Math.floor(q * s.length))] * 1000) / 1000;
}
function routeLockReport() {
  if (rl.started && rl.simTid === null) rl.simTid = findInterpThread();
  const secs = rl.started ? (nowMs() - rl.started) / 1000 : 0, r3 = x => Math.round(x * 1000) / 1000;
  const hist = {};
  for (const b of OVERLAP_BUCKETS) hist[b === Infinity ? '>100' : '<=' + b] = 0;
  for (const g of rl.gaps) { const b = OVERLAP_BUCKETS.find(x => g <= x); hist[b === Infinity ? '>100' : '<=' + b]++; }
  const threads = Object.fromEntries(Object.entries(rl.threads).map(([tid, t]) => [tid, {
    sim: Number(tid) === rl.simTid, acquires: t.acquires, perIteration: rl.iterations ? r3(t.acquires / rl.iterations) : null,
    waitMs: r3(t.waitMs), maxWaitMs: r3(t.maxWaitMs), holdMs: r3(t.holdMs), maxHoldMs: r3(t.maxHoldMs) }]));
  return {
    since: rl.started && Math.round(secs), hooked: rl.listeners.length > 0, lock: rl.lock && rl.lock.toString(), simThread: rl.simTid,
    iterations: rl.iterations, acquireHooksPerSec: secs ? Math.round(rl.acquireHooks / secs) : 0, threads,
    plans: { n: rl.plans.length, byThread: rl.plansByThread, totalMs: r3(rl.planMs), p50: pct(rl.plans, 0.5), p90: pct(rl.plans, 0.9),
             p99: pct(rl.plans, 0.99), max: pct(rl.plans, 1) },
    // O = share of plan time a worker could run before the sim thread next needs the routing lock
    overlap: { n: rl.gaps.length, totalMs: r3(rl.overlapMs), O: rl.planMs ? r3(rl.overlapMs / rl.planMs) : null,
               routeMsPerSec: secs ? r3(rl.planMs / secs) : null, overlapMsPerSec: secs ? r3(rl.overlapMs / secs) : null, histogramMs: hist },
  };
}

rpc.exports = {
  net(start) {
    if (start) netStart();
    return { since: net.started && Math.round((Date.now() - net.started) / 1000), hooked: net.hooked.length, calls: net.calls,
             loaded: Object.keys(NET_APIS).filter(d => Process.findModuleByName(d) !== null) };
  },

  routeLock(start, stop) {
    if (start) routeLockStart();
    if (stop) routeLockStop();
    return routeLockReport();
  },

  taskStall(start, stop, ms, sampleMs, n) {
    if (ms > 0) ts.ms = ms;
    if (sampleMs > 0) ts.sampleMs = sampleMs;
    if (start) taskStallStart();
    if (stop) taskStallStop();
    const o = ts.open;
    return { since: ts.started && new Date(ts.started).toISOString(), hooked: ts.listeners.length > 0, ms: ts.ms, sampleMs: ts.sampleMs,
             open: o ? { stage: o.stage, forMs: Math.round(nowMs() - o.t0) } : null, counts: ts.counts, stalls: ts.stalls.slice(-(n || 10)) };
  },

  // ts3native.asi counters (docs/15): route cache and route worker, null when the plugin is not loaded.
  // routeWorker 0|1 sets the worker's runtime switch (our plugin's own data, not game memory); only when the worker runs.
  // routeBudget 0|1 likewise sets the route search budget's switch (ts3native >= 0.4.0, [RouteBudget] Enabled=1).
  nativeCounters(routeWorker, routeBudget) {
    const m = Process.findModuleByName('ts3native.asi'); // Frida 17 has no static Module.findExportByName
    const c = m === null ? null : m.findExportByName('ts3native_counters');
    if (c === null) return null;
    const f = ['version', 'enabled', 'calls', 'hits', 'misses', 'stored', 'otherFails', 'windowMs'];
    if (c.readS32() >= 2) f.push('workerMode', 'workerReady', 'workerCalls', 'workerUs', 'workerMaxUs', 'waitUs', 'fallbacks', 'watchdogs');
    if (c.readS32() >= 3) f.push('budgetMode', 'budgetReady', 'budgetCalls', 'budgetCapped', 'budgetCapNodes', 'budgetCapUs', 'budgetMaxNodes', 'budgetMaxNodesOk', 'budgetMaxUsOk');
    if (c.readS32() >= 4) f.push('invokeHook', 'invokes'); // 0.4.1: worker's managed calls proxied to the sim thread (+0x64, +0x68)
    const r = Object.fromEntries(f.map((n, i) => [n, c.add(i * 4).readS32()]));
    if (routeWorker === 0 || routeWorker === 1) {
      if (r.workerReady !== 1) return Object.assign(r, { error: 'route worker not running ([RouteWorker] Enabled=0 or ts3native < 0.3.0)' });
      c.add(8 * 4).writeS32(routeWorker);
      r.workerMode = c.add(8 * 4).readS32();
    }
    if (routeBudget === 0 || routeBudget === 1) {
      if (r.budgetReady !== 1) return Object.assign(r, { error: 'route budget not active ([RouteBudget] Enabled=0 or ts3native < 0.4.0)' });
      c.add(16 * 4).writeS32(routeBudget);
      r.budgetMode = c.add(16 * 4).readS32();
    }
    return r;
  },

  agentSend(cmd, args) {
    return mailboxSend(cmd, args);
  },

  agentReply(seq) {
    return mailboxReply(seq);
  },

  agentStatus() {
    const p = findMailbox();
    return p ? { address: p.toString(), heartbeat: p.add(MBX.offHeartbeat).readU32(), worldLoaded: p.add(MBX.offState).readU32() === 1 }
             : { address: null, lastScan };
  },

  status() {
    const m = Process.getModuleByName('TS3W.exe');
    return { pid: Process.id, arch: Process.arch, exeBase: m.base.toString(), threads: Process.enumerateThreads().length,
             modules: Process.enumerateModules().length, attachedSeconds: Math.round((Date.now() - startedAt) / 1000),
             lastEventSeq: eventSeq };
  },

  // Every thread with registers and a symbolizable stack (the host maps addresses to Ghidra names).
  threads(maxFrames) {
    return Process.enumerateThreads().map(t => ({
      id: t.id, state: t.state, name: t.name || null, registers: regs(t.context), stack: frames(t.context, maxFrames || 32),
    }));
  },

  // Whole 4 GB address space: what is committed, reserved and free, and the largest free block (Error 12 decides on it).
  addressSpace() {
    const mbi = Memory.alloc(MBI_SIZE);
    const totals = { commit: 0, reserve: 0, free: 0 };
    const byType = { image: 0, mapped: 0, private: 0 };
    let largestFree = { base: '0x0', size: 0 };
    const reservedBlocks = [];
    let a = ptr(0x10000);
    const end = ptr('0xffff0000');
    while (a.compare(end) < 0) {
      if (VirtualQuery(a, mbi, MBI_SIZE) === 0) break;
      const base = mbi.readPointer();
      const size = mbi.add(12).readU32();
      const state = mbi.add(16).readU32();
      const type = mbi.add(24).readU32();
      if (state === MEM_FREE) {
        totals.free += size;
        if (size > largestFree.size) largestFree = { base: base.toString(), size };
      } else {
        if (state === MEM_COMMIT) totals.commit += size; else totals.reserve += size;
        if (type === MEM_IMAGE) byType.image += size; else if (type === MEM_MAPPED) byType.mapped += size; else byType.private += size;
        if (state === MEM_RESERVE && size >= 4 << 20) reservedBlocks.push({ base: base.toString(), size, owner: owner(base) });
      }
      const next = base.add(size);
      if (next.compare(a) <= 0) break;
      a = next;
    }
    const mb = x => Math.round(x / 1048576 * 10) / 10;
    reservedBlocks.sort((x, y) => y.size - x.size);
    return {
      commitMB: mb(totals.commit), reserveOnlyMB: mb(totals.reserve), freeMB: mb(totals.free),
      largestFreeMB: mb(largestFree.size), largestFreeBase: largestFree.base,
      imageMB: mb(byType.image), mappedMB: mb(byType.mapped), privateMB: mb(byType.private),
      biggestReservedOnly: reservedBlocks.slice(0, 15).map(b => ({ base: b.base, MB: mb(b.size), owner: b.owner })),
    };
  },

  // The sim thread only: cheap enough for a watchdog (suspends one thread for a few microseconds).
  // Managed call stack of the sim thread, innermost first: frame[1] is the method's interpreter info, info+8 its
  // MonoMethod (klass +8, name +0x18; class name/namespace +0x34/+0x38) [verified 2026-10-12, docs/04 F6a].
  interpStack(maxFrames) {
    const tid = findInterpThread();
    if (tid === null) return { error: 'sim thread not found' };
    const r = interpFrames(tid, maxFrames);
    if (r.error) return r;
    return { methods: r.frames.map(f => methodNameOfInfo(ptr(f.words[1]))) };
  },

  interpFrames(maxFrames) {
    const tid = findSimThread();
    return tid === null ? { error: 'sim thread not found' } : interpFrames(tid, maxFrames);
  },

  simStack(maxFrames) {
    const tid = findSimThread();
    if (tid === null) return { thread: null, stack: [] };
    return { thread: tid, stack: threadStack(tid, maxFrames || 24) };
  },

  modules() {
    return Process.enumerateModules().map(m => ({ name: m.name, base: m.base.toString(), size: m.size, path: m.path }));
  },

  read(address, length) {
    const len = Math.min(length || 64, 65536);
    const bytes = new Uint8Array(ptr(address).readByteArray(len));
    return Array.from(bytes).map(b => b.toString(16).padStart(2, '0')).join('');
  },

  readString(address, kind) {
    const p = ptr(address);
    return kind === 'utf16' ? p.readUtf16String() : p.readCString();
  },

  // Resource caches (docs/08 §2.5), read-only. Manager singleton [0x11c78d8] (getter 0x4afd20), vtable 0xfb2da0;
  // its cache array +0xa0/+0xa4 holds 8-byte {cache*, priority} entries (FindCache 0x4b0070) [verified].
  // Per cache (ctor 0x732910, vtable 0xffdd94): +0x8c CacheBudget, +0x90 overflow budget, +0x94 bytes [verified];
  // +0x98 bytes on the flush list (moved to +0x94 on a hit, 0x732b60) and +0x28 entry count (EASTL hash_map
  // after buckets +0x20 / bucket count +0x24) [inferred]. Every pointer is checked before it is followed.
  resCache() {
    const u32 = p => { try { return p.readU32(); } catch (e) { return null; } };
    const mgr = safeRead(ptr('0x11c78d8'));
    if (!mgr || mgr.isNull()) return { ok: false, error: 'manager not created yet' };
    // live object is a subclass: vtable 0xffe250 on Steam 1.67 [verified 2026-10-11]; 0xfb2da0 is the base ctor's
    if (u32(mgr) !== 0xffe250 && u32(mgr) !== 0xfb2da0) return { ok: false, error: 'manager vtable mismatch', manager: mgr.toString() };
    const begin = safeRead(mgr.add(0xa0)), end = safeRead(mgr.add(0xa4));
    if (!begin || !end) return { ok: false, error: 'cache array unreadable' };
    const n = end.sub(begin).toInt32() / 8;
    if (n < 0 || n > 16 || n !== Math.floor(n)) return { ok: false, error: `odd cache array size ${n}` };
    const caches = [];
    for (let i = 0; i < n; i++) {
      const c = safeRead(begin.add(i * 8));
      if (!c || c.isNull()) continue;
      const vt = u32(c), known = vt === 0xffdd94;
      caches.push({
        address: c.toString(), priority: u32(begin.add(i * 8 + 4)), vtable: vt === null ? null : '0x' + vt.toString(16), known,
        // other cache classes have another layout: only read the ResourceCache fields from ResourceCache objects
        ...(known ? { budget: u32(c.add(0x8c)), overflowBudget: u32(c.add(0x90)), bytes: u32(c.add(0x94)),
          flushBytes: u32(c.add(0x98)), entries: u32(c.add(0x28)) } : {}),
      });
    }
    return { ok: true, manager: mgr.toString(), caches };
  },

  // Opt-in from the host: every native exception (thousands per second at times) enters this script.
  enableExceptions() {
    installExceptionHandler();
    return true;
  },

  exceptionCounts() {
    return Object.entries(exceptionCounts).sort((a, b) => b[1] - a[1]).slice(0, 50).map(([site, n]) => ({ site, n }));
  },

  events(sinceSeq) {
    return events.filter(e => e.seq > (sinceSeq || 0));
  },
};

pushEvent({ kind: 'note', message: `probe attached to pid ${Process.id}` });
