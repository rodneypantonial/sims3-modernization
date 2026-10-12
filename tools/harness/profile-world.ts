// One profiling run on a loaded world, the same for every world so results compare:
//   node tools/harness/profile-world.ts --tag sv [--minutes 10]
// Needs the probe server (ts3probe.py serve) and a world in live mode (cycle.ts --save X). For the whole run:
// autopilot at x3 with popups cleared, keep_alive + decline_requests every 30 s, and the native stutter profiler
// (separate Frida session). Samplers in turn: managed C# stack (minutes 1-4), native sim-thread stack (5-8).
// Output: probe-out/profile/<tag>-{600-seconds.csv,600-hitches.jsonl,600.log,upkeep.log,managed.*,simstack.jsonl,stuck.json,houseboats.json}.
import { spawn } from 'node:child_process';
import { appendFileSync, createWriteStream, mkdirSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { parseArgs } from 'node:util';

const { values } = parseArgs({ options: { tag: { type: 'string' }, minutes: { type: 'string', default: '10' } } });
if (!values.tag) throw new Error('--tag required');
const ROOT = resolve(import.meta.dirname, '..', '..');
const OUT = join(ROOT, 'probe-out', 'profile');
mkdirSync(OUT, { recursive: true });
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
await ag('autopilot', { enabled: true, speed: 'ultra', dialogs: true });
const boats0 = (await ag('houseboats')).result ?? []; // empty outside Isla Paradiso

const prof = spawn('python', ['-m', 'frida_tools.repl', '-n', 'TS3W.exe', '-l', join(ROOT, 'tools', 'frida', 'stutter-profile.js'),
  '-P', JSON.stringify({ out: `${pre}-${secs}`.replace(/\\/g, '/'), hitchMs: 50 }), '-q', '-t', String(secs)], { stdio: ['ignore', 'pipe', 'pipe'] });
const plog = createWriteStream(`${pre}-${secs}.log`);
prof.stdout.pipe(plog); prof.stderr.pipe(plog);
const profDone = new Promise(r => prof.on('exit', r));

const t0 = Date.now(), at = (s: number) => t0 + s * 1000;
// Stuck Sims, as watchdog.ts judges them: same spot and interaction for 6 checks (3 min), or routing without moving for 3.
// Flagged Sims recently teleported by a shortcut are marked, so every shortcut gets checked for leaving Sims stuck.
const simSeen = new Map<string, { key: string; checks: number; reported: boolean }>();
const stuck: any[] = [];
async function checkStuck() {
  const r = await ag('sims_activity');
  if (!r.ok) return;
  const recent: string[] = (await ag('patches')).result?.recent ?? [];
  for (const sim of r.result) {
    const key = `${sim.x},${sim.z},${sim.interaction},${sim.routing}`;
    const prev = simSeen.get(sim.id);
    const e = prev && prev.key === key ? { key, checks: prev.checks + 1, reported: prev.reported } : { key, checks: 1, reported: false };
    simSeen.set(sim.id, e);
    if (!e.reported && sim.interaction && e.checks >= (sim.routing ? 3 : 6)) {
      e.reported = true;
      stuck.push({ ...sim, checks: e.checks, shortcut: recent.some(l => l.startsWith(sim.name + ' -> ') || l.startsWith(sim.name + ' => ')) });
    }
  }
}
writeFileSync(`${pre}-upkeep.log`, '');
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
const window = Math.min(180, Math.floor(secs / 2) - 60); // 3 min each in a 10 min run
await sleep(Math.max(0, at(60) - Date.now()));
console.log(`${stamp()} managed sampler ${window} s`);
await run([join(ROOT, 'tools', 'probe', 'managed-sample.mjs'), String(window), '5', `${pre}-managed.jsonl`], `${pre}-managed.txt`);
await sleep(Math.max(0, at(secs / 2) - Date.now()));
console.log(`${stamp()} native sampler ${window} s`);
writeFileSync(`${pre}-simstack.jsonl`, '');
for (const end = Date.now() + window * 1000; Date.now() < end;) {
  const t = Date.now();
  try { appendFileSync(`${pre}-simstack.jsonl`, JSON.stringify({ t, stack: (await get('simstack?frames=24', 5000)).stack }) + '\n'); } catch { /* sample lost */ }
  await sleep(Math.max(0, 200 - (Date.now() - t)));
}
await Promise.all([upkeep, profDone]);
writeFileSync(`${pre}-stuck.json`, JSON.stringify(stuck, null, 1));
console.log(`${stamp()} stuck Sims: ${stuck.length}${stuck.length ? ` (${stuck.map(x => `${x.name}${x.shortcut ? ' [teleported]' : ''} on ${x.interaction}`).join('; ')})` : ''}`);
if (boats0.length) {
  const boats1 = (await ag('houseboats')).result ?? [];
  const moved = boats1.filter((b: any) => { const o = boats0.find((x: any) => x.lot === b.lot); return !o || Math.hypot(o.x - b.x, o.z - b.z) > 1; });
  writeFileSync(`${pre}-houseboats.json`, JSON.stringify({ start: boats0, end: boats1 }, null, 1));
  console.log(`${stamp()} houseboats: ${boats1.length}, moved: ${moved.map((b: any) => `${b.lot} (${b.household ?? 'empty'})`).join(', ') || 'none'}`);
}
console.log(`${stamp()} done; game ${(await ag('game_state')).ok ? 'alive' : 'NOT answering'}`);
