// Ages the loaded game: runs at speed 3 for N sim days (dismissing dialogs), then saves under a new name.
// Needs the probe server (tools/probe/ts3probe.py serve) attached and the ts3agent mod in the game.
// Saves a checkpoint (<save>-dayN) every --checkpoint sim days, so a crash can be reproduced from the last one.
// Keeps the active household alive (keep_alive tops up motives below --keep-alive, default -50, and repairs broken home objects).
// Usage: node tools/harness/age-save.ts --days 14 --save Baseline-SV [--until-day 15] [--checkpoint 2] [--keep-alive -50|off]
import { rmSync, writeFileSync, mkdirSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { parseArgs } from 'node:util';

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
  await agent('set_speed', { speed: 'pause' });
  await dismissDialogs();
  const r = await agent('save_game', { name });
  if (!r.ok) { console.log(`${stamp()} save ${name} not started: ${r.error}`); return; }
  for (let i = 0; i < 120; i++) { // the save itself runs as a game task
    const log = await agent('async_log');
    if (log.ok && log.result.some((l: string) => l.includes(`save ${name} -> `))) break;
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
const t0 = Date.now();
let last = start.result;
let stalls = 0;
let lastCheckpoint = -1;
for (;;) {
  await dismissDialogs();
  if (values['keep-alive'] !== 'off') {
    const k = await agent('keep_alive', { threshold: Number(values['keep-alive']) });
    if (k.ok && k.result.topped_up.length) console.log(`${stamp()} keep_alive: ${k.result.topped_up.join(', ')}`);
    if (k.ok && k.result.repaired?.length) console.log(`${stamp()} repaired: ${k.result.repaired.join(', ')}`);
  }
  const s = await agent('set_speed', { speed: 'ultra' });
  const t = s.ok ? s.result : (await agent('time')).result;
  if (!t) { console.log(`${stamp()} no reply (${s.error}); retrying`); await sleep(5000); continue; }
  stalls = t.minutes_total === last.minutes_total ? stalls + 1 : 0;
  if (stalls === 6) console.log(`${stamp()} sim clock has not moved for a minute (speed ${t.speed}, locked ${t.speed_locked})`);
  last = t;
  if (t.day_number >= targetDay) break;
  const every = Number(values.checkpoint);
  if (every > 0 && t.day_number > start.result.day_number && (t.day_number - start.result.day_number) % every === 0 && t.day_number !== lastCheckpoint) {
    lastCheckpoint = t.day_number;
    await saveAs(`${values.save}-day${t.day_number}`);
  }
  const simDays = (t.minutes_total - start.result.minutes_total) / 1440;
  const realMin = (Date.now() - t0) / 60000;
  console.log(`${stamp()} day ${t.day_number} ${t.clock} ${t.speed}; ${simDays.toFixed(2)} sim days in ${realMin.toFixed(1)} min`);
  await sleep(30000);
}
clearInterval(quiet);
console.log(`${stamp()} reached day ${last.day_number}`);
await saveAs(values.save);
