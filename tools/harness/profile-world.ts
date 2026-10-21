// One profiling run on a loaded world, the same for every world so results compare:
//   node tools/harness/profile-world.ts --tag sv [--minutes 10] [--ab gohome,visit,... --slice 2] [--taskstall 250]
// --taskstall MS: also run the probe's /taskstall watch (docs/04 F17; S3PP SimulatorStageProfiler must be off); stalls go to
// probe-out/<probe session>-taskstall.jsonl.
// Needs the probe server (ts3probe.py serve) and a world in live mode (cycle.ts --save X). For the whole run:
// autopilot at x3 with popups cleared, keep_alive + decline_requests every 30 s, and the native stutter profiler
// (separate Frida session). Samplers in turn: managed C# stack (minutes 1-4), native sim-thread stack (5-8).
// --ab: A/B in one load. The named fixes (patches switches; `routeworker` / `routebudget` / `routeasync` = ts3native [RouteWorker] / [RouteBudget] / [RouteAsync] via /native) switch every --slice minutes: a warm-up slice
// (on, not counted), then off,on,on,off repeating (ABBA; --ab-on-first: on,off,off,on); use minutes = slice x (1 + multiple of 4). Slice boundaries go to <tag>-ab.json and profile-compare.mjs splits the per-second rows into <tag>:on and <tag>:off.
// The samplers are skipped in this mode (they would mix both sides); hitches, iterations, routes, GC and stuck Sims remain.
// --fixes-off a,b,c: switch those ts3fixes patches (--ab names, e.g. gohome,rabbithole,visit) OFF for the whole run while paused, log `fixes off: ... (verified)` after reading them back; previous state restored at the end and on a crash.
// --fixes-on a,b,c: the same, switched ON (e.g. autshadow: tuning kAutShadow is off, so pilot jobs turn it on here and every other job keeps it off).
// --until-compares N: P4 shadow runs: every 30 s count the compares in Bin/ts3native-shadow.jsonl (classify() of shadow-compare-report.ts: not M4, not M5) and end the window early at >= N; N reached before 5 min -> target 2N; --minutes is the cap.
// --seed N: seed_rng {seed: N} while paused, just before the unpause that starts the window (docs/27 §B); game RNG put back at the end.
// --repro ID: run the steps of that tools/harness/repros.json entry (agent commands, waitS after each) from the window start,
// e.g. the lot streaming pan (lotstream-pan, docs/28 §5.5). The stack samplers are skipped (they suspend the sim thread);
// with --mainstack too, only the main-thread stacks run, for the whole window (they never suspend the sim thread).
// --mainstack main|all|TID: in the native sampler window also sample the main thread (or every non-sim game thread, or one id)
// through /threadstack right after each sim sample, into <tag>-mainstack.jsonl (docs/20 §7); never suspends the sim thread.
// --inroom: also run tools/frida/inroom-reads.js (docs/111: lot tree root-border dumps, SetBounds / QuadTreeBuild reads, in-room
// context flags) from the window start for the whole run, into <tag>-inroom.json (inroom-report.ts) and <tag>-inroom.log; the
// stack samplers are skipped (they suspend the sim thread, where its tree dumps run).
// --watchstalk: run tools/frida/watch-stalk.js instead of the stutter profiler (docs/113: route +0x1F4 watchpoints, then Stalker
// on sampled sim-thread Route_PlanLocked calls) into <tag>-watchstalk.json (watch-stalk-report.ts) and <tag>-watchstalk.log; no
// <tag>-<secs>-* profiler files (it hooks Route_PlanLocked too, and Stalker would follow into its listeners); samplers skipped.
// --watchstalk-worker: with --watchstalk, the stalk phase follows the route worker instead of the sim thread (docs/113 §8).
// --livereads2: also run tools/frida/live-reads2.js (docs/114: O+0x58, footprint grid, grid writer threads, 0xC6BAB0 points,
// follow slot +0x38 callers, houseboat start frame) from the window start, into <tag>-livereads2.json (live-reads2-report.ts) and
// <tag>-livereads2.log; the stack samplers are skipped (its grid dumps and point calls run on the sim thread).
// --f4b: also run tools/frida/f4b-reads.js (docs/99 §12.6: CopyNodes node y / +0x20 / path length, path slot +0x100 prepends, agent
// y vs terrain) from the window start, into <tag>-f4b.json (f4b-report.ts) and <tag>-f4b.log; the stack samplers are skipped
// (its terrain call runs on the sim thread).
// --vres: callvirt inline-cache snapshots through the probe agent's /vcache every 60 s, union in <tag>-vres.json (docs/133,
// tools/virtual-resolution-report.ts); the stack samplers are skipped. --vres-dry N: the same with N methods per snapshot.
// --no-samplers: skip the stack samplers without any other probe (docs/133 §5: the int3-only bisect job matches bp-vres).
// --no-prof: no stutter profiler session (no <tag>-<secs>-* files). queue.ts sets it for [ProbeSnap] jobs: a second Frida
// agent injecting while the int3 sampler storms the sim thread with breakpoints crashed the game in that agent's exception
// path (docs/67 §7, bp-rare-winter-pond 2026-10-19).
// --travel WORLD: before anything else, travel there with save-matrix.ts --cases travel --to WORLD --stay (university, future,
// or a vacation world: france | china | egypt; the trip code and its fail-fast checks are save-matrix's; it saves the loaded
// save as a Matrix-* copy first) and run the window in the destination world (docs/117 §2.11). A failed trip fails the run.
// Output: probe-out/profile/<tag>-{600-seconds.csv,600-hitches.jsonl,600.log,upkeep.log,managed.*,simstack.jsonl,mainstack.jsonl,stuck.json,houseboats.json,
// rng.json (seed reply, sim minutes at window start and end), activity.jsonl (every Sim's interaction per 30 s check; rng-compare.ts)}.
import { spawn } from 'node:child_process';
import { appendFileSync, createWriteStream, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { classify } from './shadow-compare-report.ts';
import { parseArgs } from 'node:util';
import { reproSave } from './repro-save.ts';
import { focusGame, giveFocusBack } from './focus-game.ts';
import { mayBeStuck } from './stuck-rule.ts';
import { watchGame } from './game-alive.ts';
import { startMachineLoad, summarize } from './machine-load.ts';

const { values } = parseArgs({ options: { tag: { type: 'string' }, minutes: { type: 'string', default: '10' }, ab: { type: 'string' }, 'ab-on-first': { type: 'boolean' }, 'fixes-off': { type: 'string' }, 'fixes-on': { type: 'string' }, 'until-compares': { type: 'string' }, slice: { type: 'string', default: '2' }, taskstall: { type: 'string' }, seed: { type: 'string' }, repro: { type: 'string' }, mainstack: { type: 'string' }, inroom: { type: 'boolean' }, watchstalk: { type: 'boolean' }, 'watchstalk-worker': { type: 'boolean' }, livereads2: { type: 'boolean' }, f4b: { type: 'boolean' }, vres: { type: 'boolean' }, 'vres-dry': { type: 'string' }, 'no-samplers': { type: 'boolean' }, 'no-env': { type: 'boolean' }, 'no-prof': { type: 'boolean' }, travel: { type: 'string' } } });
if (!values.tag) throw new Error('--tag required');
if (values['vres-dry'] !== undefined && !/^[1-9]\d*$/.test(values['vres-dry'])) throw new Error('--vres-dry must be a positive integer');
if (values.mainstack !== undefined && !/^(main|all|\d+)$/.test(values.mainstack)) throw new Error('--mainstack must be main, all or a thread id');
const ROOT = resolve(import.meta.dirname, '..', '..');
const OUT = join(ROOT, 'probe-out', 'profile');
mkdirSync(OUT, { recursive: true });
const reproEntry: any = values.repro ? (JSON.parse(readFileSync(join(ROOT, 'tools', 'harness', 'repros.json'), 'utf8')) as any[]).find(r => r.id === values.repro) : { steps: [] };
const reproSteps: any[] | undefined = reproEntry?.steps;
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

if (values.travel) {
  if (!/^[a-z]+$/i.test(values.travel)) throw new Error('--travel must be a world name (university, future, france, china, egypt)');
  console.log(`${stamp()} travel to ${values.travel} (save-matrix.ts --stay)`);
  const code = await new Promise<number | null>(res => spawn(process.execPath, [join(ROOT, 'tools', 'harness', 'save-matrix.ts'), '--cases', 'travel', '--to', values.travel!, '--stay'], { stdio: 'inherit' }).on('exit', res));
  if (code !== 0) throw new Error(`travel to ${values.travel} failed: save-matrix.ts exit ${code}`);
}
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
// --fixes-off / --fixes-on: patches switches (same agent command as --ab) off / on for the whole run; read back, restored in restoreFixes
const fixesOff = values['fixes-off'] ? values['fixes-off'].split(',') : [];
const fixesOn = values['fixes-on'] ? values['fixes-on'].split(',') : [];
if ([...fixesOff, ...fixesOn].some(n => !/^[a-z0-9]+$/.test(n))) throw new Error('--fixes-off / --fixes-on must be comma-separated switch names');
const fixesSet: Record<string, boolean> = { ...Object.fromEntries(fixesOff.map(n => [n, false])), ...Object.fromEntries(fixesOn.map(n => [n, true])) };
const fixesNames = Object.keys(fixesSet);
const fixesPrev: Record<string, boolean> = {};
let fixesRestored = !fixesNames.length;
const restoreFixes = async () => { if (fixesRestored) return; fixesRestored = true; await ag('patches', fixesPrev); };
if (fixesNames.length) {
  const both = fixesNames.filter(n => values.ab?.split(',').includes(n) || (fixesOff.includes(n) && fixesOn.includes(n)));
  if (both.length) throw new Error(`--fixes-off / --fixes-on / --ab name ${both.join(',')} twice`);
  const before = await ag('patches');
  const unknown = fixesNames.filter(n => typeof before.result?.[n] !== 'boolean'); // native / counter switches and typos are not patches switches
  if (!before.ok || unknown.length) throw new Error(`--fixes-off / --fixes-on: not a patches switch: ${unknown.join(',') || before.error}`);
  for (const n of fixesNames) fixesPrev[n] = before.result[n];
  const after = await ag('patches', fixesSet);
  if (fixesNames.some(n => after.result?.[n] !== fixesSet[n])) { await restoreFixes(); throw new Error(`--fixes-off / --fixes-on: switches not set: ${JSON.stringify(after.result && Object.fromEntries(fixesNames.map(n => [n, after.result[n]])))}`); }
  if (fixesOff.length) console.log(`${stamp()} fixes off: ${fixesOff.join(',')} (verified)`);
  if (fixesOn.length) console.log(`${stamp()} fixes on: ${fixesOn.join(',')} (verified)`);
  for (const ev of ['uncaughtException', 'unhandledRejection'] as const) process.on(ev, e => { console.error(e); restoreFixes().finally(() => process.exit(1)); });
}
// --until-compares: counted from the game's shadow log (incremental read); the window then ends early through the normal end path
let stopped = false;
const untilN = values['until-compares'] ? Number(values['until-compares']) : 0;
if (values['until-compares'] && !(Number.isInteger(untilN) && untilN > 0)) throw new Error('--until-compares must be a positive integer');
const shadowFile = join(process.env.TS3_BIN ?? 'C:/Program Files (x86)/Steam/steamapps/common/The Sims 3/Game/Bin', 'ts3native-shadow.jsonl');
let shadowOff = 0, shadowCarry = '', compares = 0, target = untilN;
function countCompares() {
  try {
    const b = readFileSync(shadowFile);
    if (b.length < shadowOff) { shadowOff = 0; shadowCarry = ''; compares = 0; } // the game restarted the log
    const lines = (shadowCarry + b.subarray(shadowOff).toString('utf8')).split('\n');
    shadowOff = b.length; shadowCarry = lines.pop()!;
    for (const l of lines) if (l.trim() && classify(JSON.parse(l)).cls < 'M4') compares++;
  } catch { /* no log yet or a half-written line: counted next time */ }
}
function untilCheck() {
  if (!untilN) return;
  countCompares();
  const el = (Date.now() - t0) / 60000;
  console.log(`${stamp()} compares ${compares} (target ${target}), ${Math.max(0, mins - el).toFixed(1)} min left`);
  if (compares < target) return;
  if (el < 5) { target = 2 * compares; console.log(`${stamp()} ${compares} compares before 5 min: target raised to ${target}`); return; }
  console.log(`${stamp()} stopped at ${compares} compares after ${el.toFixed(1)} min (target ${untilN})`);
  stopped = true; prof?.kill();
}
const waitUntil = async (t: number) => { while (!stopped && Date.now() < t) await sleep(Math.min(1000, t - Date.now())); };
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
  for (const end = Date.now() + secs * 1000; !stopped && Date.now() < end;) {
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
    prof?.kill();
    await restoreFixes();
    process.exit(1);
  }
})();
const boats0 = (await ag('houseboats')).result ?? []; // empty outside Isla Paradiso
if (values.taskstall) console.log(`${stamp()} taskstall: ${JSON.stringify(await get(`taskstall?start=1&ms=${Number(values.taskstall)}`))}`);

