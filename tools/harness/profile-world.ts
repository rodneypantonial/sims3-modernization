// One profiling run on a loaded world, the same for every world so results compare:
//   node tools/harness/profile-world.ts --tag sv [--minutes 10] [--ab gohome,visit,... --slice 2] [--taskstall 250]
// --taskstall MS: also run the probe's /taskstall watch (docs/04 F17; S3PP SimulatorStageProfiler must be off); stalls go to
// probe-out/<probe session>-taskstall.jsonl.
// Needs the probe server (ts3probe.py serve) and a world in live mode (cycle.ts --save X). For the whole run:
// autopilot at x3 with popups cleared, keep_alive + decline_requests every 30 s, and the native stutter profiler
// (separate Frida session). Samplers in turn: managed C# stack (minutes 1-4), native sim-thread stack (5-8).
// --ab: A/B in one load. The named fixes (patches switches; `routeworker` / `routebudget` / `routeasync` = ts3native [RouteWorker] / [RouteBudget] / [RouteAsync] via /native) switch every --slice minutes: a warm-up slice
// (on, not counted), then off,on,on,off repeating (ABBA); use minutes = slice x (1 + multiple of 4). Slice boundaries go to <tag>-ab.json and profile-compare.mjs splits the per-second rows into <tag>:on and <tag>:off.
// The samplers are skipped in this mode (they would mix both sides); hitches, iterations, routes, GC and stuck Sims remain.
// --seed N: seed_rng {seed: N} while paused, just before the unpause that starts the window (docs/27 §B); game RNG put back at the end.
// --repro ID: run the steps of that tools/harness/repros.json entry (agent commands, waitS after each) from the window start,
// e.g. the lot streaming pan (lotstream-pan, docs/28 §5.5). The stack samplers are skipped (they suspend the sim thread);
// with --mainstack too, only the main-thread stacks run, for the whole window (they never suspend the sim thread).
// --mainstack main|all|TID: in the native sampler window also sample the main thread (or every non-sim game thread, or one id)
// through /threadstack right after each sim sample, into <tag>-mainstack.jsonl (docs/20 §7); never suspends the sim thread.
// --inroom: also run tools/frida/inroom-reads.js (docs/111: lot tree root-border dumps, SetBounds / QuadTreeBuild reads, in-room
// context flags) from the window start for the whole run, into <tag>-inroom.json (inroom-report.ts) and <tag>-inroom.log; the
// stack samplers are skipped (they suspend the sim thread, where its tree dumps run).
// Output: probe-out/profile/<tag>-{600-seconds.csv,600-hitches.jsonl,600.log,upkeep.log,managed.*,simstack.jsonl,mainstack.jsonl,stuck.json,houseboats.json,
// rng.json (seed reply, sim minutes at window start and end), activity.jsonl (every Sim's interaction per 30 s check; rng-compare.ts)}.
import { spawn } from 'node:child_process';
import { appendFileSync, createWriteStream, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { parseArgs } from 'node:util';
import { reproSave } from './repro-save.ts';
import { focusGame, giveFocusBack } from './focus-game.ts';
import { mayBeStuck } from './stuck-rule.ts';
import { watchGame } from './game-alive.ts';
import { startMachineLoad, summarize } from './machine-load.ts';

const { values } = parseArgs({ options: { tag: { type: 'string' }, minutes: { type: 'string', default: '10' }, ab: { type: 'string' }, slice: { type: 'string', default: '2' }, taskstall: { type: 'string' }, seed: { type: 'string' }, repro: { type: 'string' }, mainstack: { type: 'string' }, inroom: { type: 'boolean' }, 'no-env': { type: 'boolean' } } });
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
// runs are measured unfocused (Rodney, 2026-10-17): the game takes focus at launch; give it back to the window that was in
// front when the queue started, then record the state (a focused run is not comparable with unfocused ones)
console.log(`${stamp()} focus: ${giveFocusBack()}`);
console.log(`${stamp()} game window ${focusGame() ? 'FOCUSED (not comparable with unfocused runs)' : 'not focused (standard)'}`);
// set up while paused (Rodney, 2026-10-17): test_env, seed, profiler attach happen with the world still; the measured
// window starts when autopilot unpauses to x3 right after the profiler spawn (seeded draws only start there)
await ag('autopilot', { enabled: true, speed: 'none', dialogs: true });
const paused = await ag('set_speed', { speed: 'pause' });
if (!paused.ok) throw new Error(`pause before setup failed: ${paused.error}`);
// world conditions that skew measurements (agent test_env, docs/TESTING.md): the save's season, weather and moon play out;
// winter / extreme cold (Sims freeze solid) becomes spring and a near full moon becomes first quarter. Before seeding so
// a correction does not shift the seeded draws; the clock guard below repeats it every 30 s. --no-env turns it off.
const env = values['no-env'] ? null : await ag('test_env', {});
if (env && !env.ok) throw new Error(`test_env failed: ${env.error}`); // an agent without test_env would silently test another season
if (env) console.log(`${stamp()} test_env: ${JSON.stringify(env.result)}`);
const rng: any = { seed: values.seed === undefined ? null : Number(values.seed), reply: null, simMinutes: {} };
if (rng.seed !== null) {
  const r = await ag('seed_rng', { seed: rng.seed });
  if (!r.ok) throw new Error(`seed_rng failed: ${r.error}`); // agent without seed_rng: a seeded run would be unseeded
  rng.reply = r.result;
  console.log(`${stamp()} seed_rng: ${JSON.stringify(r.result)}`);
}
rng.simMinutes.start = (await ag('time')).result?.minutes_total ?? null;
// fail fast (docs/TESTING.md): game time must move unless the game says it is paused; 2026-10-17 bp-p0-sync froze after its
// save step twice and still "passed" with 39-47 stuck Sims. 2 checks 30 s apart without movement = FAIL.
const clockGuard = (async () => {
  let last = rng.simMinutes.start, misses = 0;
  for (const end = Date.now() + secs * 1000; Date.now() < end;) {
    await sleep(30000);
    if (env) {  // keep skew sources out during the run (idempotent; only acts on winter, extreme cold or a full moon)
      const g = (await ag('test_env', { min_days_to_full: 1 }).catch(() => null))?.result;
      if (g?.changed?.length) console.log(`${stamp()} test_env guard: ${g.changed.join(', ')} (season ${g.season}, moon ${g.moon_phase})`);
    }
    const t = (await ag('time').catch(() => null))?.result;
    if (!t || t.speed === 'Paused' || t.minutes_total !== last) { misses = 0; last = t?.minutes_total ?? last; continue; }
    if (++misses < 2) continue;
    console.log(`${stamp()} FAIL: sim clock stuck at day ${t.day_number} ${t.clock} for ~60 s (speed ${t.speed}; fail-fast guard)`);
    // pause bits, game dt, speed and window state at the trip (docs/85 §6: minimize pause = bit 2, device pause = bit 4)
    const cs = await ag('clock_state');
    console.log(`${stamp()} clock_state at the trip: ${JSON.stringify(cs.ok ? cs.result : cs.error)}`);
    prof.kill();
    process.exit(1);
  }
})();
const boats0 = (await ag('houseboats')).result ?? []; // empty outside Isla Paradiso
if (values.taskstall) console.log(`${stamp()} taskstall: ${JSON.stringify(await get(`taskstall?start=1&ms=${Number(values.taskstall)}`))}`);

const prof = spawn('python', ['-m', 'frida_tools.repl', '-n', 'TS3W.exe', '-l', join(ROOT, 'tools', 'frida', 'stutter-profile.js'),
  '-P', JSON.stringify({ out: `${pre}-${secs}`.replace(/\\/g, '/'), hitchMs: 50 }), '-q', '-t', String(secs)], { stdio: ['ignore', 'pipe', 'pipe'] });
const plog = createWriteStream(`${pre}-${secs}.log`);
prof.stdout.pipe(plog); prof.stderr.pipe(plog);
const profDone = new Promise(r => prof.on('exit', r));
// fail fast: the profiler stops itself after -t; one still running 90 s past that is hung, and the run would wait on it forever
setTimeout(() => { if (prof.exitCode === null && prof.signalCode === null) { console.log(`${stamp()} FAIL: stutter profiler still running 90 s past -t ${secs}; killed (fail-fast guard)`); prof.kill(); } }, (secs + 90) * 1000).unref();

await ag('autopilot', { enabled: true, speed: 'ultra', dialogs: true }); // window start: unpause to x3
const t0 = Date.now(), at = (s: number) => t0 + s * 1000;
// --inroom: its own Frida session like the stutter profiler; the script writes <tag>-inroom.json itself and prints a line every 30 s
const inroomDone = values.inroom ? new Promise(r => {
  const p = spawn('python', ['-m', 'frida_tools.repl', '-n', 'TS3W.exe', '-l', join(ROOT, 'tools', 'frida', 'inroom-reads.js'),
    '-P', JSON.stringify({ out: pre.replace(/\\/g, '/'), minutes: mins }), '-q', '-t', String(secs)], { stdio: ['ignore', 'pipe', 'pipe'] });
  const log = createWriteStream(`${pre}-inroom.log`);
  p.stdout.on('data', (d: Buffer) => { log.write(d); for (const l of String(d).split(/\r?\n/)) if (l.includes('[inroom]')) console.log(`${stamp()} ${l.trim()}`); });
  p.stderr.pipe(log);
  p.on('exit', r);
  setTimeout(() => { if (p.exitCode === null && p.signalCode === null) { console.log(`${stamp()} FAIL: inroom-reads still running 90 s past -t ${secs}; killed (fail-fast guard)`); p.kill(); } }, (secs + 90) * 1000).unref();
}) : Promise.resolve();
const abNames = values.ab ? values.ab.split(',') : [];
const slices: { from: number; on: boolean }[] = [];
const toggler = (async () => {
  if (!abNames.length) return;
  for (let i = 0; i * Number(values.slice) * 60 < secs; i++) {
    // slice 0 is warm-up (on, not counted); then off,on,on,off (ABBA) so a drift in world activity hits both sides alike
    const on = i === 0 || [false, true, true, false][(i - 1) % 4];
    await sleep(Math.max(0, at(i * Number(values.slice) * 60) - Date.now()));
    // ts3native counters at every switch (end of the previous slice): wait-ab.ts splits the sim-thread waits per side
    await get('native').then(c => appendFileSync(`${pre}-ab-native.jsonl`, JSON.stringify({ i, s: (Date.now() - t0) / 1000, c }) + '\n')).catch(() => {});
    const nativeNames = ['routeworker', 'routebudget', 'routeasync', 'alloc', 'waterjps']; // ts3native's runtime switches, not C# patches
    // ts3native switches that live only as a Ts3nCounters field (no /native parameter): name -> byte offset
    const counterSwitches: Record<string, number> = { heightcache: 0x1B4, simskipshared: 0xBF4, allocdedupe: 0xC1C, lotsincos: 0xC28, idcache: 0xC3C, terraingrave: 0xD00, discourage: 0x1074, discdestroy: 0x135C, discsweep: 0x1360, steerro: 0x10A4, steershadow: 0x10A8, fprebuildlock: 0x1110, lotfdro: 0x1134, lotfdshadow: 0x1138, locowin: 0x13E8, asyncreplan: 0x14A8, obstructedro: 0x1528, obstructedshadow: 0x152C, slotenabledro: 0x1568, slotenabledshadow: 0x156C };
    const patchNames = abNames.filter(n => !nativeNames.includes(n) && !(n in counterSwitches));
    if (patchNames.length) await ag('patches', Object.fromEntries(patchNames.map(n => [n, on])));
    for (const n of abNames.filter(n => nativeNames.includes(n))) await get(`native?${n}=${on ? 1 : 0}`);
    for (const n of abNames.filter(n => n in counterSwitches))
      await get(`nativeexport?name=ts3native_counters&offset=${counterSwitches[n]}&value=${on ? 1 : 0}`);
    slices.push({ from: (Date.now() - t0) / 1000, on, ...(i === 0 && { warmup: true }) });
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
    if (!e.reported && mayBeStuck(sim.interaction, !!sim.routing) && e.checks >= (sim.routing ? 3 : 6)) {
      e.reported = true;
      stuck.push({ ...sim, checks: e.checks, shortcut: moved[sim.name] ?? false });
    }
  }
}
writeFileSync(`${pre}-upkeep.log`, '');
writeFileSync(`${pre}-machine.jsonl`, '');
const stopLoad = startMachineLoad(`${pre}-machine.jsonl`); // other processes and CPU clock (machine-load.ts)
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
const window = abNames.length || values.taskstall || values.repro || values.inroom ? 0 : Math.min(180, Math.floor(secs / 2) - 60); // 3 min each in a 10 min run; none in --ab mode
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
await Promise.all([upkeep, profDone, toggler, reproRun, mainOnlyRun, inroomDone]);
stopLoad();
console.log(`${stamp()} ${summarize(`${pre}-machine.jsonl`).line}`);
rng.simMinutes.end = (await ag('time')).result?.minutes_total ?? null;
if (rng.seed !== null) rng.restored = (await ag('seed_rng')).result?.restored ?? false;
writeFileSync(`${pre}-rng.json`, JSON.stringify(rng, null, 1));
if (values.taskstall) { try { const r = await get('taskstall?stop=1&n=0'); console.log(`${stamp()} taskstall: ${JSON.stringify(r.counts ?? r)}`); } catch (e) { console.log(`${stamp()} taskstall stop failed: ${e.message}`); } }
if (abNames.length) await get('native').then(c => appendFileSync(`${pre}-ab-native.jsonl`, JSON.stringify({ i: slices.length, s: (Date.now() - t0) / 1000, c }) + '\n')).catch(() => {});
if (abNames.length) { writeFileSync(`${pre}-ab.json`,JSON.stringify({ names: abNames, slices }, null, 1)); await ag('patches', Object.fromEntries(abNames.map(n => [n, true]))); }
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
