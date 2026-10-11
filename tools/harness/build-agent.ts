// Compiles mods/ts3agent against the game's own DLLs and packs build/ts3agent.package.
// Windows-only by nature: it needs the .NET Framework C# compiler that ships with Windows (csc.exe, C# 5).
// Usage: node tools/harness/build-agent.ts
import { execFileSync } from 'node:child_process';
import { existsSync, mkdirSync, readdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { TYPE_S3SA, fnv64, s3saEncode, writePackage } from '../dbpf.ts';

const ROOT = resolve(import.meta.dirname, '..', '..');
const CSC = 'C:\\Windows\\Microsoft.NET\\Framework\\v4.0.30319\\csc.exe';
const REF_DIR = join(ROOT, 'game-copy', 'assemblies'); // from tools/extract-assemblies.ts
const REFS = ['mscorlib', 'System', 'System.Xml', 'ScriptCore', 'SimIFace', 'Sims3Metadata',
              'Sims3GameplaySystems', 'Sims3GameplayObjects', 'Sims3StoreObjects', 'UI'];
const ASSEMBLY = 'Ts3Agent';
const INSTANTIATOR = 'Ts3Agent.Instantiator';
const TYPE_XML = 0x0333406c;
// Tuning XML for the instantiator class: loading it runs the static constructor that starts the agent.
const TUNING_XML = `<?xml version="1.0" encoding="utf-8"?>
<base>
  <Current_Tuning>
    <kInstantiator value="True" />
  </Current_Tuning>
</base>
`;

function sources(dir: string): string[] {
  return readdirSync(dir, { withFileTypes: true }).flatMap(e =>
    e.isDirectory() ? sources(join(dir, e.name)) : e.name.endsWith('.cs') ? [join(dir, e.name)] : []);
}

const missing = REFS.filter(r => !existsSync(join(REF_DIR, `${r}.dll`)));
if (missing.length) {
  console.error(`missing game DLLs in ${REF_DIR}: ${missing.join(', ')}. Run tools/extract-assemblies.ts first.`);
  process.exit(1);
}
const outDir = join(ROOT, 'build');
mkdirSync(outDir, { recursive: true });
const dll = join(outDir, `${ASSEMBLY}.dll`);
execFileSync(CSC, ['/nologo', '/nostdlib+', '/noconfig', '/target:library', '/optimize+', '/langversion:5', '/warn:4',
  '/nowarn:414,169,649', `/out:${dll}`, ...REFS.map(r => `/reference:${join(REF_DIR, `${r}.dll`)}`),
  ...sources(join(ROOT, 'mods', 'ts3agent', 'src'))], { stdio: 'inherit' });

const pkg = writePackage([
  { type: TYPE_S3SA, group: 0, instance: fnv64(ASSEMBLY), data: s3saEncode(readFileSync(dll)) },
  { type: TYPE_XML, group: 0, instance: fnv64(INSTANTIATOR), data: Buffer.from(TUNING_XML, 'utf8') },
]);
const out = join(outDir, 'ts3agent.package');
writeFileSync(out, pkg);
console.log(`built ${out} (${pkg.length} bytes)`);
