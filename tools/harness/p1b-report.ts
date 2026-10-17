// Probe P1b report (docs/84 §4.4-§4.6): gate-12 waits by caller family, the gate-0 key 0x62EF7A by its caller, T-L3 and T-L5.
//   node tools/harness/p1b-report.ts probe-out/queue/<queue>-<date>-<job>-native.json [...]
//   node tools/harness/p1b-report.ts --self-check
// Reads the queue's native counter dump ({before, after}, ts3native counters v32) and uses after - before.
// Pass (docs/84 §4.5 item 1, §4.6): keyed gate-12 waits <= rgJob_lotTableContains <= keyed + blockOther (blockOther also holds
// other overflowing keys, so the equality of §4.5 is the case of an overflow made only of gate-12 waits; bp-env-on: 791 <=
// 793 <= 955), and P1b keyed at least those waits again, at most rgJob (P1b also sees non-owner threads' waits).
import { readFileSync } from 'node:fs';

type Counters = Record<string, number>;
const GATE12: Record<number, string> = { 0x62f782: 'PortalsForObject', 0x62fb74: 'GetIfPresent', 0x631814: 'FrontDoor', 0x631f40: 'FootprintsNearPoint' };
const FIRST = [0x62f782, 0x62fb74, 0x631814, 0x631f40, 0x62ef7a];  // p1bKey >> 24 = index + 1
const FAMILY: Record<number, string> = {
  0x80cf36: 'F-A', 0x80d798: 'F-A', 0x806b1c: 'F-A', 0x80b163: 'F-A', 0x80c29a: 'F-A', 0x76f15d: 'F-B', 0x6317a3: 'F-E',
  0x757fae: 'F-D', 0x7581a6: 'F-D', 0x7582ad: 'F-D', 0x82d706: 'F-D', 0x74b4b5: 'F-D',
  0x834201: 'F-F', 0x834cf6: 'F-F', 0x7558dc: 'F-F', 0x8238c5: 'F-F',
};
const hex = (n: number) => '0x' + (n >>> 0).toString(16).toUpperCase();
const d = (b: Counters, a: Counters, k: string) => (a[k] ?? 0) - (b[k] ?? 0);

export function report(before: Counters, after: Counters) {
  const lines: string[] = [];
  // P1: blockKey tables keyed by the leaf's caller (keys keep their slot, so after's key with the N/Us difference)
  let p1 = 0;
  for (let i = 0; i < 32; i++) if (GATE12[after[`blockKey_${i}`] >>> 0]) p1 += d(before, after, `blockN_${i}`);
  const other = d(before, after, 'blockOther'), job = d(before, after, 'rgJob_lotTableContains');
  const p1ok = p1 <= job && job <= p1 + other;
  lines.push(`P1: gate-12 keyed waits ${p1} <= rgJob_lotTableContains ${job} <= keyed + blockOther ${p1 + other}: ${p1ok ? 'PASS' : 'FAIL'}`);
  // P1b: second frames
  const fam: Record<string, { n: number; us: number }> = {};
  let p1b = 0;
  const rows: string[] = [];
  for (let i = 0; i < 32; i++) {
    const key = after[`p1bKey_${i}`] >>> 0;
    if (!key) continue;
    const n = d(before, after, `p1bN_${i}`), us = d(before, after, `p1bUs_${i}`);
    const first = FIRST[(key >>> 24) - 1], second = key & 0xffffff;
    const f = first === 0x62ef7a ? 'gate 0' : first === 0x631814 ? 'F-C' : first === 0x631f40 ? (FAMILY[second] ?? 'F-G') : (FAMILY[second] ?? 'other');
    if (first !== 0x62ef7a) p1b += n;
    (fam[f] ??= { n: 0, us: 0 }).n += n;
    fam[f].us += us;
    rows.push(`  ${hex(first)} <- ${second === 0xffffff ? 'outside the exe' : hex(second)}  ${f}  waits ${n}  ${(us / 1000).toFixed(1)} ms`);
  }
  const noFrame = d(before, after, 'p1bNoFrame'), p1bOther = d(before, after, 'p1bOther');
  const s = p1b + noFrame + p1bOther, p1bok = p1 <= s && s <= job;
  lines.push(`P1b: P1 ${p1} <= gate-12 waits keyed ${p1b} + no frame ${noFrame} + table full ${p1bOther} <= rgJob ${job}: ${p1bok ? 'PASS' : 'FAIL'}`);
  const total = Object.entries(fam).filter(([f]) => f !== 'gate 0').reduce((s, [, v]) => s + v.us, 0) || 1;
  for (const [f, v] of Object.entries(fam).sort((x, y) => y[1].us - x[1].us))
    lines.push(`  ${f}: waits ${v.n}, ${(v.us / 1000).toFixed(1)} ms${f === 'gate 0' ? '' : `, ${((100 * v.us) / total).toFixed(0)}% of gate 12`}`);
  lines.push(...rows);
  const ref = ['AddRef', 'Release'].map(o => ['Sim', 'WorkerInQl', 'WorkerNoQl', 'Other'].map(t => `${t} ${d(before, after, `portalRef${o}${t}`)}`).join(', '));
  lines.push(`T-L3 portal AddRef during jobs: ${ref[0]}; Release: ${ref[1]} (L2 needs WorkerNoQl = 0)`);
  lines.push(`T-L5 caSimInJob: ${['FEB848', 'FEB970', 'FEBD10', 'FEBE38'].map(c => `${c} ${d(before, after, `caSimInJob${c}`)}`).join(', ')}`);
  return { ok: p1ok && p1bok, lines };
}

if (process.argv[1] && import.meta.filename === process.argv[1]) {
  const args = process.argv.slice(2);
  if (args[0] === '--self-check') {
    const b: Counters = {}, a: Counters = {
      blockKey_0: 0x62f782, blockN_0: 10, blockKey_1: 0x631814, blockN_1: 2, blockKey_2: 0x85fe10, blockN_2: 50, blockOther: 1,
      rgJob_lotTableContains: 13, p1bKey_0: (1 << 24) | 0x80cf36, p1bN_0: 7, p1bUs_0: 70000, p1bKey_1: (1 << 24) | 0x76f15d, p1bN_1: 3,
      p1bUs_1: 9000, p1bKey_2: (3 << 24) | 0x788373, p1bN_2: 2, p1bUs_2: 40000, p1bKey_3: (5 << 24) | 0x62f3d0, p1bN_3: 4, p1bUs_3: 1000,
    };
    const r = report(b, a);
    const fa = r.lines.find(l => l.startsWith('  F-A:')), fc = r.lines.find(l => l.startsWith('  F-C:'));
    if (!r.ok || !fa?.includes('waits 7, 70.0 ms, 59%') || !fc?.includes('waits 2') || !r.lines[0].includes('12 <= rgJob_lotTableContains 13 <= keyed + blockOther 13')) throw new Error('p1b-report self-check failed\n' + r.lines.join('\n'));
    a.rgJob_lotTableContains = 11;
    if (report(b, a).ok) throw new Error('p1b-report self-check: a P1 gap must fail');
    console.log('p1b-report self-check ok');
  } else {
    let ok = true;
    for (const f of args) {
      const j = JSON.parse(readFileSync(f, 'utf8'));
      const r = report(j.before ?? {}, j.after ?? j);
      console.log(`== ${f}\n${r.lines.join('\n')}`);
      ok &&= r.ok;
    }
    process.exitCode = ok ? 0 : 1;
  }
}
