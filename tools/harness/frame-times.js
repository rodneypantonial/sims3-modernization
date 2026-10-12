// Frame-time recorder for TS3W.exe (Steam build, SHA-256 10b2423e...0609), loaded by tools/harness/tests-be.ts (tests B, C).
// Hooks FUN_00ec9f00 (0xEC9F00-0xEC9FDE in game-copy/analysis/functions.csv): the per-frame function that ends with the
// post-present "inactive window ~30 FPS" limiter at 0xEC9FBA (S3SS Smooth Patch anchor, docs/03). S3SS patches bytes
// inside the function, not its prologue, so the prologue check below still holds with S3SS installed.
// [inferred] it runs once per rendered frame on the render thread; check the first run against DXVK's FPS HUD.
// Output <out>: CSV `epochMs,frameMs,workMs,fg` per frame. frameMs = time since the previous call started (the frame
// time), workMs = time inside this call, fg = 1 when the game window had focus in that second (an unfocused game is
// capped at ~30 FPS by the limiter above, so B numbers only count with fg = 1).
'use strict';

const FRAME = [0xec9f00, '83 ec 20 56 e8 b7 43 74'];

const k32 = Process.getModuleByName('kernel32.dll');
const qpc = new NativeFunction(k32.getExportByName('QueryPerformanceCounter'), 'int', ['pointer']);
const qpf = new NativeFunction(k32.getExportByName('QueryPerformanceFrequency'), 'int', ['pointer']);
const qbuf = Memory.alloc(8);
qpf(qbuf);
const ticksPerMs = Number(qbuf.readS64()) / 1000;
function now() { qpc(qbuf); return Number(qbuf.readS64()) / ticksPerMs; }
const epoch0 = Date.now(), q0 = now();

const u32 = Process.getModuleByName('user32.dll');
const getFg = new NativeFunction(u32.getExportByName('GetForegroundWindow'), 'pointer', []);
const getPid = new NativeFunction(u32.getExportByName('GetWindowThreadProcessId'), 'uint32', ['pointer', 'pointer']);
const pidBuf = Memory.alloc(4);
function foreground() { const h = getFg(); if (h.isNull()) return 0; getPid(h, pidBuf); return pidBuf.readU32() === Process.id ? 1 : 0; }

let file = null, buf = [], frames = 0, otherThread = 0, tid = null, last = 0, lastFlush = 0, fg = 1, listener = null;
const r2 = x => Math.round(x * 100) / 100;

function flush() { if (file && buf.length) { file.write(buf.join('')); file.flush(); buf = []; } }

function install(out) {
  if (!Process.getModuleByName('TS3W.exe').base.equals(ptr(0x400000))) throw new Error('TS3W.exe not at 0x400000');
  const p = ptr(FRAME[0]);
  const got = Array.from(new Uint8Array(p.readByteArray(8))).map(b => b.toString(16).padStart(2, '0')).join(' ');
  if (got !== FRAME[1]) throw new Error(`frame fn @ ${p}: bytes ${got}, expected ${FRAME[1]} (detoured or different build)`);
  file = new File(out, 'w');
  file.write('epochMs,frameMs,workMs,fg\n');
  listener = Interceptor.attach(p, {
    onEnter() {
      const id = Process.getCurrentThreadId();
      if (tid === null) tid = id;
      if (id !== tid) { otherThread++; return; }
      this.t = now();
    },
    onLeave() {
      if (this.t === undefined) return;
      const t = this.t, end = now();
      if (last) {
        buf.push(`${Math.round(epoch0 + t - q0)},${r2(t - last)},${r2(end - t)},${fg}\n`);
        frames++;
      }
      last = t;
      if (end - lastFlush >= 1000) { fg = foreground(); flush(); lastFlush = end; }
    },
  });
  console.log(`[frames] hooked ${p} -> ${out}`);
}

rpc.exports = {
  init(stage, parameters) { install(parameters.out); }, // throws on a prologue mismatch: nothing gets hooked
  dispose() {
    if (listener) listener.detach();
    flush();
    if (file) file.close();
    console.log(`[frames] ${frames} frames on thread ${tid}, ${otherThread} calls from other threads`);
  },
};
