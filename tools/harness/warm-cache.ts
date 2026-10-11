// Warms the Windows file cache before a launch: reads game and CC packages once, in priority order, up to a budget,
// so the game's later streaming reads come from RAM instead of the SSD. Windows evicts least-recently-used data
// first, so files read last stay longest; the most important ones are therefore read LAST.
// Usage: node tools/harness/warm-cache.ts [--budget-gb 12] [--save Baseline-BP.sims3]
import { closeSync, openSync, readdirSync, readSync, statSync } from 'node:fs';
import { homedir } from 'node:os';
import { join } from 'node:path';
import { parseArgs } from 'node:util';

const { values } = parseArgs({ options: { 'budget-gb': { type: 'string', default: '12' }, save: { type: 'string' } } });
const GAME = process.env.TS3_GAME ?? 'C:\\Program Files (x86)\\Steam\\steamapps\\common\\The Sims 3';
const USER = process.env.TS3_USER ?? join(homedir(), 'Documents', 'Electronic Arts', 'The Sims 3');

function files(dir: string, ext: RegExp): string[] {
  let out: string[] = [];
  try {
    for (const e of readdirSync(dir, { withFileTypes: true })) {
      const p = join(dir, e.name);
      if (e.isDirectory()) out = out.concat(files(p, ext));
      else if (ext.test(e.name)) out.push(p);
    }
  } catch { /* missing folder */ }
  return out;
}

// least important first, most important last (see header)
const groups: [string, string[]][] = [
  ['expansion and stuff pack packages', readdirSync(GAME).filter(d => /^(EP|SP)\d+$/.test(d)).flatMap(d => files(join(GAME, d, 'GameData'), /\.package$/i))],
  ['base game packages', files(join(GAME, 'GameData'), /\.package$/i).concat(files(join(GAME, 'Game', 'Bin'), /\.package$/i))],
  ['custom content', files(join(USER, 'Mods'), /\.package$/i).concat(files(join(USER, 'DCCache'), /\.dbc$/i))],
  ['save', values.save ? files(join(USER, 'Saves', values.save), /./) : []],
];

const budget = Number(values['budget-gb']) * 2 ** 30;
const all = groups.flatMap(([g, fs]) => fs.map(f => ({ g, f, size: statSync(f).size })));
// keep the most important files within the budget: drop from the front (least important) until it fits
let total = all.reduce((s, x) => s + x.size, 0);
while (total > budget && all.length) total -= all.shift()!.size;

const buf = Buffer.alloc(8 << 20);
const t0 = Date.now();
let done = 0;
for (const x of all) {
  const fd = openSync(x.f, 'r');
  try { while (readSync(fd, buf, 0, buf.length, null) > 0) { /* reading is the point */ } } finally { closeSync(fd); }
  done += x.size;
}
const gb = (n: number) => (n / 2 ** 30).toFixed(1);
console.log(`warmed ${gb(done)} GB in ${all.length} files (${Math.round((Date.now() - t0) / 1000)} s); groups: ${groups.map(([g]) => g).join(' < ')}`);
