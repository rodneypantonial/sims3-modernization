// Save bloat by source (docs/09 §6): which script objects and images grow between checkpoints of one save.
//   node tools/harness/save-sources.ts Baseline-BP-fresh Baseline-BP        (saves in checkpoint order)
//   TOP=40 FILTER='LifeEvent|Relationship' node tools/harness/save-sources.ts ...
// Reads only. save-stats.ts sums resource types over all files; this one splits by file and opens the script state
// (type 0x06B981ED "OBJS") to count persisted objects per class.
//
// OBJS layout (reverse engineered from our saves, version 0x0833): u32 'OBJS'-tagged header; +8 type count, +12 record
// count, +16 type table offset, +20 record offset table (u32 per record). A record starting 0x10 is an object: u32 type
// index follows; 0x11 is an array. Type table entry: flag byte (0x20 = generic: definition then one type per `N arity),
// name length (big-endian 7-bit groups), name, then field count and fields (length, name, type byte). Some entries have
// no field list (count byte absent or 0x7F); the parser detects those by checking that the next entry parses.
import { readdirSync, readFileSync } from 'node:fs';
import { homedir } from 'node:os';
import { join } from 'node:path';
import { readIndex, refpack } from '../extract-assemblies.ts';

const SAVES = join(process.env.TS3_USER ?? join(homedir(), 'Documents', 'Electronic Arts', 'The Sims 3'), 'Saves');
const OBJS = 0x06b981ed;
const IMAGE_TYPES: Record<number, string> = { 0x00b2d882: 'dds', 0x0580a2ce: 'sim-portrait', 0x6b6d837e: 'family-portrait', 0xd84e7fc6: 'lot-thumb' };

