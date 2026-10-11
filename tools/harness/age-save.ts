// Ages the loaded game: runs at speed 3 for N sim days (dismissing dialogs), then saves under a new name.
// Needs the probe server (tools/probe/ts3probe.py serve) attached and the ts3agent mod in the game.
// Usage: node tools/harness/age-save.ts --days 14 --save Baseline-SV [--port 47891]
import { parseArgs } from 'node:util';

const { values } = parseArgs({ options: {
  days: { type: 'string', default: '14' }, save: { type: 'string' }, port: { type: 'string', default: '47891' },
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

async function dismissDialogs(): Promise<void> {
  for (let i = 0; i < 5; i++) {
    const d = await agent('pending_dialogs');
    if (!d.ok || !d.result?.open) return;
    const button = d.result.dialog?.buttons?.[0];
    console.log(`${stamp()} dialog "${d.result.dialog?.text?.[0]?.slice(0, 60)}" -> ${button}`);
    await agent('respond_dialog', { button });
    await sleep(1000);
  }
}

const start = await agent('time');
if (!start.ok) throw new Error(`agent not answering: ${start.error}`);
const targetDay = start.result.day_number + Number(values.days);
console.log(`${stamp()} start day ${start.result.day_number} ${start.result.clock}, target day ${targetDay}`);
const t0 = Date.now();
let last = start.result;
let stalls = 0;
for (;;) {
  await dismissDialogs();
  const s = await agent('set_speed', { speed: 'ultra' });
  const t = s.ok ? s.result : (await agent('time')).result;
  if (!t) { console.log(`${stamp()} no reply (${s.error}); retrying`); await sleep(5000); continue; }
  stalls = t.minutes_total === last.minutes_total ? stalls + 1 : 0;
  if (stalls === 6) console.log(`${stamp()} sim clock has not moved for a minute (speed ${t.speed}, locked ${t.speed_locked})`);
  last = t;
  if (t.day_number >= targetDay) break;
  const simDays = (t.minutes_total - start.result.minutes_total) / 1440;
  const realMin = (Date.now() - t0) / 60000;
  console.log(`${stamp()} day ${t.day_number} ${t.clock} ${t.speed}; ${simDays.toFixed(2)} sim days in ${realMin.toFixed(1)} min`);
  await sleep(30000);
}
await agent('set_speed', { speed: 'pause' });
await dismissDialogs();
const saved = await agent('save_game', { name: values.save });
console.log(`${stamp()} reached day ${last.day_number}; save: ${saved.ok ? 'started' : saved.error}`);
