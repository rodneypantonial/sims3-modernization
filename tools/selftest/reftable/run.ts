// Offline self-check of the save identity table (mods/ts3fixes/src/FastRefTable.cs) against the game's own table. Windows (.NET Framework csc).
//   node tools/selftest/reftable/run.ts
import { execFileSync } from 'node:child_process';
import { mkdirSync } from 'node:fs';
import { join, resolve } from 'node:path';

const ROOT = resolve(import.meta.dirname, '..', '..', '..');
const CSC = 'C:/Windows/Microsoft.NET/Framework/v4.0.30319/csc.exe';
const out = join(ROOT, 'build', 'selftest');
mkdirSync(out, { recursive: true });
const exe = join(out, 'reftable-test.exe');
execFileSync(CSC, ['/nologo', '/optimize+', '/platform:x86', `/out:${exe}`,
  join(import.meta.dirname, 'RefTableTest.cs'), join(ROOT, 'mods', 'ts3fixes', 'src', 'FastRefTable.cs')], { stdio: 'inherit' });
execFileSync(exe, [join(ROOT, 'game-copy', 'assemblies', 'ScriptCore.dll')], { stdio: 'inherit' });