const prof = values.watchstalk || values['no-prof'] ? null : spawn('python', ['-m', 'frida_tools.repl', '-n', 'TS3W.exe', '-l', join(ROOT, 'tools', 'frida', 'stutter-profile.js'),
  '-P', JSON.stringify({ out: `${pre}-${secs}`.replace(/\\/g, '/'), hitchMs: 50 }), '-q', '-t', String(secs)], { stdio: ['ignore', 'pipe', 'pipe'] });
if (prof) { const plog = createWriteStream(`${pre}-${secs}.log`); prof.stdout.pipe(plog); prof.stderr.pipe(plog); }
const profDone = prof ? new Promise(r => prof.on('exit', r)) : Promise.resolve();
// fail fast: the profiler stops itself after -t; one still running 90 s past that is hung, and the run would wait on it forever
setTimeout(() => { if (prof && prof.exitCode === null && prof.signalCode === null) { console.log(`${stamp()} FAIL: stutter profiler still running 90 s past -t ${secs}; killed (fail-fast guard)`); prof.kill(); } }, (secs + 90) * 1000).unref();

await ag('autopilot', { enabled: true, speed: 'ultra', dialogs: true }); // window start: unpause to x3
const t0 = Date.now(), at = (s: number) => t0 + s * 1000;
// --inroom / --watchstalk: own Frida session like the stutter profiler; the script writes <tag>-<name>.json itself and prints a line every 30 s
const side = (on: boolean | undefined, file: string, name: string, extra = {}) => on ? new Promise(r => {
  const p = spawn('python', ['-m', 'frida_tools.repl', '-n', 'TS3W.exe', '-l', join(ROOT, 'tools', 'frida', file),
    // +5 s: unload apart from the stutter profiler's session (two agents unloading together, docs/99 §12.7 / I-99-62)
    '-P', JSON.stringify({ out: pre.replace(/\\/g, '/'), minutes: mins, ...extra }), '-q', '-t', String(secs + 5)], { stdio: ['ignore', 'pipe', 'pipe'] });
  const log = createWriteStream(`${pre}-${name}.log`);
  p.stdout.on('data', (d: Buffer) => { log.write(d); for (const l of String(d).split(/\r?\n/)) if (l.includes(`[${name}]`)) console.log(`${stamp()} ${l.trim()}`); });
  p.stderr.pipe(log);
  p.on('exit', r);
  setTimeout(() => { if (p.exitCode === null && p.signalCode === null) { console.log(`${stamp()} FAIL: ${file} still running 90 s past -t ${secs}; killed (fail-fast guard)`); p.kill(); } }, (secs + 90) * 1000).unref();
}) : Promise.resolve();
const inroomDone = side(values.inroom, 'inroom-reads.js', 'inroom');
const watchDone = side(values.watchstalk, 'watch-stalk.js', 'watchstalk', values['watchstalk-worker'] ? { worker: true } : {});
const lr2Done = side(values.livereads2, 'live-reads2.js', 'livereads2');
const f4bDone = side(values.f4b, 'f4b-reads.js', 'f4b');
// --vres (docs/133): a callvirt inline-cache snapshot through the probe agent every 60 s from the window start, then a last one
// that writes the union to <tag>-vres.json; nothing is hooked. --vres-dry N: the same, but each snapshot walks only N methods
// (no call / target following; docs/133 §5 bisect). A snapshot is many short /vcache calls (one tick-aligned slice of <= 20 ms
// each, 50 ms apart), so the agent's script thread is never held for long: bp-vres (2026-10-20) held it with one synchronous
// call past game-alive's 60 s heartbeat limit, and the harness killed a running game.
const vresDry = Number(values['vres-dry'] ?? 0), vresOn = values.vres || values['vres-dry'] !== undefined;
const vresSnapshot = async (q: string) => {
  let fails = 0, slowest = 0, r: any;
  for (const t = Date.now(); !stopped;) {
    const s0 = Date.now();
    try { r = await get(`vcache?snap=1&budget=45&slice=20&wait=100${vresDry ? `&dry=${vresDry}` : ''}${q}`, 10000); fails = 0; }
    catch (e: any) { if (++fails >= 2) throw new Error(`two slices in a row failed: ${e.message}`); continue; }
    slowest = Math.max(slowest, Date.now() - s0);
    if (r.done) return Object.assign(r, { slowest });
    if (Date.now() - t > 60000) throw new Error(`not done after 60 s (agent budget is 45 s): ${r.methods} methods, ${r.queued} queued`);
    await sleep(50);
  }
  return Object.assign(r ?? {}, { slowest });
};
const vresLine = (r: any) => `${r.ms} ms wall, ${r.workMs} ms work in ${r.slices} slices (${r.tickAligned} tick-aligned, slowest call ${r.slowest} ms), ${r.images} images, ${r.methods} methods walked, ${r.decoded} decoded, ${r.filled} filled caches, ${r.sites} sites, +${r.newPairs} pairs${r.partial ? ' (budget hit: partial)' : ''}${r.dry ? ` (dry ${r.dry})` : ''}`;
const vresDone = vresOn ? (async () => {
  let fails = 0;
  for (let s = 60; s < secs - 20 && !stopped; s += 60) {
    await sleep(Math.max(0, at(s) - Date.now()));
    try {
      const r = await vresSnapshot('');
      fails = 0;
      console.log(`${stamp()} [vres] snapshot ${r.snap}: ${vresLine(r)}; ${Math.ceil((secs - s) / 60)} min left`);
    } catch (e: any) {
      console.log(`${stamp()} [vres] snapshot failed: ${e.message}`);
      if (++fails >= 2) { console.log(`${stamp()} FAIL: [vres] two snapshots in a row failed (fail-fast guard)`); break; }
    }
  }
  const out = `${pre}-vres.json`.replace(/\\/g, '/');
  try {
    const r = await vresSnapshot('');
    const w = await get(`vcache?out=${encodeURIComponent(out)}`, 30000);
    console.log(`${stamp()} [vres] last snapshot ${r.snap}: ${vresLine(r)}; wrote ${w.wrote}`);
  } catch (e: any) { console.log(`${stamp()} FAIL: [vres] last snapshot / dump: ${e.message}`); }
})() : Promise.resolve();
const abNames = values.ab ? values.ab.split(',') : [];
const slices: { from: number; on: boolean }[] = [];
// a rerun with the same tag must not append to the last run's snapshots (wait-ab read run 1 twice, 2026-10-20)
if (abNames.length) writeFileSync(`${pre}-ab-native.jsonl`, '');
const toggler = (async () => {
  if (!abNames.length) return;
  for (let i = 0; i * Number(values.slice) * 60 < secs; i++) {
    // slice 0 is warm-up (on, not counted); then off,on,on,off (ABBA) so a drift in world activity hits both sides alike;
    // --ab-on-first: on,off,off,on (docs/136 §2: the first counted slice is a heavy stretch, so pair jobs by start side)
    const on = i === 0 || [false, true, true, false][(i - 1) % 4] !== !!values['ab-on-first'];
    await sleep(Math.max(0, at(i * Number(values.slice) * 60) - Date.now()));
    // ts3native counters at every switch (end of the previous slice): wait-ab.ts splits the sim-thread waits per side
    await get('native').then(c => appendFileSync(`${pre}-ab-native.jsonl`, JSON.stringify({ i, s: (Date.now() - t0) / 1000, c }) + '\n')).catch(() => {});
    const nativeNames = ['routeworker', 'routebudget', 'routeasync', 'alloc', 'waterjps']; // ts3native's runtime switches, not C# patches
    // ts3native switches that live only as a Ts3nCounters field (no /native parameter): name -> byte offset
    const counterSwitches: Record<string, number> = { heightcache: 0x1B4, simskipshared: 0xBF4, allocdedupe: 0xC1C, lotsincos: 0xC28, idcache: 0xC3C, terraingrave: 0xD00, discourage: 0x1074, discdestroy: 0x135C, discsweep: 0x1360, steerro: 0x10A4, steershadow: 0x10A8, fprebuildlock: 0x1110, lotfdro: 0x1134, lotfdshadow: 0x1138, locowin: 0x13E8, asyncreplan: 0x14A8, obstructedro: 0x1528, obstructedshadow: 0x152C, slotenabledro: 0x1568, slotenabledshadow: 0x156C, routesnap: 0x2F74, routesnaprel: 0x302C, routesnapgate: 0x3068, routesnapfree: 0x307C, fallbackasync: 0x314C, deferfootprint: 0x3178, m1retry: 0x31C8, destroypark: 0x31E0, avoidro: 0x31F0, avoidshadow: 0x31F4, earlyout: 0x3260, unseenthrottle: 0x33C8, unseenavoid: 0x3424, unseenfootprint: 0x3428, unseenrate: 0x342C, fbjobcap: 0x354C };
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
    appendFileSync(`${pre}-repro.jsonl`, JSON.stringify({ t: stamp(), cmd: s.cmd, args: s.args, r }) + '\n'); // full result: the log line is cut
    // fail fast, as repro.ts: a failed setup step means the rest of the run measures nothing (2026-10-19 bp-ab-wildflowertask
    // ended PASS after wildflower_drain failed); "optional": true steps may fail. The queue closes the game after the job.
    if (!r.ok && !s.optional) {
      console.log(`${stamp()} FAIL: repro step ${s.cmd} failed: ${String(r.error).split('\n')[0].slice(0, 300)}`);
      for (const c of reproEntry.cleanup ?? []) { const cr = await ag(c.cmd, c.args); console.log(`${stamp()} cleanup ${c.cmd}: ${cr.ok ? 'ok' : 'ERROR ' + cr.error}`); }
      process.exit(1);
    }
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
  while (!stopped && Date.now() - t0 < secs * 1000) {
    const k = await ag('keep_alive', { threshold: -50 }); await ag('decline_requests');
    const ap = await ag('autopilot');
    await checkStuck();
    appendFileSync(`${pre}-upkeep.log`, `${stamp()} ${k.ok ? `topped ${k.result.topped_up.length} repaired ${k.result.repaired?.length ?? 0}` : k.error} ${ap.ok ? `speedfix ${ap.result.speed_fixes} dialogs ${ap.result.cleared.length}` : ''}\n`);
    untilCheck();
    if (stopped) break;
    await sleep(30000);
  }
})();

const run = (args: string[], file: string) => new Promise(r => {
  const p = spawn(process.execPath, args, { stdio: ['ignore', 'pipe', 'inherit'] });
  p.stdout.pipe(createWriteStream(file)); p.on('exit', r);
});
// none with --taskstall: two samplers suspending the sim thread deadlocked the probe agent (bp-detour, 2026-10-13)
const window = abNames.length || values.taskstall || values.repro || values.inroom || values.watchstalk || values.livereads2 || values.f4b || vresOn || values['no-samplers'] ? 0 : Math.min(180, Math.floor(secs / 2) - 60); // 3 min each in a 10 min run; none in --ab mode
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
await waitUntil(at(60));
console.log(`${stamp()} managed sampler ${window} s`);
if (window) await run([join(ROOT, 'tools', 'probe', 'managed-sample.mjs'), String(window), '5', `${pre}-managed.jsonl`], `${pre}-managed.txt`);
await waitUntil(at(secs / 2));
console.log(`${stamp()} native sampler ${window} s`);
writeFileSync(`${pre}-simstack.jsonl`, '');
if (values.mainstack && !window && !mainOnly) console.log(`${stamp()} mainstack skipped: no sampler window in this mode`);
let simLost = 0; // an empty simstack.jsonl went unnoticed for 3 days (2026-10-16-sampler-hang.md): print the loss
for (const end = Date.now() + window * 1000; !stopped && Date.now() < end;) {
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
await Promise.all([upkeep, profDone, toggler, reproRun, mainOnlyRun, inroomDone, watchDone, lr2Done, f4bDone, vresDone]);
stopLoad();
await restoreFixes();
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