type Stat = { c: number; b: number };
export function readObjs(o: Buffer) {
  const nTypes = o.readUInt32LE(8), n = o.readUInt32LE(12), typeTab = o.readUInt32LE(16), offTab = o.readUInt32LE(20);
  const vint = (q: number): [number, number] => { let v = 0, b; do { b = o[q++]; v = (v << 7) | (b & 0x7f); } while (b & 0x80); return [v, q]; };
  const tname = /^[A-Za-z_][\w.`+<>\[\],=\s-]*$/, ident = /^[A-Za-z_<][\w<>`.$|-]*$/;
  const typeAt = (q: number, depth = 0): [string, number] | null => {
    if (q >= o.length || depth > 8) return null;
    const flag = o[q++];
    if (flag & 0x20) {
      const d = typeAt(q, depth + 1); if (!d) return null;
      const arity = [...d[0].matchAll(/`(\d+)/g)].reduce((s, m) => s + +m[1], 0);
      const args: string[] = []; let r = d[1];
      for (let i = 0; i < arity; i++) { const a = typeAt(r, depth + 1); if (!a) return null; args.push(a[0]); r = a[1]; }
      return [`${d[0]}<${args.join(',')}>`, r];
    }
    if (flag & ~0x01) return null;
    const [len, r] = vint(q); if (len < 2 || len > 400) return null;
    const s = o.toString('latin1', r, r + len); return tname.test(s) ? [s, r + len] : null;
  };
  const fieldsAt = (q: number): number => {
    const [nf, r0] = vint(q); let r = r0;
    for (let f = 0; f < nf; f++) { const [fl, r1] = vint(r); if (!fl || !ident.test(o.toString('latin1', r1, r1 + fl)) || o[r1 + fl] > 0x40) return -1; r = r1 + fl + 1; }
    return r;
  };
  const types: string[] = []; let p = typeTab;
  while (types.length < nTypes) {
    const t = typeAt(p); if (!t) break; // rest stays unnamed ('?unparsed')
    types.push(t[0]); p = t[1];
    const nextOk = (q: number) => types.length === nTypes || typeAt(q) !== null;
    const f = fieldsAt(p);
    if (f >= 0 && nextOk(f)) { p = f; continue; }
    if (o[p] === 0x7f && nextOk(p + 1)) { p++; continue; }
    if (nextOk(p)) continue;
    let k = 1; while (k < 16 && !nextOk(p + k)) k++;
    if (k === 16) break;
    p += k;
  }
  const off = (i: number) => o.readUInt32LE(offTab + 4 * i);
  const byClass = new Map<string, Stat>();
  for (let i = 0; i < n; i++) {
    const a = off(i), b = i + 1 < n ? off(i + 1) : offTab;
    const k = o[a] === 0x10 ? (types[o.readUInt32LE(a + 1)] ?? '?unparsed') : `<array/tag 0x${o[a].toString(16)}>`;
    const s = byClass.get(k) ?? { c: 0, b: 0 }; s.c++; s.b += b - a; byClass.set(k, s);
  }
  return { records: n, typesParsed: types.length, nTypes, bytes: o.length, byClass };
}

function load(name: string) {
  const dir = join(SAVES, name.endsWith('.sims3') ? name : `${name}.sims3`);
  const files: { f: string; size: number; images: Map<string, Stat> }[] = [];
  let objs: ReturnType<typeof readObjs> | null = null;
  for (const f of readdirSync(dir)) {
    const pkg = readFileSync(join(dir, f));
    const images = new Map<string, Stat>();
    if (pkg.toString('latin1', 0, 4) === 'DBPF') for (const e of readIndex(pkg)) {
      if (IMAGE_TYPES[e.type]) { const k = `${IMAGE_TYPES[e.type]} g${e.group.toString(16)}`; const s = images.get(k) ?? { c: 0, b: 0 }; s.c++; s.b += e.fileSize; images.set(k, s); }
      if (e.type === OBJS && f.endsWith('.nhd')) { const raw = pkg.subarray(e.offset, e.offset + e.fileSize); objs = readObjs(e.compressed === 0xffff ? refpack(raw) : raw); }
    }
    files.push({ f, size: pkg.length, images });
  }
  return { name, files, objs: objs as ReturnType<typeof readObjs> | null };
}

const names = process.argv.slice(2);
if (!names.length) { console.error('usage: node tools/harness/save-sources.ts <save> [<later save> ...]'); process.exit(2); }
const saves = names.map(load);
const mb = (n: number) => (n / 1048576).toFixed(2);
const short = (k: string) => k.replace(/System\.Collections\.Generic\./g, '').replace(/Sims3\.(Gameplay|SimIFace)\./g, '');

console.log('== files (MB) and images (count/MB)');
for (const s of saves) {
  console.log(s.name);
  for (const f of s.files) console.log(`  ${f.f.padEnd(44)} ${mb(f.size).padStart(7)}  ${[...f.images].map(([k, v]) => `${k}: ${v.c}/${mb(v.b)}`).join('  ')}`);
}

console.log('\n== script state (OBJS, uncompressed)');
for (const s of saves) if (s.objs) console.log(`  ${s.name.padEnd(28)} ${mb(s.objs.bytes)} MB  records ${s.objs.records}  types named ${s.objs.typesParsed}/${s.objs.nTypes}`);
const first = saves[0].objs, last = saves.at(-1)!.objs;
if (first && last) {
  const filter = process.env.FILTER ? new RegExp(process.env.FILTER) : null;
  const keys = [...new Set(saves.flatMap(s => [...(s.objs?.byClass.keys() ?? [])]))].filter(k => !filter || filter.test(k));
  const rows = keys.map(k => ({ k, counts: saves.map(s => s.objs?.byClass.get(k)?.c ?? 0), d: (last.byClass.get(k)?.b ?? 0) - (first.byClass.get(k)?.b ?? 0), now: last.byClass.get(k)?.b ?? 0 }));
  console.log(`\n== classes by byte growth, first -> last (TOP=${process.env.TOP ?? 30}${filter ? `, FILTER=${filter.source}` : ''})`);
  for (const r of rows.sort((a, b) => b.d - a.d).slice(0, +(process.env.TOP ?? 30)))
    console.log(`  ${short(r.k).slice(0, 90).padEnd(90)} ${r.counts.join(' -> ').padEnd(28)} ${(r.d >= 0 ? '+' : '') + (r.d / 1024).toFixed(1)} KB (now ${(r.now / 1024).toFixed(0)} KB)`);
}
