// [UnseenProbe] / [UnseenThrottle] report (docs/143, counters v79, phase 2 v80; the probe was v78 on its first branch): sim-thread locomotion work per second, visible vs unseen agents.
//   node tools/harness/unseen-report.ts probe-out/queue/<queue>-<date>-<job>-native.json [...] [--seconds 600]
//   node tools/harness/unseen-report.ts --self-check
// Reads the queue's native counter dump ({before, after}) and uses after - before over --seconds (the profile length).
// Pass: probe installed (unReady bit 0), agent updates classified, camera read on most passes.
import { readFileSync } from 'node:fs';

type Counters = Record<string, number>;
const CLS = ['visible', 'unseen', 'outside'];
// utWhy_<i> order (unseen.cpp UTW_*): why an obstacle-list call ran
const UT_WHY = ['off', 'no agent', 'visible', 'EA byte', 'state', 'linked', 'viewer', 'no viewers', 'after', 'turn', 'near goal',
  'portal', 'path'];
// phase 2 (v80): utAvWhy_<i> / utRateWhy_<i> = UT_WHY then these; utFpWhy_<i> = UT_FP_WHY
const UT_WHY2 = [...UT_WHY, 'not a Sim', 'timer', 'new path', 'step cap', 'slot'];
const UT_FP_WHY = ['off', 'pass', 'no class', 'visible', 'not a Sim', 'viewer', 'no viewers', 'parked', 'change', 'due', 'full', 'rider'];
const ms = (us: number, s: number) => (us / 1000 / s).toFixed(2);

