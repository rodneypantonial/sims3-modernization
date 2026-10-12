// Save bloat per resource type (PLAN task 8, docs/09): sizes of everything in a save's world file(s), so checkpoints of
// a long-running save can be compared and the parts that grow found.
//   node tools/harness/save-stats.ts Baseline-IP-fresh Baseline-IP-day3 Baseline-IP   (saves in checkpoint order)
// Reads only. Image resources are labelled by sniffing their header (PNG, JPEG, DDS); other types show as hex.
import { readdirSync, readFileSync, statSync } from 'node:fs';
import { homedir } from 'node:os';
import { join } from 'node:path';
import { readIndex, refpack } from '../extract-assemblies.ts';

const SAVES = join(process.env.TS3_USER ?? join(homedir(), 'Documents', 'Electronic Arts', 'The Sims 3'), 'Saves');
const names = process.argv.slice(2).map(n => n.endsWith('.sims3') ? n : `${n}.sims3`);
if (!names.length) { console.error('usage: node tools/harness/save-stats.ts <save> [<later save> ...]'); process.exit(2); }

type Stat = { count: number; bytes: number; label: string };
const label = (b: Buffer): string => b.subarray(0, 4).equals(Buffer.from([0x89, 0x50, 0x4e, 0x47])) ? 'png'
  : b[0] === 0xff && b[1] === 0xd8 ? 'jpeg' : b.toString('latin1', 0, 4) === 'DDS ' ? 'dds' : '';

const saves = names.map(name => {
  const dir = join(SAVES, name);
  const stats = new Map<string, Stat>();
  let total = 0;
  for (const f of readdirSync(dir)) {
    const p = join(dir, f);
    total += statSync(p).size;
    if (!/\.(nhd|package)$/i.test(f)) continue;
    const pkg = readFileSync(p);
    if (pkg.toString('latin1', 0, 4) !== 'DBPF') continue;
    for (const e of readIndex(pkg)) {
      const key = `0x${e.type.toString(16).padStart(8, '0')}`;
      const s = stats.get(key) ?? { count: 0, bytes: 0, label: '' };
      s.count++;
      s.bytes += e.fileSize;
      if (!s.label && s.count <= 3) { // sniff the first few of each type
        try {
          const raw = pkg.subarray(e.offset, e.offset + e.fileSize);
          s.label = label(e.compressed === 0xffff ? refpack(raw) : raw);
        } catch { /* unreadable: leave unlabelled */ }
      }
      stats.set(key, s);
    }
  }
  return { name, total, stats };
});

const mb = (n: number) => (n / 1048576).toFixed(2);
const types = [...new Set(saves.flatMap(s => [...s.stats.keys()]))]
  .sort((a, b) => (saves.at(-1)!.stats.get(b)?.bytes ?? 0) - (saves.at(-1)!.stats.get(a)?.bytes ?? 0));
console.log('type'.padEnd(12) + 'kind'.padEnd(6) + saves.map(s => s.name.replace('.sims3', '').padStart(22)).join('') + '   growth');
console.log('total MB'.padEnd(18) + saves.map(s => mb(s.total).padStart(22)).join(''));
for (const t of types.slice(0, 25)) {
  const cols = saves.map(s => s.stats.get(t));
  const first = cols[0]?.bytes ?? 0, last = cols.at(-1)?.bytes ?? 0;
  const kind = cols.find(c => c?.label)?.label ?? '';
  console.log(t.padEnd(12) + kind.padEnd(6) + cols.map(c => (c ? `${mb(c.bytes)} MB/${c.count}` : '-').padStart(22)).join('') + `   ${last - first >= 0 ? '+' : ''}${mb(last - first)} MB`);
}
