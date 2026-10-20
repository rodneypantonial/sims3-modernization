// Mono ledger probe report (docs/51 I-121-5, I-121-7, I-121-9, I-121-12, I-120-4, I-119-44; queue mono-ledger-probe.json).
//   node tools/harness/mono-ledger-report.ts probe-out/queue/<queue>-<date>-<job>-native.json [--minutes 10]
//   node tools/harness/mono-ledger-report.ts --self-check
// Reads the queue's /native dump ({before, after}): ts3native_autshadow v3 `ledger` (tools/probe/agent.js names the class
// and method pointers). Counts are after - before (the play window); the class and caller tables are cumulative, but the
// scope only opens once ts3fixes' switch autledger is on (profile.fixesOn, at the window start). queue.ts prints the same
// lines under the result table.
import { readFileSync } from 'node:fs';
import assert from 'node:assert/strict';

type Lg = {
  on: number; ticks: number; trackTicks: number; trackLast: number; scopes: number; scopeUs: number; scopeFull: number;
  in: number[]; out: number[]; slotIds: number; sleep0: number[]; sleep1: number[]; dropped: number;
  classes: { ic: string; klass: string; class: string | null; calls: number }[];
  callers: { ic: string; calls: number; frames: string[] }[];
};
const IC = ['rng', 'slotpos', 'getobjects', 'inrange'];
// docs/121 §4.1: the two RNG paths the static closure found inside meta scoring
const RNG_EXPECTED = [/GetBFFOrSimWithHighestLikingForPet/, /FillInTokens/];

const sub = (a: number[], b: number[] | undefined) => a.map((x, i) => x - (b?.[i] ?? 0));
const hist = (h: number[]) => `0:${h[0]} 1:${h[1]} 2:${h[2]} 3+:${h[3]}`;
// frame 0 is the IC's own wrapper frame when the interpreter keeps one; show the managed callers above it
const chain = (f: string[]) => f.filter(n => !/::.*Impl$|\.Query_|\.Random_NextDoubleImpl|\.Slots_GetPositionOfSlotImpl/.test(n)).slice(0, 6).join(' <- ');

export function ledgerReport(before: any, after: any, minutes: number) {
  const a: Lg | undefined = after?.autshadow?.ledger, b: Lg | undefined = before?.autshadow?.ledger;
  if (!a) return { ok: false, lines: ['mono ledger: no autshadow v3 ledger in /native (asi older than counters v3, or [AutShadow] Enabled=0)'] };
  const lines: string[] = [];
  const inN = sub(a.in, b?.in), outN = sub(a.out, b?.out), scopes = a.scopes - (b?.scopes ?? 0);
  const ticks = a.ticks - (b?.ticks ?? 0), trackTicks = a.trackTicks - (b?.trackTicks ?? 0);
  const per = (n: number) => `${n} (${(n / Math.max(minutes, 1e-9)).toFixed(1)}/min, ${scopes ? (n / scopes).toFixed(2) : '-'}/scope)`;
  const ok = (a.on & 1) === 1 && (a.on & 2) === 2 && scopes > 0;
  lines.push(`mono ledger: on 0x${a.on.toString(16)} (ledger ${a.on & 1 ? 'on' : 'OFF'}, ticks ${a.on & 2 ? 'yes' : 'NO'}, scope ${a.on & 4 ? 'yes' : 'NO'}, frames ${a.on & 8 ? 'yes' : 'no'}, task ${a.on & 16 ? 'yes' : 'no'}); ` +
    `${scopes} meta scopes over ${minutes} min, ${((a.scopeUs - (b?.scopeUs ?? 0)) / 1000).toFixed(0)} ms inside (wall), full/unmatched ${a.scopeFull}, records dropped ${a.dropped}`);
  // I-121-5
  const rngCallers = a.callers.filter(c => c.ic === 'rng');
  const rngOther = rngCallers.filter(c => !RNG_EXPECTED.some(r => c.frames.some(f => r.test(f))));
  lines.push(`I-121-5 RNG: in meta scoring ${per(inN[0])}, elsewhere ${outN[0]}; caller chains ${rngCallers.length}, ` +
    `outside the two expected paths ${rngOther.length} (${rngOther.reduce((s, c) => s + c.calls, 0)} calls)`);
  for (const c of rngCallers.slice(0, 8)) lines.push(`  rng x${c.calls}: ${chain(c.frames)}`);
  // I-121-7, I-121-12
  for (const [id, ic, want] of [['I-121-7', 'getobjects', null], ['I-121-12', 'inrange', /(^|\.)Sim$/]] as const) {
    const cls = a.classes.filter(c => c.ic === ic), w = IC.indexOf(ic);
    const off = want ? cls.filter(c => !want.test(c.class ?? '')) : [];
    lines.push(`${id} ${ic === 'getobjects' ? 'Query_GetObjects' : 'Query_GetObjectsInRange'}: in meta scoring ${per(inN[w])}, elsewhere ${outN[w]}; ` +
      `${cls.length} classes: ${cls.map(c => `${c.class ?? c.klass} x${c.calls}`).join(', ') || '-'}${want ? `; not Sim: ${off.length}` : ''}`);
  }
  // I-121-9
  const slotCallers = a.callers.filter(c => c.ic === 'slotpos');
  lines.push(`I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring ${per(inN[1])}, elsewhere ${outN[1]}; ` +
    `${a.slotIds}${a.slotIds >= 64 ? '+' : ''} distinct objects, ${slotCallers.length} caller chains`);
  for (const c of slotCallers.slice(0, 6)) lines.push(`  slotpos x${c.calls}: ${chain(c.frames)}`);
  // I-120-4
  lines.push(`I-120-4 [0x11DC18C] at each Simulate: ${ticks} ticks, ${trackTicks} non-zero${a.trackLast ? ` (last value ${a.trackLast})` : ''} -> ` +
    (ticks ? (trackTicks ? 'REFUTED (tracking on in play)' : 'confirmed (always 0)') : 'no data'));
  // I-119-44
  const s0 = sub(a.sleep0, b?.sleep0), s1 = sub(a.sleep1, b?.sleep1), n0 = s0.reduce((x, y) => x + y, 0);
  const v = !n0 ? 'no data (probe task did not run)' : s0[1] === n0 ? 'confirmed (Sleep(0) resumes the next tick, as Sleep(1))' :
    s0[0] === n0 ? 'REFUTED (Sleep(0) resumes in the same tick)' : 'mixed (see histogram)';
  lines.push(`I-119-44 Sleep(0) resume ticks ${hist(s0)}; Sleep(1) ${hist(s1)} -> ${v}`);
  return { ok, lines };
}

