// Round trips home -> destination -> home with timings and memory per leg (docs/19 section 5, PLAN item 10).
//   node tools/harness/travel-trip.ts [--to france|china|egypt|university|future] [--days 2] [--hours 6] [--trips 1]
//                                     [--sims "A B,C D"] [--save-stats <prefix>]
// Needs the probe server and a home world in live mode (cycle.ts --save X). Per trip: memory + travel_state, travel,
// wait for the destination in live mode, run --hours sim hours there (autopilot x3), travel_home, wait for the home
// world in live mode, memory + travel_state again. --save-stats P saves P-trip0 before and P-tripN after each return
// (new save files, the loaded one is left alone) and runs save-stats.ts over them at the end.
// Output: console + probe-out/travel/<to>-<time>.jsonl (one line per leg).
import { execFileSync } from 'node:child_process';
import { appendFileSync, mkdirSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { parseArgs } from 'node:util';

const { values } = parseArgs({ options: {
  to: { type: 'string', default: 'france' }, days: { type: 'string', default: '2' }, hours: { type: 'string', default: '6' },
  trips: { type: 'string', default: '1' }, sims: { type: 'string' }, 'save-stats': { type: 'string' },
  port: { type: 'string', default: '47891' },
} });
const ROOT = resolve(import.meta.dirname, '..', '..');
const OUT = join(ROOT, 'probe-out', 'travel');
mkdirSync(OUT, { recursive: true });
const outFile = join(OUT, `${values.to}-${new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19)}.jsonl`);
const base = `http://127.0.0.1:${values.port}`;
const sleep = (ms: number) => new Promise(r => setTimeout(r, ms));
const stamp = () => new Date().toISOString().slice(11, 19);
const log = (m: string) => console.log(`${stamp()} ${m}`);

async function get(path: string, timeoutMs = 30000): Promise<any> {
  return (await fetch(`${base}/${path}`, { signal: AbortSignal.timeout(timeoutMs) })).json();
}
async function agent(cmd: string, args?: object): Promise<any> {
  const q = new URLSearchParams({ cmd, timeout: '8000' });
  if (args) q.set('args', JSON.stringify(args));
  try { return await get(`agent?${q}`, 20000); } catch (e) { return { ok: false, error: String(e) }; }
}
async function until(what: string, limitS: number, check: () => Promise<boolean>, everyMs = 1000): Promise<number> {
  const t0 = Date.now();
  while (!(await check().catch(() => false))) {
    if (Date.now() - t0 > limitS * 1000) throw new Error(`${what}: not reached within ${limitS} s`);
    await sleep(everyMs);
  }
  const s = (Date.now() - t0) / 1000;
  log(`${what} after ${s.toFixed(1)} s`);
  return s;
}
async function dialog(): Promise<void> {
  const d = await agent('auto_dialog');
  if (d.ok && d.result.open) log(`dialog "${String(d.result.title ?? '').slice(0, 60)}" -> ${d.result.clicked}`);
}
async function snapshot() {
  const mem = await get('memory', 60000).catch(() => null);
  const ts = (await agent('travel_state')).result ?? null;
  return { mem: mem && { commitMB: mem.commitMB, freeMB: mem.freeMB, largestFreeMB: mem.largestFreeMB }, travel: ts };
}
async function saveAs(name: string): Promise<void> {
  await agent('autopilot', { enabled: false }); // it would unpause the save
  await agent('set_speed', { speed: 'pause' });
  await dialog();
  const r = await agent('save_game', { name });
  if (!r.ok) { log(`save ${name} not started: ${r.error}`); return; }
  await until(`saved ${name}`, 300, async () => (await agent('async_log')).result?.some((l: string) => l.includes(`save ${name} -> `)), 2000);
  await agent('autopilot', { enabled: true, speed: 'ultra', dialogs: true });
}

// One world switch: send the command, wait until the old world is gone (board), then until the new one is in live mode (load).
async function leg(name: string, cmd: string, args: object, fromWorld: string) {
  const before = await snapshot();
  const t0 = Date.now();
  const r = await agent(cmd, args);
  if (!r.ok) throw new Error(`${cmd}: ${r.error}`);
  log(`${name}: ${cmd} ${JSON.stringify(r.result)}`);
  let n = 0;
  // boarding: the van/portal routes and confirm dialogs happen here; the world flag drops when the old world unloads
  const boardS = await until(`${name}: left ${fromWorld}`, 600, async () => {
    if (++n % 3 === 0) await dialog();
    if ((await get('agentstatus', 5000)).worldLoaded === false) return true;
    const ts = await agent('travel_state');
    return ts.ok && (ts.result.in_travel === true || ts.result.world !== fromWorld);
  });
  let world = '';
  const loadS = await until(`${name}: arrived in live mode`, 900, async () => {
    if ((await get('agentstatus', 5000)).worldLoaded !== true) return false;
    if (++n % 10 === 0) await dialog();
    const ts = await agent('travel_state');
    if (!ts.ok || ts.result.in_travel || ts.result.world === fromWorld) return false;
    world = ts.result.world;
    return (await agent('game_state')).result?.sub_state === 'LiveModeState';
  }, 500);
  const after = await snapshot();
  const row = { leg: name, cmd, from: fromWorld, to: world, boardS, loadS, totalS: (Date.now() - t0) / 1000, before, after };
  appendFileSync(outFile, JSON.stringify(row) + '\n');
  const m = (s: any) => s.mem ? `${s.mem.commitMB} MB committed, largest free ${s.mem.largestFreeMB} MB` : 'memory n/a';
  log(`${name}: board ${boardS.toFixed(0)} s + load ${loadS.toFixed(0)} s; ${m(before)} -> ${m(after)}; minisims ${before.travel?.minisims} -> ${after.travel?.minisims}`);
  return world;
}

async function liveHours(hours: number): Promise<void> {
  await agent('autopilot', { enabled: true, speed: 'ultra', dialogs: true });
  const start = (await agent('time')).result;
  if (!start) throw new Error('time: no answer');
  for (;;) {
    await dialog();
    await agent('decline_requests');
    await agent('keep_alive', { threshold: -50 });
    const t = (await agent('set_speed', { speed: 'ultra' })).result ?? (await agent('time')).result;
    if (t && t.minutes_total - start.minutes_total >= hours * 60) break;
    await sleep(15000);
  }
  log(`${hours} sim hours done`);
}

const st = await agent('game_state');
if (st.result?.sub_state !== 'LiveModeState') throw new Error(`not in live mode: ${JSON.stringify(st)}`);
const home = (await agent('travel_state')).result;
if (!home || home.away) throw new Error(`start from the home world: ${JSON.stringify(home)}`);
log(`home ${home.world} (${home.world_file}), household ${home.household}, minisims ${home.minisims}; log ${outFile}`);
await agent('autopilot', { enabled: true, speed: 'ultra', dialogs: true });
await agent('quiet_ui', { memories_off: true });

const to = values.to!.toLowerCase();
const goCmd = to === 'university' ? 'travel_university' : to === 'future' ? 'travel_future' : 'travel_vacation';
const goArgs: any = to === 'university' ? { days: Number(values.days) } : to === 'future' ? {} : { world: to, days: Number(values.days) };
if (values.sims && goCmd === 'travel_vacation') goArgs.sims = values.sims.split(',').map(s => s.trim());
const prefix = values['save-stats'];
const saves: string[] = [];
if (prefix) { await saveAs(`${prefix}-trip0`); saves.push(`${prefix}-trip0`); }

for (let i = 1; i <= Number(values.trips); i++) {
  const away = await leg(`trip ${i} out`, goCmd, goArgs, home.world);
  await liveHours(Number(values.hours));
  await leg(`trip ${i} back`, 'travel_home', {}, away);
  if (prefix) { await saveAs(`${prefix}-trip${i}`); saves.push(`${prefix}-trip${i}`); }
}
if (saves.length) log(execFileSync(process.execPath, [join(ROOT, 'tools', 'harness', 'save-stats.ts'), ...saves]).toString());
log(`done; ${outFile}`);
