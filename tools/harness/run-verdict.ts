// Per-queue verdict table without reading logs by hand: one row per job (and per profile.repros window) with PASS / FAIL / VOID
// and the reason (verdict lines: self checks such as hook FAIL lines, asyncIdle and feature/fix activity, early noise stop, crash,
// timeout; plus hitch-ab/worst-hitches runInvalid for profile runs: profiler cut short, noisy machine, crash), and for profile jobs
// the hitch numbers (>= 300 / >= 1000 ms per 10 min, max, p99, it/s; A/B runs show off / on). Markdown on stdout. Read-only.
//   node tools/harness/run-verdict.ts <queue name> [--date YYYY-MM-DD] [--baseline <profile tag>]
//   node tools/harness/run-verdict.ts --self-check
// Inputs: probe-out/queue/<queue>-<date>.jsonl (queue.ts rows; newest date when --date is left out), probe-out/profile/<tag>-*.
// Set TS3_REPO=<main checkout> when running from a git worktree.
import assert from 'node:assert/strict';
import { existsSync, readdirSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import { armStats, loadRun, type Arm } from './hitch-ab.ts';

const ROOT = process.env.TS3_REPO ?? join(import.meta.dirname, '..', '..');
export const QUEUE_DIR = join(ROOT, 'probe-out', 'queue');

export const jsonlRows = (f: string): any[] => existsSync(f) ? readFileSync(f, 'utf8').split('\n').filter(Boolean).flatMap(l => { try { return [JSON.parse(l)]; } catch { return []; } }) : [];
// newest <queue>-<date>.jsonl of a queue in dir (date sorts as text)
export function queueFile(name: string, date?: string, dir = QUEUE_DIR): string | null {
  if (date) { const f = join(dir, `${name}-${date}.jsonl`); return existsSync(f) ? f : null; }
  const re = new RegExp(`^${name.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}-(\\d{4}-\\d\\d-\\d\\d)\\.jsonl$`);
  const f = readdirSync(dir).filter(x => re.test(x)).sort().at(-1);
  return f ? join(dir, f) : null;
}

export type Verdict = { v: 'PASS' | 'FAIL' | 'VOID'; why: string };
const VOID_RE = /run void|noisy machine|NOT comparable/;
// a queue row (or a repro sub-row) -> verdict; invalid = runInvalid reason of its profile run, if any
export function classify(row: any, invalid: string | null = null): Verdict {
  const lines: string[] = [...(row.verdict ?? []), ...(row.error ? [String(row.error)] : [])];
  const voids = lines.filter(l => VOID_RE.test(l));
  if (voids.length || invalid) return { v: 'VOID', why: [...voids, ...(invalid ? [`runInvalid: ${invalid}`] : [])].join('; ') };
  const bad = [row.crashed && `crash${row.crash ? `: ${String(row.crash).split('\n')[0].slice(0, 120)}` : ''}`, row.timedOut && 'timed out',
    ...lines.filter(l => /^(FAIL|STOP)\b|FAIL:|ERROR/.test(l))].filter(Boolean) as string[];
  const pass = row.pass ?? (row.exitCode === 0 && !bad.length);
  if (!pass) return { v: 'FAIL', why: bad.join('; ') || `exit code ${row.exitCode ?? '?'}` };
  return { v: 'PASS', why: lines.filter(l => /^ts3fixes activity|self check|mono ledger INCOMPLETE/.test(l)).join('; ') };
}

// hitch numbers of a profile arm: per 10 min >= 300 / >= 1000, max, p99, it/s
export function armNums(a: Arm) {
  const s = armStats(a);
  return { per300: s.per10[1], per1000: s.per10[3], max: s.max, p99: s.p99.bounded ? null : s.p99.ms, its: s.mins ? s.iters / (s.mins * 60) : 0, n1000: s.counts[3], mins: s.mins };
}
type Nums = ReturnType<typeof armNums>;
export function runNums(tag: string): { nums: Record<string, Nums>; invalid: string | null } | null {
  let r; try { r = loadRun(tag); } catch { return null; } // no profile files (not a profile job, or the run died early)
  return { nums: Object.fromEntries(Object.entries(r.arms).map(([k, a]) => [k, armNums(a)])), invalid: r.invalid };
}
const f1 = (x: number) => x.toFixed(1), p99s = (x: number | null) => x === null ? '<50' : x.toFixed(0);
const NUM_COLS: [string, (n: Nums) => string][] = [['>=300/10min', n => f1(n.per300)], ['>=1000/10min', n => f1(n.per1000)], ['max ms', n => n.max.toFixed(0)], ['p99 ms', n => p99s(n.p99)], ['it/s', n => f1(n.its)]];
// one cell per number: 'all' arm, or 'off / on' for an --ab run
// an arm with no kept seconds (whole window excluded, e.g. repro-triggered) shows '-'
const numCells = (n: Record<string, Nums> | undefined) => NUM_COLS.map(([, f]) => { const g = (x: Nums) => x.mins ? f(x) : '-'; return !n ? '' : n.all ? g(n.all) : n.off && n.on ? `${g(n.off)} / ${g(n.on)}` : ''; });
const cell = (s: string) => s.replace(/\|/g, '\\|').replace(/\n/g, ' ').slice(0, 300);

export function verdictTable(file: string, baseline?: string): string {
  const rows = jsonlRows(file), base = baseline ? runNums(baseline) : null;
  const out = [`### ${file.replace(/^.*[\\/]/, '').replace(/\.jsonl$/, '')}`, '',
    `| Job | Verdict | Reason | min | ${NUM_COLS.map(c => c[0]).join(' | ')} |${baseline ? ` vs ${baseline} |` : ''}`,
    `|${'---|'.repeat(4 + NUM_COLS.length + (baseline ? 1 : 0))}`];
  const count = { PASS: 0, FAIL: 0, VOID: 0 };
  const add = (name: string, row: any, tag: string | null, mins: number | string) => {
    const r = tag ? runNums(tag) : null, v = classify(row, r?.invalid ?? null); count[v.v]++;
    let cmp = '';
    if (base && r) { const a = r.nums.all ?? r.nums.on, b = base.nums.all ?? base.nums.off; if (a && b) cmp = `>=300 ${f1(b.per300)} -> ${f1(a.per300)}, max ${b.max.toFixed(0)} -> ${a.max.toFixed(0)}, p99 ${p99s(b.p99)} -> ${p99s(a.p99)}${base.invalid ? ' (baseline INVALID)' : ''}`; }
    out.push(`| ${name} | ${v.v === 'PASS' ? 'PASS' : `**${v.v}**`} | ${cell(v.why) || '-'} | ${mins} | ${numCells(r?.nums).join(' | ')} |${baseline ? ` ${cmp} |` : ''}`);
  };
  for (const row of rows) {
    add(row.job, row, row.repros?.length ? null : row.profileTag ?? null, row.minutes ?? '-');
    for (const rr of row.repros ?? []) add(`${row.job} / ${rr.id}`, { ...rr, pass: rr.exitCode === 0 && !(rr.verdict ?? []).some((l: string) => /^FAIL/.test(l)) }, rr.tag, '');
  }
  out.push('', `${count.PASS} PASS, ${count.FAIL} FAIL, ${count.VOID} VOID of ${count.PASS + count.FAIL + count.VOID}. A/B runs: off / on. p99 '<50' = below the profiler's 50 ms hitch threshold.`);
  return out.join('\n');
}

function selfCheck() {
  assert.equal(classify({ pass: true, verdict: ['PASS: profile 10 min'] }).v, 'PASS');
  assert.equal(classify({ pass: false, exitCode: 1, verdict: ['FAIL: self check: [RouteAsync] on but 0 async route jobs started (invokeHook 0): async off, run void'] }).v, 'VOID');
  assert.equal(classify({ pass: false, exitCode: 1, verdict: ['FAIL: noisy machine early: machine load: ...'] }).v, 'VOID');
  assert.equal(classify({ pass: true, verdict: ['PASS'] }, 'profiler cut short (100 of 600 s)').v, 'VOID');
  const c = classify({ pass: false, crashed: true, crash: 'fault TS3W.exe+0x1234\nmore', verdict: [] });
  assert.equal(c.v, 'FAIL'); assert.match(c.why, /^crash: fault TS3W\.exe\+0x1234$/);
  assert.equal(classify({ pass: false, timedOut: true, verdict: [] }).why, 'timed out');
  assert.match(classify({ pass: false, verdict: ['PASS: x', 'FAIL: events not fired: 3am'] }).why, /events not fired/);
  assert.match(classify({ pass: true, verdict: ['PASS', 'ts3fixes activity: stuckcheckasync 12'] }).why, /ts3fixes activity/);
  assert.deepEqual(numCells({ all: { per300: 0, per1000: 0, max: 0, p99: null, its: 0, n1000: 0, mins: 0 } }), ['-', '-', '-', '-', '-']);
  assert.deepEqual(numCells({ off: { per300: 1, per1000: 0, max: 300, p99: null, its: 50, n1000: 0, mins: 1 }, on: { per300: 2, per1000: 0, max: 400, p99: 80, its: 60, n1000: 0, mins: 1 } }),
    ['1.0 / 2.0', '0.0 / 0.0', '300 / 400', '<50 / 80', '50.0 / 60.0']);
  console.log('run-verdict self-check ok');
}

if (import.meta.main) {
  const args = process.argv.slice(2), opt = (k: string) => { const i = args.indexOf(k); return i < 0 ? undefined : args.splice(i, 2)[1]; };
  if (args.includes('--self-check')) selfCheck();
  else {
    const date = opt('--date'), baseline = opt('--baseline'), name = args[0];
    if (!name) { console.log('usage: node tools/harness/run-verdict.ts <queue name> [--date YYYY-MM-DD] [--baseline <tag>]'); process.exit(2); }
    const f = queueFile(name, date);
    if (!f) { console.log(`no ${name}-${date ?? '<date>'}.jsonl in ${QUEUE_DIR} (set TS3_REPO from a worktree)`); process.exit(1); }
    console.log(verdictTable(f, baseline));
  }
}
