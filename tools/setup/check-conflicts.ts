// Static conflict check for a Mods folder: finds resources (type, group, instance) defined by more than one
// package, and core mods (packages carrying a copy of one of EA's script assemblies).
//   node tools/setup/check-conflicts.ts [modsDir]      default: the game's Mods folder
// Severity: CLASH = two packages replace the same script assembly or both are core mods (only one can win, the
// other's code silently disappears); WARN = the same tuning XML or other script-adjacent data in two packages;
// INFO = ordinary asset overrides (textures, CAS parts), often intentional.
import { readdirSync, readFileSync } from 'node:fs';
import { homedir } from 'node:os';
import { join, relative } from 'node:path';
import { readIndex, refpack, decryptS3SA, moduleName } from '../extract-assemblies.ts';

const dir = process.argv.slice(2).find(a => !a.startsWith('--')) ?? join(homedir(), 'Documents', 'Electronic Arts', 'The Sims 3', 'Mods');
const S3SA = 0x073faa07, XML = 0x0333406c;
const EA_ASSEMBLIES = ['Sims3GameplaySystems', 'Sims3GameplayObjects', 'Sims3StoreObjects', 'ScriptCore', 'SimIFace', 'UI', 'Sims3Metadata', 'mscorlib', 'System', 'System.Xml'];
const severityOf = (type: number) => (type === S3SA ? 'CLASH' : type === XML ? 'WARN' : 'INFO');

function packages(d: string): string[] {
  return readdirSync(d, { withFileTypes: true }).flatMap(e =>
    e.isDirectory() ? packages(join(d, e.name)) : e.name.toLowerCase().endsWith('.package') ? [join(d, e.name)] : []);
}

// Module name of the .NET assembly inside an S3SA resource, read from its metadata (not from referenced names).
function assemblyName(body: Buffer): string | null {
  for (const candidate of [body, (() => { try { return decryptS3SA(body); } catch { return null; } })()]) {
    if (!candidate) continue;
    const mz = candidate.indexOf('MZ');
    if (mz < 0) continue;
    try { const n = moduleName(candidate.subarray(mz)); if (n) return n.replace(/\.dll$/i, ''); } catch { /* not this layout */ }
  }
  return null;
}

const owners = new Map<string, string[]>();
const coreMods: { pkg: string; replaces: string }[] = [];
let resources = 0;
const pkgs = packages(dir);
for (const p of pkgs) {
  const buf = readFileSync(p);
  let entries;
  try { entries = readIndex(buf); } catch (e) { console.log(`SKIP  ${relative(dir, p)}: ${(e as Error).message}`); continue; }
  resources += entries.length;
  const seen = new Set<string>();
  for (const e of entries) {
    const key = `${e.type.toString(16).padStart(8, '0')}:${e.group.toString(16).padStart(8, '0')}:${e.inst.toString(16).padStart(16, '0')}`;
    if (seen.has(key)) continue; // a duplicate inside one package is that package's own business
    seen.add(key);
    owners.set(key, [...(owners.get(key) ?? []), relative(dir, p)]);
    if (e.type === S3SA) {
      let body = buf.subarray(e.offset, e.offset + e.fileSize);
      if (e.compressed === 0xffff) body = refpack(body);
      const name = assemblyName(body);
      if (name && EA_ASSEMBLIES.includes(name)) coreMods.push({ pkg: relative(dir, p), replaces: name });
    }
  }
}

const counts: Record<string, number> = { CLASH: 0, WARN: 0, INFO: 0 };
const lines: string[] = [];
for (const [key, list] of owners) {
  if (list.length < 2) continue;
  const sev = severityOf(parseInt(key.slice(0, 8), 16));
  counts[sev]++;
  if (sev !== 'INFO' || process.argv.includes('--all')) lines.push(`${sev.padEnd(5)} ${key}  ${list.join('  |  ')}`);
}
// Core mods replace EA's own assemblies; two of them cannot both be active.
const byAssembly = new Map<string, string[]>();
for (const c of coreMods) byAssembly.set(c.replaces, [...(byAssembly.get(c.replaces) ?? []), c.pkg]);
for (const [asm, list] of byAssembly) {
  console.log(`CORE  ${list.join(', ')} replace(s) EA's ${asm}.dll`);
  if (list.length > 1) { counts.CLASH++; lines.unshift(`CLASH two core mods replace ${asm}.dll: ${list.join('  |  ')}`); }
}
console.log(lines.join('\n'));
console.log(`${pkgs.length} packages, ${resources} resources: ${counts.CLASH} CLASH, ${counts.WARN} WARN, ${counts.INFO} INFO (overrides; --all lists them)`);
process.exitCode = counts.CLASH ? 1 : 0;
