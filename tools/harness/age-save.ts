// Ages the loaded game: runs at speed 3 for N sim days (dismissing dialogs), then saves under a new name.
// Needs the probe server (tools/probe/ts3probe.py serve) attached and the ts3agent mod in the game.
// Saves a checkpoint (<save>-dayN) every --checkpoint sim days, so a crash can be reproduced from the last one.
// Keeps the active household alive (keep_alive tops up motives below --keep-alive, default -50, and repairs broken home objects).
// Usage: node tools/harness/age-save.ts --days 14 --save Baseline-SV [--until-day 15] [--checkpoint 2] [--keep-alive -50|off]
import { rmSync, writeFileSync, mkdirSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { parseArgs } from 'node:util';
import { GUARD_MS, stallGuard } from './game-alive.ts';

// Marker the watchdog reads: a save pauses the clock, which must not count as a stall.
const SAVING = join(resolve(import.meta.dirname, '..', '..'), 'probe-out', 'saving');

const { values } = parseArgs({ options: {
  days: { type: 'string', default: '14' }, save: { type: 'string' }, port: { type: 'string', default: '47891' },
  checkpoint: { type: 'string', default: '2' }, 'until-day': { type: 'string' },
  'keep-alive': { type: 'string', default: '-50' }, // top up active-household motives below this; 'off' to disable
} });
if (!values.save) { console.error('usage: node tools/harness/age-save.ts --days 14 --save Baseline-SV'); process.exit(2); }
const base = `http://127.0.0.1:${values.port}`;

type Reply = { ok: boolean; result?: any; error?: string };
async function agent(cmd: string, args?: object, timeoutMs = 10000): Promise<Reply> {
  const q = new URLSearchParams({ cmd, timeout: String(timeoutMs) });
  if (args) q.set('args', JSON.stringify(args));
  const r = await fetch(`${base}/agent?${q}`, { signal: AbortSignal.timeout(timeoutMs + 20000) });
  return r.json() as Promise<Reply>;
}
const sleep = (ms: number) => new Promise(r => setTimeout(r, ms));
const stamp = () => new Date().toISOString().slice(11, 19);

// auto_dialog fills name fields (e.g. a newborn) and clicks the accepting button; see HarnessHandlers.AutoDialog.
async function dismissDialogs(): Promise<void> {
  for (let i = 0; i < 8; i++) {
    const d = await agent('auto_dialog');
    if (!d.ok) { console.log(`${stamp()} dialog not cleared: ${d.error}`); return; }
    if (!d.result.open) return;
    const filled = d.result.filled?.length ? ` (named ${d.result.filled.join(', ')})` : '';
    console.log(`${stamp()} dialog "${(d.result.title ?? '').slice(0, 60)}" -> ${d.result.clicked}${filled}`);
    await sleep(1500);
  }
  console.log(`${stamp()} dialogs keep coming back: ${JSON.stringify((await agent('pending_dialogs')).result)}`);
}

await dismissDialogs();
const state = await agent('game_state');
if (!state.ok || !state.result.in_world) {
  console.error(`${stamp()} not in a world (${state.error ?? 'main menu or loading'}); load a save first (cycle.ts --save)`);
  process.exit(1);
}
if (state.result.sub_state !== 'LiveModeState') {
  // Edit Town and other modes pause the clock; aging only works in live mode.
  console.log(`${stamp()} in ${state.result.sub_state}; switching to live mode`);
  await agent('enter_live_mode');
  await sleep(15000);
  await dismissDialogs();
}
async function saveAs(name: string): Promise<void> {
  mkdirSync(join(SAVING, '..'), { recursive: true });
  writeFileSync(SAVING, JSON.stringify({ name, started: Date.now() }));
  try { await saveAsInner(name); } finally { rmSync(SAVING, { force: true }); }
}

async function saveAsInner(name: string): Promise<void> {
  await agent('autopilot', { enabled: false }); // it would unpause the save
  await agent('set_speed', { speed: 'pause' });
  await dismissDialogs();
  const r = await agent('save_game', { name });
  if (!r.ok) { console.log(`${stamp()} save ${name} not started: ${r.error}`); return; }
  // fail fast (Rodney 2026-10-16): every 30 s the save must be in progress or the game busy with it (generic guard: process,
  // probe, heartbeat/commit/log moving); the agent answering with no save in progress and no line twice = it never ran.
  // A guard failure prints no "saved" line, so queue.ts records no checkpoint (and no final save: FAIL).
  const g = stallGuard(`save ${name}`, { port: Number(values.port) });
  let idle = 0, idleAt = Date.now() + GUARD_MS;
  for (let i = 0; i < 120; i++) { // the save itself runs as a game task
    const log = await agent('async_log').catch(e => ({ ok: false, error: String(e) }) as Reply);
    if (log.ok && log.result.some((l: string) => l.includes(`save ${name} -> `))) break;
    if (Date.now() >= idleAt) {
      idleAt = Date.now() + GUARD_MS;
      const st = await agent('game_state').catch(() => null);
      idle = st?.ok && st.result?.save_in_progress === false ? idle + 1 : 0;
    }
    const why = (await g()) || (idle >= 2 ? `not in progress and no async_log line for ~${2 * GUARD_MS / 1000} s (fail-fast guard)` : '');
    if (why) { console.log(`${stamp()} save ${name} FAILED: ${why}`); return; }
    await sleep(2000);
  }
  console.log(`${stamp()} saved ${name}`);
}

// Every 10 s: decline Sims who approach or call the household, and clear the notification corner.
let quietTick = 0;
const quiet = setInterval(async () => {
  try {
    const d = await agent('decline_requests');
    if (d.ok && d.result.declined.length) console.log(`${stamp()} declined: ${d.result.declined.join('; ')}`);
    await agent('quiet_ui', { memories_off: quietTick++ % 30 === 0 }); // re-assert the option every ~5 min
  } catch { /* game busy; next tick */ }
}, 10000);

const start = await agent('time');
if (!start.ok) throw new Error(`agent not answering: ${start.error}`);
const targetDay = values['until-day'] ? Number(values['until-day']) : start.result.day_number + Number(values.days);
console.log(`${stamp()} start day ${start.result.day_number} ${start.result.clock}, target day ${targetDay}`);
// in-game autopilot: holds speed 3 and clears popups within a frame (the loop below only samples every 30 s)
await agent('autopilot', { enabled: true, speed: 'ultra', dialogs: true });
const t0 = Date.now();
let last = start.result;
let stalls = 0;
let noReply = stallGuard('age loop: agent not answering', { port: Number(values.port) });
let lastCheckpoint = -1;
for (;;) {
  await dismissDialogs();
  if (values['keep-alive'] !== 'off') {
    const k = await agent('keep_alive', { threshold: Number(values['keep-alive']) });
    if (k.ok && k.result.topped_up.length) console.log(`${stamp()} keep_alive: ${k.result.topped_up.join(', ')}`);
    if (k.ok && k.result.repaired?.length) console.log(`${stamp()} repaired: ${k.result.repaired.join(', ')}`);
  }
  const ap = await agent('autopilot');
  if (ap.ok && ap.result.speed_fixes) console.log(`${stamp()} autopilot: speed restored ${ap.result.speed_fixes}x`);
  if (ap.ok) for (const c of ap.result.cleared) console.log(`${stamp()} autopilot dialog: ${String(c).slice(0, 80)}`);
  const s = await agent('set_speed', { speed: 'ultra' });
  const t = s.ok ? s.result : (await agent('time')).result;
  if (!t) {
    // fail fast: no reply is fine while the game is busy (a long frame, a save); the guard fails it once the game is gone,
    // the probe is down, or nothing moves (heartbeat, commit, log) for ~1 min
    const why = await noReply();
    if (why) { console.log(`${stamp()} FAIL: ${why}`); process.exit(1); }
    console.log(`${stamp()} no reply (${s.error}); retrying`); await sleep(5000); continue;
  }
  noReply = stallGuard('age loop: agent not answering', { port: Number(values.port) }); // answered: start over
  stalls = t.minutes_total === last.minutes_total ? stalls + 1 : 0;
  // fail fast: checks are 30 s apart and the autopilot holds speed 3, so two in a row without a clock move (~1 min) = stuck
  if (stalls === 1) { // one recovery try, as watchdog.ts: Edit Town or another mode pauses the clock
    const g = await agent('game_state').catch(() => null);
    if (g?.ok && g.result.in_world && g.result.sub_state !== 'LiveModeState') { console.log(`${stamp()} clock stalled in ${g.result.sub_state}; entering live mode`); await agent('enter_live_mode').catch(() => null); }
  }
  if (stalls >= 2) { console.log(`${stamp()} FAIL: sim clock has not moved for ~${stalls * 30} s (speed ${t.speed}, locked ${t.speed_locked}; fail-fast guard)`); process.exit(1); }
  last = t;
  if (t.day_number >= targetDay) break;
  const every = Number(values.checkpoint);
  if (every > 0 && t.day_number > start.result.day_number && (t.day_number - start.result.day_number) % every === 0 && t.day_number !== lastCheckpoint) {
    lastCheckpoint = t.day_number;
    await saveAs(`${values.save}-day${t.day_number}`);
    await agent('autopilot', { enabled: true });
  }
  const simDays = (t.minutes_total - start.result.minutes_total) / 1440;
  const realMin = (Date.now() - t0) / 60000;
  console.log(`${stamp()} day ${t.day_number} ${t.clock} ${t.speed}; ${simDays.toFixed(2)} sim days in ${realMin.toFixed(1)} min`);
  await sleep(30000);
}
clearInterval(quiet);
console.log(`${stamp()} reached day ${last.day_number}`);
await saveAs(values.save);
