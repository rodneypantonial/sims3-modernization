// Probe P10 report (docs/87 §6): the sim thread's sync route plans by route IC, and gate-10 waits by IC (P1b row 6);
// with counters v40 also design A10's per-IC jobs (a10Report, docs/87 §5).
//   node tools/harness/p10-report.ts probe-out/queue/<queue>-<date>-<job>-native.json [...]
//   node tools/harness/p10-report.ts --self-check
// Reads the queue's native counter dump ({before, after}, ts3native counters v38; A10 part v40) and uses after - before.
// Pass: P10 installed (p10Ready bit 0), some IC entries, early returns and CanYield entries never above the entries.
import { readFileSync } from 'node:fs';

type Counters = Record<string, number>;
const IC = ['Plan', 'Replan', 'PlanFromPoint', 'ReplanFromPosition'];
const IC_ADDR: Record<number, string> = { 0x765160: 'Plan', 0x765490: 'Replan', 0x765240: 'PlanFromPoint', 0x765550: 'ReplanFromPosition' };
const d = (b: Counters, a: Counters, k: string) => (a[k] ?? 0) - (b[k] ?? 0);

export function report(before: Counters, after: Counters) {
  const lines: string[] = [];
  const ready = after.p10Ready ?? 0;
  let ok = (ready & 1) === 1, total = 0;
  lines.push(`p10Ready ${ready} (P10 ${ready & 1 ? 'on' : 'OFF'}, G10-C ${ready & 2 ? 'on' : 'off'})`);
  for (const ic of IC) {
    const n = d(before, after, `p10Entries${ic}`), us = d(before, after, `p10PlanUs${ic}`), early = d(before, after, `p10EarlyRet${ic}`);
    const y = ic === 'Plan' ? null : d(before, after, `p10CanYield${ic}`);
    total += n;
    if (early > n || (y !== null && y > n)) ok = false;
    lines.push(`  ${ic.padEnd(18)} entries ${n}, plan ${(us / 1000).toFixed(1)} ms, early returns ${early}` +
      (y === null ? ' (yield: see rfb* / asyncRefused)' : `, CanYield ${y} (${n ? ((100 * y) / n).toFixed(0) : 0}%)`));
  }
  if (!total) ok = false;
  for (let i = 0; i < 32; i++) {
    const key = after[`p1bKey_${i}`] >>> 0;
    if (key >>> 24 !== 6) continue;
    const second = key & 0xffffff;
    lines.push(`  gate 10 <- ${IC_ADDR[second] ?? '0x' + second.toString(16).toUpperCase()}: waits ${d(before, after, `p1bN_${i}`)}, ` +
      `${(d(before, after, `p1bUs_${i}`) / 1000).toFixed(1)} ms`);
  }
  return { ok, lines };
}

// A10 (docs/87 §5, counters v40): the Replan-family jobs per IC. Pass: hooks in place, no kind mismatch or orphan collect,
// collected never above started; Mode 0 (shadow): nothing started.
export function a10Report(before: Counters, after: Counters) {
  const lines: string[] = [];
  const ready = after.arReady ?? 0, mode = after.arMode ?? 0;
  const mismatch = d(before, after, 'arMismatch'), orphan = d(before, after, 'arOrphan');
  let ok = ready !== 0 && mismatch === 0 && orphan === 0;
  lines.push(`arReady ${ready} (hooked: ${IC.slice(1).filter((_, i) => ready >> i & 1).join(', ') || 'none'}), arMode ${mode} at the end, ` +
    `kind mismatches ${mismatch}, orphan collects ${orphan}`);
  for (const ic of IC.slice(1)) {
    const v = (n: string) => d(before, after, `ar${n}${ic}`);
    if (v('Collected') > v('Started')) ok = false;
    lines.push(`  ${ic.padEnd(18)} started ${v('Started')}, collected ${v('Collected')} (${(v('JobUs') / 1000).toFixed(1)} ms moved), ` +
      `refused ${v('Refused')} (route followed ${v('Active')}), dropped ${v('Dropped')}, shadow ${v('Shadow')}, ` +
      `sync ${v('SyncN')} plans ${(v('SyncUs') / 1000).toFixed(1)} ms`);
  }
  return { ok, lines };
}

if (process.argv[1] && import.meta.filename === process.argv[1]) {
  const args = process.argv.slice(2);
  if (args[0] === '--self-check') {
    const b: Counters = { p10EntriesReplan: 10 }, a: Counters = {
      p10Ready: 1, p10EntriesPlan: 5, p10EntriesReplan: 110, p10PlanUsReplan: 50000, p10EarlyRetReplan: 20, p10CanYieldReplan: 50,
      p1bKey_0: (6 << 24) | 0x765490, p1bN_0: 3, p1bUs_0: 21000, p1bKey_1: (5 << 24) | 0x62f3d0, p1bN_1: 9,
    };
    const r = report(b, a);
    const rp = r.lines.find(l => l.includes('Replan ') && l.includes('entries'));
    if (!r.ok || !rp?.includes('entries 100, plan 50.0 ms, early returns 20, CanYield 50 (50%)') ||
        !r.lines.some(l => l.includes('gate 10 <- Replan: waits 3, 21.0 ms')) || r.lines.some(l => l.includes('0x62F3D0')))
      throw new Error('p10-report self-check failed\n' + r.lines.join('\n'));
    a.p10EarlyRetReplan = 200;
    if (report(b, a).ok) throw new Error('p10-report self-check: early returns above entries must fail');
    const ab: Counters = { arStartedReplanFromPosition: 5 }, aa: Counters = { arReady: 7, arMode: 1, arStartedReplanFromPosition: 105,
      arCollectedReplanFromPosition: 90, arJobUsReplanFromPosition: 3400000, arActiveReplanFromPosition: 4, arRefusedReplanFromPosition: 6 };
    const ar = a10Report(ab, aa);
    if (!ar.ok || !ar.lines.some(l => l.includes('ReplanFromPosition') && l.includes('started 100, collected 90 (3400.0 ms moved), refused 6 (route followed 4)')))
      throw new Error('p10-report self-check (A10) failed\n' + ar.lines.join('\n'));
    aa.arMismatch = 1;
    if (a10Report(ab, aa).ok) throw new Error('p10-report self-check: a kind mismatch must fail');
    console.log('p10-report self-check ok');
  } else {
    let ok = true;
    for (const f of args) {
      const j = JSON.parse(readFileSync(f, 'utf8'));
      const before: Counters = j.before ?? {}, after: Counters = j.after ?? j;
      // P10 part when P10 ran (or nothing else did), A10 part when its hooks were in place
      const parts = [(after.p10Ready ?? 0) & 1 || !after.arReady ? report(before, after) : null, after.arReady ? a10Report(before, after) : null]
        .filter(r => r !== null);
      console.log(`== ${f}\n${parts.flatMap(r => r.lines).join('\n')}`);
      ok &&= parts.every(r => r.ok);
    }
    process.exitCode = ok ? 0 : 1;
  }
}
