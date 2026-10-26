// Offline self-check of mods/ts3fixes/src/UnseenFallbackCore.cs (switches unseenfbstuck / unseenfbplan / unseenfbteleport, docs/202). Windows (.NET Framework csc).
//   node tools/selftest/unseenfb/run.ts
import { execFileSync } from 'node:child_process';
import { mkdirSync } from 'node:fs';
import { join, resolve } from 'node:path';

const ROOT = resolve(import.meta.dirname, '..', '..', '..');
const CSC = 'C:/Windows/Microsoft.NET/Framework/v4.0.30319/csc.exe';
const out = join(ROOT, 'build', 'selftest');
mkdirSync(out, { recursive: true });
const exe = join(out, 'unseenfb-test.exe');
execFileSync(CSC, ['/nologo', '/optimize+', '/platform:x86', '/langversion:5', `/out:${exe}`,
  join(import.meta.dirname, 'UnseenFbTest.cs'), join(ROOT, 'mods', 'ts3fixes', 'src', 'UnseenFallbackCore.cs'), join(ROOT, 'mods', 'ts3fixes', 'src', 'NoStartCore.cs')], { stdio: 'inherit' });
execFileSync(exe, [], { stdio: 'inherit' });
