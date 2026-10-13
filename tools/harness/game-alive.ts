// Fails a harness run when the game dies: TS3W.exe gone, or the probe agent's heartbeat (agentstatus) frozen or the probe
// unreachable (ECONNREFUSED) for more than 60 s. Without it a crashed game read as "no stuck Sims" and a repro printed PASS
// (bp-route-async, 2026-10-13). Prints "FAIL: game died at <time> (<why>)" plus the last crash-ledger triage entry, exits 1.
//   const alive = watchGame();      start after the world is loaded (cycle.ts restarts the game on purpose)
//   await alive.check();            before printing a PASS verdict
//   node tools/harness/game-alive.ts --self-check
import { execFileSync } from 'node:child_process';
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

if (process.argv[1]?.endsWith('game-alive.ts') && process.argv.includes('--self-check')) {
  const assert = (await import('node:assert')).strict;
  assert.equal(deadReason({ running: true, now: 70000, lastBeatAt: 20000 }), '');
  assert.equal(deadReason({ running: false, now: 1, lastBeatAt: 1 }), 'TS3W.exe process gone');
  assert.match(deadReason({ running: true, now: 90000, lastBeatAt: 20000 }), /not answering for 70 s \(heartbeat frozen\)/);
  assert.match(deadReason({ running: true, now: 90000, lastBeatAt: 0, error: 'ECONNREFUSED' }), /\(ECONNREFUSED\)/);
  console.log('self-check ok');
}
