// Fails a harness run when the game dies: TS3W.exe gone, or the probe agent's heartbeat (agentstatus) frozen or the probe
// unreachable (ECONNREFUSED) for more than 60 s. Without it a crashed game read as "no stuck Sims" and a repro printed PASS
// (bp-route-async, 2026-10-13). Prints "FAIL: game died at <time> (<why>)" plus the last crash-ledger triage entry, exits 1.
//   const alive = watchGame();      start after the world is loaded (cycle.ts restarts the game on purpose)
//   await alive.check();            before printing a PASS verdict
//   node tools/harness/game-alive.ts --self-check
import { execFileSync } from 'node:child_process';
import { statSync } from 'node:fs';
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
  let lastBeat: unknown, lastBeatAt = Date.now(), error = '', dead = false;
  const check = async () => {
    if (dead) return;
    try {
      const hb = await (await fetch('http://127.0.0.1:47891/agentstatus', { signal: AbortSignal.timeout(10000) })).json();
      if (hb?.heartbeat === undefined || hb.heartbeat !== lastBeat) { lastBeat = hb?.heartbeat; lastBeatAt = Date.now(); } // no heartbeat field: an answer counts
      error = '';
    } catch (e) { error = String((e as any)?.cause?.code ?? e); }
    const now = Date.now(), running = gameRunning(), why = deadReason({ running, now, lastBeatAt, error });
    if (!why) return;
    dead = true;
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
    return why;
  };
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
  console.log('self-check ok');
}
