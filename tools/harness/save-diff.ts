// Resource-by-resource diff of two saves (docs/26): which resources are byte-identical after decompression.
//   node tools/harness/save-diff.ts Test-SaveProf Test-SaveProf.sims3.backup
//   DETAIL=1 node tools/harness/save-diff.ts A B     (also list every differing resource)
// Reads only. Matches files by name (the world .nhd / ExportDB / TravelDB names are the same in both saves) and
// resources by type/group/instance. Prints, per file and per type: identical, changed, only-in-A, only-in-B, with
// uncompressed bytes. For the OBJS script state it also reports how far the two blobs share a common prefix.
import { createHash } from 'node:crypto';
import { readdirSync, readFileSync, existsSync } from 'node:fs';
import { homedir } from 'node:os';
import { join } from 'node:path';
import { readIndex, refpack } from '../extract-assemblies.ts';

const SAVES = join(process.env.TS3_USER ?? join(homedir(), 'Documents', 'Electronic Arts', 'The Sims 3'), 'Saves');
const OBJS = 0x06b981ed;
const dirOf = (n: string) => { const d = join(SAVES, n); return existsSync(d) ? d : join(SAVES, `${n}.sims3`); };
const hex = (n: number) => n.toString(16).padStart(8, '0');

type Res = { size: number; disk: number; hash: string; data: Buffer };
function load(name: string) {
  const out = new Map<string, Map<string, Res>>();
  for (const f of readdirSync(dirOf(name))) {
    const pkg = readFileSync(join(dirOf(name), f));
    if (pkg.toString('latin1', 0, 4) !== 'DBPF') { out.set(f, new Map([['raw', { size: pkg.length, disk: pkg.length, hash: createHash('sha1').update(pkg).digest('hex'), data: pkg }]])); continue; }
    const m = new Map<string, Res>();
    for (const e of readIndex(pkg)) {
      const raw = pkg.subarray(e.offset, e.offset + e.fileSize);
      const data = e.compressed === 0xffff ? refpack(raw) : raw;
      m.set(`${hex(e.type)}:${hex(e.group)}:${e.inst.toString(16).padStart(16, '0')}`, { size: data.length, disk: e.fileSize, hash: createHash('sha1').update(data).digest('hex'), data });
    }
    out.set(f, m);
  }
  return out;
}

const [an, bn] = process.argv.slice(2);
if (!bn) { console.error('usage: node tools/harness/save-diff.ts <saveA> <saveB>'); process.exit(2); }
const A = load(an), B = load(bn);
const kb = (n: number) => (n / 1024).toFixed(0);
for (const f of new Set([...A.keys(), ...B.keys()])) {
  const a = A.get(f) ?? new Map(), b = B.get(f) ?? new Map();
  const byType = new Map<string, { same: number; sameB: number; diff: number; diffB: number; onlyA: number; onlyB: number }>();
  const row = (t: string) => { let r = byType.get(t); if (!r) byType.set(t, r = { same: 0, sameB: 0, diff: 0, diffB: 0, onlyA: 0, onlyB: 0 }); return r; };
  const detail: string[] = []; let diskSame = 0, diskAll = 0;
  for (const k of new Set([...a.keys(), ...b.keys()])) {
    const r = row(k.split(':')[0]), x = a.get(k), y = b.get(k);
    if (y) { diskAll += y.disk; if (x && x.hash === y.hash) diskSame += y.disk; }
    if (!x) { r.onlyB++; detail.push(`  +B ${k} ${y!.size}`); }
    else if (!y) { r.onlyA++; detail.push(`  +A ${k} ${x.size}`); }
    else if (x.hash === y.hash) { r.same++; r.sameB += x.size; }
    else {
      r.diff++; r.diffB += Math.max(x.size, y.size);
      let p = 0; const n = Math.min(x.size, y.size); while (p < n && x.data[p] === y.data[p]) p++;
      let s = 0; while (s < n - p && x.data[x.size - 1 - s] === y.data[y.size - 1 - s]) s++;
      let nd = 0; if (x.size === y.size) for (let i = p; i < x.size - s; i++) if (x.data[i] !== y.data[i]) nd++;
      detail.push(`  ~ ${k} ${x.size} -> ${y.size} common prefix ${p} suffix ${s}${x.size === y.size ? ` bytes differing ${nd}` : ''}`);
    }
  }
  console.log(`== ${f}  B on disk: ${(diskSame / 1048576).toFixed(2)} of ${(diskAll / 1048576).toFixed(2)} MB in resources identical to A`);
  for (const [t, r] of [...byType].sort((p, q) => (q[1].diffB + q[1].sameB) - (p[1].diffB + p[1].sameB)))
    console.log(`  ${t}  same ${r.same} (${kb(r.sameB)} KB)  changed ${r.diff} (${kb(r.diffB)} KB)  onlyA ${r.onlyA}  onlyB ${r.onlyB}`);
  if (process.env.DETAIL) for (const d of detail) console.log(d);
  else for (const d of detail.filter(d => d.includes(hex(OBJS)))) console.log(d);
}
