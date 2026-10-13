// Compiles mods/ts3fixes (the player mod with the stutter fixes, docs/04 F8-F12) against the game's own DLLs and packs
// it into build/ts3fixes.package. Windows-only by nature: it needs the .NET Framework C# compiler (csc.exe, C# 5).
//   node tools/setup/build-fixes.ts
import { execFileSync } from 'node:child_process';
import { existsSync, mkdirSync, readdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { TYPE_S3SA, fnv64, s3saEncode, writePackage } from '../dbpf.ts';

const ROOT = resolve(import.meta.dirname, '..', '..');
const CSC = 'C:/Windows/Microsoft.NET/Framework/v4.0.30319/csc.exe';
const REF_DIR = join(ROOT, 'game-copy', 'assemblies'); // from tools/extract-assemblies.ts
const REFS = ['mscorlib', 'System', 'System.Xml', 'ScriptCore', 'SimIFace', 'Sims3Metadata',
              'Sims3GameplaySystems', 'Sims3GameplayObjects', 'Sims3StoreObjects', 'UI'];
const ASSEMBLY = 'Ts3Fixes';
const TYPE_XML = 0x0333406c;
// Tuning for Ts3Fixes.Instantiator: loading it applies the fixes; the other values are the player's settings.
const TUNING_XML = `<?xml version="1.0" encoding="utf-8"?>
<base>
  <Current_Tuning>
    <kInstantiator value="True" />
    <kGoHomeShortcut value="True" />
    <kBuildingTripShortcut value="True" />
    <kLotTripShortcut value="True" />
    <kBroadcasterThrottle value="True" />
    <kWalkStyleComparer value="True" />
    <kVirtualHomeShortcut value="True" />
    <kNoRouteFailWalkOffscreen value="True" />
    <kStuckWatchdog value="True" />
    <kStuckWatchdogIdle value="True" />
    <kRouteFailureBackoff value="True" />
    <kSaveCleanup value="False" />
    <kSnapshotCap value="0" />
    <kNoStationDetourOffscreen value="True" />
    <kPortalAdaptorLeak value="True" />
    <kCasGrid value="False" />
  </Current_Tuning>
</base>
`;

const missing = REFS.filter(r => !existsSync(join(REF_DIR, `${r}.dll`)));
if (missing.length) {
  console.error(`missing game DLLs in ${REF_DIR}: ${missing.join(', ')}. Run tools/extract-assemblies.ts first.`);
  process.exit(1);
}
const outDir = join(ROOT, 'build');
mkdirSync(outDir, { recursive: true });
const src = join(ROOT, 'mods', 'ts3fixes', 'src');
const dll = join(outDir, `${ASSEMBLY}.dll`);
execFileSync(CSC, ['/nologo', '/nostdlib+', '/noconfig', '/target:library', '/optimize+', '/langversion:5', '/warn:4',
  '/nowarn:414,169,649', `/out:${dll}`, ...REFS.map(r => `/reference:${join(REF_DIR, `${r}.dll`)}`),
  ...readdirSync(src).filter(f => f.endsWith('.cs')).map(f => join(src, f))], { stdio: 'inherit' });
const pkg = writePackage([
  { type: TYPE_S3SA, group: 0, instance: fnv64(ASSEMBLY), data: s3saEncode(readFileSync(dll)) },
  { type: TYPE_XML, group: 0, instance: fnv64(`${ASSEMBLY}.Instantiator`), data: Buffer.from(TUNING_XML, 'utf8') },
]);
const out = join(outDir, 'ts3fixes.package');
writeFileSync(out, pkg);
console.log(`built ${out} (${pkg.length} bytes)`);