function selfCheck() {
  const lg = (o: Partial<Lg>): Lg => ({ on: 31, ticks: 0, trackTicks: 0, trackLast: 0, scopes: 0, scopeUs: 0, scopeFull: 0, in: [0, 0, 0, 0], out: [0, 0, 0, 0],
    slotIds: 0, sleep0: [0, 0, 0, 0], sleep1: [0, 0, 0, 0], dropped: 0, classes: [], callers: [], ...o });
  const before = { autshadow: { ledger: lg({ ticks: 100, out: [50, 0, 0, 0] }) } };
  const after = { autshadow: { ledger: lg({ ticks: 6100, scopes: 300, scopeUs: 9000000, in: [12, 30, 4, 2], out: [950, 70, 10, 0], slotIds: 3,
    sleep0: [0, 64, 0, 0], sleep1: [0, 64, 0, 0],
    classes: [{ ic: 'getobjects', klass: '0x1', class: 'Sims3.Gameplay.Objects.Lighting.LightGameObject', calls: 4 },
      { ic: 'inrange', klass: '0x2', class: 'Sims3.Gameplay.Actors.Sim', calls: 2 }],
    callers: [{ ic: 'rng', calls: 10, frames: ['ScriptCore.Random.Random_NextDoubleImpl', 'Sims3.Gameplay.Core.RandomUtil.GetRandomObjectFromList',
      'Sims3.Gameplay.Socializing.Relationship.GetBFFOrSimWithHighestLikingForPet'] },
      { ic: 'rng', calls: 2, frames: ['ScriptCore.Random.Random_NextDoubleImpl', 'Sims3.Gameplay.Objects.Toilet.Urinal.Score'] }] }) } };
  const r = ledgerReport(before, after, 10);
  assert.equal(r.ok, true);
  const t = r.lines.join('\n');
  assert.match(t, /I-121-5 RNG: in meta scoring 12 \(1\.2\/min, 0\.04\/scope\), elsewhere 900; caller chains 2, outside the two expected paths 1 \(2 calls\)/);
  assert.match(t, /I-121-7 Query_GetObjects: .* 1 classes: Sims3\.Gameplay\.Objects\.Lighting\.LightGameObject x4/);
  assert.match(t, /I-121-12 .*Sims3\.Gameplay\.Actors\.Sim x2; not Sim: 0/);
  assert.match(t, /I-121-9 .*in meta scoring 30 .* 3 distinct objects/);
  assert.match(t, /I-120-4 .*: 6000 ticks, 0 non-zero -> confirmed/);
  assert.match(t, /I-119-44 Sleep\(0\) resume ticks 0:0 1:64 2:0 3\+:0; .* confirmed/);
  assert.equal(ledgerReport({}, {}, 10).ok, false);
  const same = { autshadow: { ledger: lg({ ticks: 10, scopes: 1, sleep0: [64, 0, 0, 0], trackTicks: 2, trackLast: 1 }) } };
  assert.match(ledgerReport({}, same, 10).lines.join('\n'), /REFUTED \(tracking on in play\)[\s\S]*REFUTED \(Sleep\(0\) resumes in the same tick\)/);
  console.log('mono-ledger-report self-check ok');
}

if (process.argv[1]?.endsWith('mono-ledger-report.ts')) {  // not when queue.ts imports it
  const args = process.argv.slice(2);
  if (args[0] === '--self-check') selfCheck();
  else if (!args[0]) { console.log('usage: node tools/harness/mono-ledger-report.ts <...-native.json> [--minutes 10] | --self-check'); process.exit(2); }
  else {
    const mi = args.indexOf('--minutes'), minutes = mi >= 0 ? Number(args[mi + 1]) : 10;
    const d = JSON.parse(readFileSync(args[0], 'utf8'));
    const r = ledgerReport(d.before, d.after, minutes);
    for (const l of r.lines) console.log(l);
    process.exit(r.ok ? 0 : 1);
  }
}
