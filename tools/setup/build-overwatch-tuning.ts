// Builds build/ts3-overwatch-nostuckcheck.package: a tuning XML for NRaas Overwatch's PersistedSettings class that sets
// kStuckCheck = False (docs/04 F18, docs/07 §2.2). Overwatch V123 ships no XML for that class, so nothing is overridden;
// the game's tuning loader picks it up by instance = FNV-64 of the class name, as for our own Ts3Fixes.Instantiator.
// kStuckCheck only seeds new settings: it applies to new saves (or after Overwatch > Reset); existing saves keep their
// in-game "Stuck Check" value.
//   node tools/setup/build-overwatch-tuning.ts
import { mkdirSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { fnv64, writePackage } from '../dbpf.ts';

const TYPE_XML = 0x0333406c;
const CLASS = 'NRaas.OverwatchSpace.PersistedSettings';
const TUNING_XML = `<?xml version="1.0" encoding="utf-8"?>
<base>
  <Current_Tuning>
    <kStuckCheck value="False" />
  </Current_Tuning>
</base>
`;

console.assert(fnv64(CLASS) === 0x85842a96aec69641n, 'instance id of the PersistedSettings tuning');
const out = join(resolve(import.meta.dirname, '..', '..'), 'build');
mkdirSync(out, { recursive: true });
const pkg = writePackage([{ type: TYPE_XML, group: 0, instance: fnv64(CLASS), data: Buffer.from(TUNING_XML, 'utf8') }]);
writeFileSync(join(out, 'ts3-overwatch-nostuckcheck.package'), pkg);
console.log(`built ${join(out, 'ts3-overwatch-nostuckcheck.package')} (${pkg.length} bytes)`);
