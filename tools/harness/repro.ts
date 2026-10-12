// Repro library runner: reproduce a bug on demand from a saved situation, then check whether a fix handles it.
//   node tools/harness/repro.ts list
//   node tools/harness/repro.ts run <id> [--no-load]     (needs the probe server; loads the entry's save unless --no-load)
// Entries live in tools/harness/repros.json: { id, bug, fix, save, steps: [{cmd, args, waitS?}], watch, expect }.
//   watch.kind "stuck":  watch these Sims (or all, if "sims" is empty) for watch.minutes; PASS when none is stuck
//                        (same spot and interaction for 6 checks of 30 s, or routing without moving for 3).
//   watch.kind "counter": PASS when patches.<watch.counter> rose by at least watch.min within watch.minutes.
// Steps are agent commands (place_sim, sim_go_home, do_interaction, patches {name:false}, ...), run in order.
import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { join, resolve } from 'node:path';

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
const r = LIB.find(x => x.id === id);
if (cmd !== 'run' || !r) { console.error('usage: repro.ts list | run <id> [--no-load]'); process.exit(2); }

console.log(`${stamp()} ${r.id}: ${r.bug}\n  fix under test: ${r.fix}`);
if (!process.argv.includes('--no-load'))
  execFileSync(process.execPath, [join(ROOT, 'tools', 'harness', 'cycle.ts'), '--save', `${r.save}.sims3`], { stdio: 'inherit' });
await ag('autopilot', { enabled: true, speed: 'ultra', dialogs: true });
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
    for (const s of (await ag('sims_activity')).result ?? []) {
      if (w.sims?.length && !w.sims.includes(s.name)) continue;
      const key = `${s.x},${s.z},${s.interaction},${s.routing}`;
      const p = seen.get(s.id);
      const e = p && p.key === key ? { key, checks: p.checks + 1 } : { key, checks: 1 };
      seen.set(s.id, e);
      if (s.interaction && e.checks >= (s.routing ? 3 : 6)) stuck.add(`${s.name} on ${s.interaction} at ${s.lot ?? 'street'}`);
    }
    console.log(`${stamp()} stuck: ${stuck.size ? [...stuck].join('; ') : 'none'}`);
  }
  verdict = stuck.size ? `FAIL: ${stuck.size} stuck` : 'PASS';
}
console.log(`${stamp()} ${r.id}: ${verdict} (expected: ${r.expect})`);
process.exit(verdict === 'PASS' ? 0 : 1);
