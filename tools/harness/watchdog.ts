// Watches a running harness session and diagnoses it every --every minutes (default 5):
// game process, probe, world/live mode, sim clock progress, dialogs, address space, sim-thread stack.
// Tries to recover by itself (clear dialogs, leave Edit Town); exits non-zero with a diagnosis when the run
// is stuck or the game is gone, so whoever started it in the background gets notified.
// Only in a loaded world does a frozen clock count; the main menu and loading screens are skipped.
// Usage: node tools/harness/watchdog.ts [--every 1] [--port 47891] [--min-free-mb 300]
import { execFileSync } from 'node:child_process';
import { appendFileSync, existsSync, mkdirSync, readFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { parseArgs } from 'node:util';

const { values } = parseArgs({ options: {
  every: { type: 'string', default: '1' }, port: { type: 'string', default: '47891' }, 'min-free-mb': { type: 'string', default: '300' },
} });
const ROOT = resolve(import.meta.dirname, '..', '..');
const LOG = join(ROOT, 'probe-out', 'watchdog.log');
const SAVING = join(ROOT, 'probe-out', 'saving'); // written by age-save.ts while a save runs
const SAVE_GRACE_MS = 3 * 60000;
function savingFor(): number | null {
  try { return existsSync(SAVING) ? Date.now() - JSON.parse(readFileSync(SAVING, 'utf8')).started : null; } catch { return null; }
}
mkdirSync(join(ROOT, 'probe-out'), { recursive: true });
const base = `http://127.0.0.1:${values.port}`;
const sleep = (ms: number) => new Promise(r => setTimeout(r, ms));
const stamp = () => new Date().toISOString().replace('T', ' ').slice(0, 19);
const log = (m: string) => { const line = `${stamp()} ${m}`; console.log(line); appendFileSync(LOG, line + '\n'); };

async function get(path: string, timeoutMs = 30000): Promise<any> {
  const r = await fetch(`${base}/${path}`, { signal: AbortSignal.timeout(timeoutMs) });
  return r.json();
}
async function agent(cmd: string, args?: object): Promise<any> {
  const q = new URLSearchParams({ cmd, timeout: '8000' });
  if (args) q.set('args', JSON.stringify(args));
  return get(`agent?${q}`);
}
const gameRunning = () => execFileSync('tasklist', ['/NH', '/FI', 'IMAGENAME eq TS3W.exe']).toString().includes('TS3W.exe');

async function simStack(): Promise<string> {
  try {
    const threads = await get('threads?frames=12', 60000);
    const sim = threads.find((t: any) => t.stack.some((f: string) => f.includes('MonoScriptHost_Simulate')));
    return sim ? sim.stack.map((f: string) => f.split(' ').slice(1).join(' ')).slice(0, 8).join(' < ') : 'sim thread not found';
  } catch (e) { return `stack unavailable: ${(e as Error).message}`; }
}

function fail(reason: string, details: string[]): never {
  log(`STUCK: ${reason}`);
  for (const d of details) log(`  ${d}`);
  process.exit(1);
}

let lastMinutes: number | null = null;
let stalledChecks = 0;
log(`watchdog started, checking every ${values.every} min`);
for (;;) {
  await sleep(Number(values.every) * 60000);
  if (!gameRunning()) fail('game process is gone', ['look for an xcpt *.txt crash report in Documents\\Electronic Arts\\The Sims 3']);
  const status = await get('status', 30000).catch(e => ({ error: (e as Error).message }));
  if ((status as any).error) fail('probe not answering', [(status as any).error]);

  const state = await agent('game_state').catch(e => ({ ok: false, error: (e as Error).message }));
  const time = await agent('time').catch(e => ({ ok: false, error: (e as Error).message }));
  const mem = await get('memory', 60000).catch(() => null);
  const dialog = await agent('pending_dialogs').catch(() => null);
  const summary = [
    `state: ${state.ok ? `in_world=${state.result.in_world} sub=${state.result.sub_state} household=${state.result.active_household}` : state.error}`,
    `clock: ${time.ok ? `day ${time.result.day_number} ${time.result.clock} ${time.result.speed}` : time.error}`,
    `memory: ${mem ? `${mem.commitMB} MB committed, largest free ${mem.largestFreeMB} MB` : 'unavailable'}`,
    `dialog: ${dialog?.ok ? (dialog.result.open ? JSON.stringify(dialog.result.dialog.text?.[0] ?? '').slice(0, 80) : 'none') : 'unavailable'}`,
  ];

  if (!(state.ok && state.result.in_world)) { // menu or loading: nothing to judge yet
    log(`not in a world yet (main menu or loading) | ${summary.join(' | ')}`);
    lastMinutes = null;
    stalledChecks = 0;
    continue;
  }
  const saveAge = savingFor();
  if ((saveAge !== null && saveAge < SAVE_GRACE_MS) || state.result.save_in_progress) { // a save pauses the clock
    log(`saving (${saveAge !== null ? Math.round(saveAge / 1000) + ' s' : 'in progress'}); stall rule paused for up to 3 min | ${summary.join(' | ')}`);
    lastMinutes = null;
    stalledChecks = 0;
    continue;
  }
  const minutes = time.ok ? time.result.minutes_total : null;
  const moved = minutes !== null && lastMinutes !== null && minutes > lastMinutes;
  if (minutes !== null && lastMinutes !== null && !moved) stalledChecks++; else stalledChecks = 0;
  lastMinutes = minutes ?? lastMinutes;
  log(`ok? ${stalledChecks === 0 ? 'yes' : `clock stalled (${stalledChecks})`} | ${summary.join(' | ')}`);

  if (mem && mem.largestFreeMB < Number(values['min-free-mb'])) log(`WARNING: largest free block ${mem.largestFreeMB} MB; out-of-address-space crash risk`);

  if (stalledChecks > 0) {
    // recover: blocking dialog, Edit Town or another non-live mode, paused speed
    const d = await agent('auto_dialog').catch(() => null);
    if (d?.ok && d.result.open) log(`recovery: dialog "${d.result.title}" -> ${d.result.clicked}`);
    if (state.ok && state.result.in_world && state.result.sub_state !== 'LiveModeState') {
      log(`recovery: in ${state.result.sub_state}, entering live mode`);
      await agent('enter_live_mode').catch(() => null);
    }
    await sleep(20000); // give the recovery a moment, then judge
    const again = await agent('time').catch(() => null);
    if (again?.ok && again.result.minutes_total > (minutes ?? 0)) {
      log(`recovered: clock at day ${again.result.day_number} ${again.result.clock}`);
      lastMinutes = again.result.minutes_total;
      stalledChecks = 0;
    } else {
      fail(`sim clock has not moved for ${values.every} min in a loaded world`, [...summary, `sim thread: ${await simStack()}`]);
    }
  }
}
