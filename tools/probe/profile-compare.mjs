// Compares profile-world.ts runs side by side: node tools/probe/profile-compare.mjs bp sv ip
// Reads probe-out/profile/<tag>-600-seconds.csv, <tag>-managed.jsonl (or managed-<tag>.jsonl) and <tag>-simstack.jsonl
// (or simstack-<tag>.jsonl). Prints native per-second rates, then key C# methods (inclusive %) and the top self methods.
import { existsSync, readdirSync, readFileSync } from 'node:fs';
import { join, resolve } from 'node:path';

const DIR = join(resolve(import.meta.dirname, '..', '..'), 'probe-out', 'profile');
// A tag profiled with profile-world.ts --ab also yields <tag>:on and <tag>:off (rows split by slice; the first 15 s
// after each switch are dropped while the sim settles).
const tags = process.argv.slice(2).flatMap(t => existsSync(join(DIR, `${t}-ab.json`)) ? [`${t}:on`, `${t}:off`] : [t]);
const find = (...names) => names.map(n => join(DIR, n)).find(existsSync);
const lines = f => f ? readFileSync(f, 'utf8').split('\n').filter(Boolean) : [];
const pct = (n, d) => d ? (100 * n / d).toFixed(1) : '-';
const row = (label, vals) => console.log(label.padEnd(44) + vals.map(v => String(v).padStart(9)).join(''));

const KEY = ['Sims3.Gameplay.Actors.Sim.Simulate', 'Sims3.Gameplay.ObjectComponents.RoutingComponent.DoRoute', 'ScriptCore.Route.Route_Plan',
  'Sims3.Gameplay.Routing.SimWalkStyleRules.GetAdjustedWalkStyleForRoute', 'System.Enum.Equals', 'Sims3.Gameplay.Core.Lot.UpdateReactions',
  'Sims3.Gameplay.Autonomy.ReactionBroadcaster.PulseSims', 'Sims3.Gameplay.Actors.SimUpdate.Simulate', 'Sims3.Gameplay.Autonomy.Autonomy.Update',
  'Sims3.Gameplay.Core.GoHome.Run', 'GoToVirtualHomeInternal.Run', 'Sims3.Gameplay.Queries.GetObjects',
  'Sims3.Gameplay.EventSystem.EventTracker.SendEvent', 'Sims3.Gameplay.Core.Lot.Simulate'];
const runs = tags.map(full => {
  const [tag, side] = full.split(':');
  // <tag>-<run seconds>-seconds.csv (600 for the default 10 min run)
  const csv = lines(find(...readdirSync(DIR).filter(f => f.startsWith(`${tag}-`) && /^\d+-seconds\.csv$/.test(f.slice(tag.length + 1))))), head = csv.shift()?.split(',') ?? [];
  let rows = csv.map(l => Object.fromEntries(l.split(',').map((v, i) => [head[i], Number(v)])));
  if (side) {
    const { slices } = JSON.parse(readFileSync(join(DIR, `${tag}-ab.json`), 'utf8'));
    // slice 0 is warm-up (older runs did not mark it, but it ran slower there too); a last slice cut short by the
    // profiler's end (under half a slice of rows) is dropped: one short slice swung ip-ab-routeasync 2026-10-17
    const len = slices.length > 1 ? slices[1].from - slices[0].from : Infinity;
    const keep = slices.map((sl, i) => i > 0 && (i < slices.length - 1 || rows.length - sl.from >= len / 2));
    const sliceOf = sec => { let cur = -1; slices.forEach((sl, i) => { if (sec >= sl.from) cur = i; }); return cur >= 0 && keep[cur] && sec - slices[cur].from >= 15 ? cur : -1; };
    const all = rows;
    rows = all.filter((r, i) => sliceOf(i) >= 0 && slices[sliceOf(i)].on === (side === 'on')); // row i is roughly second i of the run
    if (side === 'on') slices.forEach((sl, i) => { // per slice, so one odd slice shows instead of hiding in the mean
      const s = all.filter((r, j) => sliceOf(j) === i), n = s.length || 1;
      if (keep[i]) console.log(`${tag} slice ${i} ${sl.on ? 'on ' : 'off'} ${s.length} s: ${(s.reduce((a, r) => a + r.iters, 0) / n).toFixed(1)} it/s, ${(s.reduce((a, r) => a + r.hitches, 0) / n).toFixed(2)} hitches/s, route ${(s.reduce((a, r) => a + r.route, 0) / n).toFixed(0)} ms/s, worst ${Math.max(0, ...s.map(r => r.maxIterMs)).toFixed(0)} ms`);
    });
  }
  const sum = k => rows.reduce((a, r) => a + (r[k] || 0), 0), n = rows.length || 1;
  // patched methods sample under the hook's name (docs/04 F8); count them as the method they replace
  const alias = n => n === 'GoHomeHook.Run' ? 'Sims3.Gameplay.Core.GoHome.Run' : n;
  const managed = lines(find(`${tag}-managed.jsonl`, `managed-${tag}.jsonl`)).map(l => JSON.parse(l).map(alias));
  const busy = managed.filter(m => m.length);
  const incl = new Map(), self = new Map();
  for (const m of busy) {
    self.set(m[0], (self.get(m[0]) ?? 0) + 1);
    for (const k of new Set(m)) incl.set(k, (incl.get(k) ?? 0) + 1);
  }
  const stacks = lines(find(`${tag}-simstack.jsonl`, `simstack-${tag}.jsonl`)).map(l => JSON.parse(l).stack ?? []);
  const interp = stacks.filter(s => s.some(f => /FUN_00e53eb0/.test(f))).length, interpTop = stacks.filter(s => /FUN_00e53eb0/.test(s[0] ?? '')).length;
  const gcTop = stacks.filter(s => /FUN_00e4c3e0/.test(s[0] ?? '')).length;
  const has = k => head.includes(k); // runs before the stage buckets (docs/04 F17) lack those columns
  return { tag: full, sum, n, has, rows, managed, busy, incl, self, stacks, interp, interpTop, gcTop };
});

