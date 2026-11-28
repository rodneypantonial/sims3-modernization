// Stall report (docs/367 §1e): groups the sim-thread stalls of one profile run by the innermost game call, says whether the
// stall is lock wait or in-call work (hitch record buckets rtLock / rtOtherLock / rtJob / rtPlanWait vs the rest), and the
// household question: a run with no active household has only NPC stalls; with one, every task guid not proven NPC counts as household
// (unknown = household, CLAUDE.md). Reads only.
//   node tools/harness/taskstall-report.ts --stalls <x-taskstall.jsonl> [--hitches <tag>-240-hitches.jsonl] [--active-household 0|1]
//   node tools/harness/taskstall-report.ts --self-check
import { readFileSync } from 'node:fs';
import { parseArgs } from 'node:util';

export type Stall = { ms: number; task?: { guid?: string | null } | null; samples?: { methods?: string[] }[] };
const lines = (t: string) => t.split(/\r?\n/).filter(Boolean).map(l => JSON.parse(l));
export function innermost(s: Stall): string {
  const c = new Map<string, number>();
  for (const x of s.samples ?? []) { const m = x.methods?.[0]; if (m) c.set(m, (c.get(m) ?? 0) + 1); }
  return [...c.entries()].sort((a, b) => b[1] - a[1])[0]?.[0] ?? '?';
}
export function caller(s: Stall): string { // first Sims3.Gameplay frame that names the Sim or boat object
  const m = (s.samples?.[0]?.methods ?? []).find(x => /^Sims3\.Gameplay\.(Objects\.Vehicles|Actors)\./.test(x));
  return m ?? '?';
}
export function report(stalls: Stall[], hitches: any[], activeHousehold: boolean) {
  const by = new Map<string, { n: number; ms: number; max: number; boat: number }>();
  for (const s of stalls) {
    const k = innermost(s).replace(/^ScriptCore\./, ''), r = by.get(k) ?? { n: 0, ms: 0, max: 0, boat: 0 };
    r.n++; r.ms += s.ms; r.max = Math.max(r.max, s.ms); if (/Boat/.test(caller(s))) r.boat++;
    by.set(k, r);
  }
  const big = hitches.filter(h => h.wallMs >= 1500), sum = (k: string) => big.reduce((a, h) => a + (Number(h[k]) || 0), 0);
  const wall = big.reduce((a, h) => a + h.wallMs, 0), lock = sum('rtLock') + sum('rtOtherLock') + sum('rtJob') + sum('rtPlanWait');
  return { stalls: stalls.length, household: activeHousehold ? 'unknown (counts as household)' : 'none active: all NPC',
    byCall: [...by.entries()].sort((a, b) => b[1].ms - a[1].ms).map(([call, r]) => ({ call, ...r })),
    hitches1500: { n: big.length, wallMs: Math.round(wall), lockMs: Math.round(lock), threadedMs: Math.round(sum('threaded')),
      lockShare: wall ? Math.round(100 * lock / wall) : null } };
}
if (process.argv.includes('--self-check')) {
  const st: Stall[] = [{ ms: 3000, task: { guid: '0x1' }, samples: [{ methods: ['ScriptCore.RouteManager.RouteManager_AddPrivateFootprint', 'Sims3.Gameplay.Objects.Vehicles.BoatRoutingComponent.EnableDynamicFootprint'] }] }];
  const r = report(st, [{ wallMs: 3000, rtLock: 10, threaded: 2990 }], false);
  if (r.byCall[0].call !== 'RouteManager.RouteManager_AddPrivateFootprint' || r.byCall[0].boat !== 1 || r.hitches1500.lockShare !== 0 || !r.household.startsWith('none')) throw new Error('self-check ' + JSON.stringify(r));
  console.log('taskstall-report self-check ok');
} else {
  const { values } = parseArgs({ options: { stalls: { type: 'string' }, hitches: { type: 'string' }, 'active-household': { type: 'string', default: '0' } } });
  if (!values.stalls) throw new Error('--stalls <taskstall.jsonl>');
  console.log(JSON.stringify(report(lines(readFileSync(values.stalls, 'utf8')), values.hitches ? lines(readFileSync(values.hitches, 'utf8')) : [], values['active-household'] === '1'), null, 1));
}
