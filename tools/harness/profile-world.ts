// One profiling run on a loaded world, the same for every world so results compare:
//   node tools/harness/profile-world.ts --tag sv [--minutes 10] [--ab gohome,visit,... --slice 2] [--taskstall 250]
// --taskstall MS: also run the probe's /taskstall watch (docs/04 F17; S3PP SimulatorStageProfiler must be off); stalls go to
// probe-out/<probe session>-taskstall.jsonl.
// Needs the probe server (ts3probe.py serve) and a world in live mode (cycle.ts --save X). For the whole run:
// autopilot at x3 with popups cleared, keep_alive + decline_requests every 30 s, and the native stutter profiler
// (separate Frida session). Samplers in turn: managed C# stack (minutes 1-4), native sim-thread stack (5-8).
// --ab: A/B in one load. The named fixes (patches switches; `routeworker` / `routebudget` / `routeasync` = ts3native [RouteWorker] / [RouteBudget] / [RouteAsync] via /native) alternate on/off every --slice minutes, starting on;
// slice boundaries go to <tag>-ab.json and profile-compare.mjs splits the per-second rows into <tag>:on and <tag>:off.
// The samplers are skipped in this mode (they would mix both sides); hitches, iterations, routes, GC and stuck Sims remain.
// --seed N: seed_rng {seed: N} right after autopilot sets the speed (window start, docs/27 §B), game RNG put back at the end.
// --repro ID: run the steps of that tools/harness/repros.json entry (agent commands, waitS after each) from the window start,
// e.g. the lot streaming pan (lotstream-pan, docs/28 §5.5). The stack samplers are skipped (they suspend the sim thread);
// with --mainstack too, only the main-thread stacks run, for the whole window (they never suspend the sim thread).
// --mainstack main|all|TID: in the native sampler window also sample the main thread (or every non-sim game thread, or one id)
// through /threadstack right after each sim sample, into <tag>-mainstack.jsonl (docs/20 §7); never suspends the sim thread.
// Output: probe-out/profile/<tag>-{600-seconds.csv,600-hitches.jsonl,600.log,upkeep.log,managed.*,simstack.jsonl,mainstack.jsonl,stuck.json,houseboats.json,
// rng.json (seed reply, sim minutes at window start and end), activity.jsonl (every Sim's interaction per 30 s check; rng-compare.ts)}.
import { spawn } from 'node:child_process';
import { appendFileSync, createWriteStream, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { parseArgs } from 'node:util';
import { reproSave } from './repro-save.ts';
import { focusGame } from './focus-game.ts';
import { endlessJob } from './stuck-rule.ts';
import { watchGame } from './game-alive.ts';

const { values } = parseArgs({ options: { tag: { type: 'string' }, minutes: { type: 'string', default: '10' }, ab: { type: 'string' }, slice: { type: 'string', default: '2' }, taskstall: { type: 'string' }, seed: { type: 'string' }, repro: { type: 'string' }, mainstack: { type: 'string' } } });
if (!values.tag) throw new Error('--tag required');
if (values.mainstack !== undefined && !/^(main|all|\d+)$/.test(values.mainstack)) throw new Error('--mainstack must be main, all or a thread id');
const ROOT = resolve(import.meta.dirname, '..', '..');
const OUT = join(ROOT, 'probe-out', 'profile');
mkdirSync(OUT, { recursive: true });
const reproSteps: any[] | undefined = values.repro ? (JSON.parse(readFileSync(join(ROOT, 'tools', 'harness', 'repros.json'), 'utf8')) as any[]).find(r => r.id === values.repro)?.steps : [];
if (!reproSteps) throw new Error(`--repro: no entry ${values.repro} in tools/harness/repros.json`);
const mins = Number(values.minutes), secs = mins * 60, pre = join(OUT, `${values.tag}`);
const sleep = (ms: number) => new Promise(r => setTimeout(r, ms));
const stamp = () => new Date().toISOString().slice(11, 19);
const get = async (p: string, to = 20000) => (await fetch(`http://127.0.0.1:47891/${p}`, { signal: AbortSignal.timeout(to) })).json();
const ag = async (cmd: string, args?: object): Promise<any> => {
  const q = new URLSearchParams({ cmd, timeout: '8000' });
  if (args) q.set('args', JSON.stringify(args));
  try { return await get(`agent?${q}`); } catch (e) { return { ok: false, error: String(e) }; }
};

const st = await ag('game_state');
if (st.result?.sub_state !== 'LiveModeState') throw new Error(`not in live mode: ${JSON.stringify(st)}`);
console.log(`${stamp()} ${values.tag}: ${JSON.stringify(st.result)}`);
const alive = watchGame();
console.log(`${stamp()} game window ${focusGame() ? 'focused' : 'NOT focused (fps capped, numbers not comparable)'}`);
await ag('autopilot', { enabled: true, speed: 'ultra', dialogs: true });
const rng: any = { seed: values.seed === undefined ? null : Number(values.seed), reply: null, simMinutes: {} };
if (rng.seed !== null) {
  const r = await ag('seed_rng', { seed: rng.seed });
  if (!r.ok) throw new Error(`seed_rng failed: ${r.error}`); // agent without seed_rng: a seeded run would be unseeded
  rng.reply = r.result;
  console.log(`${stamp()} seed_rng: ${JSON.stringify(r.result)}`);
}
rng.simMinutes.start = (await ag('time')).result?.minutes_total ?? null;
const boats0 = (await ag('houseboats')).result ?? []; // empty outside Isla Paradiso
if (values.taskstall) console.log(`${stamp()} taskstall: ${JSON.stringify(await get(`taskstall?start=1&ms=${Number(values.taskstall)}`))}`);

const prof = spawn('python', ['-m', 'frida_tools.repl', '-n', 'TS3W.exe', '-l', join(ROOT, 'tools', 'frida', 'stutter-profile.js'),
  '-P', JSON.stringify({ out: `${pre}-${secs}`.replace(/\\/g, '/'), hitchMs: 50 }), '-q', '-t', String(secs)], { stdio: ['ignore', 'pipe', 'pipe'] });
const plog = createWriteStream(`${pre}-${secs}.log`);
prof.stdout.pipe(plog); prof.stderr.pipe(plog);
const profDone = new Promise(r => prof.on('exit', r));

const t0 = Date.now(), at = (s: number) => t0 + s * 1000;
const abNames = values.ab ? values.ab.split(',') : [];
const slices: { from: number; on: boolean }[] = [];
const toggler = (async () => {
  if (!abNames.length) return;
  for (let i = 0, on = true; i * Number(values.slice) * 60 < secs; i++, on = !on) {
    await sleep(Math.max(0, at(i * Number(values.slice) * 60) - Date.now()));
    const nativeNames = ['routeworker', 'routebudget', 'routeasync', 'alloc', 'waterjps']; // ts3native's runtime switches, not C# patches
    const patchNames = abNames.filter(n => !nativeNames.includes(n) && n !== 'heightcache');
    if (patchNames.length) await ag('patches', Object.fromEntries(patchNames.map(n => [n, on])));
    for (const n of abNames.filter(n => nativeNames.includes(n))) await get(`native?${n}=${on ? 1 : 0}`);
    // ts3native switches that live only as a Ts3nCounters field (no /native parameter): name -> byte offset
    const counterSwitches: Record<string, number> = { heightcache: 0x1B4 };
    for (const n of abNames.filter(n => n in counterSwitches))
      await get(`nativeexport?name=ts3native_counters&offset=${counterSwitches[n]}&value=${on ? 1 : 0}`);
    slices.push({ from: (Date.now() - t0) / 1000, on });
    console.log(`${stamp()} ${abNames.join(',')} ${on ? 'on' : 'off'}`);
  }
})();
const reproRun = (async () => {
  for (const s of reproSteps) {
    const r = await ag(s.cmd, s.args);
    console.log(`${stamp()} repro ${values.repro} step ${s.cmd}: ${JSON.stringify(r).slice(0, 200)}`);
    if (s.waitS) await sleep(s.waitS * 1000);
  }
})();
// Stuck Sims, as watchdog.ts judges them: same spot and interaction for 6 checks (3 min), or routing without moving for 3.
// Flagged Sims recently teleported by a shortcut are marked, so every shortcut gets checked for leaving Sims stuck.
const simSeen = new Map<string, { key: string; checks: number; reported: boolean }>();
const stuck: any[] = [];
async function checkStuck() {
  const r = await ag('sims_activity');
  if (!r.ok) return;
  appendFileSync(`${pre}-activity.jsonl`, JSON.stringify({ t: Date.now() - t0, sims: r.result.map((s: any) => [s.name, s.interaction]) }) + '\n');
  const moved: Record<string, string> = (await ag('patches')).result?.moved ?? {}; // every Sim a shortcut moved
  for (const sim of r.result) {
    const key = `${sim.x},${sim.z},${sim.interaction},${sim.routing}`;
    const prev = simSeen.get(sim.id);
    const e = prev && prev.key === key ? { key, checks: prev.checks + 1, reported: prev.reported } : { key, checks: 1, reported: false };
    simSeen.set(sim.id, e);
    if (!e.reported && sim.interaction && !endlessJob(sim.interaction) && e.checks >= (sim.routing ? 3 : 6)) {
      e.reported = true;
      stuck.push({ ...sim, checks: e.checks, shortcut: moved[sim.name] ?? false });
    }
  }
}
writeFileSync(`${pre}-upkeep.log`, '');
writeFileSync(`${pre}-activity.jsonl`, '');
const upkeep = (async () => {
  while (Date.now() - t0 < secs * 1000) {
    const k = await ag('keep_alive', { threshold: -50 }); await ag('decline_requests');
    const ap = await ag('autopilot');
    await checkStuck();
    appendFileSync(`${pre}-upkeep.log`, `${stamp()} ${k.ok ? `topped ${k.result.topped_up.length} repaired ${k.result.repaired?.length ?? 0}` : k.error} ${ap.ok ? `speedfix ${ap.result.speed_fixes} dialogs ${ap.result.cleared.length}` : ''}\n`);
    await sleep(30000);
  }
})();

const run = (args: string[], file: string) => new Promise(r => {
  const p = spawn(process.execPath, args, { stdio: ['ignore', 'pipe', 'inherit'] });
  p.stdout.pipe(createWriteStream(file)); p.on('exit', r);
});
// none with --taskstall: two samplers suspending the sim thread deadlocked the probe agent (bp-detour, 2026-10-13)
const window = abNames.length || values.taskstall || values.repro ? 0 : Math.min(180, Math.floor(secs / 2) - 60); // 3 min each in a 10 min run; none in --ab mode
const mainSample = async (t: number) => {
  try {
    const r = await get(`threadstack?which=${values.mainstack}&frames=24`, 5000);
    appendFileSync(`${pre}-mainstack.jsonl`, r.threads.filter((x: any) => x.stack).map((x: any) =>
      JSON.stringify({ t, thread: x.thread, role: x.role, name: x.name, start: x.start, module: x.module, stack: x.stack }) + '\n').join(''));
  } catch { /* sample lost */ }
};
// --repro with --mainstack: main-thread stacks only (they never suspend the sim thread), the whole window, so the repro steps are covered
const mainOnly = !!(values.repro && values.mainstack && !abNames.length && !values.taskstall);
if (values.mainstack) writeFileSync(`${pre}-mainstack.jsonl`, '');
const mainOnlyRun = mainOnly ? (async () => {
  console.log(`${stamp()} mainstack only (repro mode): ${secs - 15} s`);
  for (const end = at(secs - 15); Date.now() < end;) { const t = Date.now(); await mainSample(t); await sleep(Math.max(0, 200 - (Date.now() - t))); }
})() : Promise.resolve();
await sleep(Math.max(0, at(60) - Date.now()));
console.log(`${stamp()} managed sampler ${window} s`);
if (window) await run([join(ROOT, 'tools', 'probe', 'managed-sample.mjs'), String(window), '5', `${pre}-managed.jsonl`], `${pre}-managed.txt`);
await sleep(Math.max(0, at(secs / 2) - Date.now()));
console.log(`${stamp()} native sampler ${window} s`);
writeFileSync(`${pre}-simstack.jsonl`, '');
if (values.mainstack && !window && !mainOnly) console.log(`${stamp()} mainstack skipped: no sampler window in this mode`);
let simLost = 0; // an empty simstack.jsonl went unnoticed for 3 days (2026-10-16-sampler-hang.md): print the loss
for (const end = Date.now() + window * 1000; Date.now() < end;) {
  const t = Date.now();
  try {
    const r = await get('simstack?frames=24', 5000);
    if (r.stack?.length) appendFileSync(`${pre}-simstack.jsonl`, JSON.stringify({ t, stack: r.stack }) + '\n'); // busy or not found: no line
    else simLost++;
  } catch { simLost++; /* sample lost */ }
  // one line per sampled thread: the simstack {t, stack} plus thread tags; 'all' takes ~0.1 s per round (fewer sim samples, never more)
  if (values.mainstack) await mainSample(t);
  await sleep(Math.max(0, 200 - (Date.now() - t)));
}
if (window) console.log(`${stamp()} native sampler done: ${simLost} sim samples lost`);
await Promise.all([upkeep, profDone, toggler, reproRun, mainOnlyRun]);
rng.simMinutes.end = (await ag('time')).result?.minutes_total ?? null;
if (rng.seed !== null) rng.restored = (await ag('seed_rng')).result?.restored ?? false;
writeFileSync(`${pre}-rng.json`, JSON.stringify(rng, null, 1));
if (values.taskstall) { try { const r = await get('taskstall?stop=1&n=0'); console.log(`${stamp()} taskstall: ${JSON.stringify(r.counts ?? r)}`); } catch (e) { console.log(`${stamp()} taskstall stop failed: ${e.message}`); } }
if (abNames.length) { writeFileSync(`${pre}-ab.json`, JSON.stringify({ names: abNames, slices }, null, 1)); await ag('patches', Object.fromEntries(abNames.map(n => [n, true]))); }
writeFileSync(`${pre}-stuck.json`, JSON.stringify(stuck, null, 1));
if (stuck.length) await reproSave(ag, values.tag!, stuck);
console.log(`${stamp()} stuck Sims: ${stuck.length}${stuck.length ? ` (${stuck.map(x => `${x.name}${x.shortcut ? ` [teleported: ${x.shortcut}]` : ''} on ${x.interaction}`).join('; ')})` : ''}`);
if (boats0.length) {
  const boats1 = (await ag('houseboats')).result ?? [];
  const moved = boats1.filter((b: any) => { const o = boats0.find((x: any) => x.lot === b.lot); return !o || Math.hypot(o.x - b.x, o.z - b.z) > 1; });
  writeFileSync(`${pre}-houseboats.json`, JSON.stringify({ start: boats0, end: boats1 }, null, 1));
  console.log(`${stamp()} houseboats: ${boats1.length}, moved: ${moved.map((b: any) => `${b.lot} (${b.household ?? 'empty'})`).join(', ') || 'none'}`);
}
await alive.check();
console.log(`${stamp()} done; game ${(await ag('game_state')).ok ? 'alive' : 'NOT answering'}`);
