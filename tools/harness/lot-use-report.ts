// [RouteSnap] LotUseReadout report (docs/66 §7.1 A4 readout): reads the `lot use (...)` lines of ts3native logs and prints,
// per log, the last summary (exit, else the last memory sample), the top 20 clone lots by bytes with their route jobs and
// agents, the share of lot bytes on lots no job read, and what that decides for A4. Reads only.
//   node tools/harness/lot-use-report.ts [--filter <regex on log name>]   default: every *-ts3native.log with lot use lines
//   node tools/harness/lot-use-report.ts --self-check
// Set TS3_REPO=<main checkout> when running from a git worktree.
import { existsSync, readdirSync, readFileSync } from 'node:fs';
import { join } from 'node:path';

const ROOT = process.env.TS3_REPO ?? join(import.meta.dirname, '..', '..');
const QUEUE = join(ROOT, 'probe-out', 'queue');
// docs/66 §7.1: untouched share at or above this = "never copy untouched lots" (simple variant); below = full A4
export const SIMPLE_AT_PCT = 40;

export type Summary = { when: string; lots: number; kb: number; touched: number; untouched: number; untouchedKb: number; pct: number; jobs: number; text: string };
export type Block = { summary: Summary; top: string[] };

export function parse(text: string): Block[] {
  const out: Block[] = [];
  for (const l of text.split(/\r?\n/)) {
    const m = l.match(/lot use \((\w+)(?: #\d+)?\): (\d+) clone lots, (\d+) KB in lots.*route jobs (\d+) .*touched (\d+) lots, untouched (\d+) holding (\d+) KB = ([\d.]+)% of lot bytes/);
    if (m) {
      out.push({ summary: { when: m[1], lots: +m[2], kb: +m[3], jobs: +m[4], touched: +m[5], untouched: +m[6], untouchedKb: +m[7], pct: +m[8], text: l.slice(l.indexOf('lot use')) }, top: [] });
      continue;
    }
    if (/lot use: #\s*\d+\s+lot 0x/.test(l) && out.length) out[out.length - 1].top.push(l.slice(l.indexOf('lot use: #')));
  }
  return out;
}

export function verdict(s: Summary): string {
  if (!s.jobs) return 'no route jobs noted: no verdict (P6 / VerifyEvery jobs ran?)';
  return s.pct >= SIMPLE_AT_PCT
    ? `untouched ${s.pct}% >= ${SIMPLE_AT_PCT}%: build the simple variant first (never copy lots no job reads; rebase on first read)`
    : `untouched ${s.pct}% < ${SIMPLE_AT_PCT}%: the simple variant saves little; full A4 (idle lots freed after N sim-hours) or skip`;
}

function selfCheck() {
  const log = [
    'lot use (sample #1): 82 clone lots, 20480 KB in lots (footprints 12000, quadtree 6000, portals 480, other 2000; world graph not in the clone), no-lot map entries 12 KB; route jobs 300 (read set over 32 lots: 0); touched 20 lots, untouched 62 holding 12288 KB = 60.0% of lot bytes; walk 9000 us',
    'lot use: #1  lot 0x0000000100000002 900 KB (footprints 600, quadtree 250, portals 10, other 40) jobs 40 agents 12',
    'lot use (exit): 82 clone lots, 20480 KB in lots (footprints 12000, quadtree 6000, portals 480, other 2000; world graph not in the clone), no-lot map entries 12 KB; route jobs 410 (read set over 32 lots: 1); touched 30 lots, untouched 52 holding 6144 KB = 30.0% of lot bytes; walk 0 us',
  ].join('\n');
  const b = parse(log);
  if (b.length !== 2 || b[0].top.length !== 1 || b[0].summary.pct !== 60 || b[1].summary.when !== 'exit' || b[1].summary.jobs !== 410) throw new Error('parse');
  if (!verdict(b[0].summary).startsWith('untouched 60% >=') || !verdict(b[1].summary).startsWith('untouched 30% <')) throw new Error('verdict');
  console.log('lot-use-report self-check ok');
}

if (process.argv.includes('--self-check')) selfCheck();
else {
  const fi = process.argv.indexOf('--filter');
  const re = new RegExp(fi > 0 ? process.argv[fi + 1] : '-ts3native\\.log$');
  const logs = existsSync(QUEUE) ? readdirSync(QUEUE).filter((f) => re.test(f) && f.endsWith('.log')) : [];
  let any = false;
  for (const f of logs) {
    const blocks = parse(readFileSync(join(QUEUE, f), 'utf8'));
    if (!blocks.length) continue;
    any = true;
    const last = [...blocks].reverse().find((b) => b.summary.when === 'exit') ?? blocks[blocks.length - 1];
    const withTop = [...blocks].reverse().find((b) => b.top.length) ?? last;
    console.log(`== ${f}`);
    for (const b of blocks.filter((x) => x.summary.when !== 'exit')) console.log(`  ${b.summary.when}: untouched ${b.summary.pct}% (${b.summary.untouched}/${b.summary.lots} lots, ${b.summary.untouchedKb} KB), jobs ${b.summary.jobs}`);
    console.log(`  ${last.summary.text}`);
    for (const t of withTop.top) console.log(`  ${t}`);
    console.log(`  verdict: ${verdict(last.summary)}`);
  }
  if (!any) console.log(`no lot use lines in ${QUEUE} (filter ${re})`);
}
