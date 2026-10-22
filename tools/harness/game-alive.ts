// Fails a harness run when the game dies: TS3W.exe gone, or the probe agent's heartbeat (agentstatus) frozen or the probe
// unreachable (ECONNREFUSED) for more than 60 s. Without it a crashed game read as "no stuck Sims" and a repro printed PASS
// (bp-route-async, 2026-10-13). Prints "FAIL: game died at <time> (<why>)" plus the last crash-ledger triage entry, exits 1.
//   const alive = watchGame();      start after the world is loaded (cycle.ts restarts the game on purpose)
//   await alive.check();            before printing a PASS verdict
//   node tools/harness/game-alive.ts --self-check
// A hang (heartbeat frozen, or a stall guard's "nothing moved" with the game and probe up) is captured before the FAIL line,
// so the game is still alive for it: captureHang below (docs/172 §6).
import { execFile, execFileSync } from 'node:child_process';
import { mkdirSync, readFileSync, statSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';

const LIMIT_MS = 60000;
const stamp = (t = Date.now()) => new Date(t).toISOString().slice(11, 19);

// why the game counts as dead, or '' while alive (pure, covered by --self-check)
export function deadReason(s: { running: boolean; now: number; lastBeatAt: number; error?: string }): string {
  if (!s.running) return 'TS3W.exe process gone';
  if (s.now - s.lastBeatAt > LIMIT_MS) return `agent not answering for ${Math.round((s.now - s.lastBeatAt) / 1000)} s${s.error ? ` (${s.error})` : ' (heartbeat frozen)'}`;
  return '';
}

const gameRunning = () => {
  try { return execFileSync('tasklist', ['/NH', '/FI', 'IMAGENAME eq TS3W.exe']).toString().includes('TS3W.exe'); } catch { return true; } // tasklist failed: no verdict
};

function lastTriage(): string {
  try {
    const out = execFileSync(process.execPath, [join(import.meta.dirname, 'crash-ledger.ts'), 'triage']).toString().trim();
    return out.split(/\n\s*\n/).at(-1) ?? out;
  } catch (e) { return `crash-ledger triage failed: ${String(e).split('\n')[0]}`; }
}

export function watchGame(onDeath?: () => void) {
  let lastBeat: unknown, lastBeatAt = Date.now(), error = '', dying: Promise<void> | null = null;
  const check = async (): Promise<void> => {
    if (dying) return dying; // never resolves (exit at its end): a caller's PASS cannot print during the hang capture
    try {
      const hb = await (await fetch('http://127.0.0.1:47891/agentstatus', { signal: AbortSignal.timeout(10000) })).json();
      if (hb?.heartbeat === undefined || hb.heartbeat !== lastBeat) { lastBeat = hb?.heartbeat; lastBeatAt = Date.now(); } // no heartbeat field: an answer counts
      error = '';
    } catch (e) { error = String((e as any)?.cause?.code ?? e); }
    const now = Date.now(), running = gameRunning(), why = deadReason({ running, now, lastBeatAt, error });
    if (!why) return;
    console.log(`${stamp()} FAIL: game died at ${stamp(running ? lastBeatAt : now)} (${why})`);
    console.log(`last crash report (crash-ledger triage):\n${lastTriage()}`);
    onDeath?.();
    process.exit(1);
  };
  setInterval(check, 10000).unref();
  return { check };
}

// ---- Fail-fast guard for long waits (Rodney 2026-10-16; docs/TESTING.md "Fail-fast guards").
// Loads and saves legitimately take minutes with the agent silent, so wall time alone says nothing. Every GUARD_MS the
// guard samples the game; a sample without progress is a miss, and two misses in a row (~1 min) fail the wait.
// Progress = TS3W.exe running, the probe answering, and any of: agent heartbeat, world-loaded flag, committed memory
// (whole MB), ts3native.log size, or the caller's own mark (e.g. a save folder's mtime) changed since the last sample.
export const GUARD_MS = 30000;
const BIN = process.env.TS3_BIN ?? 'C:\\Program Files (x86)\\Steam\\steamapps\\common\\The Sims 3\\Game\\Bin';
export type GameSample = { running: boolean; answering: boolean; error?: string; mark: string };

// one guard step (pure, covered by --self-check): '' while progressing, else why this sample is a miss once misses >= 2
export function stallStep(st: { last: string | null; misses: number }, s: GameSample, liveOnly = false): string {
  const why = !s.running ? 'TS3W.exe process gone' : liveOnly ? '' : !s.answering ? `probe not answering (${s.error ?? '?'})`
    : s.mark === st.last ? 'nothing moved (agent heartbeat, world flag, committed MB, ts3native.log)' : '';
  if (s.answering) st.last = s.mark;
  st.misses = why ? st.misses + 1 : 0;
  return st.misses >= 2 ? why : '';
}

export async function sampleGame(port = 47891, extra?: () => unknown): Promise<GameSample> {
  const running = gameRunning();
  const j = async (p: string, ms: number) => (await fetch(`http://127.0.0.1:${port}/${p}`, { signal: AbortSignal.timeout(ms) })).json();
  try {
    const a = await j('agentstatus', 10000), m = await j('memory', 20000).catch(() => null);
    let log = 0;
    try { log = statSync(join(BIN, 'ts3native.log')).size; } catch { /* no plugin */ }
    return { running, answering: true, mark: [a?.heartbeat, a?.worldLoaded, typeof m?.commitMB === 'number' ? Math.round(m.commitMB) : '', log, String(await extra?.() ?? '')].join('|') };
  } catch (e) { return { running, answering: false, error: String((e as any)?.cause?.code ?? e), mark: '' }; }
}

// const guard = stallGuard('world load'); then in the wait loop: const why = await guard(); if (why) throw new Error(why);
// Cheap to call often: it samples at most every GUARD_MS. liveOnly: only "TS3W.exe gone" counts, first sample after
// GUARD_MS (game start: the probe server may still be starting and the exe handing over). Worst case to fail:
// 2 * GUARD_MS + one sample (<= 30 s of request timeouts) after the last progress.
export function stallGuard(what: string, o: { port?: number; extra?: () => unknown; liveOnly?: boolean } = {}) {
  const st = { last: null as string | null, misses: 0 };
  let next = o.liveOnly ? Date.now() + GUARD_MS : 0, why = '';
  return async (): Promise<string> => {
    if (why || Date.now() < next) return why;
    next = Date.now() + GUARD_MS;
    const r = stallStep(st, await sampleGame(o.port, o.extra), o.liveOnly);
    if (r) why = `${what}: ${r} for ${Math.round(2 * GUARD_MS / 1000)} s (fail-fast guard)`;
    if (r.startsWith('nothing moved')) why += `; ${await captureHang(hangDir(), { port: o.port }).catch(e => `hang capture failed: ${e}`)}`; // game and probe up: a hang
    return why;
  };
}

// ---- Hang capture (docs/172 §6). The game is hung but alive (heartbeat frozen, probe answering): before the kill, read
// (1) /simstack x5 at 1 s spacing, (2) /threadstack?which=all once (route worker, job threads: who holds what), (3) /native and
// the [HitchAttr] Holders ring ts3native_lockholders when the export exists, (4) /managedstack (the probe reads the interpreter
// frames from memory with the sim thread suspended; it needs no sim tick), (5) a procdump minidump (TS3_HANG_DUMP=off|mini|mp|ma,
// default mini = stacks + modules, a few MB; TS3_PROCDUMP = path; skipped when procdump is not found). Every call has its own
// timeout inside a ~30 s total cap, so a dead probe cannot hold up the kill. Files go to TS3_HANG_DIR (queue.ts: probe-out/queue/
// <tag>-hang, git-ignored) as hang-HHMMSS-*.json; returns the one-line summary "hang capture: N sim stacks, top frame X; ...".
export const HANG_CAP_MS = 30000;
export const hangDir = () => process.env.TS3_HANG_DIR ?? join(import.meta.dirname, '..', '..', 'probe-out', 'hang');
const HO_ROLE = ['sim', 'main', 'route worker', 'replay consumer', 'other'];
const hx = (v: number) => `0x${(v >>> 0).toString(16).toUpperCase()}`;

// ts3native_lockholders (hitchattr.cpp HoRing): version, enabled, minUs, head, then 64 x 32-byte HoRec; record i (1-based) at
// r[(i - 1) & 63], valid when its seq == i. Pure, covered by --self-check.
export function decodeHolders(hex: string) {
  const b = Buffer.from(hex, 'hex'), i32 = (o: number) => b.readInt32LE(o), u32 = (o: number) => b.readUInt32LE(o);
  if (b.length < 0x10 + 64 * 32) return { error: `ring read ${b.length} bytes, need ${0x10 + 64 * 32}` };
  const head = i32(0xC), records = [];
  for (let i = Math.max(1, head - 63); i <= head; i++) {
    const o = 0x10 + ((i - 1) & 63) * 32;
    if (i32(o) !== i) continue; // being written or overwritten
    records.push({ seq: i, waiter: i32(o + 4) ? 'main' : 'sim', lock: hx(u32(o + 8)), us: i32(o + 12), holderTid: u32(o + 16),
      holderRole: HO_ROLE[i32(o + 20)] ?? String(i32(o + 20)), holderSite: hx(u32(o + 24)), waiterSite: hx(u32(o + 28)) });
  }
  return { version: i32(0), enabled: i32(4), minUs: i32(8), head, records };
}

type HangGet = (path: string, ms: number) => Promise<any>;
const sleepMs = (ms: number) => new Promise(r => setTimeout(r, ms));
const frameName = (f: string) => f.split(' ').slice(1).join(' ') || f; // "0x1234 TS3W.exe!Name+0x12" -> "TS3W.exe!Name+0x12"

function procdump(file: string, ms: number): Promise<string> {
  const mode = process.env.TS3_HANG_DUMP ?? 'mini';
  if (mode === 'off') return Promise.resolve('dump off (TS3_HANG_DUMP=off)');
  const args = ['-accepteula', ...(mode === 'ma' ? ['-ma'] : mode === 'mp' ? ['-mp'] : []), 'TS3W.exe', file];
  return new Promise(res => execFile(process.env.TS3_PROCDUMP ?? 'procdump', args, { timeout: ms, windowsHide: true }, (e: any) => {
    let size = 0; try { size = statSync(file).size; } catch { /* none written */ }
    res(size ? `dump ${mode} ${Math.round(size / 1048576)} MB` : e?.code === 'ENOENT' ? 'dump skipped: procdump not found (Sysinternals; TS3_PROCDUMP=path)'
      : `dump failed: ${e?.killed ? `timeout ${Math.round(ms / 1000)} s` : String(e?.message ?? e).split('\n')[0]}`);
  }));
}

export async function captureHang(dir: string, o: { port?: number; get?: HangGet; capMs?: number; spacingMs?: number; dump?: (file: string, ms: number) => Promise<string> } = {}): Promise<string> {
  const end = Date.now() + (o.capMs ?? HANG_CAP_MS), left = () => end - Date.now();
  const raw: HangGet = o.get ?? (async (p, ms) => (await fetch(`http://127.0.0.1:${o.port ?? 47891}/${p}`, { signal: AbortSignal.timeout(ms) })).json());
  // own timeout around every call (a fake or a stuck socket too), never past the cap
  const get = (p: string, ms: number) => {
    const t = Math.min(ms, left());
    return t <= 0 ? Promise.reject(new Error('capture cap reached')) : Promise.race([raw(p, t), sleepMs(t).then(() => { throw new Error(`timeout ${t} ms`); })]);
  };
  const err = (e: unknown) => ({ error: String((e as Error)?.message ?? e) }) as any;
  mkdirSync(dir, { recursive: true });
  const pre = join(dir, `hang-${new Date().toISOString().slice(11, 19).replace(/:/g, '')}`), put = (n: string, v: unknown) => writeFileSync(`${pre}-${n}.json`, JSON.stringify(v, null, 1));
  const sims: any[] = [];
  let fails = 0, sampleTimedOut = false;
  for (let i = 0; i < 5 && fails < 2 && left() > 0; i++) { // two failures in a row: the probe is dead, go on
    if (i) await sleepMs(Math.min(o.spacingMs ?? 1000, Math.max(0, left())));
    const r = await get('simstack?frames=32', 4000).catch(err);
    if (/timeout|cap reached/.test(r.error ?? '')) sampleTimedOut = true;
    fails = r.stack?.length ? 0 : fails + 1;
    sims.push({ time: new Date().toISOString(), ...r });
  }
  put('simstack', sims);
  const ok = sims.filter(s => s.stack?.length), top = ok[0] ? frameName(ok[0].stack[0]) : '-';
  const same = ok.filter(s => frameName(s.stack[0]) === top).length;
  const th = await get('threadstack?which=all&frames=24', 8000).catch(err);
  put('threadstack', th);
  const nat = await get('native', 4000).catch(err);
  put('native', nat);
  let holders = 'holders: no export';
  const ex = await get('nativeexport?name=ts3native_lockholders', 3000).catch(err);
  if (ex?.address) {
    const rd = await get(`read?addr=${ex.address}&len=${0x10 + 64 * 32}`, 3000).catch(err);
    const ring: any = rd.hex ? decodeHolders(rd.hex) : rd;
    put('lockholders', ring);
    holders = ring.records ? `holders ${ring.enabled ? 'on' : 'off'}, ${ring.records.length} records` : `holders: ${ring.error}`;
  } else if (ex?.error) holders = `holders: ${ex.error}`;
  else if (ex) holders = 'holders: probe agent without the export address';
  // a timed-out /simstack may still be suspending the sim thread in the probe: a second sampler on it deadlocked the probe (2026-10-13)
  let managed = 'managed: skipped (a sim stack sample timed out)';
  if (!sampleTimedOut) {
    const m = await get('managedstack?frames=64', 4000).catch(err);
    put('managedstack', m);
    managed = m.methods?.length ? `managed top ${m.methods[0]}` : `managed: ${m.error ?? 'empty'}`;
  }
  const dumpFile = `${process.env.TS3_HANG_DUMP_DIR ? join(process.env.TS3_HANG_DUMP_DIR, pre.split(/[\\/]/).at(-1)!) : pre}.dmp`;
  const dump = left() > 2000 ? await (o.dump ?? procdump)(dumpFile, left()) : 'dump skipped: capture cap reached';
  const threads = th.threads?.filter((t: any) => t.stack?.length).length ?? 0;
  const line = `hang capture: ${ok.length} sim stacks, top frame ${top}${ok.length > 1 ? ` (${same}/${ok.length} same)` : ''}; ${threads} other thread stacks${th.error ? ` (${th.error})` : ''}; native ${nat?.error ?? `v${nat?.version ?? '?'}`}; ${holders}; ${managed}; ${dump}; ${pre}-*`;
  writeFileSync(`${pre}-summary.txt`, line + '\n');
  return line;
}

if (process.argv[1]?.endsWith('game-alive.ts') && process.argv.includes('--self-check')) {
  const assert = (await import('node:assert')).strict;
  const st = { last: null as string | null, misses: 0 }, smp = (mark: string, running = true, answering = true) => ({ running, answering, mark });
  assert.equal(stallStep(st, smp('1|true|900|10|')), ''); // first sample is progress
  assert.equal(stallStep(st, smp('1|true|900|10|')), ''); // one miss
  assert.match(stallStep(st, smp('1|true|900|10|')), /nothing moved/); // two in a row
  assert.equal(stallStep(st, smp('2|true|900|10|')), ''); assert.equal(st.misses, 0); // progress resets
  assert.equal(stallStep(st, smp('', true, false)), ''); assert.match(stallStep(st, smp('', true, false)), /probe not answering/);
  assert.equal(stallStep(st, smp('3|true|900|10|')), ''); assert.equal(st.misses, 0); // probe back and moving
  const lo = { last: null as string | null, misses: 0 };
  assert.equal(stallStep(lo, smp('a'), true), ''); assert.equal(stallStep(lo, smp('a'), true), ''); assert.equal(stallStep(lo, smp('a'), true), ''); // liveOnly ignores the mark
  assert.equal(stallStep(lo, smp('a', false), true), ''); assert.match(stallStep(lo, smp('a', false), true), /process gone/);
  assert.equal(deadReason({ running: true, now: 70000, lastBeatAt: 20000 }), '');
  assert.equal(deadReason({ running: false, now: 1, lastBeatAt: 1 }), 'TS3W.exe process gone');
  assert.match(deadReason({ running: true, now: 90000, lastBeatAt: 20000 }), /not answering for 70 s \(heartbeat frozen\)/);
  assert.match(deadReason({ running: true, now: 90000, lastBeatAt: 0, error: 'ECONNREFUSED' }), /\(ECONNREFUSED\)/);
  // hang capture against fake probes (fixtures): healthy, dead (refused), hung (never answers)
  const { mkdtempSync, readdirSync, rmSync } = await import('node:fs'), { tmpdir } = await import('node:os');
  const ring = Buffer.alloc(0x10 + 64 * 32);
  ring.writeInt32LE(1, 0); ring.writeInt32LE(1, 4); ring.writeInt32LE(20000, 8); ring.writeInt32LE(2, 0xC);
  for (const [i, role] of [[1, 2], [2, 4]]) { const o = 0x10 + (i - 1) * 32; ring.writeInt32LE(i, o); ring.writeUInt32LE(0x11C0918, o + 8); ring.writeInt32LE(35000, o + 12); ring.writeUInt32LE(4242, o + 16); ring.writeInt32LE(role, o + 20); ring.writeUInt32LE(0x62EF7A, o + 28); }
  const hr: any = decodeHolders(ring.toString('hex'));
  assert.equal(hr.records.length, 2); assert.equal(hr.records[0].holderRole, 'route worker'); assert.equal(hr.records[1].waiterSite, '0x62EF7A');
  ring.writeInt32LE(0, 0x10 + 32); assert.equal((decodeHolders(ring.toString('hex')) as any).records.length, 1); // record 2 mid-write
  assert.match(String((decodeHolders('00') as any).error), /need/);
  const fixtures: Record<string, unknown> = {
    simstack: { thread: 7, stack: ['0x6EB41234 ts3native.asi+0x11234', '0x0062EF7A TS3W.exe!Gate0+0x4a'] },
    threadstack: { threads: [{ thread: 8, role: 'main', stack: ['0x1 TS3W.exe!Main+0x1'] }, { thread: 9, stack: null, error: 'x' }] },
    native: { version: 99 }, nativeexport: { name: 'ts3native_lockholders', value: 1, address: '0x6EC00000' }, read: { hex: ring.toString('hex') },
    managedstack: { methods: ['Sims3.Gameplay.Autonomy.Run'] },
  };
  const fake = (p: string) => Promise.resolve(fixtures[p.split('?')[0]]);
  const tmp = mkdtempSync(join(tmpdir(), 'hang-'));
  try {
    const okLine = await captureHang(join(tmp, 'a'), { get: fake, spacingMs: 0, dump: async () => 'dump fake' });
    assert.match(okLine, /^hang capture: 5 sim stacks, top frame ts3native\.asi\+0x11234 \(5\/5 same\); 1 other thread stacks; native v99; holders on, 1 records; managed top Sims3\.Gameplay\.Autonomy\.Run; dump fake; /);
    assert.deepEqual(readdirSync(join(tmp, 'a')).map(f => f.replace(/^hang-\d+-/, '')).sort(), ['lockholders.json', 'managedstack.json', 'native.json', 'simstack.json', 'summary.txt', 'threadstack.json']);
    const dead = await captureHang(join(tmp, 'b'), { get: () => Promise.reject(new Error('ECONNREFUSED')), spacingMs: 0, dump: async () => 'dump fake' });
    assert.match(dead, /^hang capture: 0 sim stacks, top frame -; 0 other thread stacks \(ECONNREFUSED\); native ECONNREFUSED; holders: ECONNREFUSED; managed: ECONNREFUSED; dump fake/);
    assert.equal(JSON.parse(readFileSync(readdirSync(join(tmp, 'b')).map(f => join(tmp, 'b', f)).find(f => f.endsWith('simstack.json'))!, 'utf8')).length, 2); // stops after two failures
    const t0 = Date.now(), hung = await captureHang(join(tmp, 'c'), { get: () => new Promise(() => {}), capMs: 400, spacingMs: 0, dump: async () => 'dump fake' });
    assert.ok(Date.now() - t0 < 1500, 'cap holds against a probe that never answers');
    assert.match(hung, /0 sim stacks.*managed: skipped \(a sim stack sample timed out\); dump skipped: capture cap reached/);
  } finally { rmSync(tmp, { recursive: true, force: true }); }
  console.log('self-check ok');
}
