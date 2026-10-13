// Repro library runner: reproduce a bug on demand from a saved situation, then check whether a fix handles it.
//   node tools/harness/repro.ts list
//   node tools/harness/repro.ts run <id> [--no-load] [--set a=true,b=false]   (needs the probe server; loads the entry's save unless --no-load)
//   node tools/harness/repro.ts run-many <id,id,...> [--no-load] [--set ...]  several repros in one load: same save, areas at
//     least 150 m apart and no shared Sims, else the conflicting ones are left for another batch; all watched together
// Entries live in tools/harness/repros.json: { id, bug, fix, save, steps: [{cmd, args, waitS?}], watch, expect }.
//   watch.kind "stuck":  watch these Sims (or all, if "sims" is empty) for watch.minutes; PASS when none is stuck
//                        (same spot and interaction for 6 checks of 30 s, or routing without moving for 3).
//   watch.kind "counter": PASS when patches.<watch.counter> rose by at least watch.min within watch.minutes.
// Steps are agent commands (place_sim, sim_go_home, do_interaction, patches {name:false}, ...), run in order.
import { execFileSync } from 'node:child_process';
import { readFileSync, rmSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { endlessJob } from './stuck-rule.ts';
import { watchGame } from './game-alive.ts';

const ROOT = resolve(import.meta.dirname, '..', '..');
const LIB = JSON.parse(readFileSync(join(ROOT, 'tools', 'harness', 'repros.json'), 'utf8')) as any[];
const [cmd, id] = process.argv.slice(2);
const sleep = (ms: number) => new Promise(r => setTimeout(r, ms));
const stamp = () => new Date().toISOString().slice(11, 19);
const ag = async (c: string, args?: object): Promise<any> => {
  const q = new URLSearchParams({ cmd: c, timeout: '8000' });
  if (args) q.set('args', JSON.stringify(args));
  try { return await (await fetch(`http://127.0.0.1:47891/agent?${q}`, { signal: AbortSignal.timeout(20000) })).json(); } catch (e) { return { ok: false, error: String(e) }; }
};

if (cmd === 'list' || !cmd) {
  for (const r of LIB) console.log(`${r.id.padEnd(28)} ${r.save.padEnd(26)} ${r.bug}`);
  process.exit(0);
}
// run-many: merge compatible entries into one virtual entry (one save, areas apart, no shared Sims)
let r: any;
if (cmd === 'run-many') {
  const want = (id ?? '').split(',').map(x => LIB.find(e => e.id === x)).filter(Boolean);
  const batch: any[] = [], later: string[] = [];
  const simsOf = (e: any) => new Set([...(e.watch?.sims ?? []), ...(e.steps ?? []).map((s: any) => s.args?.sim).filter(Boolean)]);
  for (const e of want) {
    const clash = batch.find(b => b.save !== e.save
      || (b.area && e.area && Math.hypot(b.area[0] - e.area[0], b.area[1] - e.area[1]) < 150)
      || [...simsOf(e)].some(n => simsOf(b).has(n)));
    if (clash) later.push(e.id); else batch.push(e);
  }
  if (!batch.length) { console.error('no matching entries'); process.exit(2); }
  if (later.length) console.log(`not in this batch (same place, Sim or another save): ${later.join(', ')}`);
  const counters = batch.filter(e => e.watch.kind === 'counter');
  if (counters.length) console.log(`counter watches run separately: ${counters.map(e => e.id).join(', ')}`);
  const stuckOnes = batch.filter(e => e.watch.kind !== 'counter');
  r = { id: stuckOnes.map(e => e.id).join('+'), bug: `${stuckOnes.length} repros together`, fix: '-', save: batch[0].save,
    steps: stuckOnes.flatMap(e => e.steps ?? []),
    watch: { kind: 'stuck', sims: stuckOnes.flatMap(e => e.watch.sims ?? []), minutes: Math.max(...stuckOnes.map(e => e.watch.minutes)) },
    expect: 'every repro passes' };
} else r = LIB.find(x => x.id === id);
if ((cmd !== 'run' && cmd !== 'run-many') || !r) { console.error('usage: repro.ts list | run <id> | run-many <id,id,...> [--no-load] [--set ...]'); process.exit(2); }

console.log(`${stamp()} ${r.id}: ${r.bug}\n  fix under test: ${r.fix}`);
if (!process.argv.includes('--no-load'))
  execFileSync(process.execPath, [join(ROOT, 'tools', 'harness', 'cycle.ts'), '--save', `${r.save}.sims3`], { stdio: 'inherit' });
const alive = watchGame(); // FAIL as soon as the game dies, never a PASS on a dead game
await ag('autopilot', { enabled: true, speed: 'ultra', dialogs: true });
// --set a=true,b=false: switch fixes for this run (patches command), e.g. to bisect which one causes a bug
const set = process.argv.indexOf('--set');
if (set > 0) {
  const sw = Object.fromEntries(process.argv[set + 1].split(',').map(kv => { const [k, v] = kv.split('='); return [k, v !== 'false']; }));
  console.log(`${stamp()} switches ${JSON.stringify(sw)}: ${(await ag('patches', sw)).ok ? 'ok' : 'failed'}`);
}
// tell stuck-scan.ts which Sims this repro uses, so it does not report them as new cases
const ACTIVE = join(ROOT, 'probe-out', 'repro-active.json');
writeFileSync(ACTIVE, JSON.stringify({ id: r.id, sims: [...new Set([...(r.watch?.sims ?? []), ...(r.steps ?? []).map((x: any) => x.args?.sim).filter(Boolean)])] }));
process.on('exit', () => rmSync(ACTIVE, { force: true }));
for (const s of r.steps ?? []) {
  const res = await ag(s.cmd, s.args);
  console.log(`${stamp()} step ${s.cmd} ${JSON.stringify(s.args ?? {})}: ${res.ok ? JSON.stringify(res.result).slice(0, 160) : 'ERROR ' + res.error}`);
  if (!res.ok && !s.optional) { console.log('FAIL: step failed'); process.exit(1); }
  if (s.waitS) await sleep(s.waitS * 1000);
}

const w = r.watch, t0 = Date.now();
let verdict = '';
if (w.kind === 'counter') {
  const start = (await ag('patches')).result?.[w.counter] ?? 0;
  while (Date.now() - t0 < w.minutes * 60000) {
    await sleep(15000);
    const n = ((await ag('patches')).result?.[w.counter] ?? 0) - start;
    console.log(`${stamp()} ${w.counter} +${n}`);
    if (n >= w.min) { verdict = 'PASS'; break; }
  }
  verdict ||= `FAIL: ${w.counter} rose less than ${w.min} in ${w.minutes} min`;
} else {
  const seen = new Map<string, { key: string; checks: number }>();
  const stuck = new Set<string>();
  while (Date.now() - t0 < w.minutes * 60000) {
    await sleep(30000);
    const act = await ag('sims_activity');
    if (!act.ok) { console.log(`${stamp()} stuck: ? (no answer: ${act.error})`); continue; } // a failed call is not "no stuck Sims"
    for (const s of act.result) {
      if (w.sims?.length && !w.sims.includes(s.name)) continue;
      const key = `${s.x},${s.z},${s.interaction},${s.routing}`;
      const p = seen.get(s.id);
      const e = p && p.key === key ? { key, checks: p.checks + 1 } : { key, checks: 1 };
      seen.set(s.id, e);
      if (s.interaction && !endlessJob(s.interaction) && e.checks >= (s.routing ? 3 : 6)) stuck.add(`${s.name} on ${s.interaction} at ${s.lot ?? 'street'}`);
    }
    console.log(`${stamp()} stuck: ${stuck.size ? [...stuck].join('; ') : 'none'}`);
  }
  verdict = stuck.size ? `FAIL: ${stuck.size} stuck` : 'PASS';
}
await alive.check();
console.log(`${stamp()} ${r.id}: ${verdict} (expected: ${r.expect})`);
process.exit(verdict === 'PASS' ? 0 : 1);
