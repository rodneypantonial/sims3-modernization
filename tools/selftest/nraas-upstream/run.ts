// Offline behaviour check of the NRaas upstream patches' shared helper (docs/145 §5, docs/146 §5).
// usage: node tools/selftest/nraas-upstream/run.ts <NRaas clone> [patched ref, default HEAD] [base ref, default origin/master]
// Extracts CheckInventories/CheckInventory (+ GetName, Logger) from NRaas/Helpers/Inventories.cs at both refs into
// build/selftest/nraas-upstream (git-ignored; nothing of NRaas is stored in this repo), compiles them with Stubs.cs and
// Program.cs (Roslyn csc, .NET Framework) and runs the comparison. Exit 0 = PASS.
import { execFileSync } from 'node:child_process';
import { mkdirSync, writeFileSync, existsSync } from 'node:fs';
import { join, resolve } from 'node:path';

const [clone, patched = 'HEAD', base = 'origin/master'] = process.argv.slice(2);
if (!clone) { console.error('usage: node tools/selftest/nraas-upstream/run.ts <NRaas clone> [patched ref] [base ref]'); process.exit(2); }
const here = import.meta.dirname, root = resolve(here, '..', '..', '..');
const out = join(root, 'build', 'selftest', 'nraas-upstream');
mkdirSync(out, { recursive: true });

function region(ref: string, cls: string): string {
  const src = execFileSync('git', ['-C', clone, 'show', `${ref}:NRaas/Helpers/Inventories.cs`], { encoding: 'utf8' }).replace(/\r\n/g, '\n');
  const cut = (from: string, to: string) => {
    const a = src.indexOf(from), b = src.indexOf(to, a);
    if (a < 0 || b < 0) throw new Error(`${ref}: marker not found: ${a < 0 ? from : to}`);
    return src.slice(a, b);
  };
  const name = cut('        private static string GetName(IGameObject obj)', '        public static void RestoreInventoryFromList');
  const check = cut('        public static void CheckInventories(Logger log, Logger debugLog, bool checkDestroyed)', '        public static Inventory ParentInventory');
  return ['using Sims3.Gameplay;', 'using Sims3.Gameplay.Abstracts;', 'using Sims3.Gameplay.CAS;', 'using Sims3.Gameplay.Interfaces;',
    'using System;', 'using System.Collections.Generic;', 'namespace NRaas.CommonSpace.Helpers', '{', `    public class ${cls}`, '    {',
    name, check, '    }', '}', ''].join('\n');
}

writeFileSync(join(out, 'V0.cs'), region(base, 'V0'));
writeFileSync(join(out, 'V2.cs'), region(patched, 'V2'));
writeFileSync(join(out, 'Usings.cs'), 'global using NRaas.CommonSpace.Helpers;\n');

const csc = ['C:/Program Files/Microsoft Visual Studio/2022/Professional/Msbuild/Current/Bin/Roslyn/csc.exe',
  'C:/Program Files/Microsoft Visual Studio/2022/Community/Msbuild/Current/Bin/Roslyn/csc.exe'].find(existsSync);
if (!csc) { console.error('Roslyn csc.exe not found (Visual Studio 2022)'); process.exit(2); }
const exe = join(out, 'check.exe');
execFileSync(csc, ['/nologo', '/langversion:10', '/warn:0', '/out:' + exe, join(here, 'Stubs.cs'), join(here, 'Program.cs'),
  join(out, 'V0.cs'), join(out, 'V2.cs'), join(out, 'Usings.cs')], { stdio: 'inherit', timeout: 280000 });
try { execFileSync(exe, [], { stdio: 'inherit', timeout: 120000 }); }
catch { process.exit(1); }