row('', tags);
row('seconds', runs.map(r => r.rows.length));
row('iterations/s', runs.map(r => (r.sum('iters') / r.n).toFixed(1)));
row('hitches/s (>=50 ms)', runs.map(r => (r.sum('hitches') / r.n).toFixed(2)));
row('worst iteration ms', runs.map(r => Math.max(0, ...r.rows.map(x => x.maxIterMs)).toFixed(0)));
const simCol = (k, d = 1) => runs.map(r => r.has(k) ? (r.sum(k) / r.n).toFixed(d) : '-'); // sim busy/idle: 'saved time went idle' vs 'eaten'
row('sim busy ms/s (iteration - sleeps)', simCol('simBusy'));
row('sim idle ms/s (in Sleep/NtDelay)', simCol('simIdle'));
row('sim sleeps/s', simCol('simSleeps'));
row('route ms/s', runs.map(r => (r.sum('route') / r.n).toFixed(1)));
row('routes/s', runs.map(r => (r.sum('routes') / r.n).toFixed(1)));
row('route fail %', runs.map(r => pct(r.sum('routeFails'), r.sum('routes'))));
row('gc mark / forced ms/s', runs.map(r => `${(r.sum('gcMark') / r.n).toFixed(1)}/${(r.sum('gcForcedMs') / r.n).toFixed(1)}`));
row('forced gcs / max forced ms', runs.map(r => `${r.sum('gcForced')}/${Math.max(0, ...r.rows.map(x => x.maxGcMarkMs)).toFixed(0)}`));
const stage = k => runs.map(r => r.has(k) ? (r.sum(k) / r.n).toFixed(1) : '-');
row('stage ms/s: ScriptTick (0xD80BC0)', stage('script'));
row('stage ms/s: TaskObjects', stage('taskObjects'));
row('stage ms/s: OneShotObjects', stage('oneShot'));
row('stage ms/s: ThreadedObjects', stage('threaded'));
row('stage ms/s: animation pass 1 (0x571AD0, SACS)', stage('animSacs'));
row('stage ms/s: animation pass 2 (0x571B90, pose)', stage('animPose'));
row('stage ms/s: locomotion (0x83CED0)', stage('locomotion'));
row('stage ms/s: render publish (0x593590)', stage('publish'));
row('worst TaskObjects call ms', runs.map(r => r.has('maxTaskObjectsMs') ? Math.max(0, ...r.rows.map(x => x.maxTaskObjectsMs)).toFixed(0) : '-'));
row('native samples with interpreter on stack %', runs.map(r => pct(r.interp, r.stacks.length)));
row('native top frame interpreter / GC area %', runs.map(r => `${pct(r.interpTop, r.stacks.length)}/${pct(r.gcTop, r.stacks.length)}`));
row('managed samples / idle %', runs.map(r => `${r.managed.length}/${pct(r.managed.length - r.busy.length, r.managed.length)}`));
console.log('\nC# inclusive % of all samples (as in docs/04 F7)');
for (const k of KEY) row(k.replace(/^Sims3\.Gameplay\.|^ScriptCore\./, ''), runs.map(r => pct(r.incl.get(k) ?? 0, r.managed.length)));
console.log('\nC# self, top 12 per world');
for (const r of runs) {
  console.log(`  ${r.tag}`);
  for (const [k, v] of [...r.self].sort((a, b) => b[1] - a[1]).slice(0, 12)) console.log(`    ${pct(v, r.managed.length).padStart(5)}%  ${k}`);
}
