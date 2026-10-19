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
let codeMap = new ModuleMap(), codeMapAt = 0;
function isCode(p) {
  if (codeMap.has(p)) return true;
  // a DLL may have loaded since; at most one refresh per second: a stack scan misses on most words, and a refresh per miss
  // made one /simstack call take seconds, which starved every other probe call (2026-10-16-sampler-hang.md)
  if (Date.now() - codeMapAt < 1000) return false;
  codeMapAt = Date.now();
  codeMap.update();
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
let simTid = null, simSearchAt = 0;
function findSimThread() {
  if (simTid !== null) return simTid;
  const stored = storedTid(SIM_TID_AT); // set at app init (docs/28 §5.2); no need to suspend every thread to find it
  if (stored) return (simTid = stored);
  // The search suspends every thread (Process.enumerateThreads) and walks each stack: once per 10 s at most while it fails.
  if (Date.now() - simSearchAt < 10000) return null;
  simSearchAt = Date.now();
  for (const t of Process.enumerateThreads()) {
    const st = frames(t.context, 24);
    if (st.some(a => { const p = ptr(a.replace('?', '')); return p.compare(SIMULATE[0]) >= 0 && p.compare(SIMULATE[1]) < 0; })) { simTid = t.id; break; }
  }
  return simTid;
}

// Thread ids the game stores at app init: [0x11D8A88] main (render, frame loop 0xEC9F00) and [0x11D8A8C] sim, compared by
// 0x7D98C0 (docs/28 §5.2, docs/33 §8) [verified static]. Before init (or in a dummy) the owner of the visible game window stands in.
const MAIN_TID_AT = ptr('0x11d8a88'), SIM_TID_AT = ptr('0x11d8a8c');
const storedTid = p => { try { return p.readU32() || null; } catch (e) { return null; } };
let winTid = null;
function windowThread() {
  if (winTid !== null) return winTid;
  const u = Process.findModuleByName('user32.dll');
  if (!u) return null;
  const FindWindowExW = new NativeFunction(u.getExportByName('FindWindowExW'), 'pointer', ['pointer', 'pointer', 'pointer', 'pointer']);
  const GetWindowThreadProcessId = new NativeFunction(u.getExportByName('GetWindowThreadProcessId'), 'uint', ['pointer', 'pointer']);
  const IsWindowVisible = new NativeFunction(u.getExportByName('IsWindowVisible'), 'int', ['pointer']);
  const pid = Memory.alloc(4);
  for (let h = FindWindowExW(NULL, NULL, NULL, NULL), n = 0; !h.isNull() && n < 20000; h = FindWindowExW(NULL, h, NULL, NULL), n++) {
    const tid = GetWindowThreadProcessId(h, pid);
    if (pid.readU32() === Process.id && IsWindowVisible(h)) return (winTid = tid);
  }
  return null;
}
// Thread ids of this process without suspending anything (Process.enumerateThreads suspends every thread for its contexts).
const CreateToolhelp32Snapshot = new NativeFunction(k32t.getExportByName('CreateToolhelp32Snapshot'), 'pointer', ['uint', 'uint']);
const Thread32First = new NativeFunction(k32t.getExportByName('Thread32First'), 'int', ['pointer', 'pointer']);
const Thread32Next = new NativeFunction(k32t.getExportByName('Thread32Next'), 'int', ['pointer', 'pointer']);
function processThreads() {
  const snap = CreateToolhelp32Snapshot(4, 0); // TH32CS_SNAPTHREAD: every thread of the system
  if (snap.toInt32() === -1) throw new Error('CreateToolhelp32Snapshot failed');
  const e = Memory.alloc(28); // THREADENTRY32: dwSize, cntUsage, th32ThreadID +8, th32OwnerProcessID +12, ...
  e.writeU32(28);
  const ids = [];
  try {
    for (let ok = Thread32First(snap, e); ok; ok = Thread32Next(snap, e)) if (e.add(12).readU32() === Process.id) ids.push(e.add(8).readU32());
  } finally { CloseHandle(snap); }
  return ids;
}
const getDescription = k32t.findExportByName('GetThreadDescription'); // Windows 10 1607+
const GetThreadDescription = getDescription ? new NativeFunction(getDescription, 'int', ['pointer', 'pointer']) : null;
const LocalFree = new NativeFunction(k32t.getExportByName('LocalFree'), 'pointer', ['pointer']);
// shortcut: cached per thread id, so a reused id keeps the old name; clear the cache if long runs show odd names
const threadInfo = new Map();
function infoOf(tid) {
  if (threadInfo.has(tid)) return threadInfo.get(tid);
  let name = null, start = null;
  const h = OpenThread(0x0040 | 0x0800, 0, tid); // QUERY_INFORMATION | QUERY_LIMITED_INFORMATION
  if (!h.isNull()) {
    try {
      const p = Memory.alloc(Process.pointerSize);
      if (ntQueryThread(h, 9, p, Process.pointerSize, NULL) === 0) start = p.readPointer(); // ThreadQuerySetWin32StartAddress
      if (GetThreadDescription && GetThreadDescription(h, p) >= 0) {
        const s = p.readPointer();
        if (!s.isNull()) { name = s.readUtf16String() || null; LocalFree(s); }
      }
    } finally { CloseHandle(h); }
  }
  const mod = start ? Process.findModuleByAddress(start) : null;
  const r = { name, start: start ? start.toString() : null, module: mod ? mod.name : null };
  threadInfo.set(tid, r);
  return r;
}
// Frida cloaks its own threads and agent memory (they never show in enumerateThreads/ModuleMap); Toolhelp still lists them.
const isProbeThread = (tid, i) => Cloak.hasThread(tid) || (i.start !== null && Cloak.hasRangeContaining(ptr(i.start)))
  || /frida/i.test(i.module || '') || /^(gum|gmain|gdbus|frida)/i.test(i.name || '');

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
    let hits = [];
    try { hits = Memory.scanSync(r.base, r.size, pattern); } catch { continue; } // range freed or reprotected while scanning
    for (const m of hits) {
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
// every acquire enters a C callback (CModule, below): `acquireHooksPerSec` shows the cost; stop it when done.
// Also G11 (docs/18 H6 plan): entries at the 12 S0 gate sites by threads not holding +0x918, and the lock-free window per
// sim-thread IC_Route_Plan (report `g11`; `gateHooksPerSec` shows the cost of those hooks). Queue run kind: {routelock}.
const ROUTE_PLAN_LOCKED = ptr(0x82c6e0), LOCK_ACQUIRE = ptr(0x4e16f0), LOCK_RELEASE = ptr(0x4e17b0), LOCOMOTION_UPDATE = ptr(0x83ced0);
const qpcBuf = Memory.alloc(8);
const qpcFn = new NativeFunction(k32.getExportByName('QueryPerformanceCounter'), 'int', ['pointer']);
const qpfBuf = Memory.alloc(8);
new NativeFunction(k32.getExportByName('QueryPerformanceFrequency'), 'int', ['pointer'])(qpfBuf);
const qpfPerMs = qpfBuf.readU64().toNumber() / 1000;
function nowMs() { qpcFn(qpcBuf); return qpcBuf.readU64().toNumber() / qpfPerMs; }
// G11 (docs/18 "(3) Gating measurement"): the 12 S0 gate sites (planner leaves + 2 coarse gates) and IC_Route_Plan, the
// whole job an async plan would run. Prologue bytes verified on the Steam exe; checked before any attach, all or nothing.
const RL_GATES = {
  lotTableFind: [0x62eab0, '55 8b ec 83 e4 f0 81 ec'], goalLotPlanner: [0x63f490, '83 ec 54 8b 44 24 5c 85'],
  roomConnectivity: [0x63d2f0, '55 8b ec 83 e4 f8 81 ec'], lazyLotBuild: [0x63cb90, '83 ec 5c 53 8b 5c 24 64'],
  lotRebuild: [0x63d7b0, '8b 44 24 08 55 8b e9 8b'], roomGraphRefresh: [0x63d860, '8b 44 24 08 8b 54 24 04'],
  qReset: [0x61e320, '53 8b 5c 24 08 56 8b f1'], qResetClear: [0x61e420, '53 56 8b f1 8b 46 18 83'],
  searchStamp: [0x61e360, '66 83 05 c4 80 15 01 01'], qNodeClear: [0x61c020, 'b8 70 9c 00 00 e8 36 2e'],
  goalStep: [0x75c2d0, '55 8b ec 83 e4 f0 81 ec'], connectedRooms: [0x7929f0, '83 ec 14 53 33 db 57 89'],
};
const RL_ROUTE_PLAN = [0x765160, '0f 57 c0 8b 0d c4 40 1d'];
// prologue, or the wrapper of a known plugin's detour (ts3native's IC_Route_Plan detour when RouteAsync is on); else throw
function rlHookAt(name, [a, expect]) {
  const p = ptr(a), got = Array.from(new Uint8Array(p.readByteArray(8))).map(b => b.toString(16).padStart(2, '0')).join(' ');
  if (got === expect) return p;
  const target = got.startsWith('e9') ? p.add(5).add(p.add(1).readS32()) : null;
  if (target && ['ts3native.asi', 'Sims3Performance.asi'].includes(Process.findModuleByAddress(target)?.name)) return target;
  throw new Error(`routelock: ${name} @ 0x${a.toString(16)}: bytes ${got}, expected ${expect} (different build or unknown detour; nothing hooked)`);
}

// --- g11 begin: pure accounting, no Frida API (tools/probe/g11-check.mjs runs this block in node)
// Events come from the hooks with a ms clock t. "busy" = some thread holds +0x918 or a gate site is running.
// The movable time of a sim-thread IC_Route_Plan is min(plan length, gap from its return to the sim thread's next
// routing touch: an outermost +0x918 acquire or a gate-site entry, or the next plan's entry).
function g11Tracker(t0) {
  const g = { depth: {}, gate: {}, plan: 0, planT0: 0, pending: null, busy: 0, busySince: 0, busyMs: 0,
              plans: 0, planMs: 0, movableMs: 0, moves: [], secs: {}, sites: {} };
  const bucket = t => g.secs[Math.floor((t - t0) / 1000)] ??= { movableMs: 0, unlocked: 0, busyMs: 0 };
  const inc = t => { if (g.busy++ === 0) g.busySince = t; };
  // shortcut: a busy span is booked in the second it ends (spans are short next to 1 s); split at the boundary if that matters
  const dec = t => { if (g.busy > 0 && --g.busy === 0) { g.busyMs += t - g.busySince; bucket(t).busyMs += t - g.busySince; } };
  const close = t => {
    if (!g.pending) return;
    const m = Math.min(g.pending.len, t - g.pending.end);
    g.movableMs += m; bucket(t).movableMs += m;
    if (g.moves.length < 20000) g.moves.push(m);
    g.pending = null;
  };
  const held = tid => g.depth[tid] > 0;
  return {
    g,
    lockEnter(tid, sim, t) { if (sim && g.plan === 0 && !held(tid)) close(t); },
    lockHeld(tid, t) { if (!held(tid)) inc(t); g.depth[tid] = (g.depth[tid] || 0) + 1; },
    lockRelease(tid, t) { // true when the thread's outermost hold ended; false for a hold taken before the watch started
      if (!held(tid)) return false;
      if (--g.depth[tid] > 0) return false;
      dec(t); return true;
    },
    gateEnter(tid, sim, site, t) {
      const s = g.sites[site] ??= { entries: 0, unlocked: 0, unlockedSim: 0, unlockedOuter: 0 };
      s.entries++;
      if (!held(tid)) {
        s.unlocked++; if (sim) s.unlockedSim++;
        if (!(g.gate[tid] > 0)) { s.unlockedOuter++; bucket(t).unlocked++; } // the entries a [RouteGate] detour would lock
      }
      if (sim && g.plan === 0) close(t);
      g.gate[tid] = (g.gate[tid] || 0) + 1; inc(t);
    },
    gateLeave(tid, t) { if (g.gate[tid] > 0) { g.gate[tid]--; dec(t); } },
    planEnter(sim, t) { if (sim && g.plan++ === 0) { close(t); g.planT0 = t; } },
    planLeave(sim, t) {
      if (!sim || g.plan === 0 || --g.plan > 0) return;
      const len = t - g.planT0;
      g.plans++; g.planMs += len; g.pending = { end: t, len };
    },
    report(t) {
      const r3 = x => Math.round(x * 1000) / 1000, secs = (t - t0) / 1000, full = Math.floor(secs);
      const q = (a, f) => { if (!a.length) return null; const s = a.slice().sort((x, y) => x - y); return r3(s[Math.min(s.length - 1, Math.floor(f * s.length))]); };
      const series = k => Array.from({ length: full }, (_, i) => r3(g.secs[i]?.[k] ?? 0)); // completed seconds only
      const mv = series('movableMs'), un = series('unlocked'), bz = series('busyMs');
      const unlockedTotal = Object.values(g.sites).reduce((a, s) => a + s.unlockedOuter, 0);
      return {
        formula: 'movableMsPerSec = sum over sim-thread IC_Route_Plan calls of min(plan ms, gap ms to the next sim-thread +0x918 acquire or gate-site entry) / seconds',
        seconds: r3(secs), plans: g.plans, planMsPerSec: secs ? r3(g.planMs / secs) : null,
        movableMsPerSec: secs ? r3(g.movableMs / secs) : null, movableShare: g.planMs ? r3(g.movableMs / g.planMs) : null,
        movablePerSecond: { p50: q(mv, 0.5), p90: q(mv, 0.9), max: q(mv, 1) }, movablePerPlan: { p50: q(g.moves, 0.5), p90: q(g.moves, 0.9), max: q(g.moves, 1) },
        freeShare: secs ? r3(1 - g.busyMs / (secs * 1000)) : null, // share of wall time with +0x918 free and no gate site running
        unlockedGatePerSec: secs ? r3(unlockedTotal / secs) : null,
        sites: Object.fromEntries(Object.entries(g.sites).map(([k, s]) => [k, { ...s, unlockedPerSec: secs ? r3(s.unlockedOuter / secs) : null }])),
        open: g.pending !== null, // a plan whose gap never closed is not counted
        series: { movableMs: mv, unlockedGate: un, busyMs: bz },
      };
    },
  };
}
// --- g11 end

// The hot hooks are CModule C callbacks, not JS (queue g11 2026-10-16: JS callbacks on the engine-wide Lock_Acquire ran
// every lock in the engine through the JS lock; the sim fell to ~20% of normal and the probe hung after ~4 min). C filters
// on the learned lock and appends events (tid, kind | site << 8, QPC, wait ticks) to a ring of 2^RL_RING_LOG2 records; a
// JS timer drains it every RL_DRAIN_MS into the same accounting. A lapped ring counts `ring.lost` (queue.ts: FAIL).
// To keep the ring and the drain small, C keeps each thread's +0x918 depth and gate depth (a TLS slot) and folds events
// the accounting does not need one by one: a nested acquire becomes one record (count and wait), a nested release none,
// and a gate entry under +0x918 or inside another gate entry only counts per site (`ring.folded`). The tracker then sees
// depth 1 for nested holds, which gives the same numbers: nested holds never change busy time or the window.
const RL_RING_LOG2 = 18, RL_DRAIN_MS = 25; // 2^18 x 24 bytes = 6 MB
const RL_SITES = Object.keys(RL_GATES);
const RL_C = `
#include <gum/guminterceptor.h>
typedef struct { volatile guint32 seq, tid, ev, lo, hi, dt; } Rec;
typedef struct { guint32 on; guint64 t; } Acq;
/* st: 0 lock (+0x918, 0 = not learned), 1 next write index, 2 acquire hooks, 3 gate hooks, 4 LocomotionUpdate, 5 sim tid,
   6 generation (per start), 7 TLS index; fold: per site entries, unlocked, unlockedSim of folded gate entries */
extern volatile guint32 st[8];
extern volatile guint32 fold[3 * NSITES];
extern Rec ring[];
extern int __attribute__((stdcall)) QueryPerformanceCounter(guint64 *t);
extern int __attribute__((stdcall)) TlsSetValue(guint32 i, void *v);
static void add(volatile guint32 *p) { guint32 one = 1; __asm__ __volatile__("lock xaddl %0, (%1)" : "+r"(one) : "r"(p) : "memory"); }
static guint64 now(void) { guint64 t; QueryPerformanceCounter(&t); return t; }
static void put(GumInvocationContext *ic, guint32 ev, guint64 t, guint32 dt) {
  guint32 i = 1; volatile guint32 *w = &st[1];
  __asm__ __volatile__("lock xaddl %0, (%1)" : "+r"(i) : "r"(w) : "memory");
  Rec *r = &ring[i & MASK];
  r->tid = (guint32) gum_invocation_context_get_thread_id(ic); r->ev = ev; r->lo = (guint32) t; r->hi = (guint32) (t >> 32); r->dt = dt;
  r->seq = i + 1; /* last: x86 keeps store order, the drain reads a record only once seq matches */
}
/* this thread's state word: generation << 16 | gate depth << 8 | lock depth; read from the TEB (no API call) */
static volatile guint32 *slot(void) {
  guint8 *teb; guint32 i = st[7];
  __asm__ __volatile__("movl %%fs:0x18, %0" : "=r"(teb));
  if (i < 64) return (volatile guint32 *) (teb + 0xe10 + i * 4);
  if (*(guint32 **) (teb + 0xf94) == 0) TlsSetValue(i, 0); /* allocates the expansion slots; Gum restores the last error */
  return *(volatile guint32 **) (teb + 0xf94) + (i - 64);
}
static guint32 state(volatile guint32 *s) { return (*s >> 16) == (st[6] & 0xffff) ? *s & 0xffff : 0; }
static void store(volatile guint32 *s, guint32 v) { *s = (st[6] & 0xffff) << 16 | (v & 0xffff); }
#define LD(v) ((v) & 0xff)
#define GD(v) (((v) >> 8) & 0xff)
#define ON (GUM_IC_GET_INVOCATION_DATA(ic, guint32))
/* shortcut: the counters st[2..4] are plain increments (a rare lost count across threads); they are rates, not accounting */
void plan_enter(GumInvocationContext *ic) { *ON = st[0] != 0; if (*ON) put(ic, 1, now(), 0); }
void plan_leave(GumInvocationContext *ic) { if (*ON) put(ic, 2, now(), 0); }
void gate_enter(GumInvocationContext *ic) {
  st[3]++; *ON = 0;
  if (st[0] == 0) return; /* before the lock is known, nobody's hold can be told apart */
  guint32 site = GPOINTER_TO_UINT(gum_invocation_context_get_listener_function_data(ic));
  volatile guint32 *s = slot(); guint32 v = state(s);
  if (LD(v) > 0 || GD(v) > 0) { /* folded: under the lock, or nested in an outer entry that already made it busy */
    *ON = 2; add(&fold[site * 3]);
    if (LD(v) == 0) { add(&fold[site * 3 + 1]); if ((guint32) gum_invocation_context_get_thread_id(ic) == st[5]) add(&fold[site * 3 + 2]); }
  } else { *ON = 1; put(ic, 3 | site << 8, now(), 0); }
  if (GD(v) < 255) store(s, v + 0x100);
}
void gate_leave(GumInvocationContext *ic) {
  if (*ON == 0) return;
  volatile guint32 *s = slot(); guint32 v = state(s);
  if (GD(v) > 0) store(s, v - 0x100);
  if (*ON == 1) put(ic, 4, now(), 0);
}
void acq_enter(GumInvocationContext *ic) {
  Acq *a = GUM_IC_GET_INVOCATION_DATA(ic, Acq);
  st[2]++; a->on = st[0] != 0 && ic->cpu_context->ecx == st[0];
  if (a->on) a->t = now();
}
void acq_leave(GumInvocationContext *ic) { /* one record per acquire: kind 5 outermost (enter = t - dt), 6 nested */
  Acq *a = GUM_IC_GET_INVOCATION_DATA(ic, Acq);
  if (!a->on) return;
  volatile guint32 *s = slot(); guint32 v = state(s); guint64 t = now();
  put(ic, LD(v) > 0 ? 6 : 5, t, (guint32) (t - a->t));
  if (LD(v) < 255) store(s, v + 1);
}
void rel_enter(GumInvocationContext *ic) { /* only the outermost release is a record; one without a seen acquire is ignored */
  if (st[0] == 0 || ic->cpu_context->ecx != st[0]) return;
  volatile guint32 *s = slot(); guint32 v = state(s);
  if (LD(v) == 0) return;
  store(s, v - 1);
  if (LD(v) == 1) put(ic, 7, now(), 0);
}
void rpl_enter(GumInvocationContext *ic) { if (st[0] == 0) st[0] = ic->cpu_context->ecx + 0x918; put(ic, 8, now(), 0); }
void rpl_leave(GumInvocationContext *ic) { put(ic, 9, now(), 0); }
void loco_enter(GumInvocationContext *ic) { st[4]++; }
`;
const rl = { started: null, stopped: null, listeners: [], lock: null, simTid: null, iterations: 0, acquireHooks: 0, gateHooks: 0, threads: {},
             plans: [], plansByThread: {}, planMs: 0, overlapMs: 0, gaps: [], pending: null, g11: null,
             cm: null, st: null, fold: null, foldSnap: null, ring: null, timer: null, r: 0, perThread: {}, events: 0, lost: 0, maxBacklog: 0, drainMs: 0 };
const OVERLAP_BUCKETS = [0.1, 0.5, 1, 2, 5, 10, 20, 50, 100, Infinity];
function rlThread(tid) {
  return rl.threads[tid] ??= { acquires: 0, waitMs: 0, maxWaitMs: 0, holdMs: 0, maxHoldMs: 0, held: 0 };
}
// one ring event, in ring order; the accounting the JS callbacks did before (per-thread state replaces `this`)
function rlEvent(tid, ev, t, dtMs) {
  const g = rl.g11, sim = tid === rl.simTid;
  switch (ev & 0xff) {
    case 1: g.planEnter(sim, t); break;
    case 2: g.planLeave(sim, t); break;
    case 3: g.gateEnter(tid, sim, RL_SITES[ev >>> 8], t); break;
    case 4: g.gateLeave(tid, t); break;
    case 5: case 6: { // acquire returned; 5 = outermost (entered at t - dtMs), 6 = nested (only count and wait)
      const th = rlThread(tid);
      th.acquires++; th.waitMs += dtMs; th.maxWaitMs = Math.max(th.maxWaitMs, dtMs);
      if (ev === 6) break;
      const t0 = t - dtMs;
      g.lockEnter(tid, sim, t0);
      if (sim && rl.pending) {
        const ov = Math.min(rl.pending.len, t0 - rl.pending.end);
        rl.overlapMs += ov;
        if (rl.gaps.length < 20000) rl.gaps.push(ov);
        rl.pending = null;
      }
      th.held = t;
      g.lockHeld(tid, t);
      break;
    }
    case 7:
      if (g.lockRelease(tid, t)) {
        const th = rlThread(tid), h = t - th.held;
        th.holdMs += h; th.maxHoldMs = Math.max(th.maxHoldMs, h);
      }
      break;
    case 8: (rl.perThread[tid] ??= []).push(t); break;
    case 9: {
      const t0 = rl.perThread[tid]?.pop();
      if (t0 === undefined) break;
      const len = t - t0;
      rl.planMs += len;
      rl.plansByThread[tid] = (rl.plansByThread[tid] || 0) + 1;
      if (rl.plans.length < 20000) rl.plans.push(len);
      rl.pending = { end: t, len }; // closed by the sim thread's next acquire of the routing lock
      break;
    }
  }
}
function rlDrain() {
  if (rl.st === null) return;
  const t0 = nowMs(), n = 2 ** RL_RING_LOG2, mask = n - 1, wAt = rl.st.add(4);
  rl.foldSnap = new Uint32Array(rl.fold.readByteArray(12 * RL_SITES.length)); // before the write index: the ring then covers it
  let r = rl.r, c = (wAt.readU32() - r) >>> 0;
  if (c > n) { rl.lost += c - n; r = (r + c - n) >>> 0; c = n; } // lapped while we were away
  rl.maxBacklog = Math.max(rl.maxBacklog, c);
  while (c > 0) {
    const at = r & mask, k = Math.min(c, n - at);
    const a = new Uint32Array(rl.ring.add(at * 24).readByteArray(k * 24)), w2 = wAt.readU32();
    let i = 0;
    for (; i < k; i++) {
      const idx = (r + i) >>> 0, o = i * 6;
      if (((w2 - idx) >>> 0) > n) { rl.lost++; continue; } // overwritten while copying
      if (a[o] !== ((idx + 1) >>> 0)) break; // reserved, not written yet: next drain
      rl.events++;
      rlEvent(a[o + 1], a[o + 2], (a[o + 4] * 4294967296 + a[o + 3]) / qpfPerMs, a[o + 5] / qpfPerMs);
    }
    r = (r + i) >>> 0; c -= i;
    if (i < k) break;
  }
  rl.r = r;
  const st = new Uint32Array(rl.st.readByteArray(20));
  // shortcut: u32 counters wrap at 4.29e9 (acquire hooks at 2M/s: 35 min); restart the watch for longer runs
  rl.lock = st[0] ? ptr(st[0]) : null; rl.acquireHooks = st[2]; rl.gateHooks = st[3]; rl.iterations = st[4];
  rl.drainMs += nowMs() - t0;
}
// the tracker's site counts plus the entries C folded (under +0x918 or nested; never outer, so unlockedOuter is unchanged)
function rlFoldedSites(r) {
  const f = rl.foldSnap;
  if (!r || !f) return r;
  RL_SITES.forEach((site, i) => {
    if (!f[i * 3]) return;
    const s = r.sites[site] ??= { entries: 0, unlocked: 0, unlockedSim: 0, unlockedOuter: 0, unlockedPerSec: 0 };
    s.entries += f[i * 3]; s.unlocked += f[i * 3 + 1]; s.unlockedSim += f[i * 3 + 2];
  });
  return r;
}
function routeLockStart() {
  if (rl.listeners.length) return;
  const gateAt = Object.fromEntries(Object.entries(RL_GATES).map(([k, v]) => [k, rlHookAt(k, v)])); // throws before any attach
  const planAt = rlHookAt('IC_Route_Plan', RL_ROUTE_PLAN);
  if (rl.cm === null) { // built once and kept (with its TLS slot): a thread may still be leaving a callback after a detach
    rl.st = Memory.alloc(32);
    rl.fold = Memory.alloc(12 * RL_SITES.length);
    rl.ring = Memory.alloc(2 ** RL_RING_LOG2 * 24);
    const tls = new NativeFunction(k32.getExportByName('TlsAlloc'), 'uint32', [])();
    if (tls === 0xffffffff) throw new Error('routelock: TlsAlloc failed (nothing hooked)');
    rl.st.add(28).writeU32(tls);
    rl.cm = new CModule(`#define MASK ${2 ** RL_RING_LOG2 - 1}u\n#define NSITES ${RL_SITES.length}\n${RL_C}`,
                        { st: rl.st, fold: rl.fold, ring: rl.ring, QueryPerformanceCounter: k32.getExportByName('QueryPerformanceCounter'),
                          TlsSetValue: k32.getExportByName('TlsSetValue') });
  }
  rl.simTid = findInterpThread();
  // a new start after stop measures afresh: counters zeroed, new generation (every thread's depths read as 0), the lock
  // stays learned, the write index runs on and the drain starts at it
  rl.st.add(8).writeByteArray(new Uint8Array(12));
  rl.st.add(20).writeU32(rl.simTid ?? 0);
  rl.st.add(24).writeU32((rl.st.add(24).readU32() + 1) & 0xffff || 1);
  rl.fold.writeByteArray(new Uint8Array(12 * RL_SITES.length));
  Object.assign(rl, { started: nowMs(), stopped: null, iterations: 0, acquireHooks: 0, gateHooks: 0, threads: {}, plans: [], plansByThread: {},
                      planMs: 0, overlapMs: 0, gaps: [], pending: null, r: rl.st.add(4).readU32(), perThread: {}, events: 0, lost: 0,
                      maxBacklog: 0, drainMs: 0 });
  rl.g11 = g11Tracker(rl.started);
  const cm = rl.cm;
  rl.listeners.push(Interceptor.attach(planAt, { onEnter: cm.plan_enter, onLeave: cm.plan_leave }));
  RL_SITES.forEach((site, i) => rl.listeners.push(Interceptor.attach(gateAt[site], { onEnter: cm.gate_enter, onLeave: cm.gate_leave }, ptr(i))));
  rl.listeners.push(Interceptor.attach(ROUTE_PLAN_LOCKED, { onEnter: cm.rpl_enter, onLeave: cm.rpl_leave }));
  rl.listeners.push(Interceptor.attach(LOCK_ACQUIRE, { onEnter: cm.acq_enter, onLeave: cm.acq_leave }));
  rl.listeners.push(Interceptor.attach(LOCK_RELEASE, { onEnter: cm.rel_enter }));
  rl.listeners.push(Interceptor.attach(LOCOMOTION_UPDATE, { onEnter: cm.loco_enter }));
  rl.timer = setInterval(rlDrain, RL_DRAIN_MS);
}
function routeLockStop() {
  if (rl.listeners.length) rl.stopped = nowMs();
  for (const l of rl.listeners) l.detach();
  if (rl.listeners.length) Interceptor.flush();
  rl.listeners = [];
  if (rl.timer !== null) { clearInterval(rl.timer); rl.timer = null; rlDrain(); }
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
  const at = {}; // stage -> address to hook: the prologue, or S3PP's wrapper when its SimulatorStageProfiler detoured it
  for (const [name, [a, expect]] of Object.entries(STAGES)) { // all or nothing, before the first attach
    const p = ptr(a), got = Array.from(new Uint8Array(p.readByteArray(8))).map(b => b.toString(16).padStart(2, '0')).join(' ');
    const target = got.startsWith('e9') ? p.add(5).add(p.add(1).readS32()) : null;
    if (target && Process.findModuleByAddress(target)?.name === 'Sims3Performance.asi') { at[name] = target; continue; } // same args (thiscall)
    if (got !== expect) throw new Error(`${name} @ 0x${a.toString(16)}: bytes ${got}, expected ${expect}` +
      (got.startsWith('e9') ? ' (detoured by an unknown plugin)' : ' (different TS3W.exe build?)'));
    at[name] = p;
  }
  ts.started = Date.now();
  for (const stage of Object.keys(STAGES)) {
    ts.listeners.push(Interceptor.attach(at[stage], {
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
  if (rl.timer !== null) rlDrain();
  if (rl.started && rl.simTid === null) rl.simTid = findInterpThread();
  const end = rl.stopped ?? nowMs(), secs = rl.started ? (end - rl.started) / 1000 : 0, r3 = x => Math.round(x * 1000) / 1000;
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
    // G11 (docs/18): job = whole IC_Route_Plan, window also ended by unlocked gate-site entries; upper bound until S2's native counters
    gateHooksPerSec: secs ? Math.round(rl.gateHooks / secs) : 0,
    g11: rl.g11 && rlFoldedSites(rl.g11.report(end)),
    // the C ring: lost > 0 means the accounting above missed events (queue.ts verdict FAIL); drainMsPerSec = JS drain cost
    ring: { records: 2 ** RL_RING_LOG2, events: rl.events, lost: rl.lost, folded: rl.foldSnap ? rl.foldSnap.filter((_, i) => i % 3 === 0).reduce((x, y) => x + y, 0) : 0, maxBacklog: rl.maxBacklog, drainMsPerSec: secs ? r3(rl.drainMs / secs) : 0 },
  };
}

rpc.exports = {
  // One exported ts3native DWORD by name (e.g. ts3native_footprintMode); value 0|1 writes it first. Null when missing.
  nativeExport(name, value, offset) {
    const m = Process.findModuleByName('ts3native.asi');
    const e = m === null ? null : m.findExportByName(name);
    if (e === null) return null;
    const p = e.add(offset || 0); // offset: a field of ts3native_counters, e.g. compressMode +0x150
    if (value !== null) p.writeU32(value);
    return { name, value: p.readU32() };
  },
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
  // routeAsync 0|1 likewise sets the async route plan's switch (ts3native >= 0.5.0, [RouteAsync] Enabled=1).
  // vfResetMax 1 zeroes every [Verify] maximum (vf*Max*) after reading them, so the next read holds maxima since this call
  // (tools/harness/queue.ts sends it with the dump after the load: the live window, docs/51 §1).
  nativeCounters(routeWorker, routeBudget, routeAsync, alloc, waterJps, vfResetMax) {
    const m = Process.findModuleByName('ts3native.asi'); // Frida 17 has no static Module.findExportByName
    const c = m === null ? null : m.findExportByName('ts3native_counters');
    if (c === null) return null;
    const f = ['version', 'enabled', 'calls', 'hits', 'misses', 'stored', 'otherFails', 'windowMs'];
    if (c.readS32() >= 2) f.push('workerMode', 'workerReady', 'workerCalls', 'workerUs', 'workerMaxUs', 'waitUs', 'fallbacks', 'watchdogs');
    if (c.readS32() >= 3) f.push('budgetMode', 'budgetReady', 'budgetCalls', 'budgetCapped', 'budgetCapNodes', 'budgetCapUs', 'budgetMaxNodes', 'budgetMaxNodesOk', 'budgetMaxUsOk');
    if (c.readS32() >= 4) f.push('invokeHook', 'invokes'); // 0.4.1: worker's managed calls proxied to the sim thread (+0x64, +0x68)
    if (c.readS32() >= 5) f.push('asyncMode', 'asyncReady', 'asyncStarted', 'asyncRefused', 'asyncCollected', 'asyncDropped',
      'asyncUs', 'asyncMaxUs', 'asyncPumpUs', 'asyncPumpWaits'); // 0.5.0: async route plan (+0x6C..)
    if (c.readS32() >= 6) f.push('saveReady', 'saveMode', 'saveJobs', 'savePromoted', 'saveFallbacks', 'saveCount', 'saveLastUs',
      'saveLastWriteUs', 'saveLastBackupUs', 'saveLastCopyUs', 'saveLastFinalUs'); // 0.6.0: [SaveWrite] backup copy on a worker (docs/15 phase 7)
    if (c.readS32() >= 7) f.push('decompressReady', 'decompressMode', 'decompressVerify', 'decompressCalls', 'decompressFails',
      'decompressMismatches', 'decompressMaxUs', 'decompressInMB', 'decompressOutMB', 'decompressMs', 'decompressMainCalls',
      'decompressMainMs'); // 0.7.0: [Decompress] faster RefPack decoder (docs/15 phase 6, +0xC0..)
    if (c.readS32() >= 8) f.push('hostCalls', 'planOffCalls', 'planOffFails', 'planOnCalls', 'planOnFails'); // 0.7.1 (+0xF0..): docs/18
    if (c.readS32() >= 9) f.push('allocReady', 'allocMode', 'allocHitsK', 'allocMissesK', 'allocCachedK', 'allocPassedK',
      'allocThreads', 'allocFlushed', 'allocDropped', 'allocShutdowns', 'allocOomRetries'); // 0.8.0: [Alloc] EA allocator cache (docs/15 phase 8, +0x104..)
    if (c.readS32() >= 10) f.push('waterJpsReady', 'waterJpsMode', 'waterJpsVerify', 'waterJpsCallsK', 'waterJpsCellsK', 'waterJpsMismatches',
      'waterJpsFallbacks'); // 0.9.0: [WaterJps] water-grid jump as one loop (docs/15 phase 9, +0x130..)
    if (c.readS32() >= 11) f.push('compressReady', 'compressMode', 'compressVerify', 'compressBatches', 'compressJobs', 'compressHits',
      'compressMisses', 'compressMismatches', 'compressInMB', 'compressWaitMs', 'compressLastMs', 'compressPeakMB', 'flushReady', 'flushMode',
      'flushDeferred', 'flushJobs', 'flushWaits', 'flushWaitMs', 'flushLastMs', 'flushFails'); // 0.10.0: [SaveCompress] + [SaveFlush] (docs/15 phase 10, +0x14C..)
    if (c.readS32() >= 12) f.push('compressCommits', 'compressCommitPending', 'compressDirectCalls', 'compressDirectMs',
      'compressDirectMB'); // 0.10.1: where save compression runs (docs/15 phase 10, +0x19C..)
    if (c.readS32() >= 13) f.push('heightReady', 'heightMode', 'heightCalls', 'heightHits', 'heightMisses', 'heightRebuilds',
      'heightMismatches'); // 0.10.2: terrain height cache (docs/20 §4b, +0x1B0..)
    if (c.readS32() >= 14) f.push('gcCheckReady', 'gcOwnerTid', 'gcForeignAllocs', 'gcForeignTid'); // 0.10.2: GC thread check (docs/29 §4.5, +0x1CC..)
    if (c.readS32() >= 15) f.push('icLockReady', 'icLockEntersK', 'icLockContended', 'icLockWaitUs', 'icLockMaxWaitUs', 'icLockOwnerChanges',
      'icLockPrevTid', 'icLockLastTid', 'icLockLeaveWaiters', 'icLockCallback'); // 0.10.2: Mono loader lock check (docs/29 §5, +0x1DC..)
    if (c.readS32() >= 16) f.push('rgReady', 'rgLock', 'rgOwnerTid', 'rgAcquires', 'rgEntries', 'rgUntracked',
      ...Object.keys(RL_GATES).map(k => 'rgUnlocked_' + k), ...Object.keys(RL_GATES).map(k => 'rgJob_' + k)); // 0.10.3: route gate measurement (docs/18 S2a, +0x204..)
    if (c.readS32() >= 17) f.push('cacheReady', 'cacheMode', 'cacheHits', 'cacheMisses', 'cacheMismatches', 'cacheEvicted', 'cacheEntries',
      'cacheKB', 'cacheHitMB', 'cacheHashMs'); // 0.10.3: [SaveCompress] CacheMB compressed-bytes cache (docs/26 §8, +0x27C..)
    if (c.readS32() >= 18) f.push('mtReady', 'mtMode', 'mtSimTid', 'mtThreads', 'mtSimRegistered', 'mtPushCalls', 'mtStacksPushed',
      'mtCtxFails'); // 0.10.4: [MonoThread] step 1, GC thread registration and multi-stack root scan (docs/30, +0x2A4..)
    if (c.readS32() >= 19) f.push('lsReady', 'lsMode', 'lotAdds', 'lotAddUs', 'lotAddMaxUs', 'lotAddObjs', 'lotAddMaxObjs', 'lotRemoves',
      'lotRemoveUs', 'lotRemoveMaxUs', 'pumpCalls', 'pumpUs', 'pumpMaxUs', 'pumpLotPasses', 'pumpMaxLots', 'lotScoreCalls', 'lotScoreUs',
      'lotScoreMaxUs', 'lotScoreChanged', 'lotSetOn', 'lotSetOff', 'syncWaits', 'syncWaitUs', 'syncWaitMaxUs', 'objScenes', 'objSceneUs',
      'objSceneMaxUs', 'objSceneHist_lt0_1ms', 'objSceneHist_lt1ms', 'objSceneHist_lt10ms', 'objSceneHist_ge10ms', 'lsOtherFns',
      'lsLogDropped'); // 0.10.5: [LotStream] lot streaming measurement (docs/28 §5.5, +0x2C4..)
    // v20 keys follow vf<Group><Target><Suffix> (docs/15 [Verify] has the struct field for each); vfMem*Bytes are scaled below
    if (c.readS32() >= 20) f.push('vfReady', 'vfMode',
      ...['D5D910', '619540', '60D290', '6F3DC0', '599720', '6F1B00', '6EBE30', '6EA260', '6EBF50', '6F1D10', '50A860', '7294F0']
        .flatMap(a => ['Main', 'Sim', 'Other', 'OffDevice', 'Us', 'MaxUs'].map(s => `vfThr${a}${s}`)),
      'vfRaceC2A840Calls', 'vfRaceC2A840Race', 'vfRaceC2A570KiloCalls', 'vfRaceC2A570MaxInFlight', 'vfRace4B1AE0Calls', 'vfRace4B2D00Calls',
      'vfRace4B1AE0Overlap', 'vfRace939100KiloCalls', 'vfRace939100OffSimRace', ...[0, 1, 2, 3, 4, 5, 6, 7].map(i => `vfRace939100Ret${i}`),
      'vfCost4AFFC0Calls', 'vfCost4AFFC0Us', 'vfCost4AFFC0MaxUs', 'vfCost4AA1B0Calls', 'vfCost4AA1B0MaxPerCall',
      'vfCost939100Chain0', 'vfCost939100Chain1', 'vfCost939100Chain2to3', 'vfCost939100Chain4to7', 'vfCost939100Chain8to15',
      'vfCost939100Chain16up', 'vfCost939100MaxChain', 'vfCost939100Objects', 'vfCostAB8540KiloCalls', 'vfCostC60D30Calls',
      'vfCost712480Calls', 'vfCost712480MaxPerFrame', 'vfCost693780Calls', 'vfCost693780Us', 'vfCost693780MaxUs',
      'vfCostFdbWaitCalls', 'vfCostFdbWaitUs', 'vfCostFdbWaitMaxUs',
      ...['Main', 'Sim', 'Other'].flatMap(r => [`vfCostFBA5CC${r}Calls`, `vfCostFBA5CC${r}Us`, `vfCostFBA5CC${r}MaxUs`]),
      'vfCostECAA65Calls', 'vfCostSimServiceSlice', 'vfMemSpeedTreeBytes', 'vfMemVramBytes', 'vfMemResCacheBytes', 'vfMemResCacheFlushBytes',
      'vfMemResCacheBudgetBytes', 'vfThrDeviceTid', 'vfUntracked', 'vfLogDropped'); // 0.10.6: [Verify] (docs/15 [Verify], +0x348..; vfRace939100Ret* are code addresses)
    if (c.readS32() >= 21) f.push('vfReady2', // thread slots 12-45 in verify.cpp order (42-45: shape swap, tree rebuild, JobManager and SimService locks)
      ...['6583E0', '686CC0', '7140D0', '7135B0', '712F00', '863DC0', 'D7F8B0', '782BC0', '401BE0', '4E1DF0', '619770', 'C6D970', 'C688A0',
        '7D98C0', '722E00', '5AD8F0', '5E2050', 'ADD2C0', '6883A0', 'ADAE50', '939170', '703CB0', '50B300', '5989C0', '598F90', '6E4770',
        'ABFAC0', '7D2DB0', '50B270', '5172E0', 'C2A840', '712480', 'FD9744', 'FE4DC4']
        .flatMap(a => ['Main', 'Sim', 'Other', 'OffDevice', 'Us', 'MaxUs'].map(s => `vfThr${a}${s}`)),
      'vfRaceSceneOverlap', 'vfRace50AB70Overlap', 'vfRaceR1Overlap', 'vfRace686CC0Overlap', 'vfCostFrameDbPublished',
      'vfCostECAA65Loops'); // 0.10.7: [Verify] v21 (docs/15 [Verify], +0x55C..)
    if (c.readS32() >= 22) f.push('vfReady3', // thread slots 46-79 in verify.cpp order, each with its group prefix (docs/15 [Verify] v22)
      ...['vfMonoE85020', 'vfMonoEA1E30', 'vfMonoEA3382', 'vfMonoE51400', 'vfMonoE78780', 'vfMonoE53070', 'vfRt628A90', 'vfRt835240',
        'vfRt835110', 'vfRt939420', 'vfRt682760', 'vfEv59A430', 'vfEvReadyInPump', 'vfEvPumpSamePass', 'vfEvRcInline', 'vfEvRcInLotAdd',
        'vfEvJobInSyncWait', 'vfEvLotAddInSyncWait', 'vfEv76B1A0', 'vfThrAC1130', 'vfEvLotAddManaged', 'vfThrC6C290', 'vfThr452D30',
        'vfThr6916F0', 'vfThr452630', 'vfThr59BDE0', 'vfThr49C5F0', 'vfThr799780', 'vfThr799810', 'vfThrECA350', 'vfThr505620',
        'vfMonoD71900', 'vfMonoE4BEB0', 'vfMonoInvoke']
        .flatMap(a => ['Main', 'Sim', 'Other', 'OffDevice', 'Us', 'MaxUs'].map(s => `${a}${s}`)),
      'vfRace505620Overlap', 'vfCost799780Startup', 'vfCost799810Startup', 'vfMonoIcNestMax', 'vfMonoIcNestNegCalls'); // 0.10.8: [Verify] v22 (+0x8A8..)
    if (c.readS32() >= 23) f.push('jqReady', 'jqSimMode', 'jqWorkers', 'jqSimPumps', 'jqSimSkipPasses', 'jqSimSharedJobs', 'jqSimRenderJobs',
      'jqRenderJobs', 'jqWorkerTids', 'jqWorkerJobs', 'd3dMtReady', 'allocDedupe', 'allocDupFrees'); // 0.10.9: native fix batch 2 (+0xBF0..)
    if (c.readS32() >= 24) f.push('lscReady', 'lscMode', 'lscCallsK', 'lscHitsK', 'lscMismatches', 'idcReady', 'idcMode',
      'idcLookupsK', 'idcHitsK', 'idcFillsK', 'idcNotFoundK', 'idcStepsK', 'idcErases', 'idcMismatches', 'idcObjects'); // 0.10.10: native fix batch 3 (+0xC24..)
    if (c.readS32() >= 25) f.push( // 0.10.11: H6 S2b gate sites 13-15, docs/60 §2.2 closure sites, exact G11 window, S3a terrain graveyard (+0xC60..)
      ...['lotTableContains', 'exitBuildBuy', 'rebuildLotRouting'].map(k => 'rgUnlocked_' + k),
      ...['lotTableContains', 'exitBuildBuy', 'rebuildLotRouting'].map(k => 'rgJob_' + k),
      'rtReady', 'vfRtGateOffSim', 'vfRtQtReads', 'vfRtQtBare', 'vfRtQtWriteInJob', 'vfRtQtWaitUs', 'vfRtQtWaitMaxUs', 'vfRtDestroyInJob',
      'vfRtDestroyOffSim', 'vfRtDestroyWaitUs', 'vfRtLotGridWorker', ...['Main', 'Sim', 'Worker', 'Other'].map(r => `vfRtLotIdxRebuildInJob${r}`),
      'vfRtPendingInJob', 'vfRt228WaitUs', 'vfRtDrainSave', 'vfRtDrainBrd', 'vfRtDrainLotSave', 'vfRtDrainWaitUs', 'vfRtDebugSkip',
      'vfRtTeardownInJob', 'g11Plans', 'g11PlanUs', 'g11MovableUs', 'g11GapUs',
      ...['Lock', 'Gate', 'Gate13', 'Destroy', 'QtWrite', 'Plan'].map(s => `g11End${s}`),
      'tgReady', 'tgMode', 'vfRtGraveParked', 'vfRtGraveFreed', 'vfRtGraveSpins', 'vfRtGraveRaces', 'vfRtGraveHeld');
    if (c.readS32() >= 26) f.push( // 0.11.0: H6 S3b closures + M1 START limits (+0xD18..)
      'rgEnforce', 'rgGateWaits', 'rgGateWaitUs', 'rgGateWaitMaxUs', 'rgBorrows', 'rgDropLocked',
      ...['NoLock', 'Busy', 'Closing', 'Pending', 'DirtyIndex', 'FrameOpen'].map(s => `asyncRefusedM1${s}`),
      'vfRtDestroyInInvoke', 'vfRtClosingDrains', 'rtBusy', 'rtClosing');
    if (c.readS32() >= 27) f.push( // 0.11.2: vfG11CloseBy, closer key table (+0xD58..)
      ...[...Array(32).keys()].map(i => `g11CloseKey_${i}`), ...[...Array(32).keys()].map(i => `g11CloseN_${i}`),
      ...[...Array(32).keys()].map(i => `g11CloseLostUs_${i}`), 'g11CloseOther');
    if (c.readS32() >= 28) f.push('asyncSkipped', 'asyncBackoffs', 'asyncQueueFull', 'asyncQueueMax'); // 0.11.3: async job queue (+0xEDC..)
    if (c.readS32() >= 29) f.push(...['blockKey', 'blockN', 'blockUs'].flatMap(n => [...Array(32).keys()].map(i => `${n}_${i}`)), 'blockOther'); // 0.11.4: P0 owner-thread waits (+0xEEC..)
    if (c.readS32() >= 30) f.push('discReady', 'discMode', 'discDeferredAdd', 'discDeferredRemove', 'discApplied', 'discDropped',
      'discPendingMax', 'discDrainWaitUs', 'discAgeMaxMs', 'discForced', 'discNested', 'discOverflow'); // 0.12.0: design D (+0x1070..)
    if (c.readS32() >= 31) f.push('steerReady', 'steerMode', 'steerShadow', 'steerCalls', 'steerTaken',
      ...['State', 'QlBusy', 'Dirty', 'LotMiss', 'Deferred', 'Cache', 'Shape', 'Trip'].map(n => `steerFb${n}`),
      'steerUs', 'steerMaxUs', 'steerShadowRuns', 'steerShadowMismatch', 'steerShadowOrder', 'steerShadowQueryMismatch',
      'steerQlHolds', 'steerQlWaitUs', 'steerSimTreeWrite', 'steerSimCacheWrite', 'steerSimWriteInRo', 'steerPinMoved',
      'steerSeqSkew', 'steerUntracked', // 0.13.0: design S2 (+0x10A0..)
      'caReady', 'caMode', ...['FEB848', 'FEB970', 'FEBD10', 'FEBE38'].map(n => `caSimInJob${n}`), 'caWaits', 'caWaitUs',
      'caQlHeld'); // 0.13.0: fix C-A (+0x110C..)
    if (c.readS32() >= 32) f.push('ltReady', 'ltMode', 'ltShadow', 'ltCalls', 'ltTaken',
      ...['State', 'Nested', 'NoWm', 'QlBusy', 'Shape', 'Broken'].map(n => `ltFb${n}`), 'ltUs', 'ltMaxUs', 'ltShadowRuns',
      'ltShadowMismatch', 'ltQlHeldBlock',
      ...['AddRef', 'Release'].flatMap(o => ['Sim', 'WorkerInQl', 'WorkerNoQl', 'Other'].map(t => `portalRef${o}${t}`)),
      'p1bReady', ...['p1bKey', 'p1bN', 'p1bUs'].flatMap(n => [...Array(32).keys()].map(i => `${n}_${i}`)), 'p1bOther',
      'p1bNoFrame'); // 0.14.0: design L1 + probe P1b (+0x1130..)
    if (c.readS32() >= 33) f.push('steerTwStubs', 'steerT4Bare', 'steerS7Foreign', 'steerS7Sim', 'steerTripDiscard',
      ...['6653A0', '666010', '61ED80', '61ED10', '807110', '8077A0', '62E420', '947FA0', '948050', '9479D0', '947620']
        .map(n => `steerTwSite${n}`)); // 0.15.0: S2 tripwires T4 + T-S7 (+0x131C..)
    if (c.readS32() >= 34) f.push('discDestroyMode', 'discSweepMode', 'discDropAdd', 'discDropRemove', 'discDestroyApplied',
      'discDestroyInInvoke', 'discDeadSweep', 'discDeadHit', 'discDeadKeyMap', 'discIdMismatch'); // 0.16.0: fix D-X (+0x135C..)
    if (c.readS32() >= 35) f.push('rfbLogMs', 'rfbMsRefused', 'rfbMsQueue', 'rfbMsOther', 'rfbNRefused', 'rfbNQueue', 'rfbNOther',
      'rfbMax'); // 0.17.0: sync fallbacks >= 50 ms by reason group; rfbMax = ms << 8 | reason (+0x1384..)
    if (c.readS32() >= 36) f.push('s2TscPerUs', 's2ArmJobs', 's2JobEndDrop', 's2EntryKt', 's2EntryMaxT', 's2TwCalls', 's2TwKt',
      's2TwMaxT', 's2QlCalls', 's2QlKt', 's2QlMaxT', 's2ShadowKt', 's2ShadowMaxT', 's2TryCalls', 's2TryKt', 's2TryMaxT'); // 0.18.0: S2 armed stubs + timers (+0x13A4..)
    if (c.readS32() >= 37) f.push('g0wReady', 'g0wMode', 'g0wPasses', 'g0wPassUs', 'g0wPassMaxUs', 'g0wRunningAtEntry',
      'g0wJobStartInPass', 'g0wJobsHeld', 'g0wHoldUs', 'g0wHoldMaxUs', 'g0wDropJobWait', 'g0wDropSyncPlan', 'g0wWatchdog',
      'g0wGateWaits', 'g0wGate0Waits', 'g0wGate0Us'); // 0.19.0: design H2 locomotion window (+0x13E4..)
    if (c.readS32() >= 38) f.push('p10Ready', 'p10EntriesPlan', 'p10EntriesReplan', 'p10EntriesPlanFromPoint',
      'p10EntriesReplanFromPosition', 'p10PlanUsPlan', 'p10PlanUsReplan', 'p10PlanUsPlanFromPoint', 'p10PlanUsReplanFromPosition',
      'p10EarlyRetPlan', 'p10EarlyRetReplan', 'p10EarlyRetPlanFromPoint', 'p10EarlyRetReplanFromPosition', 'p10CanYieldReplan',
      'p10CanYieldPlanFromPoint', 'p10CanYieldReplanFromPosition'); // 0.20.0: probe P10 + G10-C (+0x1424..)
    if (c.readS32() >= 39) f.push(...['ObjCap', 'NullFp', 'Slot', 'LocalCap', 'Depth', 'NoActLot', 'LotTbl', 'IdCap', 'NoPlanner',
      'NodeCap', 'NodeCap2'].map(n => `s2Shape${n}`), 's2MaxObjs', 's2MaxLocal', 's2MaxNodes', 's2MaxIds', 's2MaxDepth'); // 0.21.0: S2 shape split (+0x1464..)
    if (c.readS32() >= 40) f.push('arReady', 'arMode', ...['Started', 'Collected', 'Refused', 'Dropped', 'Shadow', 'Active', 'JobUs',
      'SyncUs', 'SyncN'].flatMap(n => ['Replan', 'PlanFromPoint', 'ReplanFromPosition'].map(ic => `ar${n}${ic}`)), 'arMismatch',
      'arOrphan', 'arReserved'); // 0.22.0: design A10 (+0x14A4..0x1523)
    if (c.readS32() >= 41) f.push('orReady', 'orMode', 'orShadow', 'orCalls', 'orTaken', ...['State', 'Try', 'Lot', 'Cache', 'Shape',
      'Trip'].map(n => `orFb${n}`), 'orUs', 'orMaxUs', 'orShadowRuns', 'orShadowMismatch', 'orHits'); // 0.22.1: read-only IsPointObstructedBySim (+0x1524..)
    if (c.readS32() >= 42) f.push('slReady', 'slMode', 'slShadow', 'slCalls', 'slTaken', 'slUs', 'slMaxUs', 'slPass12', 'slPass0',
      ...['Gate', 'Miss', 'Portal', 'Lock', 'Managed', 'Table'].map(n => `slDrop${n}`),
      ...['State', 'Nested', 'Broken', 'Try'].map(n => `slFb${n}`), 'slShadowRuns', 'slShadowMismatch', 'slShadowDrop',
      'slRefSwap', 'slRelRetry'); // 0.22.2: design SH slot position hold (+0x1564..)
    if (c.readS32() >= 43) f.push('vcReady', 'vcSites', 'vcSkipped', 'vcHits', 'vcPairs', 'vcOutside', 'vcOverflow', 'vcFaults',
      'vcWindows', 'vcArmed'); // 0.23.0: [ProbeSnap] indirect call target sampler (+0x15C4..)
    if (c.readS32() >= 44) { // 0.24.0: [ProbeValues] routing value probe (+0x15EC..0x19DB, 252 fields; docs/108, values-report.ts)
      const n = (name, count, inner) => [...Array(count).keys()].map(i => inner ? `${name}_${Math.floor(i / inner)}_${i % inner}` : `${name}_${i}`);
      f.push('pvReady', 'pvHooks', 'pvSkipped', 'pvWorlds', ...n('pvTune', 13), ...n('pvWm', 4), 'pvSeaY', 'pvSeaChanges', 'pvWmDirty',
        'pvWorld983', ...n('pvG', 5), ...n('pvR', 5), ...n('pvGuard', 3), ...n('pvGBuild', 2), 'pvGBuildUs', 'pvGBuildMaxUs',
        ...n('pvGBuildThr', 3), ...n('pvGMsg', 3), ...n('pvRebuild', 3), 'pvDirtyPosts', 'pvJobs', 'pvTies', ...n('pvAStar', 3),
        ...n('pvAStarBit', 30, 10), ...n('pvAStarRef14', 3), ...n('pvAStarRef44', 3), ...n('pvLotPlan', 3), ...n('pvLotPlanBit', 33, 11),
        ...n('pvLotPlanObst', 3), ...n('pvStatus', 40, 20), 'pvIpAlloc', 'pvIpCalls', 'pvIpNonZero', ...n('pvPortal', 25, 5),
        ...n('pvQuery', 10, 5), 'pvQueryInPlan', ...n('pvH9', 2), 'pvH9InPlan', 'pvH9Plans', 'pvH9Max', ...n('pvPart', 2),
        ...n('pvPartOffSim', 2), ...n('pvLazy', 24, 3), ...n('pvBlk', 2), 'pvHighCost', 'pvCanSee', 'pvCanSeeRaise', 'pvEv68',
        'pvEv68InCanSee', 'pvFrameFull');
      if (c.readS32() >= 45) f.push(...n('pvRaiseIn', 3), ...n('pvRoomBuild', 3), ...n('pvLevelBuild', 3), ...n('pvRestore', 3),
        ...n('pvRBlk', 3), 'pvRRebase'); // 0.24.1: value probe fixes (+0x19DC..0x1A1B; docs/112, docs/108 §8)
      // 0.25.0: [RouteSnap] snapshot change counters P1 (+0x1A1C..0x20BF, 425 fields; docs/94 §10, snap-counters-report.ts).
      // snLeaf_<row>_<class> (rows: routesnap.cpp SL_*, classes sim/worker/other), snSrc_<row>_<source> (none/ic/wev/obj/fpen)
      if (c.readS32() >= 46) f.push('snReady', 'snHooks', 'snShared', 'snSkipped', 'snIcSlots', ...n('snLeaf', 57, 3), ...n('snSrc', 95, 5),
        'snUnattributed', ...n('snUaKey', 16), ...n('snUaN', 16), 'snUaOther', ...n('snWev', 96), 'snWevOffSim', ...n('snObj', 24, 3),
        'snObjNodes', 'snObjRegFails', 'snObjOpenAtIc', ...n('snIc', 39), ...n('snIcWrites', 39), 'snIcOffSim', ...n('snGSite', 5),
        'snStarts', ...n('snStartLots', 8), ...n('snStartWrites', 8), 'snStartUnknown', 'snSetFull', ...n('snFpEn', 3), 'snSeq',
        'snSeqGBuild', 'snSeqEv7', 'snFrameFull');
      // 0.26.0: [RouteSnap] Enabled=1 P3 images and audit (+0x20C0..0x2197, 54 fields; docs/94 P3, snap-audit-report.ts).
      // spCopies_/spAudits_/spMis_<input>: 0 R, 1 Gcore, 2 Gtiles, 3 bnm, 4 grid, 5 blend, 6 tune, 7 lots
      if (c.readS32() >= 47) f.push('spReady', 'spMode', 'spCuts', ...n('spCutSkip', 3), 'spCutUs', 'spCutMaxUs', 'spEpochs',
        'spFullBuilds', 'spVersionsLive', 'spImgLive', 'spKB', 'spKBMax', ...n('spCopies', 8), ...n('spAudits', 8), ...n('spMis', 8),
        'spAuditUs', 'spAuditMaxUs', 'spLots', 'spTiles', 'spDirtyOver', 'spKeyMapFull', 'spPendingAtCut', 'spDeferredAtCut',
        'spFaults', 'spCapOff', ...n('spDoor', 6));
      // 0.27.0: [RouteSnap] P1b frames and design B records (+0x2198..0x2767, 372 fields; docs/94 §15, snap-counters-report.ts).
      // snSrc2_<row>_<source - 5> (platform, user tool, tree init, terrain, road plan, flush, drain); snRec_<kind> (routesnap.cpp RK_*)
      if (c.readS32() >= 48) f.push(...n('snSrc2', 133, 7), 'snCacheFill', ...n('snFrameCalls', 6), ...n('snRec', 12),
        ...n('snRecSec', 8), 'snRecMaxSec', 'snRecUs', 'snCloneUs', 'snCloneN', 'snCloneBytesK', 'snCloneMaxBytes', 'snCloneFault',
        'snLotCopyN', ...n('snLotCopyUs', 8), 'snLotCopyMaxUs', 'snLotCopyMaxKB', 'snLotCopyFault', ...n('snUa2Key', 64),
        ...n('snUa2Next', 64), ...n('snUa2N', 64), 'snUa2Other');
      // 0.28.0: [RouteSnap] Lots=replay P3-B, replay clone and lot audit (+0x2768..0x2967, 128 fields; docs/94 §16,
      // snap-audit-report.ts). rpRec_/rpApplied_<type>: 1 INS, 2 REM, 3 STALE, 4 LOTS, 5 EPOCH, 6 AUDIT, 7 SYNC; rpSkip_: Q not
      // in the clone, applied to a stale lot, no footprint copy, no clone
      if (c.readS32() >= 49) f.push('rpReady', 'rpEpochs', 'rpEpochFails', 'rpEpochUs', 'rpEpochMaxUs', 'rpLots', 'rpFps', 'rpKB',
        'rpKBMax', ...n('rpRec', 8), 'rpDrop', 'rpRingMax', 'rpCopyUs', 'rpCopyFail', 'rpUnknownVt', ...n('rpApplied', 8), ...n('rpSkip', 4),
        'rpApplyUs', 'rpApplyMaxUs', 'rpLockWaitUs', 'rpLag', 'rpRebaseLots', 'rpRebaseUs', 'rpRebaseMaxUs', 'rpSyncs', 'rpDiverge',
        'rpFaults', 'rpAudits', 'rpMis', 'rpAuditUs', 'rpAuditMaxUs', 'rpAuditWorkerUs', 'rpGcFreed', 'rpOrphanFme', 'rpCapOff',
        'rpPrebuilt', 'rpPrebuildFail', 'rpPrebuildUs', 'rpPrebuildKB', 'rpExclLots', ...n('rpUnkClsVt', 4), ...n('rpUnkClsN', 4), ...n('rpSpare', 63));
      // 0.29.0: [RouteSnap] Mode=1 P4 part A (+0x2968..0x2A53, 59 fields; docs/94 §18). snAbort_<i>: p4.cpp kP4Ab order
      if (c.readS32() >= 50) f.push('snP4Ready', 'snP4Mode', 'snShadowArmed', 'snShadowBusy', 'snShadow', ...n('snM', 6), 'snM1Tie',
        ...n('snAbort', 16), 'snShadowUs', 'snShadowMaxUs', 'snLiveUs', 'snMatUs', 'snShadowFault', 'snDeadStamp', 'snStampWrap',
        'snCloneManaged', 'snCloneEvent', 'snCloneLazy', 'snCloneLiveLock', 'snDoorRec', 'snDoorReplay', 'snDoorMiss', 'snDoorUnused',
        'snDoorOver', 'snWorkerDirty', 'snDirtyOver', 'snNrWaits', 'snNrWaitUs', 'snNrArmWaitUs', ...n('snNrBits', 7), 'snJsonl',
        'snJsonlFail', 'snPathsFreed');
      // 0.29.0: P4 part B clone provider (+0x2A54..0x2AB3, 24 fields; docs/94 §21, p4clone.cpp; §17.9 N15 and Los). snPcRefused_<i>: 0 cut busy,
      // 1 cut no version, 2 no clone, 3 older cut / passed, 4 wait timeout, 5 epoch or active lot moved, 6 copy failed
      if (c.readS32() >= 51) f.push('snPcReady', 'snPcCuts', 'snPcCutUs', 'snPcCutMaxUs', ...n('snPcRefused', 8), 'snPcAcquired',
        'snPcMatUs', 'snPcPending', 'snPcFlushed', 'snPcFirstUse', 'snPcFaults', 'snPcOddQ', 'snPcDirtyLots', 'snPcDiscard',
        'snNrBitDl', 'snDlOnClone', 'arLosRefused');
      // private pools (+0x2AB4..0x2AD3, 8 fields; docs/94 §16.7, pools.cpp). v52 (design G) goes before this line when it merges
      if (c.readS32() >= 53) f.push('plReady', 'rpLivePool', 'plForeign', 'plOverlap', 'plEpochs', 'plKB', 'plFull', 'plJobs');
      // P4 portal references H20 (+0x2AD4..0x2AE3; docs/94 §21.8): D5 portal family bit, clone portal references, H20c refusals
      if (c.readS32() >= 54) f.push('snNrBitPortal', 'rpPortalRefs', ...n('snPcPortalPend', 2));
      // lazy Gtiles (+0x2AE4..0x2AF7; docs/94 §21.9): tiles built after the cut left them out, [0] reader [1] audit
      if (c.readS32() >= 55) f.push(...n('spTileFill', 2), ...n('spTileFillUs', 2), 'spTileOut');
      // H20 tripwire (+0x2AF8; docs/94 §21.8.1): AddPortalLocationVariation on a clone-held portal in a clone run (expected 0)
      if (c.readS32() >= 56) f.push('p4VarTrip');
      // lot-0 remove guard and P4 deferral stop (+0x2AFC..0x2B07; docs/94 §21.11): flushes, iterated-Q leftovers, clone-run stops
      if (c.readS32() >= 57) f.push('lrPendFlush', 'lrPendBusy', 'pcDeferStop');
      // v58 (reserved by a parallel branch) goes before this line when it merges.
      // [LoadProf] world-load routing share (+0x2B08..0x2DD7 without v58; docs/39 §10, loadprof.cpp): lpSite_<site>_<row*3+k>,
      // site in kLpSites order, row 0 = load window / 1 = after, k 0 calls / 1 us / 2 max us
      if (c.readS32() >= 59) f.push('lpReady', 'lpHooks', 'lpSkipped', 'lpLoads', 'lpEv7', 'lpFrameFull', 'lpWindowMs', ...n('lpRtUs', 2),
        ...n('lpRtThrUs', 3), ...n('lpSite', 168, 6));
      // [PlanRepeat] plan cache probe (+0x2DD8..0x2F6F without v58; docs/23 §18, planrepeat.cpp): class index 0 Plan,
      // 1 Replan, 2 PlanFromPoint, 3 ReplanFromPosition, 4 sim outside a route IC, 5 other thread; prExact_<cls>_<window>
      if (c.readS32() >= 60) f.push('prReady', 'prRing', 'prEvictYoung', 'prFault', 'prUnwound', 'prVersion', ...n('prPlans', 6),
        ...n('prUs', 6), ...n('prExact', 18, 3), ...n('prExactUs', 18, 3), ...n('prFpOnly', 6), ...n('prFpOnlyUs', 6),
        ...n('prFpOnlyMatch', 6), ...n('prChanged', 6), ...n('prMatch', 6), ...n('prMismatch', 6), ...n('prLoose', 6), ...n('prLooseUs', 6));
      // [RouteSnap] Mode=2, P5 (+0x2F70..0x3027 without v58; docs/94 §22, p5.cpp): m2Fb_<P4_AB_* index> (as snAbort),
      // m2VerM_<class>, m2DoorHist_<0 / 1-2 / 3-5 / 6-10 / 11+ asks>; m2Take is the `--ab routesnap` switch
      if (c.readS32() >= 61) f.push('m2Ready', 'm2Take', 'm2Off', 'm2Sites', 'm2SiteAt', 'm2Armed', 'm2Plans', 'm2Taken', ...n('m2Fb', 16),
        'm2FbList', 'm2FbNr', 'm2Verify', ...n('m2VerM', 6), 'm2VerBad', 'm2Trips', 'm2DoorAsk', ...n('m2DoorHist', 5), 'm2DoorWaitUs',
        'm2JobUs', 'm2Stale', 'm2StaleRecs', 'm2Guard');
    }
    const r = Object.fromEntries(f.map((n, i) => [n, c.add(i * 4).readS32()]));
    if (r.version >= 21) r.vfCostFrameDbConsumed = r.vfCost693780Calls; // the consume count under the I-41-4 key
    if (r.version >= 35) { // reason codes: RFB_* in ts3native.cpp (5 = M1Pending, 9 = QueueFull); the log names them
      r.rfbMaxMs = r.rfbMax >> 8;
      r.rfbMaxWhy = r.rfbMax & 0xFF;
    }
    if (vfResetMax === 1 && r.version >= 20) {
      let n = 0;
      f.forEach((name, i) => { if (/^vf\w*Max/.test(name)) { c.add(i * 4).writeS32(0); n++; } });
      r.vfMaxReset = n;
    }
    for (const [n, m] of [['vfMemSpeedTreeBytes', 1024], ['vfMemVramBytes', 1048576], ['vfMemResCacheBytes', 1024],
      ['vfMemResCacheFlushBytes', 1024], ['vfMemResCacheBudgetBytes', 1048576]]) if (r[n] > 0) r[n] *= m; // counters hold KB / MB
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
    if (routeAsync === 0 || routeAsync === 1) {
      if (r.asyncReady !== 1) return Object.assign(r, { error: 'async route plan not active ([RouteAsync] Enabled=0 or ts3native < 0.5.0)' });
      c.add(27 * 4).writeS32(routeAsync);
      r.asyncMode = c.add(27 * 4).readS32();
    }
    if (alloc === 0 || alloc === 1) { // fields are consecutive int32s, so the name's index is its offset / 4
      if (r.allocReady !== 1) return Object.assign(r, { error: 'alloc cache not active ([Alloc] Enabled=0 or ts3native < 0.8.0)' });
      c.add(f.indexOf('allocMode') * 4).writeS32(alloc);
      r.allocMode = c.add(f.indexOf('allocMode') * 4).readS32();
    }
    if (waterJps === 0 || waterJps === 1) {
      if (r.waterJpsReady !== 1) return Object.assign(r, { error: 'water JPS not active ([WaterJps] Enabled=0 or ts3native < 0.9.0)' });
      c.add(f.indexOf('waterJpsMode') * 4).writeS32(waterJps);
      r.waterJpsMode = c.add(f.indexOf('waterJpsMode') * 4).readS32();
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

  // Allocations >= minMB, regions grouped by AllocationBase (MBI +4), read-only (docs/08 M9: who owns the 128 MB block).
  allocations(minMB) {
    const mbi = Memory.alloc(MBI_SIZE);
    const byBase = new Map();
    let a = ptr(0x10000);
    const end = ptr('0xffff0000');
    while (a.compare(end) < 0) {
      if (VirtualQuery(a, mbi, MBI_SIZE) === 0) break;
      const base = mbi.readPointer();
      const size = mbi.add(12).readU32();
      const state = mbi.add(16).readU32();
      if (state !== MEM_FREE) {
        const key = mbi.add(4).readPointer().toString();
        const e = byBase.get(key) || { base: key, reserved: 0, committed: 0, type: mbi.add(24).readU32() };
        e.reserved += size;
        if (state === MEM_COMMIT) e.committed += size;
        byBase.set(key, e);
      }
      const next = base.add(size);
      if (next.compare(a) <= 0) break;
      a = next;
    }
    const mb = x => Math.round(x / 1048576 * 10) / 10;
    const min = (minMB || 32) * 1048576;
    const typeName = t => t === MEM_IMAGE ? 'MEM_IMAGE' : t === MEM_MAPPED ? 'MEM_MAPPED' : t === MEM_PRIVATE ? 'MEM_PRIVATE' : `0x${t.toString(16)}`;
    const list = [...byBase.values()].filter(e => e.reserved >= min).sort((x, y) => y.reserved - x.reserved);
    return { minMB: minMB || 32, count: list.length, allocations: list.map(e => ({ base: e.base, reservedMB: mb(e.reserved),
      committedMB: mb(e.committed), type: typeName(e.type), owner: e.type === MEM_IMAGE ? owner(ptr(e.base)) : null })) };
  },

  // First 5 bytes of MSVCR80!malloc/free and where a jmp there leads (S3SS mimalloc patches them), read-only.
  mallocHook() {
    const crt = Process.enumerateModules().find(m => m.name.toLowerCase() === 'msvcr80.dll');
    if (!crt) return { error: 'MSVCR80.dll not loaded' };
    const one = name => {
      const f = crt.findExportByName(name);
      if (f === null) return { name, error: 'no export' };
      const b = new Uint8Array(f.readByteArray(5));
      const r = { name, address: f.toString(), bytes: Array.from(b).map(x => x.toString(16).padStart(2, '0')).join(' '), jump: null, target: null };
      try {
        if (b[0] === 0xe9) { r.jump = 'E9 rel32'; r.target = f.add(5 + f.add(1).readS32()); }
        else if (b[0] === 0xff && b[1] === 0x25) { r.jump = 'FF25 [abs32]'; r.target = f.add(2).readPointer().readPointer(); }
      } catch (e) { r.error = `jump target unreadable: ${e.message}`; }
      if (r.target !== null) { r.targetModule = owner(r.target); r.target = r.target.toString(); }
      return r;
    };
    return { crt: crt.path, malloc: one('malloc'), free: one('free') };
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

  // Native stacks of other threads, one SuspendThread each (a few microseconds), never all at once. which: 'main' (default),
  // a thread id, or 'all' = every thread of the game except the sim thread, the probe's own threads and this one.
  // Never the sim thread: /simstack owns it (two samplers on it deadlocked the probe, 2026-10-13).
  threadStacks(which, maxFrames) {
    const self = Process.getCurrentThreadId();
    const sim = new Set([simTid, interpTid, storedTid(SIM_TID_AT)].filter(x => x));
    const window = windowThread(), main = storedTid(MAIN_TID_AT) ?? window;
    let tids;
    if (which === 'all') tids = processThreads().filter(t => t !== self && !sim.has(t) && !isProbeThread(t, infoOf(t)));
    else if (!which || which === 'main') tids = main ? [main] : [];
    else if (/^\d+$/.test(String(which))) tids = [Number(which)];
    else throw new Error(`which must be main, all or a thread id, not ${which}`);
    const threads = tids.map(tid => {
      const t = { thread: tid, role: tid === main ? 'main' : null, ...infoOf(tid), stack: null };
      if (sim.has(tid)) t.error = 'sim thread: sampled by /simstack only';
      else try { t.stack = threadStack(tid, maxFrames || 24); } catch (e) { t.error = e.message; }
      return t;
    });
    return { main, window, sim: [...sim], threads };
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
