// Summary of a queue.ts age run (PLAN lane G 10, task 8): save growth MB per sim day, save time s per sim day, largest-free trend.
//   node tools/harness/age-report.ts probe-out/queue/<queue>-<date>-<job>-age.csv [...]
//   node tools/harness/age-report.ts --self-check
// The CSV has one row per checkpoint (the first is the loaded save before aging); queue.ts writes it and puts the same
// summary in its md row. Slopes are least-squares fits over sim day; rows with an empty value are left out of that fit.
import { readFileSync } from 'node:fs';

export const CSV_HEADER = 'time,save,day,folderMB,saveMs,saveResult,commitMB,privateMB,largestFreeMB,types';
export type AgeRow = { time: string; save: string; day: number | null; folderMB: number | null; saveMs: number | null; saveResult: number | null;
  commitMB: number | null; privateMB: number | null; largestFreeMB: number | null; types: string };

const num = (s: string | undefined) => (s === undefined || s === '' ? null : Number(s));
const cell = (v: unknown) => (v === null || v === undefined ? '' : String(v).replace(/[,\r\n]/g, ' '));
export const csvLine = (r: AgeRow) => [r.time, r.save, r.day, r.folderMB, r.saveMs, r.saveResult, r.commitMB, r.privateMB, r.largestFreeMB, r.types].map(cell).join(',');

export function parseCsv(text: string): AgeRow[] {
  return text.split(/\r?\n/).filter(l => l && l !== CSV_HEADER).map(l => {
    const c = l.split(',');
    return { time: c[0], save: c[1], day: num(c[2]), folderMB: num(c[3]), saveMs: num(c[4]), saveResult: num(c[5]),
      commitMB: num(c[6]), privateMB: num(c[7]), largestFreeMB: num(c[8]), types: c[9] ?? '' };
  });
}

// least-squares slope of y over x; null with fewer than 2 points or no spread in x
export function slope(pts: [number, number][]): number | null {
  if (pts.length < 2) return null;
  const mx = pts.reduce((s, p) => s + p[0], 0) / pts.length, my = pts.reduce((s, p) => s + p[1], 0) / pts.length;
  const sxx = pts.reduce((s, p) => s + (p[0] - mx) ** 2, 0);
  return sxx ? pts.reduce((s, p) => s + (p[0] - mx) * (p[1] - my), 0) / sxx : null;
}

const pts = (rows: AgeRow[], k: keyof AgeRow, scale = 1) =>
  rows.filter(r => r.day !== null && r[k] !== null).map(r => [r.day!, (r[k] as number) * scale] as [number, number]);
const f = (n: number | null, d = 2) => (n === null ? '?' : `${n >= 0 && d ? '+' : ''}${n.toFixed(d)}`);

// "0x06b981ed=7700000/1;0x342779a7=..." -> type -> bytes
const types = (s: string) => new Map(s.split(';').filter(Boolean).map(t => { const [k, v] = t.split('='); return [k, Number(v.split('/')[0])] as [string, number]; }));

export function summarize(rows: AgeRow[]): string {
  if (!rows.length) return 'no checkpoints';
  const days = rows.filter(r => r.day !== null).map(r => r.day!);
  const span = days.length ? Math.max(...days) - Math.min(...days) : 0;
  const first = rows[0], last = rows.at(-1)!;
  const lf = rows.map(r => r.largestFreeMB).filter((n): n is number => n !== null);
  // saves after the first row only: the first row is the loaded save, its save time is not ours
  const saves = rows.slice(1).filter(r => r.saveMs !== null);
  const t0 = types(first.types), t1 = types(last.types);
  const grow = [...t1].map(([k, b]) => [k, b - (t0.get(k) ?? 0)] as [string, number]).sort((a, b) => b[1] - a[1]).slice(0, 3).filter(g => g[1] > 0);
  return [
    `${rows.length} checkpoints over ${span} sim days (day ${first.day ?? '?'} -> ${last.day ?? '?'})`,
    `size ${first.folderMB ?? '?'} -> ${last.folderMB ?? '?'} MB, growth ${f(slope(pts(rows, 'folderMB')))} MB/day`,
    `save ${saves.length ? `${(saves[0].saveMs! / 1000).toFixed(1)} -> ${(saves.at(-1)!.saveMs! / 1000).toFixed(1)} s` : '? s'}, ${f(slope(pts(rows.slice(1), 'saveMs', 1 / 1000)))} s/day`,
    `largest free ${lf.length ? `${lf[0]} -> ${lf.at(-1)} MB (min ${Math.min(...lf)})` : '?'}, ${f(slope(pts(rows, 'largestFreeMB')), 1)} MB/day`,
    ...(grow.length ? [`top growth ${grow.map(([k, b]) => `${k} +${(b / 1048576).toFixed(2)} MB`).join(', ')}`] : []),
    ...(rows.some(r => r.saveResult !== null && r.saveResult !== 0) ? [`save result != 0 at ${rows.filter(r => r.saveResult !== null && r.saveResult !== 0).map(r => r.save).join(', ')}`] : []),
  ].join('; ');
}

if (import.meta.main ?? process.argv[1]?.endsWith('age-report.ts')) {
  if (process.argv[2] === '--self-check') {
    const assert = (await import('node:assert')).strict;
    assert.equal(slope([[0, 1], [2, 5], [4, 9]]), 2);
    assert.equal(slope([[1, 1]]), null);
    assert.equal(slope([[1, 1], [1, 2]]), null);
    const r = (day: number, mb: number, ms: number | null, lf: number, t = ''): AgeRow =>
      ({ time: 't', save: `S-day${day}`, day, folderMB: mb, saveMs: ms, saveResult: ms === null ? null : 0, commitMB: 1800, privateMB: 1700, largestFreeMB: lf, types: t });
    const rows = [r(1, 20, 99999, 1900, '0x1=1000/1;0x2=500/2'), r(3, 21, 10000, 1850), r(5, 22, 11000, 1800), r(7, 23, null, 1750, '0x1=1049576/1;0x2=500/2;0x3=2097152/4')];
    const text = [CSV_HEADER, ...rows.map(csvLine)].join('\n') + '\n';
    assert.deepEqual(parseCsv(text), rows);
    assert.equal(csvLine({ ...rows[0], save: 'a,b' }).split(',').length, 10, 'commas in cells cut');
    const s = summarize(parseCsv(text));
    assert.match(s, /^4 checkpoints over 6 sim days \(day 1 -> 7\)/);
    assert.match(s, /size 20 -> 23 MB, growth \+0\.50 MB\/day/);
    assert.match(s, /save 10\.0 -> 11\.0 s, \+0\.50 s\/day/); // the loaded save's 99999 ms is ignored
    assert.match(s, /largest free 1900 -> 1750 MB \(min 1750\), -25\.0 MB\/day/);
    assert.match(s, /top growth 0x3 \+2\.00 MB, 0x1 \+1\.00 MB$/);
    assert.equal(summarize([]), 'no checkpoints');
    assert.match(summarize([rows[0], { ...rows[1], saveResult: 5 }]), /save result != 0 at S-day3$/);
    console.log('self-check ok');
    process.exit(0);
  }
  if (process.argv.length < 3) { console.error('usage: node tools/harness/age-report.ts <age.csv> [...] | --self-check'); process.exit(2); }
  for (const p of process.argv.slice(2)) console.log(`${p}: ${summarize(parseCsv(readFileSync(p, 'utf8')))}`);
}
