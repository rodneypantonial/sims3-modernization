// Dumps TS3W.exe's script-binding table: runs of {void* fn, char* name} pairs in .data, each run ended by a null pair.
// Names look like "ScriptCore.Route::Route_Plan"; scripts reach them through [DllImport("Sims3Common.dll")] stubs.
// Usage: node tools/internal-calls-map.ts <TS3W.exe> > game-copy/analysis/internal-calls-map.csv
import { readFileSync } from 'node:fs';

const NAME = /^(ScriptCore|Sims3)[\w.]*::\w+$/;

export function bindingTable(exe: Buffer): Map<string, number> {
  const pe = exe.readUInt32LE(0x3c);
  const nSec = exe.readUInt16LE(pe + 6), opt = pe + 24, secs = opt + exe.readUInt16LE(pe + 20);
  const base = exe.readUInt32LE(opt + 28);
  const sections = Array.from({ length: nSec }, (_, i) => {
    const s = secs + i * 40;
    return { name: exe.toString('latin1', s, s + 8).replace(/\0+$/, ''), va: base + exe.readUInt32LE(s + 12),
             vsize: exe.readUInt32LE(s + 8), rsize: exe.readUInt32LE(s + 16), raw: exe.readUInt32LE(s + 20) };
  });
  const off = (a: number) => {
    const s = sections.find(s => a >= s.va && a < s.va + s.rsize);
    return s ? a - s.va + s.raw : -1;
  };
  const text = sections.find(s => s.name === '.text')!, data = sections.find(s => s.name === '.data')!;
  const isCode = (a: number) => a >= text.va && a < text.va + text.vsize;
  const nameAt = (a: number) => {
    const o = off(a);
    if (o < 0) return null;
    const s = exe.toString('latin1', o, exe.indexOf(0, o));
    return NAME.test(s) ? s : null;
  };
  const map = new Map<string, number>();
  for (let o = data.raw + 4; o + 4 <= data.raw + data.rsize; o += 4) {
    const name = nameAt(exe.readUInt32LE(o));
    const fn = exe.readUInt32LE(o - 4); // fn precedes its name (verified: 41 of 42 runs start with a code pointer, none ends with one)
    if (name && isCode(fn)) map.set(name, fn);
  }
  return map;
}

if (process.argv[1]?.endsWith('internal-calls-map.ts')) {
  const exe = process.argv[2];
  if (!exe) { console.error('usage: node tools/internal-calls-map.ts <TS3W.exe>'); process.exit(2); }
  const map = bindingTable(readFileSync(exe));
  // self-check against functions read in Ghidra: real Route_Plan reaches the locked A* search via 0x7641d0
  console.assert(map.get('ScriptCore.Route::Route_Plan') === 0x765160, 'Route_Plan should be 0x765160 on the Steam build');
  process.stdout.write('name,address\n' + [...map].sort().map(([n, a]) => `${n},0x${a.toString(16)}`).join('\n') + '\n');
  console.error(`${map.size} bindings`);
}
