// README / CHANGELOG release tables from profile runs, no log reading by hand. Read-only, markdown on stdout.
//   node tools/harness/release-numbers.ts --vanilla <tags> --prev <tags> --rel <tags> [--soak <tags>]
//   node tools/harness/release-numbers.ts --self-check
// Tags: full profile tags, space or comma separated (bash brace lists work: overnight-1023-2026-10-23-{bp,ip,sv}-rvb-vanilla). The
// town comes from -bp- / -ip- / -sv- in the tag. Per town and side: >= 300 ms per 10 min, worst, p99, it/s (several runs: mean,
// worst = max); cells 'vanilla -> prev -> **rel**'. Soaks: one row per run, >= 1 s count, worst, it/s. A run that worst-hitches.ts
// runInvalid marks (noisy machine, profiler cut short, crash) keeps its counts and worst with '*', p99 and it/s become n/a, and
// the reason is listed under the table; a town with one run per side gets the single-run note. Numbers = hitch-ab.ts 'all' arm.
// Set TS3_REPO=<main checkout> when running from a git worktree.
import assert from 'node:assert/strict';
import { armNums, runNums } from './run-verdict.ts';

const TOWNS: [string, string][] = [['bp', 'Bridgeport'], ['sv', 'Sunset Valley'], ['ip', 'Isla Paradiso']];
export const townOf = (tag: string) => TOWNS.find(([k]) => tag.includes(`-${k}-`))?.[0] ?? null;
type Nums = ReturnType<typeof armNums>;
type Run = { tag: string; n: Nums; invalid: string | null };
type Cell = { per300: number; max: number; p99: number | null; its: number | null; bad: boolean; runs: number };

// several runs of one side and town: means, worst = max; invalid runs only feed counts and worst
export function side(runs: Run[]): Cell | null {
  if (!runs.length) return null;
  const ok = runs.filter(r => !r.invalid), m = (xs: number[]) => xs.reduce((a, b) => a + b, 0) / xs.length;
  const p = ok.map(r => r.n.p99);
  return { per300: m(runs.map(r => r.n.per300)), max: Math.max(...runs.map(r => r.n.max)), p99: !ok.length ? null : p.includes(null) ? -1 : m(p as number[]),
    its: ok.length ? m(ok.map(r => r.n.its)) : null, bad: ok.length < runs.length, runs: runs.length };
}
const star = (c: Cell, s: string) => `${s}${c.bad ? '*' : ''}`;
const p99s = (c: Cell) => c.p99 === null ? 'n/a' : c.p99 < 0 ? '<50' : c.p99.toFixed(0);
const ROWS: [string, (c: Cell) => string, string][] = [
  ['freezes of 300 ms or longer, per 10 minutes', c => star(c, c.per300.toFixed(1)), ''],
  ['worst freeze', c => star(c, c.max.toFixed(0)), ' ms'],
  ['p99 sim step (99% of steps are shorter)', p99s, ' ms'],
  ['sim iterations per second', c => c.its === null ? 'n/a' : c.its.toFixed(1), ''],
];
export function table(sides: [string, Run[]][]): string {
  const cells = TOWNS.map(([k]) => sides.map(([, rs]) => side(rs.filter(r => townOf(r.tag) === k))));
  const out = [`Cells: ${sides.map(s => s[0]).join(' -> ')}.`, '', `| | ${TOWNS.map(t => t[1]).join(' | ')} |`, `|---|${'---|'.repeat(TOWNS.length)}`];
  for (const [label, f, unit] of ROWS)
    out.push(`| ${label} | ${cells.map(cs => cs.every(c => !c) ? '-' : cs.map((c, i) => !c ? '-' : i === cs.length - 1 ? `**${f(c)}${unit}**` : f(c)).join(' -> ')).join(' | ')} |`);
  const single = TOWNS.filter((_, i) => cells[i].some(c => c?.runs === 1)).map(t => t[1]);
  if (single.length) out.push('', `Single run per side in ${single.join(', ')}: one 10-minute load, small differences are within noise.`);
  const bad = sides.flatMap(([n, rs]) => rs.filter(r => r.invalid).map(r => `${n} ${r.tag}: ${r.invalid}`));
  if (bad.length) out.push('', `\\* INVALID run (counts and worst kept, p99 and it/s n/a): ${bad.join('; ')}`);
  return out.join('\n');
}
export function soakTable(runs: Run[]): string {
  const out = ['| Soak | town | min | >= 1 s | worst ms | >= 300 per 10 min | it/s |', '|---|---|---|---|---|---|---|'];
  for (const r of runs) out.push(`| ${r.tag}${r.invalid ? ' (INVALID)' : ''} | ${TOWNS.find(t => t[0] === townOf(r.tag))?.[1] ?? '?'} | ${r.n.mins.toFixed(1)} | ${r.n.n1000} | ${r.n.max.toFixed(0)} | ${r.n.per300.toFixed(1)} | ${r.invalid ? 'n/a' : r.n.its.toFixed(1)} |`);
  const bad = runs.filter(r => r.invalid).map(r => `${r.tag}: ${r.invalid}`);
  if (bad.length) out.push('', `INVALID: ${bad.join('; ')}`);
  out.push('', "min = minutes kept after hitch-ab.ts exclusions (first minute, harness steps), not the run length.");
  return out.join('\n');
}