export function report(b: Counters, a: Counters, seconds: number) {
  const d = (k: string) => (a[k] ?? 0) - (b[k] ?? 0);
  const lines: string[] = [];
  const ready = a.unReady ?? 0, passes = d('unPasses');
  const agents = [0, 1].map(c => d(`unAgents_${c}_0`) + d(`unAgents_${c}_1`));
  const total = agents[0] + agents[1];
  const ok = (ready & 1) === 1 && total > 0 && d('unCamFail') <= passes / 10;
  lines.push(`unReady 0x${ready.toString(16)} (footprint ${ready & 2 ? 'on' : 'off'}, plans ${ready & 4 ? 'on' : 'off'}, keyed lock ${ready & 8 ? 'on' : 'off'}), ` +
    `${passes} passes (${(passes / seconds).toFixed(1)}/s), camera fails ${d('unCamFail')}, not classified ${d('unSkipped')}, ` +
    `class flips ${d('unFlips')} (${(d('unFlips') / seconds).toFixed(2)}/s), last reach ${((a.unReachX10 ?? 0) / 10).toFixed(0)} m`);
  lines.push(`unseen share of agent updates: ${total ? ((100 * agents[1]) / total).toFixed(1) : 0}% ` +
    `(per pass: ${passes ? (agents[0] / passes).toFixed(1) : 0} visible, ${passes ? (agents[1] / passes).toFixed(1) : 0} unseen); ` +
    `EA off-screen byte 0 in ${[0, 1].map(c => `${agents[c] ? ((100 * d(`unEaOff_${c}`)) / agents[c]).toFixed(0) : 0}% of ${CLS[c]}`).join(', ')}`);
  for (const c of [0, 1]) {
    lines.push(`  ${CLS[c].padEnd(8)} updates Sim ${d(`unAgents_${c}_0`)} ${ms(d(`unAgentUs_${c}_0`), seconds)} ms/s, ` +
      `other ${d(`unAgents_${c}_1`)} ${ms(d(`unAgentUs_${c}_1`), seconds)} ms/s, longest ${((a[`unAgentMaxUs_${c}`] ?? 0) / 1000).toFixed(1)} ms`);
  }
  for (const c of [0, 1, 2]) {
    const acq = [0, 1, 2].map(g => (d(`unAcq_${c}_${g}`) / seconds).toFixed(1));
    const w = [0, 1, 2, 3].map(g => `${d(`unWaitN_${c}_${g}`)} / ${ms(d(`unWaitUs_${c}_${g}`), seconds)}`);
    lines.push(`  ${CLS[c].padEnd(8)} +0x918 acquires/s steering ${acq[0]}, avoidance ${acq[1]}, other ${acq[2]}; ` +
      `waits n / ms/s steering ${w[0]}, avoidance ${w[1]}, footprint ${w[2]}, other ${w[3]}`);
  }
  lines.push(`  footprint updates (n / ms/s): ${[0, 1, 2].map(c => `${CLS[c]} ${d(`unFp_${c}`)} / ${ms(d(`unFpUs_${c}`), seconds)}`).join(', ')}`);
  for (const k of [0, 1])
    lines.push(`  plans ${k ? 'car-only' : 'other   '} (n / ms/s): ` +
      [0, 1, 2].map(c => `${c === 2 ? 'unknown' : CLS[c]} ${d(`unPlan_${k}_${c}`)} / ${ms(d(`unPlanUs_${k}_${c}`), seconds)}`).join(', '));
  if ((a.utReady ?? 0) & 1) {
    const calls = d('utCalls'), skipped = d('utSkipped');
    lines.push(`throttle: utReady 0x${(a.utReady ?? 0).toString(16)}, every ${a.utEvery ?? 0}, mode ${a.utMode ?? 0}, obstacle-list calls ` +
      `${calls} (${(calls / seconds).toFixed(1)}/s), skipped ${skipped} (${calls ? ((100 * skipped) / calls).toFixed(1) : 0}%), ` +
      `path tests ${ms(d('utPathUs'), seconds)} ms/s, viewer tables ${d('utViewerPushes')} (last ${a.utViewers ?? 0}, bad ${d('utViewerBad')})`);
    lines.push(`  ran because: ${UT_WHY.map((w, i) => [w, d(`utWhy_${i}`)] as const).filter(([, n]) => n).map(([w, n]) => `${w} ${n}`).join(', ') || 'none'}`);
  }
  const why = (names: string[], key: string) =>
    names.map((w, i) => [w, d(`${key}_${i}`)] as const).filter(([, n]) => n).map(([w, n]) => `${w} ${n}`).join(', ') || 'none';
  const pct = (n: number, of: number) => (of ? ((100 * n) / of).toFixed(1) : '0');
  const r2 = a.ut2Ready ?? 0;
  if (r2 & 1) {
    const calls = d('utAvCalls'), skipped = d('utAvSkipped');
    lines.push(`avoidance: mode ${a.utAvMode ?? 0}, calls ${calls} (${(calls / seconds).toFixed(1)}/s), skipped ${skipped} (${pct(skipped, calls)}%); ran because: ${why(UT_WHY2, 'utAvWhy')}`);
  }
  if (r2 & 2) {
    const calls = d('utFpCalls'), skipped = d('utFpSkipped');
    lines.push(`footprint: mode ${a.utFpMode ?? 0}${r2 & 4 ? ' (below design U)' : ''}, calls ${calls}, held back ${skipped} (${pct(skipped, calls)}%), ` +
      `folded ${d('utFpCoalesced')}, flushed ${d('utFpFlushed')}, gone ${d('utFpGone')}, flush ${ms(d('utFpFlushUs'), seconds)} ms/s, ` +
      `most pending ${a.utFpPendMax ?? 0}; ran because: ${why(UT_FP_WHY, 'utFpWhy')}`);
  }
  if (r2 || (a.utReady ?? 0) & 1) {
    const parts = ['list', 'avoidance', 'footprint', 'rate'];
    lines.push(`snap-backs (became visible after a skip, n / largest owed ms): ${parts.map((p, i) => `${p} ${d(`utSnap_${i}`)} / ${a[`utSnapMaxMs_${i}`] ?? 0}`).join(', ')}`);
  }
  if (r2 & 8) {
    const calls = d('utRateCalls'), skipped = d('utRateSkipped');
    lines.push(`rate: mode ${a.utRateMode ?? 0}, updates looked at ${calls}, skipped ${skipped} (${pct(skipped, calls)}%), ` +
      `catch-up runs ${d('utRateCatchUp')}, largest step ${((a.utRateDebtMaxUs ?? 0) / 1000).toFixed(0)} ms, debts dropped ${d('utRateDropped')}; ` +
      `ran because: ${why(UT_WHY2, 'utRateWhy')}`);
  }
  return { ok, lines };
}

