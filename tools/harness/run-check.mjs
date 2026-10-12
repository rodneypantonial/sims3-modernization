// Regression gate for multi-world runs: compares one profile tag with a reference tag and exits 1 on an obvious
// regression, so a run script can stop before profiling the next world on a bad build.
//   node tools/harness/run-check.mjs <tag> <reference tag> [--max-stuck 3] [--min-ratio 0.9]
// Fails when stuck Sims exceed --max-stuck or sim iterations per second fall below --min-ratio x the reference.
import { existsSync, readdirSync, readFileSync } from 'node:fs';
import { join, resolve } from 'node:path';

const [tag, ref] = process.argv.slice(2);
const opt = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? Number(process.argv[i + 1]) : d; };
const DIR = join(resolve(import.meta.dirname, '..', '..'), 'probe-out', 'profile');
const iters = t => {
  const f = readdirSync(DIR).find(f => f.startsWith(`${t}-`) && /^\d+-seconds\.csv$/.test(f.slice(t.length + 1)));
  if (!f) return null;
  const rows = readFileSync(join(DIR, f), 'utf8').trim().split('\n').slice(1).map(l => Number(l.split(',')[0]));
  return rows.reduce((a, b) => a + b, 0) / (rows.length || 1);
};
const stuckFile = join(DIR, `${tag}-stuck.json`);
const stuck = existsSync(stuckFile) ? JSON.parse(readFileSync(stuckFile, 'utf8')) : [];
const a = iters(tag), b = iters(ref);
const problems = [];
if (stuck.length > opt('--max-stuck', 3)) problems.push(`${stuck.length} stuck Sims (${stuck.filter(s => s.shortcut).length} moved by shortcuts)`);
if (a != null && b != null && a < b * opt('--min-ratio', 0.9)) problems.push(`iterations/s ${a.toFixed(1)} vs ${b.toFixed(1)} in ${ref}`);
console.log(`${tag}: ${problems.length ? 'REGRESSION: ' + problems.join('; ') : `ok (iterations/s ${a?.toFixed(1)} vs ${b?.toFixed(1)}, ${stuck.length} stuck)`}`);
process.exit(problems.length ? 1 : 0);
