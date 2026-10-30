// Quiet save-reload report (docs/204 §2): reads probe-out/save-reload/*.json (save-reload.ts: play and control jobs of queue
// save-reload-memory.json) and prints per world, from the latest play file and the latest control file:
//   times: normal save (a), reload cycle by route (save + unload + load), full restart + load (control, upper bound)
//   memory at every mark (largest free, free, commit, private, mapped from the probe's /memory; rc, gcHeap, pools from the
//   diag: mem mark line) and "closes": the share of a full restart's change that the in-process reload gets:
//     menu route   (sr-reload  - sr-pre)     / (sr-ctl - sr-pre)
//     direct route (sr-direct  - sr-reload10) / (sr-ctl - sr-reload10)   (sr-ctl loads exactly the file the direct route reloaded)
//   and the gap left 10 min later (sr-reload10 / sr-direct10 - sr-ctl10).
//   node tools/harness/save-reload-report.ts [--dir probe-out/save-reload] [--world BP]
//   node tools/harness/save-reload-report.ts --self-check
import assert from 'node:assert/strict';
import { existsSync, readdirSync, readFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { parseArgs } from 'node:util';

type Mark = { memory?: Record<string, number> | null; diag?: Record<string, number | null> | null };
type Peak = { reads: number; bigBefore: number | null; minBig: number | null; bigNeedMB: number | null; commitPeakMB: number | null; minFree: number | null };
export type Run = { world: string; mode: 'play' | 'control'; started: string; phases: Record<string, number>; saves: { label: string; wallMs: number; nativeMs: number | null; peak?: Peak }[];
  marks: Record<string, Mark>; errors: string[]; reloadPeak?: { menu?: Peak; direct?: Peak } };

// field -> value at a mark: the probe's /memory first (verified walk), else the diag: mem field of the mark line
export const FIELDS: [string, (m: Mark) => number | null | undefined][] = [
  ['largest free', m => m.memory?.largestFreeMB ?? m.diag?.big], ['free', m => m.memory?.freeMB ?? m.diag?.free],
  ['commit', m => m.memory?.commitMB ?? m.diag?.wCom], ['private', m => m.memory?.privateMB ?? m.diag?.priv],
  ['mapped', m => m.memory?.mappedMB ?? m.diag?.map], ['rc', m => m.diag?.rc], ['gcHeap', m => m.diag?.gcHeap ?? m.memory?.gcHeapMB],
  ['gcFree', m => m.diag?.gcFree ?? m.memory?.gcFreeMB], ['miCom', m => m.diag?.miCom], ['eaDir', m => m.diag?.eaDir],
  ['rp', m => m.diag?.rp], ['pl', m => m.diag?.pl], ['sp', m => m.diag?.sp], ['rest', m => m.diag?.rest],
];
const v = (r: Run | undefined, tag: string, f: (m: Mark) => number | null | undefined) => { const m = r?.marks[tag]; const x = m ? f(m) : null; return typeof x === 'number' ? x : null; };
// share of the restart's change (ctl - before) that the reload reached (after - before); null when the restart moved < 1 MB
export function closes(before: number | null, after: number | null, ctl: number | null): number | null {
  if (before === null || after === null || ctl === null || Math.abs(ctl - before) < 1) return null;
  return (after - before) / (ctl - before);
}
const MARKS = ['sr-pre', 'sr-save', 'sr-reload', 'sr-reload10', 'sr-direct', 'sr-direct10', 'sr-ctl', 'sr-ctl10'];
const n = (x: number | null) => (x === null ? '-' : String(Math.round(x)));
const pct = (x: number | null) => (x === null ? '-' : `${Math.round(x * 100)}%`);
const pk = (p?: Peak) => (p ? `largest free ${n(p.bigBefore)} -> low ${n(p.minBig)} MB (need ${n(p.bigNeedMB)}), commit peak +${n(p.commitPeakMB)}, free low ${n(p.minFree)}, ${p.reads} reads` : '-');
const s = (ms: number | null | undefined) => (typeof ms === 'number' ? `${(ms / 1000).toFixed(1)} s` : '-');

export function render(play: Run | undefined, ctl: Run | undefined): string {
  const out: string[] = [`## ${play?.world ?? ctl?.world}  (play ${play?.started ?? 'missing'}, control ${ctl?.started ?? 'missing'})`];
  for (const r of [play, ctl]) if (r?.errors.length) out.push(`  ${r.mode} errors: ${r.errors.join('; ')}`);
  const sv = (l: string) => play?.saves.find(x => x.label === l);
  out.push(`  normal save (a): wall ${s(sv('normal')?.wallMs)}, native ${s(sv('normal')?.nativeMs)}`,
    `  menu route (b): cycle ${s(play?.phases.reloadCycleMs)} = save ${s(sv('reloadSave')?.wallMs)} + unload ${s(play?.phases.menuMs)} + menu ${s(play?.phases.menuToLoadMs)} + load ${s(play?.phases.loadMs)}`,
    `  direct route (b2, ReloadWorld): cycle ${s(play?.phases.directCycleMs)} = save ${s(sv('directSave')?.wallMs)} + unload/load ${s(play?.phases.directLoadMs)}`,
    `  full restart + load (control, upper bound): ${s(ctl?.phases.restartMs)}`,
    '  address-space need (docs/204 §3 threshold; /memory read back to back, a lower bound):',
    ...(play?.saves ?? []).map(x => `    save ${x.label}: ${pk(x.peak)}`),
    ...(['menu', 'direct'] as const).filter(k => play?.reloadPeak?.[k]).map(k => `    ${k} unload + load: ${pk(play!.reloadPeak![k])}`),
    '', `  MB            ${MARKS.map(m => m.replace('sr-', '').padStart(8)).join('')}   closes: menu  direct   left @10: menu  direct`);
  for (const [name, f] of FIELDS) {
    const at = (t: string) => v(t.startsWith('sr-ctl') ? ctl : play, t, f);
    const vals = MARKS.map(at);
    if (vals.every(x => x === null)) continue;
    const left = (t: string) => (at(t) === null || at('sr-ctl10') === null ? null : at(t)! - at('sr-ctl10')!);
    out.push(`  ${name.padEnd(13)} ${vals.map(x => n(x).padStart(8)).join('')}   ${pct(closes(at('sr-pre'), at('sr-reload'), at('sr-ctl'))).padStart(11)}${pct(closes(at('sr-reload10'), at('sr-direct'), at('sr-ctl'))).padStart(8)}   ${n(left('sr-reload10')).padStart(14)}${n(left('sr-direct10')).padStart(8)}`);
  }
  out.push('  closes = share of the full restart\'s change the in-process reload reached (100% = as good as a restart; < 0 = worse than before).');
  return out.join('\n');
}

function selfCheck() {
  const mk = (big: number, com: number, rc: number): Mark => ({ memory: { largestFreeMB: big, commitMB: com }, diag: { rc, gcHeap: null } });
  const play: Run = { world: 'BP', mode: 'play', started: '2026-10-30T02:00:00Z', errors: [],
    phases: { reloadCycleMs: 95000, menuMs: 20000, menuToLoadMs: 1000, loadMs: 60000, directCycleMs: 80000, directLoadMs: 66000 },
    reloadPeak: { menu: { reads: 90, bigBefore: 600, minBig: 420, bigNeedMB: 180, commitPeakMB: 150, minFree: 1100 } },
    saves: [{ label: 'normal', wallMs: 12000, nativeMs: 11500, peak: { reads: 30, bigBefore: 600, minBig: 520, bigNeedMB: 80, commitPeakMB: 60, minFree: 1200 } }, { label: 'reloadSave', wallMs: 13000, nativeMs: 12000 }, { label: 'directSave', wallMs: 14000, nativeMs: 13000 }],
    marks: { 'sr-pre': mk(600, 2400, 400), 'sr-reload': mk(900, 2000, 200), 'sr-reload10': mk(850, 2100, 250), 'sr-direct': mk(880, 2050, 210), 'sr-direct10': mk(860, 2100, 250) } };
  const ctl: Run = { world: 'BP', mode: 'control', started: '2026-10-30T03:00:00Z', errors: [], phases: { restartMs: 150000 }, saves: [],
    marks: { 'sr-ctl': mk(1000, 1900, 150), 'sr-ctl10': mk(950, 2000, 200) } };
  assert.equal(closes(600, 900, 1000), 0.75);
  assert.equal(closes(600, 900, 600.5), null); // restart did not move: no share
  assert.equal(closes(850, 880, 1000), 0.2);
  const t = render(play, ctl);
  assert.match(t, /largest free\s+600\s+-\s+900\s+850\s+880\s+860\s+1000\s+950\s+75%\s+20%\s+-100\s+-90/);
  assert.match(t, /commit\s+2400 .* 80%/); // (2000 - 2400) / (1900 - 2400)
  assert.match(t, /menu route \(b\): cycle 95\.0 s = save 13\.0 s \+ unload 20\.0 s/);
  assert.match(t, /full restart \+ load \(control, upper bound\): 150\.0 s/);
  assert.match(t, /save normal: largest free 600 -> low 520 MB \(need 80\), commit peak \+60/);
  assert.match(t, /menu unload \+ load: largest free 600 -> low 420 MB \(need 180\)/);
  assert.doesNotMatch(t, /gcHeap/); // all null: row dropped
  console.log('save-reload-report self-check ok');
}

if (import.meta.filename === resolve(process.argv[1] ?? '')) {
  const { values } = parseArgs({ options: { dir: { type: 'string' }, world: { type: 'string' }, 'self-check': { type: 'boolean' } } });
  if (values['self-check']) { selfCheck(); process.exit(0); }
  const dir = values.dir ?? join(resolve(import.meta.dirname, '..', '..'), 'probe-out', 'save-reload');
  const runs: Run[] = existsSync(dir) ? readdirSync(dir).filter(f => f.endsWith('.json')).map(f => JSON.parse(readFileSync(join(dir, f), 'utf8'))) : [];
  const worlds = [...new Set(runs.map(r => r.world))].filter(w => !values.world || w === values.world);
  if (!worlds.length) { console.log(`no save-reload runs in ${dir}`); process.exit(0); }
  const latest = (w: string, m: string) => runs.filter(r => r.world === w && r.mode === m).sort((a, b) => a.started.localeCompare(b.started)).at(-1);
  console.log(worlds.map(w => render(latest(w, 'play'), latest(w, 'control'))).join('\n\n'));
}