function load(tag: string): Run {
  const r = runNums(tag);
  if (!r) throw new Error(`no profile run ${tag} (set TS3_REPO from a worktree)`);
  const n = r.nums.all;
  if (!n) throw new Error(`${tag} is an --ab run (arms ${Object.keys(r.nums).join(', ')}), not a release run`);
  return { tag, n, invalid: r.invalid };
}

function selfCheck() {
  const n = (per300: number, max: number, p99: number | null, its: number): Nums => ({ per300, per1000: 0, max, p99, its, n1000: 0, mins: 9 });
  const c = side([{ tag: 'a', n: n(4, 300, 70, 60), invalid: null }, { tag: 'b', n: n(6, 500, 90, 50), invalid: 'noisy machine' }])!;
  assert.deepEqual(c, { per300: 5, max: 500, p99: 70, its: 60, bad: true, runs: 2 });
  assert.equal(side([{ tag: 'a', n: n(4, 300, 70, 60), invalid: 'x' }])!.p99, null);
  assert.equal(townOf('q-2026-10-24-ip-rvb-rel015'), 'ip');
  const t = table([['v', [{ tag: 'x-bp-v', n: n(4.8, 441, 79, 59), invalid: null }]], ['r', [{ tag: 'x-bp-r', n: n(5, 324, 79, 57), invalid: null }]]]);
  assert.match(t, /\| 4\.8 -> \*\*5\.0\*\* \| - \| - \|/);
  assert.match(t, /441 -> \*\*324 ms\*\*/);
  assert.match(t, /Single run per side in Bridgeport/);
  console.log('release-numbers self-check ok');
}

if (import.meta.main) {
  const argv = process.argv.slice(2);
  if (argv.includes('--self-check')) selfCheck();
  else {
    const lists: Record<string, string[]> = {}; let cur = '';
    for (const a of argv) if (a.startsWith('--')) cur = a.slice(2), lists[cur] ??= []; else if (cur) lists[cur].push(...a.split(',').filter(Boolean));
    if (!lists.vanilla?.length || !lists.prev?.length || !lists.rel?.length) {
      console.log('usage: node tools/harness/release-numbers.ts --vanilla <tags> --prev <tags> --rel <tags> [--soak <tags>]'); process.exit(2);
    }
    console.log(table([['no fixes', lists.vanilla.map(load)], ['previous release', lists.prev.map(load)], ['this release', lists.rel.map(load)]]));
    if (lists.soak?.length) console.log(`\n${soakTable(lists.soak.map(load))}`);
  }
}
