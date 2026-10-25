// Offline self-check of save_watch's Saver-made save count (mods/ts3agent/src/plugin/SaverCount.cs).
// Windows (.NET Framework csc).
//   node tools/selftest/savercount/run.ts
import { execFileSync } from 'node:child_process';
import { mkdirSync } from 'node:fs';
import { join, resolve } from 'node:path';

const ROOT = resolve(import.meta.dirname, '..', '..', '..');
const CSC = 'C:/Windows/Microsoft.NET/Framework/v4.0.30319/csc.exe';
const out = join(ROOT, 'build', 'selftest');
mkdirSync(out, { recursive: true });
const exe = join(out, 'savercount-test.exe');
execFileSync(CSC, ['/nologo', '/optimize+', '/platform:x86', '/langversion:5', `/out:${exe}`,
  join(import.meta.dirname, 'SaverCountTest.cs'), join(ROOT, 'mods', 'ts3agent', 'src', 'plugin', 'SaverCount.cs')], { stdio: 'inherit' });
execFileSync(exe, [], { stdio: 'inherit' });
