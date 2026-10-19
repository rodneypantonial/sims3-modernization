// [PlanRepeat] report (docs/23 §18): how often a route plan repeats with identical inputs and an unchanged routing world,
// and the plan time those repeats cost (the upper bound of an on-thread plan cache's savings).
//   node tools/harness/plan-repeat-report.ts probe-out/queue/plan-repeat-<date>-<job>-native.json [...] [--seconds 600]
//   node tools/harness/plan-repeat-report.ts --self-check
// Reads the queue's native dump ({before, after}, counters v60 pr*) and uses after - before over --seconds (the profile
// length, default 600). Decision rule (docs/23 §18): build the cache only if sim-thread exact repeats save >= 5 ms/s.
import { readFileSync } from 'node:fs';
import { parseArgs } from 'node:util';

type Counters = Record<string, number>;
const CLS = ['Plan', 'Replan', 'PlanFromPoint', 'ReplanFromPosition', 'sim, no route IC', 'other thread'];
const WIN = ['1 s', '5 s', '30 s'];
const SIM = [0, 1, 2, 3, 4];
const pct = (a: number, b: number) => (b > 0 ? `${((100 * a) / b).toFixed(1)}%` : 'n/a');

export function report(b: Counters, a: Counters, seconds: number) {
  const d = (k: string) => (a[k] ?? 0) - (b[k] ?? 0);
  const lines: string[] = [];
  const ready = a.prReady ?? 0;
  const ok = (a.version ?? 0) >= 60 && ready === 3;
  lines.push(`prReady ${ready} (counting ${ready & 1 ? 'on' : 'OFF'}, world version ${ready & 2 ? 'on' : 'MISSING'}), ring ${a.prRing ?? 0}, ` +
    `evicted < 30 s ${d('prEvictYoung')}, faults ${d('prFault')}, unwound ${d('prUnwound')}, ${seconds} s`);
  const sum = (k: string, cls: number[]) => cls.reduce((s, c) => s + d(`${k}_${c}`), 0);
  for (let c = 0; c < CLS.length; c++) {
    const n = d(`prPlans_${c}`);
    if (!n) continue;
    const ex = WIN.map((_, w) => d(`prExact_${c}_${w}`)), exUs = WIN.map((_, w) => d(`prExactUs_${c}_${w}`));
    lines.push(`  ${CLS[c].padEnd(19)} plans ${n} (${(d(`prUs_${c}`) / 1000 / seconds).toFixed(1)} ms/s); exact ` +
      WIN.map((w, i) => `<=${w} ${ex[i]} (${pct(ex[i], n)}, ${(exUs[i] / 1000 / seconds).toFixed(2)} ms/s)`).join(', ') +
      `; same answer ${d(`prMatch_${c}`)}, other answer ${d(`prMismatch_${c}`)}; footprints-only ${d(`prFpOnly_${c}`)} ` +
      `(same answer ${d(`prFpOnlyMatch_${c}`)}, ${(d(`prFpOnlyUs_${c}`) / 1000 / seconds).toFixed(2)} ms/s); world changed ${d(`prChanged_${c}`)}; ` +
      `loose key ${d(`prLoose_${c}`)} (${(d(`prLooseUs_${c}`) / 1000 / seconds).toFixed(2)} ms/s)`);
  }
  const plans = sum('prPlans', SIM), planMs = sum('prUs', SIM) / 1000 / seconds;
  const ex30 = SIM.reduce((s, c) => s + d(`prExact_${c}_2`), 0), save = SIM.reduce((s, c) => s + d(`prExactUs_${c}_2`), 0) / 1000 / seconds;
  const mis = sum('prMismatch', SIM), fpSave = sum('prFpOnlyUs', SIM) / 1000 / seconds, fpMatch = sum('prFpOnlyMatch', SIM), fp = sum('prFpOnly', SIM);
  lines.push(`sim thread: plans ${plans} (${planMs.toFixed(1)} ms/s), exact repeats <= 30 s ${ex30} (${pct(ex30, plans)}), ` +
    `savings bound ${save.toFixed(2)} ms/s; answer changed on ${mis} exact repeats (${pct(mis, ex30)}); ` +
    `footprints-only repeats ${fp} (${fpSave.toFixed(2)} ms/s, ${pct(fpMatch, fp)} same answer)`);
  const verdict = !ok ? 'OPEN (probe incomplete)' : save >= 5 ? 'BUILD the plan cache (>= 5 ms/s)' : 'NO cache (< 5 ms/s)';
  lines.push(`verdict: ${verdict}` + (ok && mis > 0.01 * ex30 ? '; WARN: > 1% of exact repeats changed answer (nondeterminism or an input the key or version misses)' : '') +
    (d('prEvictYoung') > 0 ? '; the 30 s column is a lower bound (ring evictions)' : ''));
  return { ok, save, lines };
}

if (process.argv[1] && import.meta.filename === process.argv[1]) {
  const { values, positionals } = parseArgs({ allowPositionals: true, options: { 'self-check': { type: 'boolean' }, seconds: { type: 'string', default: '600' } } });
  if (values['self-check']) {
    const b: Counters = { prPlans_1: 100 };
    const a: Counters = {
      version: 60, prReady: 3, prRing: 4096, prPlans_1: 6100, prUs_1: 36000000, prExact_1_0: 600, prExact_1_1: 1500, prExact_1_2: 3000,
      prExactUs_1_0: 600000, prExactUs_1_1: 1500000, prExactUs_1_2: 3600000, prMatch_1: 2990, prMismatch_1: 10, prFpOnly_1: 900,
      prFpOnlyMatch_1: 450, prFpOnlyUs_1: 1200000, prPlans_5: 7, prExact_5_2: 7, prExactUs_5_2: 99000000,
    };
    const r = report(b, a, 600);
    const rp = r.lines.find(l => l.includes('Replan'));
    if (!r.ok || Math.abs(r.save - 6) > 1e-9 || !rp?.includes('plans 6000 (60.0 ms/s)') || !rp.includes('<=30 s 3000 (50.0%, 6.00 ms/s)') ||
        !r.lines.some(l => l.startsWith('verdict: BUILD')) || r.lines.some(l => l.includes('WARN')))
      throw new Error('plan-repeat-report self-check failed\n' + r.lines.join('\n'));
    a.prExactUs_1_2 = 2400000;  // 4 ms/s; the other-thread plans never count
    a.prMismatch_1 = 100;
    const r2 = report(b, a, 600);
    if (!r2.lines.some(l => l.startsWith('verdict: NO cache') && l.includes('WARN'))) throw new Error('plan-repeat-report self-check (no) failed\n' + r2.lines.join('\n'));
    a.prReady = 1;
    if (report(b, a, 600).ok) throw new Error('plan-repeat-report self-check: a missing world version must not pass');
    console.log('plan-repeat-report self-check ok');
  } else {
    if (!positionals.length) { console.error('usage: node tools/harness/plan-repeat-report.ts <native.json> [...] [--seconds 600] | --self-check'); process.exit(2); }
    let ok = true;
    for (const f of positionals) {
      const j = JSON.parse(readFileSync(f, 'utf8'));
      const r = report(j.before ?? {}, j.after ?? j, Number(values.seconds));
      console.log(`== ${f}\n${r.lines.join('\n')}`);
      ok &&= r.ok;
    }
    process.exitCode = ok ? 0 : 1;
  }
}
