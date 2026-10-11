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

rpc.exports = {

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