if (process.argv[1] && import.meta.filename === process.argv[1]) {
  const args = process.argv.slice(2);
  const si = args.indexOf('--seconds');
  const seconds = si >= 0 ? Number(args.splice(si, 2)[1]) : 600;
  if (args[0] === '--self-check') {
    const b: Counters = { unPasses: 100 }, a: Counters = {
      unReady: 15, unPasses: 6100, unCamFail: 2, unAgents_0_0: 30000, unAgents_1_0: 90000, unAgents_1_1: 30000,
      unAgentUs_1_0: 1_200_000, unAcq_1_1: 6000, unWaitN_1_1: 300, unWaitUs_1_1: 3_000_000, unFp_1: 50, unFpUs_1: 60000,
      unPlan_1_1: 12, unPlanUs_1_1: 600_000, unReachX10: 800, unEaOff_1: 60000,
      utReady: 3, utEvery: 4, utMode: 1, utCalls: 1000, utSkipped: 600, utWhy_2: 300, utWhy_9: 100, utPathUs: 60000, utViewerPushes: 50, utViewers: 3,
      ut2Ready: 15, utAvMode: 1, utAvCalls: 800, utAvSkipped: 600, utAvWhy_9: 200, utFpMode: 1, utFpCalls: 100, utFpSkipped: 75,
      utFpCoalesced: 40, utFpFlushed: 30, utFpWhy_9: 25, utRateMode: 1, utRateCalls: 1000, utRateSkipped: 700, utRateWhy_9: 250,
      utRateWhy_16: 50, utRateCatchUp: 290, utRateDebtMaxUs: 170000, utSnap_3: 12, utSnapMaxMs_3: 125,
    };
    const r = report(b, a, 600).lines.join('\n');
    const want = ['unseen share of agent updates: 80.0%', 'per pass: 5.0 visible, 20.0 unseen', 'Sim 90000 2.00 ms/s',
      'avoidance 10.0', 'avoidance 300 / 5.00', 'unseen 50 / 0.10', 'car-only (n / ms/s): visible 0 / 0.00, unseen 12 / 1.00',
      'byte 0 in 0% of visible, 50% of unseen', 'skipped 600 (60.0%), path tests 0.10 ms/s', 'ran because: visible 300, turn 100',
      'avoidance: mode 1, calls 800 (1.3/s), skipped 600 (75.0%); ran because: turn 200',
      'footprint: mode 1 (below design U), calls 100, held back 75 (75.0%), folded 40, flushed 30', 'ran because: due 25',
      'rate: mode 1, updates looked at 1000, skipped 700 (70.0%), catch-up runs 290, largest step 170 ms', 'turn 250, step cap 50', 'rate 12 / 125'];
    const miss = want.filter(w => !r.includes(w));
    if (miss.length || !report(b, a, 600).ok) throw new Error(`unseen-report self-check failed (${miss.join('; ')})\n${r}`);
    if (report(b, { ...a, unReady: 0 }, 600).ok) throw new Error('unseen-report self-check: probe off must fail');
    console.log('unseen-report self-check ok');
  } else {
    let ok = true;
    for (const f of args) {
      const j = JSON.parse(readFileSync(f, 'utf8'));
      const r = report(j.before ?? {}, j.after ?? j, seconds);
      console.log(`== ${f}\n${r.lines.join('\n')}`);
      ok &&= r.ok;
    }
    process.exitCode = ok ? 0 : 1;
  }
}
